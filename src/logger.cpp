#include "logger.h"
#include "module/led.h"
#include "module/usb/usb_device_cdc.h"

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

void logger::error()
{
  // 程序遇到了严重错误 暂停所有操作
  // 闪灯显示错误状态
  led::black();
  while (true)
  {
    led::rgb(255, 255, 0);
    delay(500);
    led::rgb(255, 0, 0);
    delay(500);
  }
}
