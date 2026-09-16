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
#include "freertos/idf_additions.h"

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

// 当前实际生效增益(dB):命令值由i2s_gain保存,自动/峰值减少算法动态下调时同步更新,
// 供状态包上报给接收端(rf任务读,int8_t原子读取)
static volatile AudioGain encoderGain = 0;

#ifdef BUILD_DEBUG
static void logI2SClock(i2s_chan_handle_t handle, uint32_t requested_rate, uint32_t mclk_multiple)
{
  i2s_chan_info_t info = {};
  i2s_tuning_info_t tuning = {};
  const esp_err_t info_result = i2s_channel_get_info(handle, &info);
  const esp_err_t tuning_result = i2s_channel_tune_rate(handle, nullptr, &tuning);
  if (info_result != ESP_OK || tuning_result != ESP_OK || mclk_multiple == 0)
  {
    LOGGER_INFO("Audio Encoder I2S clock query failed: info=%s tuning=%s",
                esp_err_to_name(info_result), esp_err_to_name(tuning_result));
    return;
  }

  const uint64_t effective_millihz = static_cast<uint64_t>(tuning.curr_mclk_hz) * 1000ULL / mclk_multiple;
  LOGGER_INFO("Audio Encoder I2S clock requested=%luHz source=%u sclk=%lu mclk=%ld bclk=%lu effective=%lu.%03luHz dma=%lu water=%lu%%",
              static_cast<unsigned long>(requested_rate), static_cast<unsigned int>(info.clk_src),
              static_cast<unsigned long>(info.sclk_hz), static_cast<long>(tuning.curr_mclk_hz),
              static_cast<unsigned long>(info.bclk_hz),
              static_cast<unsigned long>(effective_millihz / 1000ULL),
              static_cast<unsigned long>(effective_millihz % 1000ULL),
              static_cast<unsigned long>(info.total_dma_buf_size),
              static_cast<unsigned long>(tuning.water_mark));
}
#endif

#ifdef BUILD_DEBUG
struct RawPcmStats
{
  uint32_t left_peak;
  uint32_t right_peak;
  int64_t left_sum;
  int64_t right_sum;
  uint32_t sample_pairs;
  uint32_t zero_samples;
  uint32_t clipped_samples;
};

struct EncoderDebugSnapshot
{
  bool ready;
  bool on;
  uint8_t mode;
  int8_t gain;
  int32_t auto_gain_tenths;
  uint32_t left_peak;
  uint32_t right_peak;
  int32_t left_dc;
  int32_t right_dc;
  uint32_t zero_permille;
  uint32_t clipped_samples;
  uint32_t output_peak;
  uint32_t frames_read;
  uint32_t bytes_sent;
  uint32_t read_errors;
  uint32_t read_wait_average_us;
  uint32_t read_wait_max_us;
  uint32_t process_errors;
  uint32_t process_max_us;
};

static portMUX_TYPE encoder_debug_mux = portMUX_INITIALIZER_UNLOCKED;
static EncoderDebugSnapshot encoder_debug_snapshot = {};

