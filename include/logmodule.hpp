#ifndef LOGGER_H
#define LOGGER_H

#include <boost/shared_ptr.hpp>
#include <alcommon/almodule.h>
#include <string>

#include <alproxies/almemoryproxy.h>
#include <alproxies/altexttospeechproxy.h>
#include <KlasifikacijaGovora.h>
#include <althread/almutex.h>

namespace AL
{
  class ALBroker;
}


/**
  * Class used to process FaceDetected events and schedule the calls
  */
class ResponseToNameLogger : public AL::ALModule
{
  public:

    /**
      * Standard default constructor which will be called by the local NAOqi of the robot
      */
    ResponseToNameLogger(boost::shared_ptr<AL::ALBroker> broker, const std::string& name);

    /**
      * Default destructor
      */
    virtual ~ResponseToNameLogger();

    /**
      * Method overloading overloading ALModule::init().
      * This is called right after the module has been loaded by NAOqi
      */
    virtual void init();

    /**
      * This method will be called every time the event FaceDetected is raised
      * Each occurence of the face will be logged
      */
    void onFaceDetected();

    /**
      * This method will be called when StartSession event is raised
      * Logger will be configured for a new session by setting new output file and resetting internal variables
      * Scheduler thread will be started
      */
    void onStartLogger();

    /**
      * This method will be called when EndSession event is raised
      * Logger will close the output file and stop scheduler thread
      */
    void onStopLogger(const std::string &key, const AL::ALValue &value, const AL::ALValue &msg);

    /**
      * This method will be called when ChildCalled event is raised
      * Iteration count will be updated
      */
    void onChildCalled(const std::string &key, const AL::ALValue &value, const AL::ALValue &msg);

    /**
      * This method will be called when SoundClassified event is raised
      */
    void onSoundClassified(const std::string &key, const AL::ALValue &value, const AL::ALValue &msg);

  private:
    /**
      * Object implementation
      */
    struct Impl;

    /**
      * Pointer to class implementation
      */
    boost::shared_ptr<Impl> impl;
};

#endif
