#pragma once

#include "queue.h"

#define AUDIO_ENCODER_WS 17
#define AUDIO_ENCODER_CLK 16
#define AUDIO_ENCODER_SD 15

#define AUDIO_ENCODER_MD0 21
#define AUDIO_ENCODER_MD1 18

#define AUDIO_ENCODER_MAX_DATA_SIZE 1536
#define AUDIO_ENCODER_MAX_BUFFER_SIZE 10

struct AudioData
{
  uint32_t num;
  uint32_t size;
  uint8_t data[AUDIO_ENCODER_MAX_DATA_SIZE];
};

namespace audio::encoder
{
  void setup();
  void on(uint32_t rate = 192 * 1000, uint32_t bit = 32);
  void off();
  bool isOn();

  // 默认采用 Linear phase filters ，可以设置为 Low latency filters
  void setLowLatencyFilter(bool on);
  // 设置 Dynamic Range Enhancer
  void setDRE(bool on);

  // 获取音频数据
  AudioData *getData();
  AudioData *getDataFromIndex(uint8_t index);
  AudioData *getDataFromNumber(uint32_t number);
}