static void encoderDebugHandle(void *arg)
{
  (void)arg;
  vTaskDelay(pdMS_TO_TICKS(350));
  while (true)
  {
    EncoderDebugSnapshot snapshot = {};
    portENTER_CRITICAL(&encoder_debug_mux);
    if (encoder_debug_snapshot.ready)
    {
      snapshot = encoder_debug_snapshot;
      encoder_debug_snapshot.ready = false;
    }
    portEXIT_CRITICAL(&encoder_debug_mux);
    if (snapshot.ready)
    {
      LOGGER_INFO("Audio capture on=%u mode=%u gain=%d agc_gain=%ld.%01lddB raw_l_peak=%lu raw_r_peak=%lu raw_l_dc=%ld raw_r_dc=%ld zero=%lu.%lu%% clip=%lu output_peak=%lu frames=%lu bytes=%lu read_errors=%lu read_avg_us=%lu read_max_us=%lu process_errors=%lu process_max_us=%lu",
                  snapshot.on ? 1U : 0U, static_cast<unsigned int>(snapshot.mode), static_cast<int>(snapshot.gain),
                  static_cast<long>(snapshot.auto_gain_tenths / 10),
                  static_cast<long>(labs(snapshot.auto_gain_tenths % 10)),
                  static_cast<unsigned long>(snapshot.left_peak), static_cast<unsigned long>(snapshot.right_peak),
                  static_cast<long>(snapshot.left_dc), static_cast<long>(snapshot.right_dc),
                  static_cast<unsigned long>(snapshot.zero_permille / 10U),
                  static_cast<unsigned long>(snapshot.zero_permille % 10U),
                  static_cast<unsigned long>(snapshot.clipped_samples),
                  static_cast<unsigned long>(snapshot.output_peak),
                  static_cast<unsigned long>(snapshot.frames_read), static_cast<unsigned long>(snapshot.bytes_sent),
                  static_cast<unsigned long>(snapshot.read_errors),
                  static_cast<unsigned long>(snapshot.read_wait_average_us),
                  static_cast<unsigned long>(snapshot.read_wait_max_us),
                  static_cast<unsigned long>(snapshot.process_errors),
                  static_cast<unsigned long>(snapshot.process_max_us));
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

static void updateRawPcmStats(const uint32_t *pcm, size_t size, RawPcmStats &stats)
{
  const size_t sample_pairs = size / (sizeof(uint32_t) * AUDIO_ENCODER_CHANNEL);
  for (size_t i = 0; i < sample_pairs; i++)
  {
    const int32_t left = static_cast<int32_t>(pcm[i * AUDIO_ENCODER_CHANNEL]);
    const int32_t right = static_cast<int32_t>(pcm[i * AUDIO_ENCODER_CHANNEL + 1]);
    const uint32_t left_magnitude = static_cast<uint32_t>(left < 0 ? -static_cast<int64_t>(left) : left);
    const uint32_t right_magnitude = static_cast<uint32_t>(right < 0 ? -static_cast<int64_t>(right) : right);

    if (left_magnitude > stats.left_peak)
    {
      stats.left_peak = left_magnitude;
    }
    if (right_magnitude > stats.right_peak)
    {
      stats.right_peak = right_magnitude;
    }
    stats.left_sum += left;
    stats.right_sum += right;
    stats.zero_samples += (left == 0) + (right == 0);
    stats.clipped_samples += (left_magnitude >= 0x7F000000U) + (right_magnitude >= 0x7F000000U);
  }
  stats.sample_pairs += sample_pairs;
}

static uint32_t getPcmPeak(const uint8_t *pcm, size_t size, AudioBit bit)
{
  uint32_t peak = 0;
  const size_t bytes_per_sample = static_cast<size_t>(bit) / 8;
  for (size_t offset = 0; bytes_per_sample > 0 && offset + bytes_per_sample <= size; offset += bytes_per_sample)
  {
    int64_t sample = 0;
    if (bit == AUDIO_BIT_16)
    {
      sample = static_cast<int16_t>(static_cast<uint16_t>(pcm[offset]) |
                                    (static_cast<uint16_t>(pcm[offset + 1]) << 8));
    }
    else if (bit == AUDIO_BIT_24)
    {
      int32_t value = static_cast<int32_t>(pcm[offset]) |
                      (static_cast<int32_t>(pcm[offset + 1]) << 8) |
                      (static_cast<int32_t>(pcm[offset + 2]) << 16);
      if ((value & 0x00800000) != 0)
      {
        value |= 0xFF000000;
      }
      sample = value;
    }
    else
    {
      sample = static_cast<int32_t>(static_cast<uint32_t>(pcm[offset]) |
                                    (static_cast<uint32_t>(pcm[offset + 1]) << 8) |
                                    (static_cast<uint32_t>(pcm[offset + 2]) << 16) |
                                    (static_cast<uint32_t>(pcm[offset + 3]) << 24));
    }

    const uint32_t magnitude = static_cast<uint32_t>(sample < 0 ? -sample : sample);
    if (magnitude > peak)
    {
      peak = magnitude;
    }
  }
  return peak;
}
#endif

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
  float auto_gain = 0;
  const float auto_gain_target = pow10f(AGC_GAIN_TARGET / 20.0f);
  const float auto_gain_peak = pow10f(AGC_GAIN_PEAK / 20.0f);
  int32_t auto_peak = 0;
  uint32_t auto_avg = 0;
  int8_t last_auto_gain = INT8_MIN;
#ifdef BUILD_DEBUG
  uint32_t level_log_time = millis();
  RawPcmStats raw_stats = {};
  uint32_t output_peak = 0;
  uint32_t frames_read = 0;
  uint32_t bytes_sent = 0;
  uint32_t read_errors = 0;
  uint32_t process_errors = 0;
  uint32_t process_max_us = 0;
  uint32_t read_wait_total_us = 0;
  uint32_t read_wait_max_us = 0;
  uint32_t read_calls = 0;
#endif

  while (true)
  {
    // 开启后由I2S DMA的阻塞读取定节拍，并在任务短暂延迟后连续取走已积压的DMA数据。
    // 固定4ms系统延时会错过追赶机会，最终让发送帧率低于硬件采样率。
    if (!powerOn)
    {
      vTaskDelay(pdMS_TO_TICKS(TASK_AUDIO_ENCODER_PERIOD));
    }

    // 启动了芯片才读取数据
    if (powerOn)
    {
      size = (uint32_t)i2s_rate / 1000 * AUDIO_ENCODER_POLLING_CYCLE * AUDIO_ENCODER_BIT / 8 * AUDIO_ENCODER_CHANNEL;
      bytes_read = 0;
#ifdef BUILD_DEBUG
      const uint32_t read_start_us = micros();
#endif
      ret = i2s_channel_read(i2s_rx_handle, i2s_data1, size, &bytes_read, pdMS_TO_TICKS(AUDIO_ENCODER_POLLING_CYCLE * 2));
#ifdef BUILD_DEBUG
      const uint32_t read_wait_us = micros() - read_start_us;
      read_wait_total_us += read_wait_us;
      read_calls++;
      if (read_wait_us > read_wait_max_us)
      {
        read_wait_max_us = read_wait_us;
      }
#endif
      if (ret == ESP_OK && bytes_read > 0)
      {
        size = bytes_read;
#ifdef BUILD_DEBUG
        frames_read++;
        updateRawPcmStats(i2s_data1, size, raw_stats);
        const uint32_t process_start_us = micros();
#endif
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

                const int8_t requested_auto_gain = static_cast<int8_t>(auto_gain);
                if (requested_auto_gain != last_auto_gain)
                {
                  esp_ae_alc_set_gain(alc_handle, 0, requested_auto_gain);
                  esp_ae_alc_set_gain(alc_handle, 1, requested_auto_gain);
                  last_auto_gain = requested_auto_gain;
                  encoderGain = requested_auto_gain;
                }
              }
              break;
            }
            // 峰值减少增益:输入峰值叠加当前增益将超过输出上限时下调增益(只降不升),
            // 初始增益为接收端下发的配置值,下调量经状态包回报给接收端
            case AUDIO_MODE_PEEK:
            {
              auto_data = (int32_t *)i2s_data1;
              int64_t peak = 0;
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
              }
              if (peak > 0)
              {
                const int32_t peak_i32 = (peak > INT32_MAX) ? INT32_MAX : (int32_t)peak;
                // 输出峰值 = 输入峰值 + 当前增益,超过上限时按需下调
                const float allowed = AGC_PEEK_CEILING - dBFromV(peak_i32 * 1.0f / INT32_MAX);
                if (allowed < i2s_gain)
                {
                  AudioGain new_gain = (AudioGain)allowed;
                  if (new_gain < AGC_GAIN_MIN)
                  {
                    new_gain = AGC_GAIN_MIN;
                  }
                  if (new_gain != i2s_gain)
                  {
                    i2s_gain = new_gain;
                    encoderGain = new_gain;
                    esp_ae_alc_set_gain(alc_handle, 0, new_gain);
                    esp_ae_alc_set_gain(alc_handle, 1, new_gain);
                  }
                }
              }
              break;
            }
            // 手动增益
            case AUDIO_MODE_MANUAL:
            {
              // 固定增益已在on()/setGain()中设置，不必每4ms重复配置ALC。
              break;
            }
            }
            in_data1 = false;
          }
          else
          {
            LOGGER_WARN("Audio Encoder's ALC process failed!");
            frame_valid = false;
#ifdef BUILD_DEBUG
            process_errors++;
#endif
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
#ifdef BUILD_DEBUG
            process_errors++;
#endif
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
#ifdef BUILD_DEBUG
            process_errors++;
#endif
          }
        }
        if (!frame_valid)
        {
          continue;
        }
        // 送到缓冲里面 (写完后再发布, 避免发送任务读到写了一半的数据)
        const uint8_t *output_data = reinterpret_cast<const uint8_t *>(in_data1 ? i2s_data1 : i2s_data2);
