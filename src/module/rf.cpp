#include "logger.h"
#include "config.h"
#include "module/rf.h"
#include "module/led.h"
#include "module/power.h"
#include "module/audio/encoder.h"
#include "module/audio/buffer.h"

/*****************************
          WIFI协议
*****************************/
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/idf_additions.h"
#include "lwip/err.h"
#include "lwip/api.h"
static bool wifiIsOpen = false;
static bool wifiIsClosing = false;
static bool netifInitialized = false;
static bool eventLoopInitialized = false;
static uint8_t wifiRetryTime = 0;
static uint32_t wifiIP;
static uint32_t wifiGatewayIP;
static esp_netif_t *wifiNetIF;
static EventGroupHandle_t wifiEventGroup;
static esp_event_handler_instance_t wifiHandleInstance1;
static esp_event_handler_instance_t wifiHandleInstance2;
static bool socketIsOpen = false;
static netconn *socketSendInstance = NULL;
static netconn *socketReceiveInstance = NULL;
static ip_addr_t socketDestination;

#ifdef BUILD_DEBUG
struct RfDebugSnapshot
{
  bool ready;
  bool ble_mode;
  AudioTxBufferDebugStats buffer;
  uint32_t sent_parts;
  uint32_t sent_payload;
  uint32_t send_fail;
  uint32_t max_batch_us;
  int32_t rssi;
  uint32_t ble_parts;
  uint32_t ble_payload;
  uint32_t ble_fail;
};

static portMUX_TYPE rf_debug_mux = portMUX_INITIALIZER_UNLOCKED;
static RfDebugSnapshot rf_debug_snapshot = {};

