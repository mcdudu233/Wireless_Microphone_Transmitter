#include "logger.h"
#include "module/voltage.h"

#include "Arduino.h"

void voltage::setup()
{
  // 分辨率
  analogReadResolution(16);
  // 衰减 0~3100mV
  analogSetAttenuation(ADC_11db);
  logger::debugln("Voltage is started!");
}

double voltage::getVCCVoltage()
{
  return analogReadMilliVolts(ADC_VCC_IO) / 1000.0 * 3.0;
}

double voltage::getBATVoltage()
{
  return analogReadMilliVolts(ADC_BAT_IO) / 1000.0 * 2.0;
}