#ifdef BUILD_DEBUG
        // 输出峰值是诊断量，每约16帧抽样一次即可。
        if ((frames_read & 0x0FU) == 1U)
        {
          const uint32_t frame_output_peak = getPcmPeak(output_data, size, i2s_bit);
          if (frame_output_peak > output_peak)
          {
            output_peak = frame_output_peak;
          }
        }
        bytes_sent += size;
#endif
        data = audio::buffer::getWritePointer(size);
        memcpy(data, output_data, size);
        audio::buffer::commitWrite();
#ifdef BUILD_DEBUG
        const uint32_t process_us = micros() - process_start_us;
        if (process_us > process_max_us)
        {
          process_max_us = process_us;
        }
#endif
      }
      else if (ret == ESP_ERR_TIMEOUT)
      {
        // read_loss++;
        LOGGER_WARN("Audio Encoder's I2S read fail! Time out!");
#ifdef BUILD_DEBUG
        read_errors++;
#endif
      }
      else
      {
        // read_loss++;
        LOGGER_WARN("Audio Encoder's I2S read fail! ");
#ifdef BUILD_DEBUG
        read_errors++;
#endif
      }
    }
    else
    {
      // 下次启动自动增益时从确定状态重新开始，避免沿用上一次ALC实例的缓存值。
      auto_gain = 0;
      last_auto_gain = INT8_MIN;
    }

