#pragma once

#define INFO_SERVICE_UUID "180a"
#define DEVICE_CHARACTERISTIC_UUID "2a00"
#define MODEL_CHARACTERISTIC_UUID "2a24"
#define MANUFACTURER_CHARACTERISTIC_UUID "2a29"

#define BATTERY_SERVICE_UUID "180f"
#define BATTERY_CHARACTERISTIC_UUID "2a19"

#define AUDIO_SERVICE_UUID "1843"
#define DATA_CHARACTERISTIC_UUID "2b81"
#define CONTROL_CHARACTERISTIC_UUID "2b7b"

enum AudioControlMode
{
  AUDIO_CONTROL_MODE_BLE = 0,
  AUDIO_CONTROL_MODE_WIFI = 1,
};

struct AudioControl
{
  bool start = false;
  bool mode = AUDIO_CONTROL_MODE_BLE;
  uint8_t channel = 2;
  uint16_t rate = 48000;
  uint8_t bit = 16;
};

namespace ble
{
  void setup();
}