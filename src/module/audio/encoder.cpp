#include "config.h"
#include "logger.h"
#include "module/audio/power.h"
#include "module/audio/encoder.h"

#include "ESP_I2S.h"

static I2SClass I2S;
const int audio::encoder::buffer_size = 1024;
char audio::encoder::buffer[1024];

static bool powerOn = false;

// 实时处理音频数据
static int64_t read_len = 0;
static unsigned long last_time = millis();
static void audioHandle(void *arg)
{
  while (true)
  {
    // 启动了芯片才读取数据
    if (powerOn)
    {
      if (millis() - last_time >= 1000)
      {
        last_time = millis();
        logger::debugln("%d Kbps", read_len / 1024);
        read_len = 0;
      }
      if (I2S.available() != -1)
      {
        int len = I2S.readBytes(audio::encoder::buffer, audio::encoder::buffer_size);
        read_len += len;
      }
    }
    else
    {
      vTaskDelay(10);
    }
  }
}

void audio::encoder::setup()
{
  pinMode(AUDIO_ENCODER_MD0, OUTPUT);
  pinMode(AUDIO_ENCODER_MD1, OUTPUT);
  digitalWrite(AUDIO_ENCODER_MD0, LOW);
  digitalWrite(AUDIO_ENCODER_MD1, LOW);
  I2S.setPins(AUDIO_ENCODER_CLK, AUDIO_ENCODER_WS, -1, AUDIO_ENCODER_SD, -1); // SCK, WS, SDOUT, SDIN, MCLK
  xTaskCreatePinnedToCore(audioHandle, "audio_encoder_handle", TASK_AUDIO_ENCODER_STACK, NULL, TASK_AUDIO_ENCODER_PRIORITY, NULL, TASK_AUDIO_ENCODER_CORE);
  logger::debugln("Audio Encoder is started!");
}

void audio::encoder::on(uint32_t rate, uint32_t bit)
{
  if (powerOn)
  {
    off();
  }
  else
  {
    powerOn = true;
  }

  if (!audio::power::isOn())
  {
    audio::power::on();
  }
  I2S.begin(I2S_MODE_STD, rate, (i2s_data_bit_width_t)bit, I2S_SLOT_MODE_STEREO);
  logger::debugln("Audio Encoder is on.");
}

void audio::encoder::off()
{
  if (powerOn)
  {
    powerOn = false;
    if (audio::power::isOn()
        // && !audio::decoder::isOn()
    )
    {
      audio::power::off();
    }
    I2S.end();
    logger::debugln("Audio Encoder is off.");
  }
}

bool audio::encoder::isOn()
{
  return powerOn;
}

void audio::encoder::setLowLatencyFilter(bool on)
{
  if (on)
  {
    digitalWrite(AUDIO_ENCODER_MD0, HIGH);
  }
  else
  {
    digitalWrite(AUDIO_ENCODER_MD0, LOW);
  }
}

void audio::encoder::setDRE(bool on)
{
  if (on)
  {
    digitalWrite(AUDIO_ENCODER_MD1, HIGH);
  }
  else
  {
    digitalWrite(AUDIO_ENCODER_MD1, LOW);
  }
}