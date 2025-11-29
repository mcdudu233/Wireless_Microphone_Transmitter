#include "logger.h"
#include "config.h"
#include "module/rf.h"
#include "module/led.h"
#include "module/audio/encoder.h"

#include "BLEDevice.h"
#include "BLEServer.h"
#include "BLEUtils.h"
#include "BLE2902.h"
#include "WiFi.h"
#include "NetworkUdp.h"
#include "esp_wifi.h"

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
static BLECharacteristic *configControlCharacteristic = NULL;
static BLECharacteristic *audioControlCharacteristic = NULL;

// 蓝牙状态
static bool bleConnected = false;
static ConfigControl configBasic;
static AudioControl configAudio;

// WIFI传输
static bool wifiConnected = false;
static NetworkUDP wifiUDPClient;
static NetworkClient wifiTCPClient;
static IPAddress wifiConnectIP;
static uint32_t wifiConnectPort = 3333;

class BLEServerCallback : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    bleConnected = true;
    logger::debugln("BLE Server has client connected. For mtu=%d.", pServer->getPeerMTU(pServer->getConnId()));
    pServer->updatePeerMTU(pServer->getConnId(), 517);
    logger::debugln("BLE Server has client connected. For mtu=%d.", pServer->getPeerMTU(pServer->getConnId()));
    // Continue advertising for more connections
    // BLEDevice::startAdvertising();
  };

  void onDisconnect(BLEServer *pServer)
  {
    bleConnected = false;
    BLEDevice::startAdvertising();
    logger::debugln("BLE Server has client disconnected.");
  }
};

class ConfigControlCallback : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristic)
  {
    if (pCharacteristic->getLength() > 0)
    {
      // 获取到蓝牙配置数据包
      ConfigControl *tmp = (ConfigControl *)pCharacteristic->getData();
      configBasic.start = tmp->start;
      configBasic.mode = tmp->mode;
      configBasic.ip = tmp->ip;
      strcpy(configBasic.name, tmp->name);
      strcpy(configBasic.password, tmp->password);
      logger::debugln("BLE set config value {start=%d, mode=%d, name=%s, password=%s}.", configBasic.start, configBasic.mode, configBasic.name, configBasic.password);
    }
  }
};

class AudioControlCallback : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristic)
  {
    if (pCharacteristic->getLength() > 0)
    {
      // 获取到蓝牙音频控制数据包
      configAudio = *(AudioControl *)pCharacteristic->getData();
      logger::debugln("BLE set audio value {start=%d, rate=%d, bit=%d}.", configAudio.start, configAudio.rate, configAudio.bit);
    }
  }
};