static void rfDebugHandle(void *arg)
{
  (void)arg;
  vTaskDelay(pdMS_TO_TICKS(750));
  while (true)
  {
    RfDebugSnapshot snapshot = {};
    portENTER_CRITICAL(&rf_debug_mux);
    if (rf_debug_snapshot.ready)
    {
      snapshot = rf_debug_snapshot;
      rf_debug_snapshot.ready = false;
    }
    portEXIT_CRITICAL(&rf_debug_mux);
    if (snapshot.ready)
    {
      if (snapshot.ble_mode)
      {
        LOGGER_INFO("Audio TX BLE parts=%lu payload=%lu send_fail=%lu",
                    static_cast<unsigned long>(snapshot.ble_parts),
                    static_cast<unsigned long>(snapshot.ble_payload),
                    static_cast<unsigned long>(snapshot.ble_fail));
      }
      else
      {
        LOGGER_INFO("Audio TX WiFi frames=%lu skipped=%lu backlog_max=%lu parts=%lu payload=%lu sent_parts=%lu sent_payload=%lu send_fail=%lu alloc_fail=%lu max_batch_us=%lu rssi=%ld",
                    static_cast<unsigned long>(snapshot.buffer.frames),
                    static_cast<unsigned long>(snapshot.buffer.skipped_frames),
                    static_cast<unsigned long>(snapshot.buffer.max_backlog),
                    static_cast<unsigned long>(snapshot.buffer.parts),
                    static_cast<unsigned long>(snapshot.buffer.payload_bytes),
                    static_cast<unsigned long>(snapshot.sent_parts),
                    static_cast<unsigned long>(snapshot.sent_payload),
                    static_cast<unsigned long>(snapshot.send_fail),
                    static_cast<unsigned long>(snapshot.buffer.allocation_errors),
                    static_cast<unsigned long>(snapshot.max_batch_us), static_cast<long>(snapshot.rssi));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
#endif

static bool wifi_is_connected()
{
  return (wifiIsOpen && socketIsOpen);
}

// 发送数据 需要 netbuf_new
static bool wifi_send(netbuf *buf)
{
  if (buf == NULL || socketSendInstance == NULL)
  {
    if (buf != NULL) netbuf_delete(buf);
    return false;
  }
  err_t err = netconn_sendto(socketSendInstance, buf, &socketDestination, WIFI_NO_PORT);
  if (err != ERR_OK)
  {
    netbuf_delete(buf);
    return false;
  }
  // 释放
  netbuf_delete(buf);
  return true;
}

// 读取数据 需要 netbuf_delete
static bool wifi_receive(netbuf **buf)
{
  if (buf == NULL || socketReceiveInstance == NULL) return false;
  err_t err = netconn_recv(socketReceiveInstance, buf);
  if (err == ERR_WOULDBLOCK)
  {
    return false;
  }
  else if (err != ERR_OK)
  {
    LOGGER_WARN("WiFi Socket receive failed: %d", err);
    return false;
  }
  return true;
}

static bool wifi_socket_close()
{
  if (socketIsOpen)
  {
    socketIsOpen = false;
    if (socketSendInstance != NULL)
    {
      netconn_delete(socketSendInstance);
      socketSendInstance = NULL;
    }
    if (socketReceiveInstance != NULL)
    {
      netconn_delete(socketReceiveInstance);
      socketReceiveInstance = NULL;
    }
  }
  LOGGER_INFO("WiFi Socket is shutdown.");
  return true;
}

static bool wifi_socket_open(uint32_t localIP, uint32_t destIP)
{
  if (socketIsOpen)
  {
    wifi_socket_close();
  }

  // 创建
  socketSendInstance = netconn_new_with_proto_and_callback(NETCONN_RAW, WIFI_IP_PROTOCOL, NULL);
  if (socketSendInstance == NULL)
  {
    LOGGER_WARN("WiFi Socket unable to create!");
    return false;
  }
  socketReceiveInstance = netconn_new_with_proto_and_callback(NETCONN_RAW, WIFI_IP_PROTOCOL, NULL);
  if (socketReceiveInstance == NULL)
  {
    netconn_delete(socketSendInstance);
    LOGGER_WARN("WiFi Socket unable to create!");
    return false;
  }

  // 绑定到指定地址
  ip_addr_t local_ip = {};
  local_ip.u_addr.ip4.addr = localIP;
  err_t err = netconn_bind(socketSendInstance, &local_ip, WIFI_NO_PORT);
  if (err != ERR_OK)
  {
    netconn_delete(socketSendInstance);
    socketSendInstance = NULL;
    netconn_delete(socketReceiveInstance);
    socketReceiveInstance = NULL;
    LOGGER_WARN("WiFi Socket netconn bind failed: %d", err);
    return false;
  }
  err = netconn_bind(socketReceiveInstance, IP_ADDR_ANY, WIFI_NO_PORT);
  if (err != ERR_OK)
  {
    netconn_delete(socketSendInstance);
    socketSendInstance = NULL;
    netconn_delete(socketReceiveInstance);
    socketReceiveInstance = NULL;
    LOGGER_WARN("WiFi Socket netconn bind failed: %d", err);
    return false;
  }
  // 远程地址
  socketDestination.u_addr.ip4.addr = destIP;

  // 设置非阻塞模式
  netconn_set_nonblocking(socketReceiveInstance, true);

  socketIsOpen = true;
  LOGGER_INFO("WiFi Socket is started.");
  return true;
}

static void wifi_close();
static bool ble_open();

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
  if (event_base == WIFI_EVENT)
  {
    switch (event_id)
    {
    // STA 启动事件
    case WIFI_EVENT_STA_START:
    {
      // 开始连接
      wifiRetryTime = 0;
      esp_wifi_connect();
      LOGGER_INFO("WiFi is starting to connect.");
      break;
    }

    // STA 连接成功事件
    case WIFI_EVENT_STA_CONNECTED:
    {
      break;
    }

    // STA 断开连接事件
    case WIFI_EVENT_STA_DISCONNECTED:
    {
      // 关闭流程中忽略事件:回调可能在事件任务中仍在执行,
      // 而事件组即将被释放,此时访问会造成内存破坏
      if (wifiIsClosing)
      {
        break;
      }
      if (wifiIsOpen)
      {
        // 断开连接了
        LOGGER_WARN("WiFi is disconnected from AP.");
        // 断开 socket
        if (socketIsOpen)
        {
          wifi_socket_close();
        }
        if (wifiRetryTime < WIFI_RETRY)
        {
          esp_wifi_connect();
          wifiRetryTime++;
          LOGGER_WARN("WiFi %dst try to reconnect to the AP.", wifiRetryTime);
        }
        else
        {
          power::core_restart();
        }
      }
      else
      {
        // 正在连接 没连上
        if (wifiRetryTime < WIFI_RETRY)
        {
          esp_wifi_connect();
          wifiRetryTime++;
          LOGGER_WARN("WiFi %dst try to connect to the AP.", wifiRetryTime);
        }
        else
        {
          xEventGroupSetBits(wifiEventGroup, WIFI_FAIL_BIT);
          LOGGER_WARN("WiFi connect to the AP fail!");
        }
      }
      break;
    }

    default:
    {
      break;
    }
    }
  }
  else if (event_base == IP_EVENT)
  {
    switch (event_id)
    {
    // STA 获得 IP 地址事件
    case IP_EVENT_STA_GOT_IP:
    {
      ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
      wifiIP = event->ip_info.ip.addr;
      wifiGatewayIP = event->ip_info.gw.addr;
      if (wifiIsOpen && !socketIsOpen)
      {
        wifi_socket_open(wifiIP, wifiGatewayIP);
      }
      xEventGroupSetBits(wifiEventGroup, WIFI_CONNECTED_BIT);
      LOGGER_INFO("WiFi got IP:" IPSTR, IP2STR(&event->ip_info.ip));
      break;
    }

    // STA 丢失 IP 地址事件
    case IP_EVENT_STA_LOST_IP:
    {
      wifi_socket_close();
      wifiIP = 0;
      wifiGatewayIP = 0;
      LOGGER_INFO("WiFi lost IP.");
      break;
    }

    default:
    {
      break;
    }
    }
  }
}

static void wifi_close()
{
  if (wifiIsOpen)
  {
    // 标记关闭中:esp_wifi_stop会派发断开事件,注销前已在事件任务中
    // 开始执行的回调借此标志跳过处理,避免访问即将释放的事件组
    wifiIsClosing = true;
    wifi_socket_close();
    wifiIsOpen = false;

    // 先注销事件回调再停止WiFi:注销后esp_wifi_stop派发的事件不再进入回调
    esp_err_t err;
    err = esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifiHandleInstance1);
    if (err != ESP_OK)
    {
      LOGGER_WARN("WiFi esp_event_handler_instance_unregister failed! Reason=%s", esp_err_to_name(err));
    }
    err = esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, wifiHandleInstance2);
    if (err != ESP_OK)
    {
      LOGGER_WARN("WiFi esp_event_handler_instance_unregister failed! Reason=%s", esp_err_to_name(err));
    }

    // 逐步释放并容忍个别步骤出错,保证清理完整执行到底
    err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED)
    {
      LOGGER_WARN("WiFi esp_wifi_stop failed! Reason=%s", esp_err_to_name(err));
    }
    err = esp_wifi_deinit();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT)
    {
      LOGGER_WARN("WiFi esp_wifi_deinit failed! Reason=%s", esp_err_to_name(err));
    }
    esp_netif_destroy(wifiNetIF);

    // 事件组最后释放:此时所有可能使用它的回调均已注销
    vEventGroupDelete(wifiEventGroup);
    wifiEventGroup = nullptr;
    wifiIsClosing = false;
  }
  LOGGER_INFO("WiFi is shutdown.");
}

