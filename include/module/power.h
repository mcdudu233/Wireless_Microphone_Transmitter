#pragma once

#define BUTTON_IO GPIO_NUM_7
#define BUTTON_SHUTDOWN_TIME (2 * 1000) // 长按3秒钟关机

#define CHARGING_IO GPIO_NUM_8
#define BATTERY_MAX 4.2 // 电池最高电压
#define BATTERY_MIN 3.6 // 电池最低电压

#define ADC_BAT_IO GPIO_NUM_1
#define ADC_VCC_IO GPIO_NUM_4
#define ADC_POWER_SUPPLY 4.4 // 判断USB还是电池供电的分界线 4.4V

namespace power
{
  void setup();
  void deepSleep();

  // 获取电压
  double getVCCVoltage();
  double getBATVoltage();
  // 获取供电方式
  bool isUSBSupply();
  bool isBATSupply();
  // 是否正在充电
  bool isCharging();
  // 获取电量
  double getBATPercent();
}