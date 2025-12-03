#include "config.h"
#include "logger.h"
#include "module/audio/power.h"
#include "module/audio/encoder.h"

#include "cctype"
#include "driver/i2s_std.h"
#include "esp_ae_alc.h"
#include "esp_ae_bit_cvt.h"
#include "esp_ae_ch_cvt.h"

// I2S 参数
static const i2s_chan_config_t i2s_chan_cfg = {
    .id = I2S_NUM_AUTO,
    .role = I2S_ROLE_MASTER,
    .dma_desc_num = 4,    // 多少个DMA
    .dma_frame_num = 384, // 每个DMA大小
    .auto_clear_after_cb = false,
    .auto_clear_before_cb = false,
    .allow_pd = false,
    .intr_priority = 0,
};
static const i2s_std_gpio_config_t i2s_gpio_cfg = {
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
static const i2s_std_slot_config_t i2s_slot_cfg = {
    .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
    .slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT,
    .slot_mode = I2S_SLOT_MODE_STEREO,
    .slot_mask = I2S_STD_SLOT_BOTH,
    .ws_width = I2S_SLOT_BIT_WIDTH_32BIT,
    .ws_pol = false,
    .bit_shift = true,
    .left_align = false,
    .big_endian = false,
    .bit_order_lsb = false};

static i2s_chan_handle_t i2s_rx_handle;

// 原始数据
static uint32_t *i2s_data1;
static uint32_t *i2s_data2;
static uint32_t i2s_rate = 192 * 1000;
static i2s_data_bit_width_t i2s_bit = I2S_DATA_BIT_WIDTH_32BIT;
static uint8_t i2s_channel = 2;
static int8_t i2s_gain = 0;
static bool i2s_auto = false;
static bool i2s_peek = false;
// 音频处理
static esp_ae_alc_handle_t alc_handle = NULL;
static esp_ae_bit_cvt_handle_t bit_cvt_handle = NULL;
static esp_ae_ch_cvt_handle_t ch_cvt_handle = NULL;

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

      // size_t size = i2s_rate * AUDIO_ENCODER_BIT * AUDIO_ENCODER_CHANNEL / 8 * AUDIO_ENCODER_POLLING_CYCLE / 1000;
      // AudioData &buffer = data[data_pointer];
      // data_pointer = (data_pointer + 1) % AUDIO_ENCODER_MAX_BUFFER_SIZE;
      // buffer.num = data_number++ % UINT32_MAX;
      // buffer.size = size;
      size_t size = i2s_rate / 1000 * AUDIO_ENCODER_POLLING_CYCLE * AUDIO_ENCODER_BIT / 8 * AUDIO_ENCODER_CHANNEL;
      if (i2s_channel_read(i2s_rx_handle, i2s_data1, size, NULL, AUDIO_ENCODER_POLLING_CYCLE * 2) == ESP_OK)
      {
        size_t sample_num = size * 8 / AUDIO_ENCODER_BIT / AUDIO_ENCODER_CHANNEL;
        bool in_data1 = true;
        // 增益
        if (!i2s_peek)
        {
          esp_ae_alc_set_gain(alc_handle, 0, i2s_gain);
          esp_ae_alc_set_gain(alc_handle, 1, i2s_gain);
        }
        if (alc_handle != NULL)
        {
          if (esp_ae_alc_process(alc_handle, sample_num, i2s_data1, i2s_data2) == ESP_OK)
          {
            in_data1 = false;
          }
          else
          {
            logger::warnln("Audio Encoder's ALC process failed!");
          }
        }
        // 声道转换
        if (ch_cvt_handle != NULL)
        {
          if ((in_data1 ? esp_ae_ch_cvt_process(ch_cvt_handle, sample_num, i2s_data1, i2s_data2)
                        : esp_ae_ch_cvt_process(ch_cvt_handle, sample_num, i2s_data2, i2s_data1)) == ESP_OK)
          {
            in_data1 = in_data1 ? false : true;
          }
          else
          {
            logger::warnln("Audio Encoder's channel process failed!");
          }
        }
        // 比特转换
        if (bit_cvt_handle != NULL)
        {
          if ((in_data1 ? esp_ae_bit_cvt_process(bit_cvt_handle, sample_num, i2s_data1, i2s_data2)
                        : esp_ae_bit_cvt_process(bit_cvt_handle, sample_num, i2s_data2, i2s_data1)) == ESP_OK)
          {
            in_data1 = in_data1 ? false : true;
          }
          else
          {
            logger::warnln("Audio Encoder's bit process failed!");
          }
        }

        AudioData &buffer = data[data_pointer];
        data_pointer = (data_pointer + 1) % AUDIO_ENCODER_MAX_BUFFER_SIZE;
        buffer.num = data_number++ % UINT32_MAX;
        buffer.size = size;
        if (in_data1)
        {
          memcpy(buffer.data, i2s_data1, size);
        }
        else
        {
          memcpy(buffer.data, i2s_data2, size);
        }
        int8_t gain;
        esp_ae_alc_get_gain(alc_handle, 0, &gain);
        printf("gain is %d", gain);
      }
      else
      {
        logger::warnln("Audio Encoder's I2S read fail! Size not same!");
      }
      // read_len++;
    }
  }
}

