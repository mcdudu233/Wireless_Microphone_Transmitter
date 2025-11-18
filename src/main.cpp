#include "logger.h"
#include "module/led.h"
#include "module/voltage.h"
#include "module/audio/power.h"
#include "module/audio/encoder.h"
#include "module/usb/usb.h"

#include "module/usb/usb_device_cdc.h"

// #include <BLEDevice.h>
// #include <BLEUtils.h>
// #include <BLEScan.h>
// #include <BLEAdvertisedDevice.h>

void setup()
{
  usb::setup();
  logger::setup();
  led::setup();
  voltage::setup();
  audio::power::setup();
  audio::encoder::setup();
  logger::infoln("All modules are started now!");
  audio::encoder::on(48000, 16);

  // BLEDevice::init("test");
}

void loop()
{
  // led::blue();
  // delay(100);
  // led::black();
  // delay(100);
  // logger::infoln("VCC: %D", voltage::getVCCVoltage());
  // logger::infoln("BAT: %D", voltage::getBATVoltage());
  // logger::debugln("test");
  delay(100);
}