static bool wifi_open(const char *ssid, const char *password)
{
  if (wifiIsOpen)
  {
    wifi_close();
  }

  wifiEventGroup = xEventGroupCreate();
  if (wifiEventGroup == nullptr)
  {
    LOGGER_ERROR("WiFi event group create failed!");
    return false;
  }

  esp_err_t err;
  // 创建网络接口
  if (!netifInitialized)
  {
    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
      LOGGER_ERROR("WiFi esp_netif_init failed! Reason=%s", esp_err_to_name(err));
      vEventGroupDelete(wifiEventGroup);
      wifiEventGroup = nullptr;
      return false;
    }
    netifInitialized = true;
  }
  if (!eventLoopInitialized)
  {
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
      LOGGER_ERROR("WiFi esp_event_loop_create_default failed! Reason=%s", esp_err_to_name(err));
      vEventGroupDelete(wifiEventGroup);
      wifiEventGroup = nullptr;
      return false;
    }
    eventLoopInitialized = true;
  }
  wifiNetIF = esp_netif_create_default_wifi_sta();
  // 网络接口已创建,之后的失败路径统一走wifi_close()完整清理
  wifiIsOpen = true;

  // 初始化 WiFi
  static wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&wifi_init_config);
  if (err != ESP_OK)
  {
    LOGGER_ERROR("WiFi esp_wifi_init failed! Reason=%s", esp_err_to_name(err));
    wifi_close();
    return false;
  }

  // 注册事件
  err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &wifiHandleInstance1);
  if (err != ESP_OK)
  {
    LOGGER_ERROR("WiFi esp_event_handler_instance_register failed! Reason=%s", esp_err_to_name(err));
    wifi_close();
    return false;
  }
  err = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &wifiHandleInstance2);
  if (err != ESP_OK)
  {
    LOGGER_ERROR("WiFi esp_event_handler_instance_register failed! Reason=%s", esp_err_to_name(err));
    wifi_close();
    return false;
  }

  // 启动 WiFi STA
  static wifi_config_t wifi_config;
  strcpy((char *)wifi_config.sta.ssid, ssid);
  strcpy((char *)wifi_config.sta.password, password);
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err != ESP_OK)
  {
    LOGGER_ERROR("WiFi esp_wifi_set_mode failed! Reason=%s", esp_err_to_name(err));
    wifi_close();
    return false;
  }
  err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  if (err != ESP_OK)
  {
    LOGGER_ERROR("WiFi esp_wifi_set_config failed! Reason=%s", esp_err_to_name(err));
    wifi_close();
    return false;
  }
  err = esp_wifi_start();
  if (err != ESP_OK)
  {
    LOGGER_ERROR("WiFi esp_wifi_start failed! Reason=%s", esp_err_to_name(err));
    wifi_close();
    return false;
  }

  // 禁用STA省电模式: 默认的省电模式会让AP缓存数据按DTIM周期突发下发,
  // 接收端40ms的缓冲无法吸收这种抖动, 导致音频"一段一段"断续
  err = esp_wifi_set_ps(WIFI_PS_NONE);
  if (err != ESP_OK)
  {
    LOGGER_WARN("WiFi esp_wifi_set_ps failed! Reason=%s", esp_err_to_name(err));
  }

  // 等待 WiFi 连接成功或者失败
  EventBits_t bits = xEventGroupWaitBits(wifiEventGroup, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
  if (bits & WIFI_CONNECTED_BIT)
  {
    LOGGER_INFO("WiFi is started for local IP %d.%d.%d.%d, gateway IP %d.%d.%d.%d.",
                ((uint8_t *)&wifiIP)[0], ((uint8_t *)&wifiIP)[1], ((uint8_t *)&wifiIP)[2], ((uint8_t *)&wifiIP)[3],
                ((uint8_t *)&wifiGatewayIP)[0], ((uint8_t *)&wifiGatewayIP)[1], ((uint8_t *)&wifiGatewayIP)[2], ((uint8_t *)&wifiGatewayIP)[3]);
    if (!wifi_socket_open(wifiIP, wifiGatewayIP))
    {
      wifi_close();
      return false;
    }
    return true;
  }
  else
  {
    wifi_close();
    LOGGER_WARN("WiFi started failed! Can't connect to %s (%s)!", ssid, password);
    return false;
  }
}
/****************************/

/*****************************
          BLE协议
*****************************/
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "store/config/ble_store_config.h"
// IDF的ble_store_config.h未声明init函数,官方示例同样是手动前置声明(注意保持C链接)
extern "C" void ble_store_config_init(void);