void audio::encoder::setup()
{
  logger::debugln("Audio Encoder is starting...");
  data = (AudioData *)heap_caps_malloc(sizeof(AudioData) * AUDIO_ENCODER_MAX_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
  i2s_data1 = (uint32_t *)heap_caps_malloc(AUDIO_ENCODER_RATE / 1000 * AUDIO_ENCODER_POLLING_CYCLE * AUDIO_ENCODER_BIT / 8 * AUDIO_ENCODER_CHANNEL, MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT);
  i2s_data2 = (uint32_t *)heap_caps_malloc(AUDIO_ENCODER_RATE / 1000 * AUDIO_ENCODER_POLLING_CYCLE * AUDIO_ENCODER_BIT / 8 * AUDIO_ENCODER_CHANNEL, MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT);
  pinMode(AUDIO_ENCODER_MD0, OUTPUT);
  pinMode(AUDIO_ENCODER_MD1, OUTPUT);
  digitalWrite(AUDIO_ENCODER_MD0, LOW);
  digitalWrite(AUDIO_ENCODER_MD1, LOW);
  xTaskCreatePinnedToCore(audioHandle, "audio_encoder_handle", TASK_AUDIO_ENCODER_STACK, NULL, TASK_AUDIO_ENCODER_PRIORITY, NULL, TASK_AUDIO_ENCODER_CORE);
  logger::debugln("Audio Encoder is started!");
}

void audio::encoder::on()
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
  i2s_new_channel(&i2s_chan_cfg, NULL, &i2s_rx_handle);
  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(i2s_rate),
      .slot_cfg = i2s_slot_cfg,
      .gpio_cfg = i2s_gpio_cfg,
  };
  i2s_channel_init_std_mode(i2s_rx_handle, &std_cfg);
  i2s_channel_enable(i2s_rx_handle);
  // 启动增益模块
  esp_ae_alc_cfg_t alc_cfg = {
      .sample_rate = i2s_rate,
      .channel = I2S_SLOT_MODE_STEREO,
      .bits_per_sample = I2S_DATA_BIT_WIDTH_32BIT};
  esp_ae_alc_open(&alc_cfg, &alc_handle);
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
    if (alc_handle != NULL)
    {
      esp_ae_alc_close(alc_handle);
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

void audio::encoder::setRate(uint32_t rate)
{
  i2s_rate = rate;
  if (powerOn)
  {
    off();
    setChannel(i2s_channel);
    on();
  }
}

void audio::encoder::setChannel(uint8_t channel)
{
  i2s_channel = channel;
  setBit(i2s_bit);
  if (channel == I2S_SLOT_MODE_MONO)
  {
    esp_ae_ch_cvt_cfg_t ch_cvt_cfg = {
        .sample_rate = i2s_rate,
        .bits_per_sample = I2S_DATA_BIT_WIDTH_32BIT,
        .src_ch = I2S_SLOT_MODE_STEREO,
        .dest_ch = channel,
        .weight = NULL,
        .weight_len = 0};
    if (ch_cvt_handle == NULL)
    {
      esp_ae_ch_cvt_open(&ch_cvt_cfg, &ch_cvt_handle);
    }
    else
    {
      esp_ae_ch_cvt_close(ch_cvt_handle);
      esp_ae_ch_cvt_open(&ch_cvt_cfg, &ch_cvt_handle);
    }
  }
  else
  {
    if (ch_cvt_handle != NULL)
    {
      esp_ae_ch_cvt_close(ch_cvt_handle);
    }
  }
}

void audio::encoder::setBit(uint32_t bit)
{
  i2s_bit = (i2s_data_bit_width_t)bit;
  if (i2s_bit != I2S_DATA_BIT_WIDTH_32BIT)
  {
    esp_ae_bit_cvt_cfg_t bit_cvt_cfg = {
        .sample_rate = i2s_rate,
        .channel = i2s_channel,
        .src_bits = I2S_DATA_BIT_WIDTH_32BIT,
        .dest_bits = i2s_bit == I2S_DATA_BIT_WIDTH_24BIT ? I2S_DATA_BIT_WIDTH_24BIT : I2S_DATA_BIT_WIDTH_16BIT};
    if (bit_cvt_handle == NULL)
    {
      esp_ae_bit_cvt_open(&bit_cvt_cfg, &bit_cvt_handle);
    }
    else
    {
      esp_ae_bit_cvt_close(bit_cvt_handle);
      esp_ae_bit_cvt_open(&bit_cvt_cfg, &bit_cvt_handle);
    }
  }
  else
  {
    if (bit_cvt_handle != NULL)
    {
      esp_ae_bit_cvt_close(bit_cvt_handle);
    }
  }
}

// 设置自动增益
void audio::encoder::setAuto(bool on)
{
}

// 设置自动降低增益
void audio::encoder::setPeek(bool on)
{
  i2s_peek = on;
}

// 设置增益(dB)
void audio::encoder::setGain(int8_t db)
{
  i2s_gain = db;
  esp_ae_alc_set_gain(alc_handle, 0, i2s_gain);
  esp_ae_alc_set_gain(alc_handle, 1, i2s_gain);
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