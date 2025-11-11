#pragma once

namespace logger
{
  void setup();

  template <class T, typename... Args>
  void debug(T msg, Args... args);
  template <class T, typename... Args>
  void info(T msg, Args... args);
  template <class T, typename... Args>
  void warn(T msg, Args... args);
  template <class T, typename... Args>
  void error(T msg, Args... args);
  template <class T, typename... Args>
  void debugln(T msg, Args... args);
  template <class T, typename... Args>
  void infoln(T msg, Args... args);
  template <class T, typename... Args>
  void warnln(T msg, Args... args);
  template <class T, typename... Args>
  void errorln(T msg, Args... args);
}
