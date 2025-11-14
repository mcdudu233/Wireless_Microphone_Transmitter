#pragma once

#define AUDIO_POWER_IO 9

namespace audio::power{
  void setup();
  void on();
  void off();
  bool isOn();
}