static bool bleIsOpen = false;
static bool bleIsClosing = false;
static bool bleIsAdvertising = false;
static bool bleChannelBusy = false; // 通道上暂挂着未发完的SDU,等COC_TX_UNSTALLED
static uint16_t bleConnectionHandle = 0;
static ble_l2cap_chan *bleChannel = NULL;
// 蓝牙接收队列，避免在 NimBLE 回调和 RF 任务之间共享 STL 容器。
struct BleReceive
{
  uint16_t size;
  uint8_t data[BLE_L2CAP_MTU];
};
static QueueHandle_t bleReceiveQueue;

static void ble_start_advertising();
static void ble_stop_advertising();

static int ble_l2cap_handler(struct ble_l2cap_event *event, void *arg)
{
  int rc;
  switch (event->type)
  {
  // 连接事件
  case BLE_L2CAP_EVENT_COC_CONNECTED:
  {
    if (event->connect.status == 0)
    {
      ble_l2cap_chan_info chan_info;
      rc = ble_l2cap_get_chan_info(event->connect.chan, &chan_info);
      if (rc != 0)
      {
        LOGGER_WARN("Failed to get L2CAP channel info: %d", rc);
        break;
      }
      if (chan_info.psm == BLE_L2CAP_PSM)
      {
        bleChannel = event->connect.chan;
        bleChannelBusy = false;
        LOGGER_INFO("BLE channel connected.");
      }
    }
    else
    {
      LOGGER_WARN("BLE L2CAP COC error: %d", event->connect.status);
    }
    break;
  }

  // 断开连接事件
  case BLE_L2CAP_EVENT_COC_DISCONNECTED:
  {
    ble_l2cap_chan_info chan_info;
    rc = ble_l2cap_get_chan_info(event->disconnect.chan, &chan_info);
    if (rc != 0)
    {
      LOGGER_WARN("Failed to get L2CAP channel info: %d", rc);
      break;
    }
    if (chan_info.psm == BLE_L2CAP_PSM)
    {
      bleChannel = NULL;
      bleChannelBusy = false;
      LOGGER_INFO("BLE channel disconnected.");
    }
    break;
  }

  // 暂挂SDU续发完成事件(信用回充后栈自动发完挂起的数据)
  case BLE_L2CAP_EVENT_COC_TX_UNSTALLED:
  {
    bleChannelBusy = false;
    if (event->tx_unstalled.status != 0)
    {
      LOGGER_WARN("BLE tx unstalled with error: %d", event->tx_unstalled.status);
    }
    break;
  }

  // 接收连接事件
  case BLE_L2CAP_EVENT_COC_ACCEPT:
  {
    // 接受连接
    os_mbuf *sdu_rx;
    sdu_rx = os_msys_get_pkthdr(BLE_L2CAP_MTU, 0);
    if (sdu_rx == NULL)
    {
      LOGGER_WARN("BLE L2CAP accept no memory!");
      break;
    }
    rc = ble_l2cap_recv_ready(event->accept.chan, sdu_rx);
    if (rc != 0)
    {
      LOGGER_WARN("BLE L2CAP accept failed!");
      break;
    }
    LOGGER_INFO("BLE L2CAP accept request.");
    break;
  }

  // 接收到数据事件
  case BLE_L2CAP_EVENT_COC_DATA_RECEIVED:
  {
    if (event->receive.sdu_rx != NULL)
    {
      // 放进接收队列
      uint16_t size = event->receive.sdu_rx->om_len;
      BleReceive receive = {.size = size};
      if (size > sizeof(receive.data))
      {
        LOGGER_WARN("BLE packet is too large: %u", size);
        os_mbuf_free(event->receive.sdu_rx);
        break;
      }
      memcpy(receive.data, event->receive.sdu_rx->om_data, size);
      if (xQueueSend(bleReceiveQueue, &receive, 0) != pdPASS)
      {
        LOGGER_WARN("BLE receive queue is full.");
      }
      os_mbuf_free(event->receive.sdu_rx);
    }

    // 响应数据 准备接收下一个数据包
    os_mbuf *sdu_rx;
    sdu_rx = os_msys_get_pkthdr(BLE_L2CAP_MTU, 0);
    if (sdu_rx == NULL)
    {
      LOGGER_WARN("BLE L2CAP accept no memory!");
      break;
    }
    rc = ble_l2cap_recv_ready(event->receive.chan, sdu_rx);
    if (rc != 0)
    {
      LOGGER_WARN("BLE L2CAP accept failed!");
      break;
    }
    break;
  }

  default:
  {
    break;
  }
  }
  return 0;
}

