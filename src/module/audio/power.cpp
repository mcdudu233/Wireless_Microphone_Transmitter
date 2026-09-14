#include "logger.h"
#include "module/audio/power.h"

static bool powerOn = false;
void audio::power::setup()
{
  LOGGER_INFO("Audio Power is starting...");
  pinMode(AUDIO_POWER_IO, OUTPUT);
  digitalWrite(AUDIO_POWER_IO, LOW);
  LOGGER_INFO("Audio Power is started!");
}

void audio::power::on()
{
  if (!powerOn)
  {
    digitalWrite(AUDIO_POWER_IO, HIGH);
    powerOn = true;
    LOGGER_INFO("Audio Power is on.");
  }
}

void audio::power::off()
{
  if (powerOn)
  {
    digitalWrite(AUDIO_POWER_IO, LOW);
    powerOn = false;
    LOGGER_INFO("Audio Power is off.");
  }
}

bool audio::power::isOn()
{
  return powerOn;
}
