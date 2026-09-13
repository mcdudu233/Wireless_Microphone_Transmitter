#pragma once

#define AUDIO_POWER_IO GPIO_NUM_38

// PCM1822: power-down to power-up must be at least 100 ms. Give the board
// regulators additional time to settle before BCLK/FSYNC are enabled.
#define AUDIO_POWER_MIN_OFF_TIME_MS 100
#define AUDIO_POWER_SETTLE_TIME_MS 10

namespace audio::power
{
  void setup();
  void on();
  void off();
  bool isOn();
}
