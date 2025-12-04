#include "logger.h"
#include "config.h"
#include "module/rf.h"
#include "module/led.h"
#include "module/audio/encoder.h"
#include "module/audio/buffer.h"

#include "string"
#include "vector"

static ConfigServerControl configBasic;
static AudioServerControl configAudio;

/*****************************
          传输层协议
*****************************/
#include "lwip/err.h"
#include "lwip/api.h"
static bool socketIsOpen = false;
static netconn *socketInstance;
static ip_addr_t socketDestination;

static bool socket_send(netbuf *buf)
{
  err_t err = netconn_sendto(socketInstance, buf, &socketDestination, WIFI_NO_PORT);
  if (err != ERR_OK)
  {
    logger::warnln("Socket send failed: %d", err);
  }
  // 释放
  netbuf_delete(buf);
  return true;
}

static bool socket_close()
{
  if (socketIsOpen)
  {
    socketIsOpen = false;
    if (socketInstance != NULL)
    {
      netconn_delete(socketInstance);
      socketInstance = NULL;
    }
  }
  logger::debugln("Socket is shutdown.");
  return true;
}

static bool socket_open(uint32_t localIP, uint32_t destIP)
{
  if (socketIsOpen)
  {
    socket_close();
  }

  // 创建
  socketInstance = netconn_new_with_proto_and_callback(NETCONN_RAW, WIFI_IP_PROTOCOL, NULL);
  if (socketInstance == NULL)
  {
    logger::warnln("Socket unable to create:!");
    return false;
  }

  // 绑定到本地地址
  ip_addr_t local_ip = {.addr = localIP};
  err_t ret = netconn_bind(socketInstance, &local_ip, WIFI_NO_PORT);
  if (ret != ERR_OK)
  {
    logger::warnln("Socket netconn bind failed: %d", ret);
    netconn_delete(socketInstance);
    socketInstance = NULL;
    return false;
  }
  // 绑定远程地址
  socketDestination.addr = destIP;

  // 设置非阻塞模式
  // netconn_set_nonblocking(socketInstance, true);

  socketIsOpen = true;
  logger::debugln("Socket is started.");
  return true;
}
/****************************/

/*****************************
          WIFI协议
*****************************/
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
static bool wifiIsOpen = false;
static uint8_t wifiRetryTime = 0;
static uint32_t wifiIP;
static uint32_t wifiGatewayIP;
static esp_netif_t *wifiNetIF;
static EventGroupHandle_t wifiEventGroup;
static esp_event_handler_instance_t wifiHandleInstance1;
static esp_event_handler_instance_t wifiHandleInstance2;
static const wifi_init_config_t wifiInitConfig = WIFI_INIT_CONFIG_DEFAULT();
static wifi_config_t wifiConfig;

static bool wifi_close();
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
  if (event_base == WIFI_EVENT)
  {
    if (event_id == WIFI_EVENT_STA_START)
    {
      wifiRetryTime = 0;
      esp_wifi_connect();
      logger::debugln("WiFi is starting to connect.");
    }
    else if (event_id == WIFI_EVENT_STA_CONNECTED)
    {
      logger::debugln("WiFi success to connect.");
    }
    else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
      logger::warnln("WiFi failed to connect.");
      if (wifiRetryTime < WIFI_RETRY)
      {
        esp_wifi_connect();
        wifiRetryTime++;
        logger::warnln("WiFi retry to connect to the AP.");
      }
      else
      {
        if (wifiIsOpen)
        {
          // 意外断开连接
          wifi_close();
        }
        else
        {
          xEventGroupSetBits(wifiEventGroup, WIFI_FAIL_BIT);
          logger::warnln("WiFi connect to the AP fail!");
        }
      }
    }
  }
  else if (event_base == IP_EVENT)
  {
    if (event_id == IP_EVENT_STA_GOT_IP)
    {
      ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
      wifiIP = event->ip_info.ip.addr;
      wifiGatewayIP = event->ip_info.gw.addr;
      logger::debugln("WiFi got ip:" IPSTR, IP2STR(&event->ip_info.ip));
      xEventGroupSetBits(wifiEventGroup, WIFI_CONNECTED_BIT);
    }
    else if (event_id == IP_EVENT_STA_LOST_IP)
    {
      wifiIP = 0;
      wifiGatewayIP = 0;
    }
  }
}

