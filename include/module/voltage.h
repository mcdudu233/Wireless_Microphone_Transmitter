#pragma once

#define ADC_BAT_IO 1
#define ADC_VCC_IO 4

namespace voltage
{
  void setup();
  float getVCCVoltage();
  float getBATVoltage();
}