static int ble_gap_handler(struct ble_gap_event *event, void *arg)
{
  int rc;
  switch (event->type)
  {
  // 连接事件
  case BLE_GAP_EVENT_CONNECT:
  {
    if (event->connect.status == 0)
    {
      // 设置蓝牙控制器包长提升性能
      rc = ble_hs_hci_util_set_data_len(event->connect.conn_handle, BLE_PACKET_LENGTH, BLE_PACKET_TIME);
      if (rc != 0)
      {
        LOGGER_WARN("BLE set packet length failed; rc = %d", rc);
      }

      // 连接成功
      bleConnectionHandle = event->connect.conn_handle;
      // 创建 L2CAP 服务器
      rc = ble_l2cap_create_server(BLE_L2CAP_PSM, BLE_L2CAP_MTU, ble_l2cap_handler, NULL);

      if (rc != 0)
      {
        LOGGER_WARN("BLE failed to create config L2CAP server: %d", rc);
      }
      ble_stop_advertising();
      LOGGER_INFO("BLE connected, conn_handle: %d", event->connect.conn_handle);
    }
    else
    {
      // 连接失败
      ble_start_advertising();
      LOGGER_WARN("BLE connected failed! status %d", event->connect.status);
    }
    break;
  }

  // 断开连接事件
  case BLE_GAP_EVENT_DISCONNECT:
  {
    bleConnectionHandle = BLE_HS_CONN_HANDLE_NONE;
    // 清理L2CAP通道
    bleChannel = NULL;
    if (!bleIsClosing && bleIsOpen)
    {
      ble_start_advertising();
    }
    LOGGER_INFO("BLE disconnected. reason=%d", event->disconnect.reason);
    break;
  }

  // 连接参数更新事件
  case BLE_GAP_EVENT_CONN_UPDATE:
  {
    ble_gap_conn_desc desc;
    rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
    if (rc != 0)
    {
      LOGGER_WARN("BLE connection updated, but ble_gap_conn_find desc failed!");
    }
    LOGGER_INFO("BLE connection updated; status=%d handle=%d conn_itvl=%d conn_latency=%d supervision_timeout=%d encrypted=%d authenticated=%d bonded=%d",
                event->conn_update.status, desc.conn_handle,
                desc.conn_itvl, desc.conn_latency, desc.supervision_timeout,
                desc.sec_state.encrypted, desc.sec_state.authenticated, desc.sec_state.bonded);
    break;
  }

  // 蓝牙广告停止事件
  case BLE_GAP_EVENT_ADV_COMPLETE:
  {
    // 重新开始广告
    ble_start_advertising();
    LOGGER_INFO("BLE restart to advertising!");
    break;
  }

  default:
  {
    break;
  }
  }
  return 0;
}

// 重置
static void ble_on_reset(int reason)
{
  LOGGER_WARN("BLE reset, reason: %d", reason);
  ble_stop_advertising();
  bleConnectionHandle = BLE_HS_CONN_HANDLE_NONE;
  bleChannel = NULL;
  bleIsOpen = false;
}

// NimBLE 协议栈加载完成
static void ble_on_sync(void)
{
  int rc;

  // 设置设备名称
  rc = ble_svc_gap_device_name_set(BLE_NAME);
  if (rc != 0)
  {
    LOGGER_WARN("BLE set device name failed! reason=%d", rc);
    return;
  }

  // 默认偏好2M PHY:接收端发起PHY更新时优先协商到2M提升空口速率
  rc = ble_gap_set_prefered_default_le_phy(BLE_GAP_LE_PHY_1M_MASK | BLE_GAP_LE_PHY_2M_MASK,
                                           BLE_GAP_LE_PHY_1M_MASK | BLE_GAP_LE_PHY_2M_MASK);
  if (rc != 0)
  {
    LOGGER_WARN("BLE set default phy failed! rc=%d", rc);
  }

  // 开始广告
  ble_start_advertising();
}

void ble_host_task(void *param)
{
  LOGGER_INFO("BLE Host Task Started.");
  /* This function will return only when nimble_port_stop() is executed */
  nimble_port_run();
  nimble_port_freertos_deinit();
}

static void ble_stop_advertising()
{
  if (bleIsAdvertising)
  {
    led::black();
    ble_gap_adv_stop();
    bleIsAdvertising = false;
  }
}

static void ble_start_advertising()
{
  if (!bleIsAdvertising)
  {
    led::blue();

    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name;
    int rc;

    /**
     *  Set the advertisement data included in our advertisements:
     *     o Flags (indicates advertisement type and other general info).
     *     o Advertising tx power.
     *     o Device name.
     *     o 16-bit service UUIDs (alert notifications).
     */
    memset(&fields, 0, sizeof(fields));
    // 配置设备名称
    name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    // 配置发射功率
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    // 配置标志:
    // o Discoverability in forthcoming advertisement (general)
    // o BLE-only (BR/EDR unsupported).
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
      LOGGER_WARN("BLE error setting advertisement data! rc=%d", rc);
      return;
    }

    // 开始广告
    memset(&adv_params, 0, sizeof(adv_params));
    // adv_params.itvl_min = 0x00A0;
    // adv_params.itvl_max = 0x00B0;
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_handler, NULL);
    if (rc != 0)
    {
      LOGGER_WARN("BLE error enabling advertisement! rc=%d", rc);
      return;
    }

    bleIsAdvertising = true;
  }
}

static bool ble_close()
{
  bleIsClosing = true;
  if (bleIsOpen)
  {
    int rc;
    // 停止广告
    ble_stop_advertising();

    // 停止NimBLE主机任务
    rc = nimble_port_stop();
    if (rc != 0)
    {
      LOGGER_WARN("NimBLE stop failed: %d", rc);
      bleIsClosing = false;
      return false;
    }
    // 反初始化NimBLE
    rc = nimble_port_deinit();
    if (rc != 0)
    {
      LOGGER_WARN("NimBLE deinit failed: %d", rc);
      bleIsClosing = false;
      return false;
    }

    // 清理全局状态
    bleConnectionHandle = BLE_HS_CONN_HANDLE_NONE;
    bleChannel = NULL;
    bleIsOpen = false;
  }
  bleIsClosing = false;
  LOGGER_INFO("BLE is close.");
  return true;
}

