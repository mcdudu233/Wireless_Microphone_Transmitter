#include "config.h"
#include "logger.h"
#include "module/audio/power.h"
#include "module/audio/encoder.h"

#include "cctype"
#include "ESP_I2S.h"
#include "wav_header.h"

static i2s_chan_handle_t i2s_rx_handle;
static i2s_chan_config_t i2s_chan_cfg = {
    .id = I2S_NUM_AUTO,
    .role = I2S_ROLE_MASTER,
    .dma_desc_num = 4,    // 多少个DMA
    .dma_frame_num = 384, // 每个DMA大小
    .auto_clear_after_cb = false,
    .auto_clear_before_cb = false,
    .allow_pd = false,
    .intr_priority = 0,
};
static i2s_std_gpio_config_t i2s_gpio_cfg = {
    .mclk = I2S_GPIO_UNUSED,
    .bclk = AUDIO_ENCODER_CLK,
    .ws = AUDIO_ENCODER_WS,
    .dout = I2S_GPIO_UNUSED,
    .din = AUDIO_ENCODER_SD,
    .invert_flags = {
        .mclk_inv = false,
        .bclk_inv = false,
        .ws_inv = false,
    }};
static uint32_t i2s_rate;
static i2s_data_bit_width_t i2s_bit;
static bool powerOn = false;

// 循环缓冲区
static AudioData *data;
static uint8_t data_pointer;
static uint32_t data_number;

// 实时处理音频数据
// static int64_t read_len = 0;
// static unsigned long last_time = millis();
static void audioHandle(void *arg)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(TASK_AUDIO_ENCODER_PERIOD);
  while (true)
  {
    xTaskDelayUntil(&xLastWakeTime, xFrequency);

    // 启动了芯片才读取数据
    if (powerOn)
    {
      // 录制 WAV 测试
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

      size_t size = i2s_rate * AUDIO_ENCODER_BIT * AUDIO_ENCODER_CHANNEL / 8 * AUDIO_ENCODER_POLLING_CYCLE / 1000;
      AudioData &buffer = data[data_pointer];
      data_pointer = (data_pointer + 1) % AUDIO_ENCODER_MAX_BUFFER_SIZE;
      buffer.num = data_number++ % UINT32_MAX;
      buffer.size = size;
      if (i2s_channel_read(i2s_rx_handle, buffer.data, size, NULL, AUDIO_ENCODER_POLLING_CYCLE) != ESP_OK)
      {
        logger::warnln("Audio Encoder's I2S read fail! Size not same!");
      }
      // read_len++;
    }
  }
}

void audio::encoder::setup()
{
  data = (AudioData *)heap_caps_malloc(sizeof(AudioData) * AUDIO_ENCODER_MAX_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
  pinMode(AUDIO_ENCODER_MD0, OUTPUT);
  pinMode(AUDIO_ENCODER_MD1, OUTPUT);
  digitalWrite(AUDIO_ENCODER_MD0, LOW);
  digitalWrite(AUDIO_ENCODER_MD1, LOW);
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

  // 刷新缓存
  for (int i = 0; i < AUDIO_ENCODER_MAX_BUFFER_SIZE; i++)
  {
    data[i].num = 0;
    data[i].size = 0;
  }
  data_pointer = 0;
  data_number = 0;
  // 启动 i2s
  i2s_rate = rate;
  i2s_bit = (i2s_data_bit_width_t)bit;
  i2s_new_channel(&i2s_chan_cfg, NULL, &i2s_rx_handle);
  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(i2s_rate),
      .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = i2s_gpio_cfg,
  };
  i2s_channel_init_std_mode(i2s_rx_handle, &std_cfg);
  i2s_channel_enable(i2s_rx_handle);
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
    i2s_channel_disable(i2s_rx_handle);
    i2s_del_channel(i2s_rx_handle);
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

uint8_t audio::encoder::getNumber()
{
  return data_pointer;
}

AudioData *audio::encoder::getData()
{
  return getDataFromIndex(0);
}

AudioData *audio::encoder::getDataFromIndex(uint8_t index)
{
  return &data[(data_pointer + AUDIO_ENCODER_MAX_BUFFER_SIZE - 1 - index) % AUDIO_ENCODER_MAX_BUFFER_SIZE];
}

AudioData *audio::encoder::getDataFromNumber(uint32_t number)
{
  int32_t now = getData()->num;
  if (number > now)
  {
    return nullptr;
  }
  else if (number == now)
  {
    return getData();
  }
  else
  {
    if ((now - number) >= AUDIO_ENCODER_MAX_BUFFER_SIZE)
    {
      return nullptr;
    }
    return getDataFromIndex(now - number);
  }
}