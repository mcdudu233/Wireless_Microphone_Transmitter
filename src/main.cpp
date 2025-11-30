#include "logger.h"
#include "module/led.h"
#include "module/voltage.h"
#include "module/audio/power.h"
#include "module/audio/encoder.h"
#include "module/usb/usb.h"
#include "module/rf.h"

#include "module/usb/usb_device_cdc.h"
#include "esp_psram.h"

void setup()
{
  logger::setup();
  led::setup();
  led::green();
  voltage::setup();
  audio::power::setup();
  audio::encoder::setup();
  // usb::setup();
  rf::setup();

  logger::infoln("All modules are started now!");
}

void loop()
{
  // IRAM
  logger::debugln("Internal:\n");
  logger::debugln("  Total: %d bytes\n", heap_caps_get_total_size(MALLOC_CAP_INTERNAL));
  logger::debugln("  Free: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  logger::debugln("  Min Free: %d bytes\n", heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
  // PSRAM
  logger::debugln("PSRAM:\n");
  logger::debugln("  Total: %d bytes\n", heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
  logger::debugln("  Free: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  logger::debugln("  Min Free: %d bytes\n", heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
  delay(10000);
}