#ifdef BUILD_DEBUG
    if (millis() - level_log_time >= 1000)
    {
      level_log_time = millis();
      const int32_t left_dc = raw_stats.sample_pairs > 0 ? raw_stats.left_sum / raw_stats.sample_pairs : 0;
      const int32_t right_dc = raw_stats.sample_pairs > 0 ? raw_stats.right_sum / raw_stats.sample_pairs : 0;
      const uint32_t raw_samples = raw_stats.sample_pairs * AUDIO_ENCODER_CHANNEL;
      const uint32_t zero_permille = raw_samples > 0 ? raw_stats.zero_samples * 1000U / raw_samples : 0;
      const int32_t auto_gain_tenths = static_cast<int32_t>(auto_gain * 10.0f);
      EncoderDebugSnapshot snapshot = {};
      snapshot.ready = true;
      snapshot.on = powerOn;
      snapshot.mode = static_cast<uint8_t>(i2s_mode);
      snapshot.gain = static_cast<int8_t>(i2s_gain);
      snapshot.auto_gain_tenths = auto_gain_tenths;
      snapshot.left_peak = raw_stats.left_peak;
      snapshot.right_peak = raw_stats.right_peak;
      snapshot.left_dc = left_dc;
      snapshot.right_dc = right_dc;
      snapshot.zero_permille = zero_permille;
      snapshot.clipped_samples = raw_stats.clipped_samples;
      snapshot.output_peak = output_peak;
      snapshot.frames_read = frames_read;
      snapshot.bytes_sent = bytes_sent;
      snapshot.read_errors = read_errors;
      snapshot.read_wait_average_us = read_calls > 0 ? read_wait_total_us / read_calls : 0;
      snapshot.read_wait_max_us = read_wait_max_us;
      snapshot.process_errors = process_errors;
      snapshot.process_max_us = process_max_us;
      portENTER_CRITICAL(&encoder_debug_mux);
      encoder_debug_snapshot = snapshot;
      portEXIT_CRITICAL(&encoder_debug_mux);
      raw_stats = {};
      output_peak = 0;
      frames_read = 0;
      bytes_sent = 0;
      read_errors = 0;
      process_errors = 0;
      process_max_us = 0;
      read_wait_total_us = 0;
      read_wait_max_us = 0;
      read_calls = 0;
    }