static bool ble_open()
{
  if (bleIsOpen)
  {
    ble_close();
  }

  // 初始化 NimBLE
  esp_err_t ret = nimble_port_init();
  if (ret != ESP_OK)
  {
    LOGGER_WARN("BLE port init failed: %d", ret);
    return false;
  }

  // 初始化键值存储回调:否则协议栈启动时"Failed to persist local IRK"告警
  // (本机不使用加密/绑定,仅注册RAM存储让IRK写入成功)
  ble_store_config_init();

  // 初始化配置
  ble_hs_cfg.reset_cb = ble_on_reset;
  ble_hs_cfg.sync_cb = ble_on_sync;

  // 启动 NimBLE 主机任务
  nimble_port_freertos_init(ble_host_task);

  bleIsOpen = true;
  LOGGER_INFO("BLE is started.");
  return true;
}

// 发送数据
bool ble_send(const uint8_t *data, uint16_t len)
{
  if (!bleIsOpen || !bleChannel)
  {
    LOGGER_WARN("BLE channel not ready.");
    return false;
  }

  if (len > BLE_L2CAP_MTU)
  {
    LOGGER_WARN("BLE data too large for L2CAP MTU.");
    return false;
  }

  os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
  if (!om)
  {
    LOGGER_WARN("BLE failed to allocate mbuf.");
    return false;
  }

  int rc = ble_l2cap_send(bleChannel, om);
  if (rc == 0 || rc == BLE_HS_ESTALLED)
  {
    // 0=已全部发出;ESTALLED=栈已接管SDU(挂在通道上等对端信用,回充后自动续发),
    // 两种情况下mbuf均归栈所有,不得释放,均视为发送成功
    if (rc == BLE_HS_ESTALLED)
    {
      // 通道暂挂:收到COC_TX_UNSTALLED事件前不再投递新SDU
      bleChannelBusy = true;
    }
    return true;
  }
  if (rc == BLE_HS_EBUSY || rc == BLE_HS_EBADDATA)
  {
    // 通道上仍有未发完的SDU或载荷超限,本次SDU未被栈接收,缓冲仍归调用方
    bleChannelBusy = true;
    os_mbuf_free_chain(om);
    return false;
  }
  // 其余错误(如ENOMEM)路径中栈已自行释放SDU,不得重复释放
  LOGGER_WARN("BLE failed to send data: %d", rc);
  return false;
}
/****************************/

// 打开指定射频模式(模式切换与切换失败回退共用)
static bool rf_open_mode(RFMode mode)
{
  switch (mode)
  {
  case RF_MODE_BLE:
  {
    return ble_open();
  }
  case RF_MODE_WIFI:
  {
    return wifi_open(config::status.rf.ssid, config::status.rf.password);
  }
  default:
  {
    return false;
  }
  }
}

// 解析数据包
static bool rf_receive_packet(const uint8_t *data, size_t len)
{
  if (data == nullptr || len < sizeof(PacketType))
  {
    return false;
  }
  Packet *packet = (Packet *)data;
  switch (packet->type)
  {
  // 服务器响应ACK
  case PACKET_TYPE_SERVER_ACK:
  {
    break;
  }

  // 配置设备
  case PACKET_TYPE_SERVER_CONTROL_RF:
  {
    if (len != PACKET_SERVER_CONTROL_RF_SIZE ||
        memchr(packet->packet.serverControlRF.ssid, '\0', sizeof(RFText)) == nullptr ||
        memchr(packet->packet.serverControlRF.password, '\0', sizeof(RFText)) == nullptr)
    {
      return false;
    }
    LOGGER_INFO("RF get config control.");
    ServerControlRFPacket *src = &packet->packet.serverControlRF;
    // 复制 SSID 和 密码
    if (strcmp(config::status.rf.ssid, src->ssid) != 0)
    {
      strcpy(config::status.rf.ssid, src->ssid);
    }
    if (strcmp(config::status.rf.password, src->password) != 0)
    {
      strcpy(config::status.rf.password, src->password);
    }
    // 射频模式改变(BLE与WiFi绝不同时运行:先关旧协议栈再开新协议栈,避免IRAM被同时占满)
    if (config::status.rf.mode != src->mode)
    {
      const RFMode oldMode = config::status.rf.mode;
      // 先关闭原来的射频
      switch (oldMode)
      {
      case RF_MODE_BLE:
      {
        if (!ble_close())
        {
          LOGGER_WARN("RF switch aborted, BLE close failed.");
          return false; // 旧协议栈仍在运行,保持原模式
        }
        break;
      }
      case RF_MODE_WIFI:
      {
        // wifi_close内部保证完整清理(容忍个别步骤出错),这里不再因清理告警中止切换
        wifi_close();
        break;
      }
      default:
      {
        return false;
      }
      }
      // 再开启新的射频模式
      if (!rf_open_mode(src->mode))
      {
        // 新模式启动失败(如WiFi连接不上):回退旧模式,保证设备始终保有可用射频
        LOGGER_WARN("RF open mode %u failed, rolling back to mode %u.",
                    static_cast<unsigned int>(src->mode), static_cast<unsigned int>(oldMode));
        if (!rf_open_mode(oldMode))
        {
          // 回退也失败:两个协议栈都不可用,重启恢复到默认BLE模式
          LOGGER_ERROR("RF rollback failed, restarting to recover.");
          power::core_restart();
        }
        return false;
      }
      config::status.rf.mode = src->mode;
    }
    break;
  }

  // 配置音频
  case PACKET_TYPE_SERVER_CONTROL_AUDIO:
  {
    if (len != PACKET_SERVER_CONTROL_AUDIO_SIZE)
    {
      return false;
    }
    LOGGER_INFO("BLE get audio control.");
    ServerControlAudioPacket *src = &packet->packet.serverControlAudio;
    // BLE带宽仅支持48000Hz/16bit/单声道,收到更高格式时收敛(未来支持立体声)
    if (config::status.rf.mode == RF_MODE_BLE &&
        (src->channel != AUDIO_CHANNEL_SINGLE || src->rate != AUDIO_RATE_48000 || src->bit != AUDIO_BIT_16))
    {
      LOGGER_WARN("BLE mode only supports 48000Hz/16bit/mono, clamped from %luHz/%ubit/%uch.",
                  static_cast<unsigned long>(src->rate), static_cast<unsigned int>(src->bit),
                  static_cast<unsigned int>(src->channel));
      src->channel = AUDIO_CHANNEL_SINGLE;
      src->rate = AUDIO_RATE_48000;
      src->bit = AUDIO_BIT_16;
    }
    if (config::status.audio.channel != src->channel ||
        config::status.audio.rate != src->rate ||
        config::status.audio.bit != src->bit)
    {
      if (audio::encoder::isOn())
      {
        audio::encoder::on(src->channel, src->rate, src->bit, config::status.audio.mode, config::status.audio.gain);
      }
      config::status.audio.channel = src->channel;
      config::status.audio.rate = src->rate;
      config::status.audio.bit = src->bit;
    }
    // 音频模式改变
    if (config::status.audio.mode != src->mode)
    {
      if (audio::encoder::isOn())
      {
        audio::encoder::setMode(src->mode);
      }
      config::status.audio.mode = src->mode;
    }
    if (config::status.audio.gain != src->gain)
    {
      if (audio::encoder::isOn())
      {
        audio::encoder::setGain(src->gain);
      }
      config::status.audio.gain = src->gain;
    }
    // 打开或者关闭音频传输
    if (config::status.audio.start != src->start)
    {
      if (src->start)
      {
        audio::encoder::on(config::status.audio.channel, config::status.audio.rate, config::status.audio.bit, config::status.audio.mode, config::status.audio.gain);
      }
      else
      {
        audio::encoder::off();
      }
      config::status.audio.start = src->start;
    }
    break;
  }

  default:
  {
    LOGGER_WARN("RF unknow packet type=%d", packet->type);
    break;
  }
  }
  return true;
}

