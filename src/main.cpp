#include "logger.h"
#include "module/led.h"
#include "module/voltage.h"
#include "module/audio/power.h"
#include "module/audio/encoder.h"
#include "module/usb/usb.h"
#include "module/ble.h"

#include "module/usb/usb_device_cdc.h"
#include "esp_psram.h"

void setup()
{
  usb::setup();
  logger::setup();
  led::setup();
  voltage::setup();
  audio::power::setup();
  audio::encoder::setup();
  ble::setup();
  logger::infoln("All modules are started now!");

  audio::encoder::on(48000);
}

void loop()
{
  // led::blue();
  // delay(100);
  // led::black();
  // delay(100);
  // logger::infoln("VCC: %D", voltage::getVCCVoltage());
  // logger::infoln("BAT: %D", voltage::getBATVoltage());
  // if (esp_psram_is_initialized())
  // {
  //   logger::debugln("PSRAM is enabled.\n");

  //   // 获取 PSRAM 总大小
  //   size_t psram_total = esp_psram_get_size();
  //   logger::debugln("Total PSRAM: %u bytes\n", psram_total);

  //   // 获取 PSRAM 剩余空间
  //   size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  //   logger::debugln("Free PSRAM: %u bytes\n", psram_free);
  // }
  // else
  // {
  //   logger::debugln("PSRAM is not enabled.\n");
  // }
  delay(1000);
}