#endif
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
#ifdef BUILD_DEBUG
  if (xTaskCreatePinnedToCoreWithCaps(encoderDebugHandle, "audio_encoder_debug", 3072, nullptr, 1, nullptr,
                                      0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS)
  {
    LOGGER_INFO("Audio Encoder debug task creation failed.");
  }
#endif
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
  encoderGain = gain;

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

  esp_err_t i2s_result = i2s_new_channel(&i2s_chan_cfg, NULL, &i2s_rx_handle);
  if (i2s_result != ESP_OK)
  {
    i2s_rx_handle = NULL;
    LOGGER_WARN("Audio Encoder cannot create I2S channel: %s", esp_err_to_name(i2s_result));
    return;
  }

  i2s_result = i2s_channel_init_std_mode(i2s_rx_handle, &std_cfg);
  if (i2s_result != ESP_OK)
  {
    LOGGER_WARN("Audio Encoder cannot configure I2S: %s", esp_err_to_name(i2s_result));
    i2s_del_channel(i2s_rx_handle);
    i2s_rx_handle = NULL;
    return;
  }

  i2s_result = i2s_channel_enable(i2s_rx_handle);
  if (i2s_result != ESP_OK)
  {
    LOGGER_WARN("Audio Encoder cannot enable I2S: %s", esp_err_to_name(i2s_result));
    i2s_del_channel(i2s_rx_handle);
    i2s_rx_handle = NULL;
    audio::power::off();
    return;
  }
#ifdef BUILD_DEBUG
  logI2SClock(i2s_rx_handle, static_cast<uint32_t>(i2s_rate),
              static_cast<uint32_t>(I2S_MCLK_MULTIPLE_256));
#endif
  // 启动增益模块
  esp_ae_alc_cfg_t alc_cfg = {
      .sample_rate = (uint32_t)rate,
      .channel = I2S_SLOT_MODE_STEREO,
      .bits_per_sample = I2S_DATA_BIT_WIDTH_32BIT,
  };
  esp_ae_alc_open(&alc_cfg, &alc_handle);
  esp_ae_alc_set_gain(alc_handle, 0, gain);
  esp_ae_alc_set_gain(alc_handle, 1, gain);
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
  LOGGER_INFO("Audio Encoder is on: %luHz/%ubit/%uch mode=%u gain=%d.", static_cast<unsigned long>(i2s_rate),
              static_cast<unsigned int>(i2s_bit), static_cast<unsigned int>(i2s_channel),
              static_cast<unsigned int>(i2s_mode), static_cast<int>(i2s_gain));
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
    encoderGain = gain;
    esp_ae_alc_set_gain(alc_handle, 0, gain);
    esp_ae_alc_set_gain(alc_handle, 1, gain);
  }
}

// 获取当前实际生效增益(dB)
AudioGain audio::encoder::getGain()
{
  return encoderGain;
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