static void rf_handle(void *arg)
{
  // 缓存
  Packet **bleSendBuffer = NULL;
  netbuf *wifiReceiveBuffer = NULL;
  netbuf **wifiSendBuffer = NULL;
  // 定时发送设备状态
  uint16_t statusNumber = 0;
#ifdef BUILD_DEBUG
  uint32_t wifiReportTime = millis();
  uint32_t wifiSendOk = 0;
  uint32_t wifiSendFail = 0;
  uint32_t wifiSentPayload = 0;
  uint32_t wifiMaxBatchUs = 0;
  uint32_t bleSendOk = 0;
  uint32_t bleSendFail = 0;
  uint32_t bleSentPayload = 0;
#endif

  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(TASK_RF_PERIOD);
  while (true)
  {
    xTaskDelayUntil(&xLastWakeTime, xFrequency);

    // 判断当前射频模式
    switch (config::status.rf.mode)
    {
    case RF_MODE_BLE:
    {
      /* 处理 BLE 模块 */
      if (bleIsOpen && bleChannel != NULL)
      {
        /* 发送(通道暂挂期间跳过,等COC_TX_UNSTALLED通知后再投递,避免EBUSY风暴) */
        if (!bleChannelBusy)
        {
          if (config::status.audio.start)
          {
            uint8_t size = audio::buffer::getBLEPacketFront(&bleSendBuffer);
            bool allSent = size > 0;
            for (int part = 0; part < size; part++)
            {
              Packet *packet = bleSendBuffer[part];
              const bool sent = ble_send((uint8_t *)packet, PACKET_BLE_AUDIO_HEAD_SIZE + packet->packet.audioDataBLE.size);
#ifdef BUILD_DEBUG
              if (sent)
              {
                bleSendOk++;
                bleSentPayload += packet->packet.audioDataBLE.size;
              }
              else
              {
                bleSendFail++;
              }
#endif
              allSent = allSent && sent;
            }
            // 全部分片发送成功才推进进度;失败的帧下一轮整帧重发,避免丢帧产生爆音
            if (allSent)
            {
              audio::buffer::setBLEPacketSent(bleSendBuffer[0]->packet.audioDataBLE.number);
            }
            if (bleSendBuffer != NULL)
            {
              free(bleSendBuffer);
              bleSendBuffer = NULL;
            }
          }
          if (statusNumber++ >= RF_CLIENT_STATUS_PERIOD)
          {
            // 发送状态包
            Packet *packet = (Packet *)malloc(PACKET_CLIENT_STATUS_SIZE);
            packet->type = PACKET_TYPE_CLIENT_STATUS;
            packet->packet.clientStatus.status = PACKET_CLIENT_STATUS_OK;
            packet->packet.clientStatus.battery = (uint8_t)power::getBATPercent();
            ble_send((uint8_t *)packet, PACKET_CLIENT_STATUS_SIZE);
            free(packet);
            statusNumber = 0;
          }
        }

        /* 接收 */
        BleReceive receive;
        while (xQueueReceive(bleReceiveQueue, &receive, 0) == pdPASS)
        {
          // 解析数据包
          rf_receive_packet(receive.data, receive.size);
        }
      }
      break;
    }
    case RF_MODE_WIFI:
    {
      /* 处理 WIFI 模块 */
      if (wifi_is_connected())
      {
        /* 发送 */
        if (config::status.audio.start)
        {
          uint8_t size = audio::buffer::getWiFiPacketFront(&wifiSendBuffer);
#ifdef BUILD_DEBUG
          const uint32_t batchStartUs = micros();
#endif
          for (int part = 0; part < size; part++)
          {
            void *packetData = nullptr;
            uint16_t packetLength = 0;
            if (wifiSendBuffer[part] != nullptr)
            {
              netbuf_data(wifiSendBuffer[part], &packetData, &packetLength);
            }
            const uint16_t payloadSize = packetData != nullptr && packetLength >= PACKET_WIFI_AUDIO_HEAD_SIZE
                                             ? reinterpret_cast<Packet *>(packetData)->packet.audioDataWiFi.size
                                             : 0;
            const bool sent = wifi_send(wifiSendBuffer[part]);
#ifdef BUILD_DEBUG
            if (sent)
            {
              wifiSendOk++;
              wifiSentPayload += payloadSize;
            }
            else
            {
              wifiSendFail++;
            }
#else
            (void)payloadSize;
            (void)sent;
#endif
          }
#ifdef BUILD_DEBUG
          if (size > 0)
          {
            const uint32_t batchUs = micros() - batchStartUs;
            if (batchUs > wifiMaxBatchUs)
            {
              wifiMaxBatchUs = batchUs;
            }
          }
#endif
          if (wifiSendBuffer != NULL)
          {
            free(wifiSendBuffer);
            wifiSendBuffer = NULL;
          }
        }
        if (statusNumber++ >= RF_CLIENT_STATUS_PERIOD)
        {
          // 发送状态包
          netbuf *buf = netbuf_new();
          if (buf != NULL)
          {
            Packet *packet = (Packet *)netbuf_alloc(buf, PACKET_CLIENT_STATUS_SIZE);
            packet->type = PACKET_TYPE_CLIENT_STATUS;
            packet->packet.clientStatus.status = PACKET_CLIENT_STATUS_OK;
            packet->packet.clientStatus.battery = (uint8_t)power::getBATPercent();
            wifi_send(buf);
          }
          statusNumber = 0;
        }

        /* 接收 */
        if (wifi_receive(&wifiReceiveBuffer))
        {
          uint8_t *data;
          uint16_t len;
          do
          {
            netbuf_data(wifiReceiveBuffer, (void **)&data, &len);
            if (data == nullptr || len < WIFI_IP_HEAD_LEN)
            {
              break;
            }
            data += WIFI_IP_HEAD_LEN;
            len -= WIFI_IP_HEAD_LEN;
            // 解析数据包
            rf_receive_packet(data, len);
          } while (netbuf_next(wifiReceiveBuffer) >= 0);
          netbuf_delete(wifiReceiveBuffer);
        }
      }
      break;
    }
    }

#ifdef BUILD_DEBUG
    // 每秒汇总一次发送统计(BLE与WiFi共用,当前未激活的一侧计数为零)
    if (millis() - wifiReportTime >= 1000)
    {
      wifiReportTime = millis();
      AudioTxBufferDebugStats bufferStats = {};
      audio::buffer::getWiFiDebugStats(bufferStats);
      wifi_ap_record_t apInfo = {};
      const int32_t rssi = (config::status.rf.mode == RF_MODE_WIFI && wifi_is_connected() &&
                            esp_wifi_sta_get_ap_info(&apInfo) == ESP_OK)
                               ? apInfo.rssi
                               : 0;
      RfDebugSnapshot snapshot = {};
      snapshot.ready = true;
      snapshot.ble_mode = config::status.rf.mode == RF_MODE_BLE;
      snapshot.buffer = bufferStats;
      snapshot.sent_parts = wifiSendOk;
      snapshot.sent_payload = wifiSentPayload;
      snapshot.send_fail = wifiSendFail;
      snapshot.max_batch_us = wifiMaxBatchUs;
      snapshot.rssi = rssi;
      snapshot.ble_parts = bleSendOk;
      snapshot.ble_payload = bleSentPayload;
      snapshot.ble_fail = bleSendFail;
      portENTER_CRITICAL(&rf_debug_mux);
      rf_debug_snapshot = snapshot;
      portEXIT_CRITICAL(&rf_debug_mux);
      wifiSendOk = 0;
      wifiSendFail = 0;
      wifiSentPayload = 0;
      wifiMaxBatchUs = 0;
      bleSendOk = 0;
      bleSendFail = 0;
      bleSentPayload = 0;
    }
#endif
  }
}

void rf::setup()
{
  LOGGER_INFO("Radio Frequency is starting...");
  bleReceiveQueue = xQueueCreate(16, sizeof(BleReceive));
  if (bleReceiveQueue == nullptr)
  {
    LOGGER_ERROR("BLE receive queue creation failed.");
    return;
  }
  ble_open();
  // 启动发送接收线程
  xTaskCreatePinnedToCore(rf_handle, "rf_handle", TASK_RF_STACK, NULL, TASK_RF_PRIORITY, NULL, TASK_RF_CORE);
#ifdef BUILD_DEBUG
  if (xTaskCreatePinnedToCoreWithCaps(rfDebugHandle, "rf_debug", 2560, nullptr, 1, nullptr,
                                      1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS)
  {
    LOGGER_INFO("RF debug task creation failed.");
  }
#endif
  LOGGER_INFO("Radio Frequency is started!");
}
