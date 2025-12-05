#pragma once

// 蓝牙
#define BLE_NAME "Microphone Transmitter"
#define SERVICE_NUM 3
#define INFO_SERVICE_UUID 0x180A
#define DEVICE_CHARACTERISTIC_UUID 0x2A00
#define MODEL_CHARACTERISTIC_UUID 0x2A24
#define MANUFACTURER_CHARACTERISTIC_UUID 0x2A29
#define BATTERY_SERVICE_UUID 0x180F
#define BATTERY_CHARACTERISTIC_UUID 0x2A19
#define AUDIO_SERVICE_UUID 0x1843
#define DATA_CHARACTERISTIC_UUID 0x2B81
#define CONFIG_CONTROL_CHARACTERISTIC_UUID 0x2B7A
#define AUDIO_CONTROL_CHARACTERISTIC_UUID 0x2B7B
// WiFi
#define WIFI_RETRY 3
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_IP_PROTOCOL 0xE9
#define WIFI_NO_PORT 0

enum ConfigControlMode
{
  AUDIO_CONTROL_MODE_BLE = 0,
  AUDIO_CONTROL_MODE_WIFI = 1,
};

struct ConfigClientControl
{
  bool success = false;
  uint32_t ip = 0x00000000;
};

struct AudioClientControl
{
  bool success = false;
};

struct ConfigServerControl
{
  bool start = false;
  bool startWiFi = false;
  bool startBLE = true;
  ConfigControlMode mode = AUDIO_CONTROL_MODE_BLE;
  char name[32] = "";
  char password[32] = "";
};

struct AudioServerControl
{
  bool start = false;
  uint8_t channel = 2;
  uint16_t rate = 48000;
  uint8_t bit = 16;
  bool autoVolumn = false;
  bool peekVolumn = false;
  uint8_t volumn = 0;
};

// WIFI音频包
#define WIFI_PACKET_HEAD_SIZE (sizeof(AudioPacketWIFI) - WIFI_PACKET_DATA_MAX_SIZE)
#define WIFI_PACKET_DATA_MAX_SIZE 1420
enum AudioPacketWIFIType
{
  AUDIO_PACKET_WIFI_TYPE_DATA = 0,
  AUDIO_PACKET_WIFI_TYPE_CONTROL = 1,
};
struct __attribute__((packed)) AudioPacketWIFI
{
  uint8_t type; // 包类型
  uint16_t size;
  uint32_t number;
  uint8_t part;
  uint8_t data[WIFI_PACKET_DATA_MAX_SIZE];
};

// BLE音频包
#define BLE_PACKET_HEAD_SIZE (sizeof(AudioPacketBLE) - BLE_PACKET_DATA_MAX_SIZE)
#define BLE_PACKET_DATA_MAX_SIZE 384
struct __attribute__((packed)) AudioPacketBLE
{
  uint16_t size;
  uint32_t number;
  uint8_t part;
  uint8_t data[BLE_PACKET_DATA_MAX_SIZE];
};

namespace rf
{
  void setup();
}