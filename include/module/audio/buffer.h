#pragma once

#include "module/rf.h"
#include "module/audio/encoder.h"
#include "cinttypes"
#include "lwip/api.h"

#define AUDIO_BUFFER_MAX_DATA_SIZE (AUDIO_ENCODER_RATE * AUDIO_ENCODER_BIT * AUDIO_ENCODER_CHANNEL / 8 * AUDIO_ENCODER_POLLING_CYCLE / 1000)
// 发送端按序发送积压帧；使用PSRAM保留约400ms，吸收WiFi任务的短时阻塞。
#define AUDIO_BUFFER_MAX_BUFFER_SIZE 100

struct AudioData
{
  uint32_t num;
  uint32_t size;
  uint8_t data[AUDIO_BUFFER_MAX_DATA_SIZE];
};

#ifdef BUILD_DEBUG
struct AudioTxBufferDebugStats
{
  uint32_t frames;
  uint32_t skipped_frames;
  uint32_t parts;
  uint32_t payload_bytes;
  uint32_t allocation_errors;
  uint32_t max_backlog;
};
#endif

namespace audio::buffer
{
  void setup();
  // 重置指针
  void restart();

  /* 原始方法 */
  // 获取当前指针
  uint8_t getPointer();
  // 获取当前音频包号码
  uint32_t getNumber();
  // 获取目前的音频原始数据包
  AudioData *getAudioDataFront();
  // 根据音频包号码获取音频原始数据包
  AudioData *getAudioDataFromNumber(uint32_t number);

  /* 写入数据 */
  // 获取写入数据的指针(此时包对读取端不可见,写入完成后必须调用commitWrite发布)
  uint8_t *getWritePointer(uint32_t packet_size);
  // 发布getWritePointer写入的数据包(推进指针使其对发送端可见)
  void commitWrite();

  /* 读取WiFi数据 */
  // 获取目前的音频数据包分包
  uint8_t getWiFiPacketFront(netbuf ***buffers);
  // uint8_t getWiFiPacketFromNumber(uint32_t number, netbuf **buffer);

  /* 读取BLE数据 */
  // 获取目前的音频数据包分包
  uint8_t getBLEPacketFront(Packet ***buffers);

#ifdef BUILD_DEBUG
  // 读取并清零WiFi音频分包统计。
  void getWiFiDebugStats(AudioTxBufferDebugStats &stats);
#endif
}
