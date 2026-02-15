#pragma once

#include "config.h"

// 客户端状态发送频率
#define RF_CLIENT_STATUS_PERIOD 1000 // ms

// 蓝牙
#define BLE_NAME "MicTx"
#define BLE_L2CAP_PSM 0x1001
#define BLE_L2CAP_MTU 512
// WiFi
#define WIFI_RETRY 5
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_IP_PROTOCOL 0xE9
#define WIFI_NO_PORT 0
#define WIFI_IP_HEAD_LEN 20

// 客户端状态
#define PACKET_CLIENT_STATUS_SIZE (sizeof(uint8_t) + sizeof(ClientStatusPacket))
enum PacketClientStatus : uint8_t
{
  PACKET_CLIENT_STATUS_OK = 0,
  PACKET_CLIENT_STATUS_ERROR_UNKNOW = 255,
};
struct __attribute__((packed)) ClientStatusPacket
{
  PacketClientStatus status;
  uint8_t battery;
};

// 服务端控制设备
#define PACKET_SERVER_CONTROL_RF_SIZE (sizeof(uint8_t) + sizeof(ServerControlRFPacket))
struct __attribute__((packed)) ServerControlRFPacket
{
  RFMode mode;
  RFText ssid;
  RFText password;
};

// 服务端控制音频
#define PACKET_SERVER_CONTROL_AUDIO_SIZE (sizeof(uint8_t) + sizeof(ServerControlAudioPacket))
struct __attribute__((packed)) ServerControlAudioPacket
{
  bool start;
  AudioChannel channel;
  AudioRate rate;
  AudioBit bit;
  AudioMode mode;
  AudioGain gain;
};

// WIFI传输包
#define PACKET_WIFI_AUDIO_HEAD_SIZE (sizeof(uint8_t) + sizeof(WiFiAudioPacket) - PACKET_WIFI_AUDIO_DATA_MAX_SIZE)
#define PACKET_WIFI_AUDIO_DATA_MAX_SIZE 1420
struct __attribute__((packed)) WiFiAudioPacket
{
  uint16_t size;
  uint32_t number;
  uint8_t part;
  uint8_t data[PACKET_WIFI_AUDIO_DATA_MAX_SIZE];
};

// BLE传输包
#define PACKET_BLE_AUDIO_HEAD_SIZE (sizeof(uint8_t) + sizeof(BLEAudioPacket) - PACKET_BLE_AUDIO_DATA_MAX_SIZE)
#define PACKET_BLE_AUDIO_DATA_MAX_SIZE 384
struct __attribute__((packed)) BLEAudioPacket
{
  uint16_t size;
  uint32_t number;
  uint8_t part;
  uint8_t data[PACKET_BLE_AUDIO_DATA_MAX_SIZE];
};

// 统一协议
enum PacketType : uint8_t
{
  PACKET_TYPE_WIFI_AUDIO = 0,
  PACKET_TYPE_BLE_AUDIO = 1,
  PACKET_TYPE_CLIENT_ACK = 2,
  PACKET_TYPE_SERVER_ACK = 3,
  PACKET_TYPE_CLIENT_STATUS = 4,
  PACKET_TYPE_SERVER_CONTROL_DEVICE = 5,
  PACKET_TYPE_SERVER_CONTROL_AUDIO = 6,
};
struct __attribute__((packed)) Packet
{
  PacketType type; // 标识是哪个包
  union
  {
    // 音频包
    WiFiAudioPacket audioDataWiFi;
    BLEAudioPacket audioDataBLE;
    // 状态配置包
    ClientStatusPacket clientStatus;
    ServerControlRFPacket serverControlRF;
    ServerControlAudioPacket serverControlAudio;
  } packet;
};

namespace rf
{
  void setup();
}