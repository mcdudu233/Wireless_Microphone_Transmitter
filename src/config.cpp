#include "logger.h"
#include "config.h"

#include "Preferences.h"

config::ConfigValue config::value;

static Preferences prefs;

void config::setup()
{
  logger::debugln("Config is starting...");

  prefs.begin(CONFIG_NAME);
  // TODO 添加配置

  logger::debugln("Config is started.");
}