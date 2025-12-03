#pragma once

#include "queue.h"

#define AUDIO_ENCODER_WS GPIO_NUM_17
#define AUDIO_ENCODER_CLK GPIO_NUM_16
#define AUDIO_ENCODER_SD GPIO_NUM_15

#define AUDIO_ENCODER_MD0 GPIO_NUM_21
#define AUDIO_ENCODER_MD1 GPIO_NUM_18

#define AUDIO_ENCODER_POLLING_CYCLE 3 // ms 决定了麦克风的延迟
#define AUDIO_ENCODER_RATE 192000     // 最大频率
#define AUDIO_ENCODER_BIT 32          // 固定的比特数
#define AUDIO_ENCODER_CHANNEL 2       // 固定的通道数

#define AUDIO_ENCODER_MAX_DATA_SIZE (AUDIO_ENCODER_RATE * AUDIO_ENCODER_BIT * AUDIO_ENCODER_CHANNEL / 8 * AUDIO_ENCODER_POLLING_CYCLE / 1000)
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
  void on();
  void off();
  bool isOn();

  // rate -> gain -> channel -> bit
  void setRate(uint32_t rate);
  void setBit(uint32_t bit);
  void setChannel(uint8_t channel);
  // 设置自动增益
  void setAuto(bool on);
  // 设置自动降低增益
  void setPeek(bool on);
  // 设置增益(dB)
  void setGain(int8_t db);

  // 默认采用 Linear phase filters ，可以设置为 Low latency filters
  void setLowLatencyFilter(bool on);
  // 设置 Dynamic Range Enhancer
  void setDRE(bool on);

  // 获取音频数据
  uint8_t getNumber();
  AudioData *getData();
  AudioData *getDataFromIndex(uint8_t index);
  AudioData *getDataFromNumber(uint32_t number);
}