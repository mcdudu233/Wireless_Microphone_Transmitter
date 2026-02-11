#include "logger.h"
#include "module/power.h"
#include "module/led.h"
#include "module/usb/usb_device_cdc.h"

#include "cstdarg"

void logger::setup()
{
  // 根据构建类型设置日志级别
#if defined(BUILD_RELEASE)
  esp_log_level_set("*", ESP_LOG_WARN);
#elif defined(BUILD_DEBUG)
  esp_log_level_set("*", ESP_LOG_INFO);
#endif

  // 重定向 ESP-IDF 日志输出
  // esp_log_set_vprintf(esp_apptrace_vprintf);
  // Log.begin(LOG_LEVEL_VERBOSE, &USBCDCSerial);

  LOGGER_INFO("Logger is started!");
}

void logger::error(char *str)
{
  // 程序遇到了严重错误 闪灯显示错误状态
  uint8_t i = 0;
  led::black();
  while (i++ < 10)
  {
    led::rgb(255, 255, 0);
    delay(250);
    led::rgb(255, 0, 0);
    delay(250);
  }
  // 闪灯后重启系统
  power::core_restart();
}