static bool wifi_close()
{
  if (wifiIsOpen)
  {
    socket_close();
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifiHandleInstance1));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, wifiHandleInstance2));
    ESP_ERROR_CHECK(esp_wifi_deinit());
    esp_netif_destroy(wifiNetIF);
    vEventGroupDelete(wifiEventGroup);
    wifiNetIF = NULL;
    wifiIsOpen = false;
  }
  logger::debugln("WiFi is shutdown.");
  return true;
}

static bool wifi_open(const char *ssid, const char *password)
{
  if (wifiIsOpen)
  {
    wifi_close();
  }

  wifiEventGroup = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  wifiNetIF = esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = wifiInitConfig;
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &wifiHandleInstance1));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &wifiHandleInstance2));

  strcpy((char *)wifiConfig.sta.ssid, ssid);
  strcpy((char *)wifiConfig.sta.password, password);
  wifiConfig.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifiConfig));
  ESP_ERROR_CHECK(esp_wifi_start());

  /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
   * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
  EventBits_t bits = xEventGroupWaitBits(wifiEventGroup,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE,
                                         pdFALSE,
                                         portMAX_DELAY);

  wifiIsOpen = true;
  logger::debugln("WiFi is started for local IP %d.%d.%d.%d, gateway IP %d.%d.%d.%d.",
                  ((uint8_t *)&wifiIP)[0], ((uint8_t *)&wifiIP)[1], ((uint8_t *)&wifiIP)[2], ((uint8_t *)&wifiIP)[3],
                  ((uint8_t *)&wifiGatewayIP)[0], ((uint8_t *)&wifiGatewayIP)[1], ((uint8_t *)&wifiGatewayIP)[2], ((uint8_t *)&wifiGatewayIP)[3]);

  /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
   * happened. */
  if (bits & WIFI_CONNECTED_BIT)
  {
    socket_open(wifiIP, wifiGatewayIP);
    return true;
  }
  else
  {
    return false;
  }
}
/****************************/

/*****************************
          BLE协议
*****************************/
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gatt_common_api.h"

static bool bleIsOpen = false;
static bool bleIsAdvertising = false;
static const uint16_t bleGattcId = 0;
static uint16_t bleGattcInterface = ESP_GATT_IF_NONE;

static uint16_t bleInfoServiceHandle;
static uint16_t bleBatteryServiceHandle;
static uint16_t bleAudioServiceHandle;

static uint16_t bleDeviceCharHandle;
static uint16_t bleModelCharHandle;
static uint16_t bleManufacturerCharHandle;
static uint16_t bleBatteryCharHandle;
static uint16_t bleDataCharHandle;
static uint16_t bleConfigControlCharHandle;
static uint16_t bleAudioControlCharHandle;

static std::vector<uint16_t> bleAttrHandle;

// 特征属性值
static const char *bleDeviceCharValue = "Wireless Microphone Transmitter";
static esp_attr_value_t bleDeviceChar =
    {
        .attr_max_len = (uint16_t)strlen(bleDeviceCharValue),
        .attr_len = (uint16_t)strlen(bleDeviceCharValue),
        .attr_value = (uint8_t *)bleDeviceCharValue,
};
static const char *bleModelCharValue = "mic_2_192khz_32bit";
static esp_attr_value_t bleModelChar =
    {
        .attr_max_len = (uint16_t)strlen(bleModelCharValue),
        .attr_len = (uint16_t)strlen(bleModelCharValue),
        .attr_value = (uint8_t *)bleModelCharValue,
};
static const char *bleManufacturerCharValue = "DUDU233 and TIOSA";
static esp_attr_value_t bleManufacturerChar =
    {
        .attr_max_len = (uint16_t)strlen(bleManufacturerCharValue),
        .attr_len = (uint16_t)strlen(bleManufacturerCharValue),
        .attr_value = (uint8_t *)bleManufacturerCharValue,
};
static uint8_t bleBatteryCharValue = 100;
static esp_attr_value_t bleBatteryChar =
    {
        .attr_max_len = sizeof(bleBatteryCharValue),
        .attr_len = sizeof(bleBatteryCharValue),
        .attr_value = &bleBatteryCharValue,
};
static AudioPacketBLE bleDataCharValue;
static esp_attr_value_t bleDataChar =
    {
        .attr_max_len = sizeof(bleDataCharValue),
        .attr_len = sizeof(bleDataCharValue),
        .attr_value = (uint8_t *)&bleDataCharValue,
};
static ConfigClientControl bleConfigControlCharValue;
static esp_attr_value_t bleConfigControlChar =
    {
        .attr_max_len = sizeof(bleConfigControlCharValue),
        .attr_len = sizeof(bleConfigControlCharValue),
        .attr_value = (uint8_t *)&bleConfigControlCharValue,
};
static AudioClientControl bleAudioControlCharValue;
static esp_attr_value_t bleAudioControlChar =
    {
        .attr_max_len = sizeof(bleAudioControlCharValue),
        .attr_len = sizeof(bleAudioControlCharValue),
        .attr_value = (uint8_t *)&bleAudioControlCharValue,
};

