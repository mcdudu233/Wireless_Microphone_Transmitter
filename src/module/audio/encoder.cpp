#include "config.h"
#include "logger.h"
#include "module/audio/power.h"
#include "module/audio/encoder.h"

#include "tusb.h"
#include "ESP_I2S.h"
#include "wav_header.h"

#include "module/usb/usb_device_cdc.h"

static I2SClass I2S;
static uint32_t i2s_rate;
static i2s_data_bit_width_t i2s_bit;
QueueHandle_t audio::encoder::data;

static bool powerOn = false;

// 实时处理音频数据
// static int64_t read_len = 0;
// static unsigned long last_time = millis();
static void audioHandle(void *arg)
{
  while (true)
  {
    // 启动了芯片才读取数据
    if (powerOn)
    {
      delay(15 * 1000);
      // uint32_t sample_rate = 48000;
      // uint16_t sample_width = 32;
      // uint16_t num_channels = 2;
      // size_t rec_size = 15 * ((sample_rate * (sample_width / 8)) * num_channels);
      // const pcm_wav_header_t wav_header = PCM_WAV_HEADER_DEFAULT(rec_size, sample_width, sample_rate, num_channels);
      // logger::debugln("Record WAV: rate:%lu, bits:%u, channels:%u, size:%lu", sample_rate, sample_width, num_channels, rec_size);

      // uint8_t *wav_buf = (uint8_t *)heap_caps_malloc(rec_size + PCM_WAV_HEADER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
      // if (wav_buf == NULL)
      // {
      //   logger::debugln("Failed to allocate WAV buffer with size %u", rec_size + PCM_WAV_HEADER_SIZE);
      //   return;
      // }
      // memcpy(wav_buf, &wav_header, PCM_WAV_HEADER_SIZE);
      // size_t wav_size = I2S.readBytes((char *)(wav_buf + PCM_WAV_HEADER_SIZE), rec_size);
      // if (wav_size < rec_size)
      // {
      //   logger::debugln("Recorded %u bytes from %u", wav_size, rec_size);
      // }
      // else
      // {
      //   USBCDCSerial.write(wav_buf, rec_size + PCM_WAV_HEADER_SIZE);
      // }

      // // if (millis() - last_time >= 1000)
      // // {
      // //   last_time = millis();
      // //   logger::debugln("%d Kbps", read_len * 8);
      // //   read_len = 0;
      // // }
      // if (I2S.available() != -1)
      // {
      //   size_t size = i2s_rate * 4 * 2 / 1000;
      //   char *buffer = new char[size];
      //   if (I2S.readBytes(buffer, size) == size)
      //   {
      //     // if (xQueueSend(audio::encoder::data, (uint8_t *)buffer, 0) != pdTRUE)
      //     // {
      //     //   delete[] buffer;
      //     //   logger::warnln("Audio Encoder's queue is full!");
      //     // }
      //     tud_audio_write(buffer, size);
      //     delete[] buffer;
      //   }
      //   else
      //   {
      //     logger::warnln("Audio Encoder's I2S read fail! Size not same!");
      //   }
      //   // read_len++;
      // }
    }
    else
    {
      vTaskDelay(10);
    }
  }
}

void audio::encoder::setup()
{
  audio::encoder::data = xQueueCreate(AUDIO_ENCODER_MAX_QUEUE_SIZE, sizeof(uint8_t *));
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
  I2S.begin(I2S_MODE_STD, rate, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
  i2s_rate = rate;
  i2s_bit = (i2s_data_bit_width_t)bit;
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