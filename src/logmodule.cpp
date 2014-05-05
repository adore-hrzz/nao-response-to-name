/**
 * Author: Frano Petric
 * Version: 0.9
 * Date: 2.4.2014.
 */

#include "logmodule.hpp"
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

struct ResponseToNameLogger::Impl {

    /**
      * Proxy to ALMemory
      */
    boost::shared_ptr<AL::ALMemoryProxy> memoryProxy;

    /**
      * Proxy to sound classification module
      */
    boost::shared_ptr<AL::ALProxy> classificationProxy;

    /**
      * Module object
      */
    ResponseToNameLogger &module;

    /**
      * Mutex used to lock callback functions, making them thread safe
      */
    boost::shared_ptr<AL::ALMutex> fCallbackMutex;

    /**
      * Mutex used to lock the output file, making logging thread safe
      */
    boost::mutex outputFileLock;

    /**
      * Boost thread, implementing continuous loop which schedules calls
      */
    boost::thread *t;

    /**
      * Time storing variables
      */
    boost::system_time lastFace;
    boost::system_time lastCall;
    boost::system_time sessionStart;

    /**
      * Log file
      */
    std::ofstream outputFile;

    /**
      * Internal variables for storing the number of iterations, face appearances and sessions
      */
    int iteration;
    int faceCount;
    int childCount;
    bool end;

    /**
      * Struct constructor, initializes module instance and callback mutex
      */
    Impl(ResponseToNameLogger &mod) : module(mod), fCallbackMutex(AL::ALMutex::createALMutex()) {
        // Create proxy to ALMemory and sound classification module
        try {
            memoryProxy = boost::shared_ptr<AL::ALMemoryProxy>(new AL::ALMemoryProxy(mod.getParentBroker()));
            classificationProxy = boost::shared_ptr<AL::ALProxy>(new AL::ALProxy(mod.getParentBroker(), "KlasifikacijaGovora"));
        }
        catch (const AL::ALError& e) {
            qiLogError("ResponseToNameLogger") << "Error creating proxy to ALMemory" << e.toString() << std::endl;
        }
        // Declare events generated by this module, subscribe to external events
        try {
            memoryProxy->declareEvent("CallChild", "ResponseToNameLogger");
            memoryProxy->declareEvent("EndSession", "ResponseToNameLogger");
            memoryProxy->subscribeToEvent("StartSession", "ResponseToNameLogger", "onStartLogger");
            childCount = 0;
        }
        catch (const AL::ALError& e) {
            qiLogError("ResponseToNameLogger") << "Error setting up Logger" << e.toString() << std::endl;
        }

    }

    /**
      * Thread-safe logging function
      */
    void log(std::string eventIdentifier, int value) {
        // Take current time and calculate duration from the start of the session
        boost::system_time now = boost::get_system_time();
        boost::posix_time::time_duration duration = now - sessionStart;
        // Log the data into file
        outputFileLock.lock();
        outputFile << eventIdentifier << "\t" << value << "\t" << duration.total_milliseconds()/1000.0 << "\n";
        outputFileLock.unlock();
    }

    /**
      * Function called by the SessionStart callback
      * Initializes output file, resets internal variables
      */
    void startLogger() {
        // Open output file with timestamp
        boost::posix_time::ptime now = boost::posix_time::second_clock::local_time();
        std::stringstream filename;
        filename << "/home/nao/naoqi/modules/logs/" << now.date().year() << "_" << static_cast<int>(now.date().month())
                 << "_" << now.date().day() << "_" <<  now.time_of_day().hours() << now.time_of_day().minutes() << "_ResponseToName.txt";
        outputFileLock.lock();
        outputFile.open(filename.str().c_str(), std::ios::out);
        outputFileLock.unlock();

        // Calculate sessionStart time, reset internal variables
        sessionStart = boost::get_system_time();
        iteration = 0;
        faceCount = 0;
        end = false;
        childCount++;
        // Session is starting, subscribe to external events and start sound classification
        try {
            memoryProxy->subscribeToEvent("FaceDetected", "ResponseToNameLogger", "onFaceDetected");
            memoryProxy->subscribeToEvent("ChildCalled", "ResponseToNameLogger", "onChildCalled");
            memoryProxy->subscribeToEvent("EndSession", "ResponseToNameLogger", "onStopLogger");
            memoryProxy->subscribeToEvent("KlasifikacijaGovoraEvent", "ResponseToNameLogger", "onSoundClassified");
            classificationProxy->callVoid("pocniKlasifikaciju", 6000, 10, 3, 16000, AL::ALValue(3), 8192*2);
        }
        catch (const AL::ALError& e) {
            qiLogError("ResponseToNameLogger") << "Error subscribing to events" << e.toString() << std::endl;
        }

        // Start scheduler thread
        t = new boost::thread(boost::ref(*module.impl));
    }

