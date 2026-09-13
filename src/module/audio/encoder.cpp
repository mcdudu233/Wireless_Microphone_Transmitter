#include "config.h"
#include "logger.h"
#include "module/audio/power.h"
#include "module/audio/encoder.h"
#include "module/audio/buffer.h"

#include "cctype"
#include "driver/i2s_std.h"
#include "esp_ae_alc.h"
#include "esp_ae_bit_cvt.h"
#include "esp_ae_ch_cvt.h"

// I2S 参数
static const i2s_chan_config_t i2s_chan_cfg = {
    .id = I2S_NUM_AUTO,
    .role = I2S_ROLE_MASTER,
    .dma_desc_num = AUDIO_ENCODER_POLLING_CYCLE, // 多少个DMA
    .dma_frame_num = 384,                        // 每个DMA大小 可以保存2ms数据
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
    },
};
static const i2s_std_slot_config_t i2s_slot_cfg = {
    .data_bit_width = I2S_DATA_BIT_WIDTH_32BIT,
    .slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT,
    .slot_mode = I2S_SLOT_MODE_STEREO,
    .slot_mask = I2S_STD_SLOT_BOTH,
    .ws_width = I2S_SLOT_BIT_WIDTH_32BIT,
    .ws_pol = false,
    .bit_shift = true,
    .left_align = true,
    .big_endian = false,
    .bit_order_lsb = false,
};

// 原始数据
static uint32_t *i2s_data1;
static uint32_t *i2s_data2;
// 临时数据
static AudioChannel i2s_channel;
static AudioRate i2s_rate;
static AudioBit i2s_bit;
static AudioMode i2s_mode;
static AudioGain i2s_gain;

// 音频处理
static i2s_chan_handle_t i2s_rx_handle;
static esp_ae_alc_handle_t alc_handle = NULL;
static esp_ae_bit_cvt_handle_t bit_cvt_handle = NULL;
static esp_ae_ch_cvt_handle_t ch_cvt_handle = NULL;

static bool powerOn = false;
static float auto_gain = AGC_GAIN_INITIAL;

static float dBFromV(float v)
{
  return 20.0f * log10f(v);
}

