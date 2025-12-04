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
#define WIFI_MAX_DATA_SIZE 1430
struct AudioPacketWIFI
{
  uint16_t crc;  // 校验位
  uint16_t size; // 前两位用于选择信息类型
  uint32_t number;
  uint8_t part;
  uint8_t data[WIFI_MAX_DATA_SIZE];
};

// BLE音频包
#define BLE_MAX_DATA_SIZE 384
struct AudioPacketBLE
{
  uint32_t num;
  uint8_t data[BLE_MAX_DATA_SIZE];
};

namespace rf
{
  void setup();
}