// 描述符
esp_bt_uuid_t ble2902UUID = {
    .len = ESP_UUID_LEN_16,
    .uuid = {
        .uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG,
    },
};

// 广告数据
static uint8_t bleServiceUUID[16 * SERVICE_NUM] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    0xfb,
    0x34,
    0x9b,
    0x5f,
    0x80,
    0x00,
    0x00,
    0x80,
    0x00,
    0x10,
    0x00,
    0x00,
    (uint8_t)INFO_SERVICE_UUID,
    (uint8_t)(INFO_SERVICE_UUID >> 2),
    0x00,
    0x00,
    /* LSB <--------------------------------------------------------------------------------> MSB */
    0xfb,
    0x34,
    0x9b,
    0x5f,
    0x80,
    0x00,
    0x00,
    0x80,
    0x00,
    0x10,
    0x00,
    0x00,
    (uint8_t)BATTERY_SERVICE_UUID,
    (uint8_t)(BATTERY_SERVICE_UUID >> 2),
    0x00,
    0x00,
    /* LSB <--------------------------------------------------------------------------------> MSB */
    0xfb,
    0x34,
    0x9b,
    0x5f,
    0x80,
    0x00,
    0x00,
    0x80,
    0x00,
    0x10,
    0x00,
    0x00,
    (uint8_t)AUDIO_SERVICE_UUID,
    (uint8_t)(AUDIO_SERVICE_UUID >> 2),
    0x00,
    0x00,
};
static esp_ble_adv_data_t bleAdvertisingData = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006, // slave connection min interval, Time = min_interval * 1.25 msec
    .max_interval = 0x0010, // slave connection max interval, Time = max_interval * 1.25 msec
    .appearance = 0x0221,
    .manufacturer_len = 0,       // TEST_MANUFACTURER_DATA_LEN,
    .p_manufacturer_data = NULL, // test_manufacturer,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(bleServiceUUID),
    .p_service_uuid = bleServiceUUID,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};
// scan response data
static esp_ble_adv_data_t bleAdvertisingScanData = {
    .set_scan_rsp = true,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006,
    .max_interval = 0x0010,
    .appearance = 0x0221,
    .manufacturer_len = 0,       // TEST_MANUFACTURER_DATA_LEN,
    .p_manufacturer_data = NULL, //&test_manufacturer[0],
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(bleServiceUUID),
    .p_service_uuid = bleServiceUUID,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};