// 实时处理音频数据
// static int64_t read_len = 0;
// static int64_t read_loss = 0;
// static unsigned long last_time = millis();
static void audioHandle(void *arg)
{
  // 错误
  esp_err_t ret;
  // 音频包数据
  size_t size;
  size_t bytes_read;
  size_t sample_num;
  uint8_t *data;
  // 自动增益
  int32_t *auto_data;
  const float auto_gain_target = pow10f(AGC_GAIN_TARGET / 20.0f);
  const float auto_gain_peak = pow10f(AGC_GAIN_PEAK / 20.0f);
  int32_t auto_peak = 0;
  uint32_t auto_avg = 0;
#ifdef BUILD_DEBUG
  unsigned long level_log_time = 0;
#endif

  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(TASK_AUDIO_ENCODER_PERIOD);
  while (true)
  {
    xTaskDelayUntil(&xLastWakeTime, xFrequency);

    // 启动了芯片才读取数据
    if (powerOn)
    {
      size = (uint32_t)i2s_rate / 1000 * AUDIO_ENCODER_POLLING_CYCLE * AUDIO_ENCODER_BIT / 8 * AUDIO_ENCODER_CHANNEL;
      bytes_read = 0;
      ret = i2s_channel_read(i2s_rx_handle, i2s_data1, size, &bytes_read, pdMS_TO_TICKS(AUDIO_ENCODER_POLLING_CYCLE * 2));
      if (ret == ESP_OK && bytes_read > 0)
      {
        size = bytes_read;
        // read_len++;
        // if (millis() - last_time >= 1000)
        // {
        //   last_time = millis();
        //   logger::debugln("%d packets/s", read_len);
        //   logger::debugln("lost%d packets/s", read_loss);
        //   read_len = 0;
        //   read_loss = 0;
        // }

        sample_num = size * 8 / AUDIO_ENCODER_BIT / AUDIO_ENCODER_CHANNEL;
        bool in_data1 = true;
        bool frame_valid = true;
        // 增益
        if (alc_handle != NULL)
        {
          if (esp_ae_alc_process(alc_handle, sample_num, i2s_data1, i2s_data2) == ESP_OK)
          {
            switch (i2s_mode)
            {
            // 自动增益
            case AUDIO_MODE_AUTO:
            {
              auto_data = (int32_t *)i2s_data1;
              // 计算峰值和平均幅度 (64位累加防溢出, int64取绝对值避免INT32_MIN取负溢出)
              int64_t peak = 0;
              uint64_t magnitude_sum = 0;
              for (uint16_t i = 0; i < sample_num; i++)
              {
                int64_t left = auto_data[i * 2];
                int64_t right = auto_data[i * 2 + 1];
                left = (left < 0) ? -left : left;
                right = (right < 0) ? -right : right;
                if (left > peak)
                  peak = left;
                if (right > peak)
                  peak = right;
                magnitude_sum += (uint64_t)left + (uint64_t)right;
              }
              auto_peak = (peak > INT32_MAX) ? INT32_MAX : (int32_t)peak;
              auto_avg = (uint32_t)(magnitude_sum / ((uint64_t)sample_num * 2));

              // 噪声门限: 电平过低时保持当前增益, 避免把底噪放大数十dB形成持续电流声
              const int32_t gate_level = (int32_t)(INT32_MAX * pow10f(AGC_NOISE_GATE / 20.0f));
              if (auto_peak > gate_level && auto_avg > 0)
              {
                // 根据平均电平计算目标增益
                float target_gain = dBFromV(auto_gain_target / (auto_avg * 1.0f / INT32_MAX));
                float peak_gain = dBFromV(auto_gain_peak / (auto_peak * 1.0f / INT32_MAX));
                // 取两者中较小的增益
                float new_gain = (target_gain < peak_gain) ? target_gain : peak_gain;

                // 限制增益范围
                if (new_gain > AGC_GAIN_MAX)
                {
                  new_gain = AGC_GAIN_MAX;
                }
                if (new_gain < AGC_GAIN_MIN)
                {
                  new_gain = AGC_GAIN_MIN;
                }

                // 平滑调整增益
                float gain_diff = new_gain - auto_gain;
                float adjustment_rate = (gain_diff > 0) ? AGC_SPEED_ATTACK : AGC_SPEED_RELEASE;
                auto_gain = auto_gain + adjustment_rate * gain_diff;

                esp_ae_alc_set_gain(alc_handle, 0, (int8_t)auto_gain);
                esp_ae_alc_set_gain(alc_handle, 1, (int8_t)auto_gain);
              }
#ifdef BUILD_DEBUG
              if (millis() - level_log_time >= 1000)
              {
                level_log_time = millis();
                LOGGER_DEBUG("Audio capture peak=%ld avg=%lu gain=%.1fdB bytes=%u",
                             (long)auto_peak, (unsigned long)auto_avg,
                             (double)auto_gain, (unsigned int)size);
              }
#endif
              break;
            }
            // 峰值减少增益
            case AUDIO_MODE_PEEK:
            {
              break;
            }
            // 手动增益
            case AUDIO_MODE_MANUAL:
            {
              esp_ae_alc_set_gain(alc_handle, 0, i2s_gain);
              esp_ae_alc_set_gain(alc_handle, 1, i2s_gain);
              break;
            }
            }
            in_data1 = false;
          }
          else
          {
            LOGGER_WARN("Audio Encoder's ALC process failed!");
            frame_valid = false;
          }
        }
        // 声道转换
        if (frame_valid && ch_cvt_handle != NULL)
        {
          if ((in_data1 ? esp_ae_ch_cvt_process(ch_cvt_handle, sample_num, i2s_data1, i2s_data2)
                        : esp_ae_ch_cvt_process(ch_cvt_handle, sample_num, i2s_data2, i2s_data1)) == ESP_OK)
          {

            in_data1 = in_data1 ? false : true;
            size *= (size_t)i2s_channel;
            size /= AUDIO_ENCODER_CHANNEL;
          }
          else
          {
            LOGGER_WARN("Audio Encoder's channel process failed!");
            frame_valid = false;
          }
        }
        // 比特转换
        if (frame_valid && bit_cvt_handle != NULL)
        {
          if ((in_data1 ? esp_ae_bit_cvt_process(bit_cvt_handle, sample_num, i2s_data1, i2s_data2)
                        : esp_ae_bit_cvt_process(bit_cvt_handle, sample_num, i2s_data2, i2s_data1)) == ESP_OK)
          {
            in_data1 = in_data1 ? false : true;
            size *= (size_t)i2s_bit;
            size /= AUDIO_ENCODER_BIT;
          }
          else
          {
            LOGGER_WARN("Audio Encoder's bit process failed!");
            frame_valid = false;
          }
        }
        if (!frame_valid)
        {
          continue;
        }
        // 送到缓冲里面 (写完后再发布, 避免发送任务读到写了一半的数据)
        data = audio::buffer::getWritePointer(size);
        if (in_data1)
        {
          memcpy(data, i2s_data1, size);
        }
        else
        {
          memcpy(data, i2s_data2, size);
        }
        audio::buffer::commitWrite();
      }
      else if (ret == ESP_ERR_TIMEOUT)
      {
        // read_loss++;
        LOGGER_WARN("Audio Encoder's I2S read fail! Time out!");
      }
      else
      {
        // read_loss++;
        LOGGER_WARN("Audio Encoder's I2S read fail! ");
      }
    }
  }
}

