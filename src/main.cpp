#include "logger.h"
#include "config.h"
#include "sys.h"
#include "module/led.h"
#include "module/power.h"
#include "module/audio/power.h"
#include "module/audio/buffer.h"
#include "module/audio/encoder.h"
#include "module/usb/usb.h"
#include "module/rf.h"

#include "module/usb/usb_device_cdc.h"
#include "esp_psram.h"

extern "C" void app_main()
{
  logger::setup();
  config::setup();
  led::setup();
  power::setup();
  // sys::setup();
  audio::power::setup();
  audio::buffer::setup();
  audio::encoder::setup();
  // TODO: usb::setup();
  rf::setup();

  logger::infoln("All modules are started now!");
}