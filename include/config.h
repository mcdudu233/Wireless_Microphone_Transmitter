#pragma once

#include "cinttypes"

// 配置文件名
#define CONFIG_NAME "config"
#define CONFIG_DATA_NAME "config"
#define CONFIG_VERSION_NAME "version"
#define CONFIG_VERSION_VALUE 0x000A // 前两位大版本号 后两位小版本号

#define TASK_SYSTEM_CORE 1
#define TASK_SYSTEM_PERIOD 1000
#define TASK_SYSTEM_PRIORITY 1
#define TASK_SYSTEM_STACK 3072

#define TASK_POWER_CORE 1
#define TASK_POWER_PERIOD 10
#define TASK_POWER_PRIORITY 10
#define TASK_POWER_STACK 3072

#define TASK_AUDIO_ENCODER_CORE 1
#define TASK_AUDIO_ENCODER_PERIOD 4 // 这里必须和编码器轮询周期一样！
#define TASK_AUDIO_ENCODER_PRIORITY 5
#define TASK_AUDIO_ENCODER_STACK 3072

#define TASK_TUSB_CORE 1
#define TASK_TUSB_PRIORITY 3
#define TASK_TUSB_STACK 4096

#define TASK_RF_CORE 0
#define TASK_RF_PERIOD 1
#define TASK_RF_PRIORITY 8
#define TASK_RF_STACK 4096

// 射频模式
enum RFMode : uint8_t
{
  RF_MODE_BLE = 1,
  RF_MODE_WIFI = 2
};

// 射频文本
typedef char RFText[16];

// 音频声道
enum AudioChannel : uint8_t
{
  AUDIO_CHANNEL_SINGLE = 1, // 单声道
  AUDIO_CHANNEL_STEREO = 2  // 立体声
};

// 音频采样率
enum AudioRate : uint32_t
{
  AUDIO_RATE_48000 = 48000,
  AUDIO_RATE_96000 = 96000,
  AUDIO_RATE_192000 = 192000
};

// 音频比特
enum AudioBit : uint8_t
{
  AUDIO_BIT_16 = 16,
  AUDIO_BIT_24 = 24,
  AUDIO_BIT_32 = 32
};

// 音频模式
enum AudioMode : uint8_t
{
  AUDIO_MODE_AUTO = 0,
  AUDIO_MODE_PEEK = 1,
  AUDIO_MODE_MANUAL = 2
};

// 音频增益 [-64, 63] 单位: dB
typedef int8_t AudioGain;

namespace config
{
  enum TransmitMode
  {
    TRANSMIT_MODE_BLE = 0,
    TRANSMIT_MODE_WIFI = 1,
  };

  // 全局配置
  struct StatusValue
  {
    // 设备信息
    struct
    {
      RFMode mode = RF_MODE_BLE;
      RFText ssid = "";
      RFText password = "";
    } rf;
    // 音频信息
    struct
    {
      bool start = false;
      AudioChannel channel = AUDIO_CHANNEL_SINGLE;
      AudioRate rate = AUDIO_RATE_48000;
      AudioBit bit = AUDIO_BIT_16;
      AudioMode mode = AUDIO_MODE_AUTO;
      AudioGain gain = 0;
    } audio;
  };
  extern StatusValue status;

  // 储存的配置
  struct ConfigValue
  {
    struct
    {
      bool isCorrected = false;
      float bias = 0;
    } battery;
  };
  extern ConfigValue config;

  void setup();
  void save();
}