void audio::encoder::setup()
{
  LOGGER_INFO("Audio Encoder is starting...");
  i2s_data1 = (uint32_t *)heap_caps_malloc(AUDIO_ENCODER_RATE / 1000 * AUDIO_ENCODER_POLLING_CYCLE * AUDIO_ENCODER_BIT / 8 * AUDIO_ENCODER_CHANNEL, MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT);
  i2s_data2 = (uint32_t *)heap_caps_malloc(AUDIO_ENCODER_RATE / 1000 * AUDIO_ENCODER_POLLING_CYCLE * AUDIO_ENCODER_BIT / 8 * AUDIO_ENCODER_CHANNEL, MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT);
  pinMode(AUDIO_ENCODER_MD0, OUTPUT);
  pinMode(AUDIO_ENCODER_MD1, OUTPUT);
  digitalWrite(AUDIO_ENCODER_MD0, HIGH);
  digitalWrite(AUDIO_ENCODER_MD1, LOW);
  xTaskCreatePinnedToCore(audioHandle, "audio_encoder_handle", TASK_AUDIO_ENCODER_STACK, NULL, TASK_AUDIO_ENCODER_PRIORITY, NULL, TASK_AUDIO_ENCODER_CORE);
  LOGGER_INFO("Audio Encoder is started!");
}

