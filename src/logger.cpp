#include "logger.h"

#include "ArduinoLog.h"

void printTimestamp(Print *_logOutput)
{
  // Division constants
  const unsigned long MSECS_PER_SEC = 1000;
  const unsigned long SECS_PER_MIN = 60;
  const unsigned long SECS_PER_HOUR = 3600;
  const unsigned long SECS_PER_DAY = 86400;
  // Total time
  const unsigned long msecs = millis();
  const unsigned long secs = msecs / MSECS_PER_SEC;
  // Time in components
  const unsigned long MilliSeconds = msecs % MSECS_PER_SEC;
  const unsigned long Seconds = secs % SECS_PER_MIN;
  const unsigned long Minutes = (secs / SECS_PER_MIN) % SECS_PER_MIN;
  const unsigned long Hours = (secs % SECS_PER_DAY) / SECS_PER_HOUR;
  // Time as string
  char timestamp[20];
  sprintf(timestamp, "%02lu:%02lu:%02lu.%03lu ", Hours, Minutes, Seconds, MilliSeconds);
  _logOutput->print(timestamp);
}

void printLogLevel(Print *_logOutput, int logLevel)
{
  switch (logLevel)
  {
  case 2:
    _logOutput->print("ERROR ");
    break;
  case 3:
    _logOutput->print("WARNING ");
    break;
  case 4:
    _logOutput->print("INFO ");
    break;
  case 6:
    _logOutput->print("DEBUG ");
    break;
  default:
    _logOutput->print("UNKNOW");
    break;
  }
}

void printPrefix(Print *_logOutput, int logLevel)
{
  printTimestamp(_logOutput);
  printLogLevel(_logOutput, logLevel);
}

void printSuffix(Print *_logOutput, int logLevel)
{
  _logOutput->print("");
}

void logger::setup()
{
  Log.setPrefix(printPrefix);
  Log.setSuffix(printSuffix);
  Log.setShowLevel(false);

  // 初始化串口
  Serial.begin(115200);
  while (!Serial && !Serial.available())
    ;
  Log.begin(LOG_LEVEL_VERBOSE, &Serial);

  logger::debugln("Logger is started!");
}

template <class T, typename... Args>
void logger::debug(T msg, Args... args)
{
  Log.verbose(msg, args...);
}

template <class T, typename... Args>
void logger::info(T msg, Args... args)
{
  Log.info(msg, args...);
}

template <class T, typename... Args>
void logger::warn(T msg, Args... args)
{
  Log.warning(msg, args...);
}

template <class T, typename... Args>
void logger::error(T msg, Args... args)
{
  Log.error(msg, args...);
}

template <class T, typename... Args>
void logger::debugln(T msg, Args... args)
{
  Log.verboseln(msg, args...);
}

template <class T, typename... Args>
void logger::infoln(T msg, Args... args)
{
  Log.infoln(msg, args...);
}

template <class T, typename... Args>
void logger::warnln(T msg, Args... args)
{
  Log.warningln(msg, args...);
}

template <class T, typename... Args>
void logger::errorln(T msg, Args... args)
{
  Log.errorln(msg, args...);
}