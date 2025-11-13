#include "logger.h"
#include "module/led.h"

void setup()
{
  logger::setup();
  led::setup();
  logger::infoln("All modules are started now!");
}

void loop()
{
  led::blue();
  delay(100);
  led::black();
  delay(100);
}