void audio::encoder::on(AudioChannel channel, AudioRate rate, AudioBit bit, AudioMode mode, AudioGain gain)
{
  if (powerOn)
  {
    off();
  }

  // 启动电源
  if (!audio::power::isOn())
  {
    audio::power::on();
  }

  i2s_channel = channel;
  i2s_rate = rate;
  i2s_bit = bit;
  i2s_mode = mode;
  i2s_gain = gain;
  auto_gain = (gain > AGC_GAIN_INITIAL) ? (float)gain : AGC_GAIN_INITIAL;
  // 启动 i2s
  i2s_new_channel(&i2s_chan_cfg, NULL, &i2s_rx_handle);
  i2s_std_config_t std_cfg = {
      .clk_cfg = {
          .sample_rate_hz = (uint32_t)rate,
          .clk_src = I2S_CLK_SRC_PLL_160M,
          .ext_clk_freq_hz = 0,
          .mclk_multiple = I2S_MCLK_MULTIPLE_256,
          .bclk_div = 0,
      },
      .slot_cfg = i2s_slot_cfg,
      .gpio_cfg = i2s_gpio_cfg,
  };
  i2s_channel_init_std_mode(i2s_rx_handle, &std_cfg);
  i2s_channel_enable(i2s_rx_handle);
  // 启动增益模块
  esp_ae_alc_cfg_t alc_cfg = {
      .sample_rate = (uint32_t)rate,
      .channel = I2S_SLOT_MODE_STEREO,
      .bits_per_sample = I2S_DATA_BIT_WIDTH_32BIT,
  };
  esp_ae_alc_open(&alc_cfg, &alc_handle);
  const int8_t initial_gain = (mode == AUDIO_MODE_AUTO) ? (int8_t)auto_gain : gain;
  esp_ae_alc_set_gain(alc_handle, 0, initial_gain);
  esp_ae_alc_set_gain(alc_handle, 1, initial_gain);
  // 启动通道转换
  if (channel == AUDIO_CHANNEL_SINGLE)
  {
    esp_ae_ch_cvt_cfg_t ch_cvt_cfg = {
        .sample_rate = (uint32_t)rate,
        .bits_per_sample = I2S_DATA_BIT_WIDTH_32BIT,
        .src_ch = I2S_SLOT_MODE_STEREO,
        .dest_ch = (uint8_t)channel,
        .weight = NULL,
        .weight_len = 0,
    };
    esp_ae_ch_cvt_open(&ch_cvt_cfg, &ch_cvt_handle);
  }
  // 启动比特转换
  if (bit != AUDIO_BIT_32)
  {
    esp_ae_bit_cvt_cfg_t bit_cvt_cfg = {
        .sample_rate = (uint32_t)rate,
        .channel = (uint8_t)channel,
        .src_bits = I2S_DATA_BIT_WIDTH_32BIT,
        .dest_bits = (uint8_t)bit,
    };
    esp_ae_bit_cvt_open(&bit_cvt_cfg, &bit_cvt_handle);
  }
  powerOn = true;
  LOGGER_INFO("Audio Encoder is on.");
}

void audio::encoder::off()
{
  if (powerOn)
  {
    powerOn = false;
    // 关闭各类模块
    if (ch_cvt_handle != NULL)
    {
      esp_ae_ch_cvt_close(ch_cvt_handle);
      ch_cvt_handle = NULL;
    }
    if (bit_cvt_handle != NULL)
    {
      esp_ae_bit_cvt_close(bit_cvt_handle);
      bit_cvt_handle = NULL;
    }
    if (alc_handle != NULL)
    {
      esp_ae_alc_close(alc_handle);
      alc_handle = NULL;
    }
    // 关闭 i2s
    i2s_chan_handle_t old_handle = i2s_rx_handle;
    if (old_handle != NULL)
    {
      i2s_channel_disable(old_handle);
      i2s_del_channel(old_handle);
    }
    i2s_rx_handle = NULL;
    // 关闭电源
    if (audio::power::isOn())
    {
      audio::power::off();
    }
    LOGGER_INFO("Audio Encoder is off.");
  }
}

bool audio::encoder::isOn()
{
  return powerOn;
}

// 设置增益模式
void audio::encoder::setMode(AudioMode mode)
{
  i2s_mode = mode;
}

// 设置增益(dB)
void audio::encoder::setGain(AudioGain gain)
{
  if (alc_handle != NULL)
  {
    i2s_gain = gain;
    esp_ae_alc_set_gain(alc_handle, 0, gain);
    esp_ae_alc_set_gain(alc_handle, 1, gain);
  }
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
