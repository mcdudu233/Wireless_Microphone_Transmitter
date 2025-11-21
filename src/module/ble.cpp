#include "logger.h"
#include "config.h"
#include "module/ble.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// 蓝牙服务器
static BLEServer *bleServer = NULL;
// 信息服务 用于传输设备配置信息
static BLEService *infoService = NULL;
static BLECharacteristic *deviceCharacteristic = NULL;
static BLECharacteristic *modelCharacteristic = NULL;
static BLECharacteristic *manufacturerCharacteristic = NULL;
// 电池电量服务 用于传输电池电量
static BLEService *batteryService = NULL;
static BLECharacteristic *batteryCharacteristic = NULL;
// 音频服务 用于传输音频数据
static BLEService *audioService = NULL;
static BLECharacteristic *dataCharacteristic = NULL;
static BLECharacteristic *controlCharacteristic = NULL;

// 蓝牙状态
bool clientConnected = false;
bool clientAudio = false;
bool clientAudioMode = AUDIO_CONTROL_MODE_BLE;

class BLEServerCallback : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    clientConnected = true;
    logger::debugln("BLE Server has client connected.");
    // Continue advertising for more connections
    // BLEDevice::startAdvertising();
  };

  void onDisconnect(BLEServer *pServer)
  {
    clientConnected = false;
    BLEDevice::startAdvertising();
    logger::debugln("BLE Server has client disconnected.");
  }
};

class ControlCallback : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *characteristic)
  {
    if (characteristic->getLength() > 0)
    {
      // 获取到蓝牙音频控制数据包
      AudioControl *value = (AudioControl *)characteristic->getData();
      clientAudioMode = value->mode;
      if (value->start)
      {
        clientAudio = true;
      }
      else
      {
        clientAudio = false;
      }

      logger::debugln("BLE set audio value {mode=%d, rate=%d, bit=%d}.", value->mode, value->rate, value->bit);
    }
  }
};

static void ble_handle(void *arg)
{
  while (true)
  {
    if (clientConnected)
    {
      if (clientAudio && clientAudioMode == AUDIO_CONTROL_MODE_BLE)
      {
      }
      vTaskDelay(1);
    }
    else
    {
      vTaskDelay(10);
    }
  }
}

void ble::setup()
{
  logger::debugln("BLE is starting...");
  BLEDevice::init("Microphone Transmitter");

  logger::debugln("BLE Server is starting...");
  // 创建GATT服务器
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new BLEServerCallback());
  // 创建服务和特征
  infoService = bleServer->createService(INFO_SERVICE_UUID);
  deviceCharacteristic = infoService->createCharacteristic(DEVICE_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ);
  deviceCharacteristic->addDescriptor(new BLE2902());
  deviceCharacteristic->setValue("Wireless Microphone Transmitter");
  modelCharacteristic = infoService->createCharacteristic(MODEL_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ);
  modelCharacteristic->addDescriptor(new BLE2902());
  modelCharacteristic->setValue("mic_2_192khz_32bit");
  manufacturerCharacteristic = infoService->createCharacteristic(MANUFACTURER_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ);
  manufacturerCharacteristic->addDescriptor(new BLE2902());
  manufacturerCharacteristic->setValue("DUDU233 and TIOSA");
  infoService->start();
  batteryService = bleServer->createService(BATTERY_SERVICE_UUID);
  batteryCharacteristic = batteryService->createCharacteristic(BATTERY_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  batteryCharacteristic->addDescriptor(new BLE2902());
  batteryService->start();
  audioService = bleServer->createService(AUDIO_SERVICE_UUID);
  dataCharacteristic = audioService->createCharacteristic(DATA_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  dataCharacteristic->addDescriptor(new BLE2902());
  controlCharacteristic = audioService->createCharacteristic(CONTROL_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  controlCharacteristic->addDescriptor(new BLE2902());
  controlCharacteristic->setCallbacks(new ControlCallback());
  audioService->start();
  logger::debugln("BLE Server is started.");

  // 开始广播蓝牙存在
  logger::debugln("BLE start to advertise.");
  BLEAdvertising *bleAdvertising = BLEDevice::getAdvertising();
  bleAdvertising->addServiceUUID(INFO_SERVICE_UUID);
  bleAdvertising->addServiceUUID(BATTERY_SERVICE_UUID);
  bleAdvertising->addServiceUUID(AUDIO_SERVICE_UUID);
  bleAdvertising->setScanResponse(false);
  bleAdvertising->setMinPreferred(0x0); // set value to 0x00 to not advertise this parameter
  bleAdvertising->setAppearance(0x0221);
  BLEDevice::startAdvertising();
  logger::debugln("BLE is started.");

  // 启动蓝牙发送线程
  xTaskCreatePinnedToCore(ble_handle, "ble_handle", TASK_BLE_STACK, NULL, TASK_BLE_PRIORITY, NULL, TASK_BLE_CORE);
}