    /**
      * Function used to stop the logger, called by the callback reacting to "EndSession" event
      */
    void stopLogger() {
        // close the output file
        outputFileLock.lock();
        outputFile.close();
        outputFileLock.unlock();

        // stop scheduler thread
        t->interrupt();
        t->join();

        // unsubscribe from FaceDetected event
        try {
            memoryProxy->unsubscribeToEvent("FaceDetected", "ResponseToNameLogger");
        }
        catch (const AL::ALError& e) {
            qiLogError("ResponseToNameLogger") << "Error unsubscribing from FaceDeteced" << e.toString() << std::endl;
        }
    }

    /**
      * Operator () implements scheduler thread
      */
    void operator()() {
        // In the start, initialize lastFace time
        lastFace = boost::get_system_time();

        // Start thread loop
        while( true ) {

            // Do until thread_interrupted is raised
            try {
                // Child responded after being called at least once (response = 5 consecutive face appearances)
                if( iteration >= 1 && faceCount >=5) {
                    // Log SE - session ended event with value 1 - child responded
                    log("SE", 1);
                    // Raise EndSession event
                    memoryProxy->raiseEvent("EndSession", AL::ALValue(1));
                }

                // Calculate durations from lastFace and lastCall
                boost::system_time now = boost::get_system_time();
                boost::posix_time::time_duration timeDiff = now - lastFace;
                long long sinceLastFace = timeDiff.total_milliseconds();
                timeDiff = now - lastCall;
                long long sinceLastCall = timeDiff.total_milliseconds();

                // Check if five seconds have past from last call or last face appearance
                if( sinceLastFace >= 5000 && sinceLastCall >= 5000 && !end){
                    // robot will call the child, stop sound classification
                    classificationProxy->callVoid("prekiniKlasifikaciju");
                    // For first five iterations
                    if( iteration < 5 ) {
                        // Log that the call should have started - CS = call started
                        log("CS", iteration+1);
                        // Reset face counter
                        faceCount = 0;
                        // Raise event CallChild with value 1 meaning "Call by name"
                        memoryProxy->raiseEvent("CallChild", AL::ALValue(1));
                        // Update the time of the last call
                        lastCall = boost::get_system_time();
                    }
                    // Sixth and seventh iteration
                    else if( iteration < 7 ) {
                        // Log that the call using special phrase started - PS = phrase started
                        log("PS", iteration-4);
                        // Reset face counter
                        faceCount = 0;
                        // Raise CallChild event with value 2 meaning "Use special phrase"
                        memoryProxy->raiseEvent("CallChild", AL::ALValue(2));
                        // Update the time of the last call
                        lastCall = boost::get_system_time();
                    }
                    // Child did not respond at all, end session
                    else {
                        // Log "EndSession" event with value -1 meaning child did not respond
                        log("SE", -1);
                        end = true;
                        // Raise EndSession event with value -1
                        memoryProxy->raiseEvent("EndSession", AL::ALValue(-1));
                    }
                }
                boost::this_thread::sleep(boost::posix_time::milliseconds(100));
            }
            // Catch thread_interrupted
            catch(boost::thread_interrupted&) {
                // Exit
                return;
            }
        }
    }
};

ResponseToNameLogger::ResponseToNameLogger(boost::shared_ptr<AL::ALBroker> pBroker, const std::string& pName) :  AL::ALModule(pBroker, pName) {

    setModuleDescription("Module scheduling the calls and logging events");

    functionName("onFaceDetected", getName(), "Callback for FaceDetected event");
    BIND_METHOD(ResponseToNameLogger::onFaceDetected);

    functionName("onStartLogger", getName(), "Callback for SessionStart event");
    BIND_METHOD(ResponseToNameLogger::onStartLogger);

    functionName("onStopLogger", getName(), "Callback for EndSession event");
    BIND_METHOD(ResponseToNameLogger::onStopLogger);

    functionName("onChildCalled", getName(), "Callback for ChildCalled event");
    BIND_METHOD(ResponseToNameLogger::onChildCalled);

    functionName("onSoundClassified", getName(), "Callback for ChildCalled event");
    BIND_METHOD(ResponseToNameLogger::onSoundClassified);
}

ResponseToNameLogger::~ResponseToNameLogger() {
    //Cleanup code
}

void ResponseToNameLogger::init() {
    // This method overrides ALModule::init
    try {
        // Create object
        impl = boost::shared_ptr<Impl>(new Impl(*this));
        // Initialize ALModule
        AL::ALModule::init();
    }
    catch (const AL::ALError& e) {
        qiLogError("ResponseToNameLogger") << e.what() << std::endl;
    }
    qiLogVerbose("ResponseToNameLogger") << "ResponseToName Logger initialized" << std::endl;
}

