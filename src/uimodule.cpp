/**
 * Author: Frano Petric
 * Version: 0.9
 * Date: 2.4.2014.
 */

#include "uimodule.hpp"
#include <iostream>
#include <fstream>
#include <alvalue/alvalue.h>
#include <alcommon/alproxy.h>
#include <alcommon/albroker.h>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread.hpp>
#include <boost/lambda/lambda.hpp>
#include <qi/log.hpp>
#include <althread/alcriticalsection.h>

struct ResponseToNameInterface::Impl {

    /**
      * Proxy to ALMemory
      */
    boost::shared_ptr<AL::ALMemoryProxy> memoryProxy;
    /**
      * Proxy to ALAudioPlayer for sound reproduction
      */
    boost::shared_ptr<AL::ALAudioPlayerProxy> playerProxy;
    /**
      * Proxy to ALLeds module
      */
    boost::shared_ptr<AL::ALLedsProxy> ledProxy;

    /**
      * Module object
      */
    ResponseToNameInterface &module;

    /**
      * Mutex used to lock callback functions, making them thread safe
      */
    boost::shared_ptr<AL::ALMutex> fCallbackMutex;

    /**
      * Struct constructor, initializes module instance and callback mutex
      */
    Impl(ResponseToNameInterface &mod) : module(mod), fCallbackMutex(AL::ALMutex::createALMutex()) {
        // Create proxies
        try {
            memoryProxy = boost::shared_ptr<AL::ALMemoryProxy>(new AL::ALMemoryProxy(mod.getParentBroker()));
            playerProxy = boost::shared_ptr<AL::ALAudioPlayerProxy>(new AL::ALAudioPlayerProxy(mod.getParentBroker()));
            ledProxy = boost::shared_ptr<AL::ALLedsProxy>(new AL::ALLedsProxy(mod.getParentBroker()));
        }
        catch (const AL::ALError& e) {
            qiLogError("ResponseToNameInterface") << "Error creating proxies" << e.toString() << std::endl;
        }
        // Declare events that are generated by this module
        memoryProxy->declareEvent("StartSession");
        memoryProxy->declareEvent("ChildCalled");

        // Subscribe to event FronTactilTouched, which signals the start of the session
        memoryProxy->subscribeToEvent("FrontTactilTouched", "ResponseToNameInterface", "onTactilTouched");
    }
};

ResponseToNameInterface::ResponseToNameInterface(boost::shared_ptr<AL::ALBroker> pBroker, const std::string& pName) :  AL::ALModule(pBroker, pName) {

    setModuleDescription("Interface module, reacting to events generated by the Logger module, calling child by either name or by using special phrases");

    functionName("onTactilTouched", getName(), "FrontTactilTouched callback, starts the session");
    BIND_METHOD(ResponseToNameInterface::onTactilTouched);

    functionName("callChild", getName(), "CallChild callback, plays the sound");
    BIND_METHOD(ResponseToNameInterface::callChild);

    functionName("endSession", getName(), "EndSession callback, resets the Interface");
    BIND_METHOD(ResponseToNameInterface::endSession);
}

ResponseToNameInterface::~ResponseToNameInterface() {
    // Cleanup code
}

void ResponseToNameInterface::init() {
   // This method overrides ALModule::init
    try {
        // Create object
        impl = boost::shared_ptr<Impl>(new Impl(*this));
        // Initialize ALModule
        AL::ALModule::init();
    }
    catch (const AL::ALError& e) {
        qiLogError("ResponseToNameInterface") << e.what() << std::endl;
    }
    qiLogVerbose("ResponseToNameInterface") << "ResponseToName Interface initialized" << std::endl;
}

void ResponseToNameInterface::onTactilTouched() {
    // Callback is thread safe as long as ALCriticalSection object exists
    AL::ALCriticalSection section(impl->fCallbackMutex);
    // Unsubscribe from the event
    impl->memoryProxy->unsubscribeToEvent("FrontTactilTouched", "ResponseToNameInterface");
    // Subscribe to events which can be triggered during the session
    try {
        impl->memoryProxy->subscribeToEvent("CallChild", "ResponseToNameInterface", "callChild");
        impl->memoryProxy->subscribeToEvent("EndSession", "ResponseToNameInterface", "endSession");
    }
    catch (const AL::ALError& e) {
        qiLogError("ResponseToNameInterface") << "Error subscribing to events" << e.toString() << std::endl;
    }
    // Signal the start of the session by changing eye color (unblocking call)
    impl->ledProxy->post.fadeRGB("FaceLeds", 0x00FF00, 1.5);
    // Raise event that the session should start
    impl->memoryProxy->raiseEvent("StartSession", AL::ALValue(1));
}

void ResponseToNameInterface::callChild(const std::string &key, const AL::ALValue &value, const AL::ALValue &msg) {
    // Thread safety
    AL::ALCriticalSection section(impl->fCallbackMutex);
    // Unsubscribing
    impl->memoryProxy->unsubscribeToEvent("CallChild", "ResponseToNameInterface");

    // Reproduce the sound using ALAudioDevice proxy
    if( (int)value == 1 ) {
        // If event is raised with value 1, call child by name
        qiLogVerbose("ResponseToNameInterface") << "Calling with name\n";
        // TODO: enable the player by uncommenting following line
        impl->playerProxy->playFile("/home/nao/naoqi/modules/sounds/name.wav");
    }
    else if ( (int)value == 2 ) {
        // Event is raised with value 2, use special phrase
        qiLogVerbose("ResponseToNameInterface") << "Calling with special phrase\n";
        // TODO: enable the player by uncommenting following line
        impl->playerProxy->playFile("/home/nao/naoqi/modules/sounds/phrase.wav");
    }
    // Notify the Logger module that child was called
    impl->memoryProxy->raiseEvent("ChildCalled", value);

    // Subscribe to the CallChild event again
    impl->memoryProxy->subscribeToEvent("CallChild", "ResponseToNameInterface", "callChild");
}

void ResponseToNameInterface::endSession() {
    // Thread safety
    AL::ALCriticalSection section(impl->fCallbackMutex);
    // Unsubscribe
    impl->memoryProxy->unsubscribeToEvent("EndSession", "ResponseToNameInterface");
    // Signal the end of the session by changing eye color (unblocking call)
    impl->ledProxy->post.fadeRGB("FaceLeds", 0x0000FF, 1.5);
    // Reset subscriptions
    try {
        impl->memoryProxy->unsubscribeToEvent("CallChild", "ResponseToNameInterface");
        impl->memoryProxy->subscribeToEvent("FrontTactilTouched", "ResponseToNameInterface", "onTactilTouched");
    }
    catch (const AL::ALError& e) {
        qiLogError("Interface") << "Error managing events while reseting" << e.toString() << std::endl;
    }
}
