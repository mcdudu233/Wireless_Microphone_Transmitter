#pragma once

#define AUDIO_POWER_IO GPIO_NUM_9

namespace audio::power
{
  void setup();
  void on();
  void off();
  bool isOn();
}