void ResponseToNameLogger::onFaceDetected() {
    // Code is thread safe as long as ALCriticalSection object exists
    AL::ALCriticalSection section(impl->fCallbackMutex);
    // Obtain FaceDetected data to check validity of the face
    // Must be called before the unsubscribeToEvent method
    AL::ALValue face = impl->memoryProxy->getData("FaceDetected");
    // Unsubscribe to prevent repetitive callbackss
    impl->memoryProxy->unsubscribeToEvent("FaceDetected", "ResponseToNameLogger");
    // Update the lastFace time
    impl->lastFace = boost::get_system_time();

    // Check validity of the face
    if( face.getSize() < 2 ) {
        qiLogError("ResponseToNameLogger") << "Face detected but data is invalid, size " << face.getSize() << std::endl;
    }
    else {
        // Log the appearance of the face
        impl->log("FD", ++impl->faceCount);
    }
    // Subscribe to FaceDetected
    impl->memoryProxy->subscribeToEvent("FaceDetected", "ResponseToNameLogger", "onFaceDetected");
}

void ResponseToNameLogger::onStartLogger() {
    // Thread safety of the callback
    AL::ALCriticalSection section(impl->fCallbackMutex);
    // Unsubscribe from event, maybe this can be omitted
    impl->memoryProxy->unsubscribeToEvent("StartSession", "ResponseToNameLogger");

    // Session is starting, initialize logger module and start scheduler thread
    impl->startLogger();

    // During the session this module must react to ChildCalled event
    impl->memoryProxy->subscribeToEvent("ChildCalled", "ResponseToNameLogger", "onChildCalled");
}

void ResponseToNameLogger::onStopLogger(const std::string &key, const AL::ALValue &value, const AL::ALValue &msg) {
    // Thread safety of the callback
    AL::ALCriticalSection section(impl->fCallbackMutex);
    // Unsubscriptions
    impl->memoryProxy->unsubscribeToEvent("EndSession", "ResponseToNameLogger");

    // Interupt the execution of the scheduler thread
    impl->t->interrupt();
    // Wait for thread to exit
    impl->t->join();

    // Event subscription management, stop sound classification
    try {
        impl->memoryProxy->unsubscribeToEvent("FaceDetected", "ResponseToNameLogger");
        impl->memoryProxy->subscribeToEvent("StartSession", "ResponseToNameLogger", "onStartLogger");
        impl->classificationProxy->callVoid("prekiniKlasifikaciju");
        impl->memoryProxy->unsubscribeToEvent("KlasifikacijaGovoraEvent", "ResponseToNameLogger");
    }
    catch (const AL::ALError& e) {
        qiLogError("ResponseToNameLogger") << "Error managing events" << e.toString() << std::endl;
    }

    // Close the output file
    impl->outputFileLock.lock();
    impl->outputFile.close();
    impl->outputFileLock.unlock();

}

void ResponseToNameLogger::onChildCalled(const std::string &key, const AL::ALValue &value, const AL::ALValue &msg) {
    // Thread safety of the callback
    AL::ALCriticalSection section(impl->fCallbackMutex);
    // Unsubscription
    impl->memoryProxy->unsubscribeToEvent("ChildCalled", "ResponseToNameLogger");
    // Update the time of the last call
    impl->lastCall = boost::get_system_time();
    // Increase iteration number
    impl->iteration++;
    // Log that the Interface module has ended the call
    impl->log("CE", (int)impl->iteration);
    // Robot has finished making sounds, restart the sound classification module
    impl->classificationProxy->callVoid("pocniKlasifikaciju");
    // Subscribe back to the same event
    impl->memoryProxy->subscribeToEvent("ChildCalled", "ResponseToNameLogger", "onChildCalled");
}

void ResponseToNameLogger::onSoundClassified(const std::string &key, const AL::ALValue &value, const AL::ALValue &msg) {
    // Thread safety of the callback
    AL::ALCriticalSection section(impl->fCallbackMutex);
    // Unsubscription
    impl->memoryProxy->unsubscribeToEvent("KlasifikacijaGovoraEvent", "ResponseToNameLogger");
    //qiLogWarning("Logger") << "Sound detected, reading value" << std::endl;
    // Log that the sound classification module has detected sounds
    std::string klasa = (std::string)value[0];
    if(klasa=="Neartikulirano") impl->log("SC", 0);
    else if( klasa=="Artikulirano") impl->log("SC", 1);
    // Subscribe back to the same event
    impl->memoryProxy->subscribeToEvent("KlasifikacijaGovoraEvent", "ResponseToNameLogger", "onSoundClassified");
}
