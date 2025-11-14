#include "logger.h"
#include "module/led.h"
#include "module/voltage.h"
#include "module/audio/power.h"
#include "module/audio/encoder.h"

void setup()
{
  logger::setup();
  led::setup();
  voltage::setup();
  audio::power::setup();
  audio::encoder::setup();
  logger::infoln("All modules are started now!");
  audio::encoder::on();
}

void loop()
{
  // led::blue();
  // delay(100);
  // led::black();
  // delay(100);
  // logger::infoln("VCC: %D", voltage::getVCCVoltage());
  // logger::infoln("BAT: %D", voltage::getBATVoltage());
  delay(100);
}