static esp_ble_adv_params_t bleAdvertisingParams = {
    .adv_int_min = 0x00A0,
    .adv_int_max = 0x00B0,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    // .peer_addr            =
    // .peer_addr_type       =
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// 接收到基本配置包
static bool ble_open();
static bool ble_close();
static void ble_config_control_handler(uint8_t *data)
{
  logger::debugln("BLE get config control.");
  ConfigServerControl *src = (ConfigServerControl *)data;
  if (configBasic.mode != src->mode)
  {
    configBasic.mode = src->mode;
  }
  if (strcmp(configBasic.name, src->name) != 0)
  {
    strcpy(configBasic.name, src->name);
  }
  if (strcmp(configBasic.password, src->password) != 0)
  {
    strcpy(configBasic.password, src->password);
  }
  if (configBasic.startWiFi != src->startWiFi)
  {
    configBasic.startWiFi = src->startWiFi;
    if (configBasic.startWiFi)
    {
      wifi_open(configBasic.name, configBasic.password);
      ble_close();
    }
    else
    {
      wifi_close();
    }
  }
  if (configBasic.startBLE != src->startBLE)
  {
    configBasic.startBLE = src->startBLE;
    if (configBasic.startBLE)
    {
      ble_open();
    }
    else
    {
      ble_close();
    }
  }
  if (configBasic.start != src->start)
  {
    configBasic.start = src->start;
  }
}

// 接收到音频配置包
static void ble_audio_control_handler(uint8_t *data)
{
  logger::debugln("BLE get audio control.");
  AudioServerControl *src = (AudioServerControl *)data;
  if (configAudio.channel != src->channel)
  {
    configAudio.channel = src->channel;
    audio::encoder::setChannel(configAudio.channel);
  }
  if (configAudio.rate != src->rate)
  {
    configAudio.rate = src->rate;
    audio::encoder::setRate(configAudio.rate);
  }
  if (configAudio.bit != src->bit)
  {
    configAudio.bit = src->bit;
    audio::encoder::setBit(configAudio.bit);
  }
  if (configAudio.autoVolumn != src->autoVolumn)
  {
    configAudio.autoVolumn = src->autoVolumn;
    audio::encoder::setGain(configAudio.volumn);
  }
  if (configAudio.peekVolumn != src->peekVolumn)
  {
    configAudio.peekVolumn = src->peekVolumn;
    audio::encoder::setPeek(configAudio.peekVolumn);
  }
  if (configAudio.volumn != src->volumn)
  {
    configAudio.volumn = src->volumn;
    audio::encoder::setAuto(configAudio.autoVolumn);
  }
  if (configAudio.start != src->start)
  {
    configAudio.start = src->start;
    if (configAudio.start)
    {
      audio::encoder::setRate(configAudio.rate);
      audio::encoder::setChannel(configAudio.channel);
      audio::encoder::setBit(configAudio.bit);
      audio::encoder::on();
      audio::encoder::setGain(configAudio.volumn);
      audio::encoder::setPeek(configAudio.peekVolumn);
      audio::encoder::setAuto(configAudio.autoVolumn);
    }
    else
    {
      audio::encoder::off();
    }
  }
}

static void ble_stop_advertising()
{
  if (bleIsAdvertising)
  {
    led::black();
    esp_ble_gap_stop_advertising();
    bleIsAdvertising = false;
  }
}

static void ble_start_advertising()
{
  if (!bleIsAdvertising)
  {
    led::blue();
    // 配置广告数据
    esp_ble_gap_config_adv_data(&bleAdvertisingData);
    esp_ble_gap_config_adv_data(&bleAdvertisingScanData);
    // 开始广告
    esp_ble_gap_start_advertising(&bleAdvertisingParams);
    bleIsAdvertising = true;
  }
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
  switch (event)
  {
  // 蓝牙广告开始事件
  case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
  { // advertising start complete event to indicate advertising start successfully or failed
    if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
    {
      logger::warnln("BLE advertising start failed, status %d", param->adv_start_cmpl.status);
      break;
    }
    bleIsAdvertising = true;
    logger::debugln("BLE advertising start successfully.");
    break;
  }

  // 蓝牙广告停止事件
  case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
  {
    if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
    {
      logger::warnln("BLE advertising stop failed, status %d", param->adv_stop_cmpl.status);
      break;
    }
    bleIsAdvertising = false;
    logger::debugln("BLE advertising stop successfully");
    break;
  }

  default:
  {
    break;
  }
  }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
  // 注册事件
  if (event == ESP_GATTS_REG_EVT)
  {
    if (param->reg.status == ESP_GATT_OK)
    {
      bleGattcInterface = gatts_if;
      // 设置设备名称
      esp_ble_gap_set_device_name("Microphone Transmitter");
      // 创建服务
      // 设备信息服务
      esp_gatt_srvc_id_t service_uuid = {
          .id = {
              .uuid = {
                  .len = ESP_UUID_LEN_16,
                  .uuid = {.uuid16 = INFO_SERVICE_UUID},
              },
              .inst_id = 0,
          },
          .is_primary = true,
      };
      esp_ble_gatts_create_service(bleGattcInterface, &service_uuid, 10);
      // 电池服务
      service_uuid.id.uuid.uuid.uuid16 = BATTERY_SERVICE_UUID;
      esp_ble_gatts_create_service(bleGattcInterface, &service_uuid, 6);
      // 音频服务
      service_uuid.id.uuid.uuid.uuid16 = AUDIO_SERVICE_UUID;
      esp_ble_gatts_create_service(bleGattcInterface, &service_uuid, 15);
      // 开始广告
      ble_start_advertising();
      logger::debugln("BLE register app success, app_id %04x", param->reg.app_id);
    }
    else
    {
      logger::warnln("BLE register app failed, app_id %04x, status %d", param->reg.app_id, param->reg.status);
    }
    return;
  }

  switch (event)
  {
    // 属性读取事件
  case ESP_GATTS_READ_EVT:
  {
    // If no response is needed, exit early (stack handles it automatically)
    if (!param->read.need_rsp)
    {
      return;
    }

    // logger::debugln("BLE characteristic read, conn_id %d, trans_id %" PRIu32 ", handle %d", param->read.conn_id, param->read.trans_id, param->read.handle);
    esp_gatt_rsp_t rsp;
    // rsp.handle = param->read.handle;
    rsp.attr_value.auth_req = 0;
    rsp.attr_value.offset = 0;
    rsp.attr_value.handle = param->read.handle;

    // 处理描述符
    for (auto &handle : bleAttrHandle)
    {
      if (param->read.handle == handle)
      {
        memcpy(rsp.attr_value.value, &ble2902UUID, 2);
        rsp.attr_value.len = 2;
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
        return;
      }
    }

    // 返回读取到的值
    if (param->read.handle == bleDeviceCharHandle)
    {
      rsp.attr_value.len = bleDeviceChar.attr_len;
      memcpy(rsp.attr_value.value, bleDeviceChar.attr_value, bleDeviceChar.attr_len);
    }
    else if (param->read.handle == bleModelCharHandle)
    {
      rsp.attr_value.len = bleModelChar.attr_len;
      memcpy(rsp.attr_value.value, bleModelChar.attr_value, bleModelChar.attr_len);
    }
    else if (param->read.handle == bleManufacturerCharHandle)
    {
      rsp.attr_value.len = bleManufacturerChar.attr_len;
      memcpy(rsp.attr_value.value, bleManufacturerChar.attr_value, bleManufacturerChar.attr_len);
    }
    else if (param->read.handle == bleBatteryCharHandle)
    {
      rsp.attr_value.len = bleBatteryChar.attr_len;
      memcpy(rsp.attr_value.value, bleBatteryChar.attr_value, bleBatteryChar.attr_len);
    }
    else if (param->read.handle == bleDataCharHandle)
    {
      rsp.attr_value.len = bleDataChar.attr_len;
      memcpy(rsp.attr_value.value, bleDataChar.attr_value, bleDataChar.attr_len);
    }
    else if (param->read.handle == bleConfigControlCharHandle)
    {
      rsp.attr_value.len = bleConfigControlChar.attr_len;
      memcpy(rsp.attr_value.value, bleConfigControlChar.attr_value, bleConfigControlChar.attr_len);
    }
    else if (param->read.handle == bleAudioControlCharHandle)
    {
      rsp.attr_value.len = bleAudioControlChar.attr_len;
      memcpy(rsp.attr_value.value, bleAudioControlChar.attr_value, bleAudioControlChar.attr_len);
    }
    esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
    break;
  }
  // 属性写入事件
  case ESP_GATTS_WRITE_EVT:
  {
    logger::debugln("BLE characteristic write, conn_id %d, trans_id %" PRIu32 ", handle %d, len %d", param->write.conn_id, param->write.trans_id, param->write.handle, param->write.len);
    if (!param->write.is_prep)
    {
      if (param->write.handle == bleConfigControlCharHandle)
      {
        logger::debugln("BLE bleConfigControlCharHandle write, len %d", sizeof(ConfigServerControl));
        if (param->write.len == sizeof(ConfigServerControl))
        {
          ble_config_control_handler(param->write.value);
        }
      }
      else if (param->write.handle == bleAudioControlCharHandle)
      {
        logger::debugln("BLE AudioServerControl write, len %d", sizeof(AudioServerControl));
        if (param->write.len == sizeof(AudioServerControl))
        {
          ble_audio_control_handler(param->write.value);
        }
      }
      if (param->write.need_rsp)
      {
        esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
      }
    }
    break;
  }

  // 服务创建事件
  case ESP_GATTS_CREATE_EVT:
  {
    logger::debugln("BLE service %d create, status %d, service_handle %d", param->create.service_id.id.uuid.uuid.uuid16, param->create.status, param->create.service_handle);
    switch (param->create.service_id.id.uuid.uuid.uuid16)
    {
    case INFO_SERVICE_UUID:
    {
      bleInfoServiceHandle = param->create.service_handle;

      // 添加特征
      esp_bt_uuid_t char_uuid = {
          .len = ESP_UUID_LEN_16,
          .uuid = {
              .uuid16 = DEVICE_CHARACTERISTIC_UUID,
          }};
      esp_ble_gatts_add_char(bleInfoServiceHandle, &char_uuid, ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ,
                             &bleDeviceChar, NULL);
      char_uuid.uuid.uuid16 = MODEL_CHARACTERISTIC_UUID;
      esp_ble_gatts_add_char(bleInfoServiceHandle, &char_uuid, ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ,
                             &bleModelChar, NULL);
      char_uuid.uuid.uuid16 = MANUFACTURER_CHARACTERISTIC_UUID;
      esp_ble_gatts_add_char(bleInfoServiceHandle, &char_uuid, ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ,
                             &bleManufacturerChar, NULL);

      esp_ble_gatts_start_service(bleInfoServiceHandle);
      break;
    }
    case BATTERY_SERVICE_UUID:
    {
      bleBatteryServiceHandle = param->create.service_handle;

      // 添加特征
      esp_bt_uuid_t char_uuid = {
          .len = ESP_UUID_LEN_16,
          .uuid = {
              .uuid16 = BATTERY_CHARACTERISTIC_UUID,
          }};
      esp_ble_gatts_add_char(bleBatteryServiceHandle, &char_uuid, ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                             &bleBatteryChar, NULL);

      esp_ble_gatts_start_service(bleBatteryServiceHandle);
      break;
    }
    case AUDIO_SERVICE_UUID:
    {
      bleAudioServiceHandle = param->create.service_handle;

      // 添加特征
      esp_bt_uuid_t char_uuid = {
          .len = ESP_UUID_LEN_16,
          .uuid = {
              .uuid16 = DATA_CHARACTERISTIC_UUID,
          }};
      esp_ble_gatts_add_char(bleAudioServiceHandle, &char_uuid, ESP_GATT_PERM_READ, ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY,
                             &bleDataChar, NULL);
      char_uuid.uuid.uuid16 = CONFIG_CONTROL_CHARACTERISTIC_UUID;
      esp_ble_gatts_add_char(bleAudioServiceHandle, &char_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                             ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_INDICATE,
                             &bleConfigControlChar, NULL);
      char_uuid.uuid.uuid16 = AUDIO_CONTROL_CHARACTERISTIC_UUID;
      esp_ble_gatts_add_char(bleAudioServiceHandle, &char_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                             ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_INDICATE,
                             &bleAudioControlChar, NULL);

      esp_ble_gatts_start_service(bleAudioServiceHandle);
      break;
    }
    }
    break;
  }

  // 添加特征事件
  case ESP_GATTS_ADD_CHAR_EVT:
  {
    logger::debugln("BLE characteristic add, status %d, attr_handle %d, service_handle %d",
                    param->add_char.status, param->add_char.attr_handle, param->add_char.service_handle);
    uint16_t length = 0;
    const uint8_t *prf_char;
    if (param->add_char.service_handle == bleInfoServiceHandle)
    {
      switch (param->add_char.char_uuid.uuid.uuid16)
      {
      case DEVICE_CHARACTERISTIC_UUID:
      {
        bleDeviceCharHandle = param->add_char.attr_handle;
        break;
      }
      case MODEL_CHARACTERISTIC_UUID:
      {
        bleModelCharHandle = param->add_char.attr_handle;
        break;
      }
      case MANUFACTURER_CHARACTERISTIC_UUID:
      {
        bleManufacturerCharHandle = param->add_char.attr_handle;
        break;
      }
      }
    }
    else if (param->add_char.service_handle == bleBatteryServiceHandle)
    {
      bleBatteryCharHandle = param->add_char.attr_handle;
      esp_ble_gatts_add_char_descr(param->add_char.service_handle, &ble2902UUID, ESP_GATT_PERM_READ, NULL, NULL);
    }
    else if (param->add_char.service_handle == bleAudioServiceHandle)
    {
      switch (param->add_char.char_uuid.uuid.uuid16)
      {
      case DATA_CHARACTERISTIC_UUID:
      {
        bleDataCharHandle = param->add_char.attr_handle;
        break;
      }
      case CONFIG_CONTROL_CHARACTERISTIC_UUID:
      {
        bleConfigControlCharHandle = param->add_char.attr_handle;
        break;
      }
      case AUDIO_CONTROL_CHARACTERISTIC_UUID:
      {
        bleAudioControlCharHandle = param->add_char.attr_handle;
        break;
      }
      }
      esp_ble_gatts_add_char_descr(param->add_char.service_handle, &ble2902UUID, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, NULL, NULL);
    }
    break;
  }

  // 添加描述事件
  case ESP_GATTS_ADD_CHAR_DESCR_EVT:
  {
    if (param->add_char_descr.service_handle == bleBatteryServiceHandle || param->add_char_descr.service_handle == bleAudioServiceHandle)
    {
      bleAttrHandle.push_back(param->add_char_descr.attr_handle);
    }
    logger::debugln("BLE descriptor add, status %d, attr_handle %d, service_handle %d",
                    param->add_char_descr.status, param->add_char_descr.attr_handle, param->add_char_descr.service_handle);
    break;
  }

  // 蓝牙连接到设备事件
  case ESP_GATTS_CONNECT_EVT:
  {
    ble_stop_advertising();
    esp_ble_conn_update_params_t conn_params = {0};
    memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
    /* For the IOS system, please reference the apple official documents about the ble connection parameters restrictions. */
    conn_params.latency = 0;
    conn_params.max_int = 0x20; // max_int = 0x20*1.25ms = 40ms
    conn_params.min_int = 0x00; // min_int = 0x10*1.25ms = 20ms
    conn_params.timeout = 300;  // timeout = 400*10ms = 3000ms
    // start sent the update connection parameters to the peer device.
    esp_ble_gap_update_conn_params(&conn_params);
    logger::debugln("BLE connected, conn_id %u, remote " ESP_BD_ADDR_STR "",
                    param->connect.conn_id, ESP_BD_ADDR_HEX(param->connect.remote_bda));
    break;
  }

  // 蓝牙断开连接事件
  case ESP_GATTS_DISCONNECT_EVT:
  {
    ble_start_advertising();
    logger::debugln("BLE disconnected, remote " ESP_BD_ADDR_STR ", reason 0x%02x",
                    ESP_BD_ADDR_HEX(param->disconnect.remote_bda), param->disconnect.reason);
    break;
  }

  case ESP_GATTS_OPEN_EVT:
  case ESP_GATTS_CANCEL_OPEN_EVT:
  case ESP_GATTS_CLOSE_EVT:
  case ESP_GATTS_LISTEN_EVT:
  case ESP_GATTS_CONGEST_EVT:
  case ESP_GATTS_UNREG_EVT:
  case ESP_GATTS_ADD_INCL_SRVC_EVT:
  case ESP_GATTS_EXEC_WRITE_EVT:
  case ESP_GATTS_MTU_EVT:
  case ESP_GATTS_DELETE_EVT:
  case ESP_GATTS_START_EVT:
  case ESP_GATTS_STOP_EVT:
  case ESP_GATTS_CONF_EVT:
  default:
    break;
  }
}

static bool ble_close()
{
  if (bleIsOpen)
  {

    // 停止广告
    ble_stop_advertising();

    // 断开所有连接
    esp_ble_gatts_close(bleGattcInterface, 0);

    // 注销GATTS应用
    esp_err_t ret = esp_ble_gatts_app_unregister(bleGattcId);
    if (ret != ESP_OK)
    {
      logger::warnln("BLE gatts app unregister failed: %s", esp_err_to_name(ret));
    }

    // 注销GATTS回调函数
    ret = esp_ble_gatts_register_callback(NULL);
    if (ret != ESP_OK)
    {
      logger::warnln("BLE gatts unregister callback failed: %s", esp_err_to_name(ret));
    }

    // 注销GAP回调函数
    ret = esp_ble_gap_register_callback(NULL);
    if (ret != ESP_OK)
    {
      logger::warnln("BLE gap unregister callback failed: %s", esp_err_to_name(ret));
    }

    // 禁用Bluedroid
    ret = esp_bluedroid_disable();
    if (ret != ESP_OK)
    {
      logger::warnln("BLE bluedroid disable failed: %s", esp_err_to_name(ret));
    }

    ret = esp_bluedroid_deinit();
    if (ret != ESP_OK)
    {
      logger::warnln("BLE bluedroid deinit failed: %s", esp_err_to_name(ret));
    }

    // 禁用蓝牙控制器
    ret = esp_bt_controller_disable();
    if (ret != ESP_OK)
    {
      logger::warnln("BLE controller disable failed: %s", esp_err_to_name(ret));
    }

    ret = esp_bt_controller_deinit();
    if (ret != ESP_OK)
    {
      logger::warnln("BLE controller deinit failed: %s", esp_err_to_name(ret));
    }

    // 释放蓝牙控制器内存
    ret = esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    if (ret != ESP_OK)
    {
      logger::warnln("BLE controller memory release failed: %s", esp_err_to_name(ret));
    }

    // 清理全局状态
    bleIsAdvertising = false;
    bleGattcInterface = ESP_GATT_IF_NONE;

    // 清空句柄向量
    bleAttrHandle.clear();

    // 重置服务句柄
    bleInfoServiceHandle = 0;
    bleBatteryServiceHandle = 0;
    bleAudioServiceHandle = 0;

    // 重置特征句柄
    bleDeviceCharHandle = 0;
    bleModelCharHandle = 0;
    bleManufacturerCharHandle = 0;
    bleBatteryCharHandle = 0;
    bleDataCharHandle = 0;
    bleConfigControlCharHandle = 0;
    bleAudioControlCharHandle = 0;

    bleIsOpen = false;
  }
  logger::debugln("BLE is close.");
  return true;
}

static bool ble_open()
{
  if (bleIsOpen)
  {
    ble_close();
  }

  esp_err_t ret;
  ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

  // 初始化蓝牙控制器
  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  ret = esp_bt_controller_init(&bt_cfg);
  if (ret)
  {
    logger::warnln("BLE controller initialize failed: %s", esp_err_to_name(ret));
    return false;
  }
  ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (ret)
  {
    logger::warnln("BLE controller enable failed: %s", esp_err_to_name(ret));
    return false;
  }

  esp_bluedroid_config_t cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
  ret = esp_bluedroid_init_with_cfg(&cfg);
  if (ret)
  {
    logger::warnln("BLE bluedroid init failed: %s", esp_err_to_name(ret));
    return false;
  }
  ret = esp_bluedroid_enable();
  if (ret)
  {
    logger::warnln("BLE bluedroid enable failed: %s", esp_err_to_name(ret));
    return false;
  }

  // Note: Avoid performing time-consuming operations within callback functions.
  ret = esp_ble_gap_register_callback(gap_event_handler);
  if (ret)
  {
    logger::warnln("BLE gap register failed: %s", esp_err_to_name(ret));
    return false;
  }
  ret = esp_ble_gatts_register_callback(gatts_event_handler);
  if (ret)
  {
    logger::warnln("BLE gatts register failed: %s", esp_err_to_name(ret));
    return false;
  }

  ret = esp_ble_gatts_app_register(bleGattcId);
  if (ret)
  {
    logger::warnln("BLE gatts app register error: %s", esp_err_to_name(ret));
    return false;
  }

  ret = esp_ble_gatt_set_local_mtu(517);
  if (ret)
  {
    logger::warnln("BLE set local  MTU failed, error code = %x", ret);
  }

  bleIsOpen = true;
  logger::debugln("BLE is started.");
  return true;
}
/****************************/

// static AudioPacketUDP packet;
static uint32_t packet_last_num;
static void rf_handle(void *arg)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(TASK_RF_PERIOD);
  while (true)
  {
    xTaskDelayUntil(&xLastWakeTime, xFrequency);

    if (configBasic.start || configBasic.startWiFi)
    {
      switch (configBasic.mode)
      {
      case AUDIO_CONTROL_MODE_WIFI:
      {
        if (wifiIsOpen && socketIsOpen)
        {
          netbuf **buffer;
          uint8_t size = audio::buffer::getWiFiPacketFront(&buffer);
          logger::debugln("size %d", size);
          for (int i = 0; i < size; i++)
          {
            socket_send(buffer[i]);
          }
        }
        break;
      }
      case AUDIO_CONTROL_MODE_BLE:
      {
        if (bleIsOpen)
        {
        }
        break;
      }
      }
    }
  }
}

void rf::setup()
{
  ble_open();
  // 启动发送线程
  xTaskCreatePinnedToCore(rf_handle, "rf_handle", TASK_RF_STACK, NULL, TASK_RF_PRIORITY, NULL, TASK_RF_CORE);
}