#include "logger.h"
#include "module/audio/power.h"

static bool powerOn = false;
static uint32_t powerOffTime = 0;

void audio::power::setup()
{
  LOGGER_INFO("Audio Power is starting...");
  pinMode(AUDIO_POWER_IO, OUTPUT);
  digitalWrite(AUDIO_POWER_IO, LOW);
  powerOffTime = millis();
  LOGGER_INFO("Audio Power is started!");
}

void audio::power::on()
{
  if (!powerOn)
  {
    const uint32_t off_time = millis() - powerOffTime;
    if (off_time < AUDIO_POWER_MIN_OFF_TIME_MS)
    {
      vTaskDelay(pdMS_TO_TICKS(AUDIO_POWER_MIN_OFF_TIME_MS - off_time));
    }
    digitalWrite(AUDIO_POWER_IO, HIGH);
    // Do not start PCM1822 clocks until AVDD and IOVDD are stable.
    vTaskDelay(pdMS_TO_TICKS(AUDIO_POWER_SETTLE_TIME_MS));
    powerOn = true;
    LOGGER_INFO("Audio Power is on and settled.");
  }
}

void audio::power::off()
{
  if (powerOn)
  {
    digitalWrite(AUDIO_POWER_IO, LOW);
    powerOn = false;
    powerOffTime = millis();
    LOGGER_INFO("Audio Power is off.");
  }
}

bool audio::power::isOn()
{
  return powerOn;
}
