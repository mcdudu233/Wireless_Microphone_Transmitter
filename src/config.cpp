#include "logger.h"
#include "config.h"

#include "Preferences.h"

config::StatusValue config::status;
config::ConfigValue config::config;

static Preferences prefs;

void config::setup()
{
  logger::debugln("Config is starting...");

  prefs.begin(CONFIG_NAME);

  if (!prefs.isKey(CONFIG_VERSION_NAME))
  {
    // 不存则新建配置
    prefs.putUShort(CONFIG_VERSION_NAME, CONFIG_VERSION_VALUE);
    prefs.putBytes(CONFIG_DATA_NAME, &config, sizeof(ConfigValue));
  }
  else
  {
    if (prefs.getUShort(CONFIG_VERSION_NAME) != CONFIG_VERSION_VALUE)
    {
      // 版本不一致重置配置
      prefs.clear();
      prefs.putUShort(CONFIG_VERSION_NAME, CONFIG_VERSION_VALUE);
      prefs.putBytes(CONFIG_DATA_NAME, &config, sizeof(ConfigValue));
    }
  }

  // 读取配置
  prefs.getBytes(CONFIG_DATA_NAME, &config, sizeof(ConfigValue));

  logger::debugln("Config is started.");
}