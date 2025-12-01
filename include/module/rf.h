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

enum ConfigControlMode
{
  AUDIO_CONTROL_MODE_BLE = 0,
  AUDIO_CONTROL_MODE_WIFI_UDP = 1,
  AUDIO_CONTROL_MODE_WIFI_TCP = 2,
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
  uint16_t port = 3333;
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

// TCP音频包
struct AudioPacketTCP
{
  uint32_t num;
  uint8_t data[1536];
};

// UDP音频包
struct AudioPacketUDP
{
  uint32_t num;
  uint8_t data[1536];
};

// BLE音频包
struct AudioPacketBLE
{
  uint32_t num;
  uint8_t data[384];
};

namespace rf
{
  void setup();
}