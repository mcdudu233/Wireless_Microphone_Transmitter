#pragma once

#define ADC_BAT_IO 1
#define ADC_VCC_IO 4

namespace voltage
{
  void setup();
  double getVCCVoltage();
  double getBATVoltage();
}