static AudioPacket packet;
static uint32_t packet_last_num;
static unsigned long last_time = millis();
static void rf_handle(void *arg)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(TASK_RF_PERIOD);
  while (true)
  {
    xTaskDelayUntil(&xLastWakeTime, xFrequency);

    if (bleConnected)
    {
      if (configBasic.start)
      {
        switch (configBasic.mode)
        {
        case AUDIO_CONTROL_MODE_BLE:
        {
          if (!bleConnected)
          {
            // 默认已经启动了蓝牙
          }
          break;
        }
        case AUDIO_CONTROL_MODE_WIFI_UDP:
        case AUDIO_CONTROL_MODE_WIFI_TCP:
        {
          if (!wifiConnected)
          {
            logger::debugln("WiFi is starting...");
            WiFi.useStaticBuffers(true); // 使用静态存储TX缓存 增大UDP缓存
            WiFi.mode(WIFI_STA);
            if (WiFi.begin(configBasic.name, configBasic.password) == WL_CONNECT_FAILED)
            {
              WiFi.mode(WIFI_OFF);
              logger::warnln("WiFi started fail!");
              break;
            }

            // 等待 WIFI 连接
            logger::debugln("WiFi is waiting for connect...");
            while (WiFi.status() != WL_CONNECTED)
            {
              delay(10);
            }
            logger::debugln("WiFi is connected for IP %s.", WiFi.localIP().toString());
            logger::debugln("WiFi is starting client...");
            // 启动客户端
            wifiConnectIP = WiFi.gatewayIP();
            if (configBasic.mode == AUDIO_CONTROL_MODE_WIFI_UDP)
            {
              if (!wifiUDPClient.begin(WiFi.localIP(), wifiConnectPort))
              {
                WiFi.mode(WIFI_OFF);
                logger::warnln("WiFi started client fail!");
                break;
              }
            }
            else if (configBasic.mode == AUDIO_CONTROL_MODE_WIFI_TCP)
            {
              if (!wifiTCPClient.connect(wifiConnectIP, wifiConnectPort))
              {
                WiFi.mode(WIFI_OFF);
                logger::warnln("WiFi started client fail!");
                break;
              }
            }
            // 返回客户端IP
            configBasic.ip = (uint32_t)WiFi.localIP();
            configControlCharacteristic->setValue((uint8_t *)&configBasic, sizeof(ConfigControl));
            configControlCharacteristic->indicate();
            wifiConnected = true;
            logger::debugln("WiFi is started.");
          }
          break;
        }
        }
      }
      if (configAudio.start)
      {
        // 先开启音频编码器
        if (!audio::encoder::isOn())
        {
          audio::encoder::on(configAudio.rate, configAudio.bit);
        }

        AudioData *tmp = audio::encoder::getData();
        if (tmp->size > 0 && tmp->num > packet_last_num)
        {
          packet_last_num = tmp->num;
          packet.num = tmp->num;
          memcpy(packet.data, tmp->data, tmp->size);
          size_t packet_size = sizeof(packet.num) + tmp->size;
          // 选择指定协议发送出去
          switch (configBasic.mode)
          {
          case AUDIO_CONTROL_MODE_BLE:
          {
            dataCharacteristic->setValue((uint8_t *)&packet, packet_size);
            dataCharacteristic->notify();
            break;
          }
          case AUDIO_CONTROL_MODE_WIFI_UDP:
          {

            // logger::debugln("%d", packet.num);
            wifiUDPClient.beginPacket(wifiConnectIP, wifiConnectPort);
            wifiUDPClient.write((uint8_t *)&packet, packet_size);
            wifiUDPClient.endPacket();
            break;
          }
          case AUDIO_CONTROL_MODE_WIFI_TCP:
          {
            if (wifiTCPClient.connected())
            {
              unsigned long now_time = millis();
              logger::debugln("%d", now_time - last_time);
              last_time = now_time;
              wifiTCPClient.write((uint8_t *)&packet, packet_size);
            }
            else
            {
              logger::warnln("WiFi TCP is down, reconnet!");
              if (!wifiTCPClient.connect(wifiConnectIP, wifiConnectPort))
              {
                logger::warnln("WiFi TCP started client fail!");
              }
            }
            break;
          }
          }
        }
      }
    }
    else
    {
      delay(10);
    }
  }
}

void rf::setup()
{
  logger::debugln("BLE is starting...");
  led::blue();
  BLEDevice::init("Microphone Transmitter");
  BLEDevice::setMTU(517);

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
  batteryCharacteristic->setValue((uint8_t)100);
  batteryService->start();
  audioService = bleServer->createService(AUDIO_SERVICE_UUID);
  dataCharacteristic = audioService->createCharacteristic(DATA_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  dataCharacteristic->addDescriptor(new BLE2902());
  dataCharacteristic->setValue((uint32_t)0x00000000);
  configControlCharacteristic = audioService->createCharacteristic(CONFIG_CONTROL_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_INDICATE);
  configControlCharacteristic->addDescriptor(new BLE2902());
  configControlCharacteristic->setCallbacks(new ConfigControlCallback());
  configControlCharacteristic->setValue((uint8_t *)&configBasic, sizeof(ConfigControl));
  audioControlCharacteristic = audioService->createCharacteristic(AUDIO_CONTROL_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_INDICATE);
  audioControlCharacteristic->addDescriptor(new BLE2902());
  audioControlCharacteristic->setCallbacks(new AudioControlCallback());
  audioControlCharacteristic->setValue((uint8_t *)&configAudio, sizeof(AudioControl));
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

  // 启动蓝牙发送线程
  xTaskCreatePinnedToCore(rf_handle, "rf_handle", TASK_RF_STACK, NULL, TASK_RF_PRIORITY, NULL, TASK_RF_CORE);

  led::black();
  logger::debugln("BLE is started.");

  // 默认关闭 WIFI
  WiFi.mode(WIFI_OFF);
  logger::debugln("WiFi is off.");
}