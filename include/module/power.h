#pragma once

#define BUTTON_IO GPIO_NUM_7
#define BUTTON_SHUTDOWN_TIME (2 * 1000) // 长按3秒钟关机

namespace power
{
  void setup();
  void deepSleep();
}