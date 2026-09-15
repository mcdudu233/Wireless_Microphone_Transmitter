#include "logger.h"
#include "module/audio/buffer.h"

// 循环缓冲区
static AudioData *data;
static uint8_t data_pointer;
static uint32_t data_number;
static bool data_present = false; // 已有已提交帧(区分空缓冲与序号自然回绕到0)

#ifdef BUILD_DEBUG
static AudioTxBufferDebugStats wifi_debug_stats = {};
#endif

uint8_t *audio::buffer::getWritePointer(uint32_t packet_size)
{
  // 写入空闲槽位但暂不推进指针(此时包对读取端不可见),
  // 配合commitWrite发布完整数据, 避免发送任务读到写了一半的包(撕裂包产生杂音)
  AudioData *buffer = &data[data_pointer];
  buffer->num = data_number;
  buffer->size = packet_size;
  return buffer->data;
}

void audio::buffer::commitWrite()
{
  data_pointer = (data_pointer + 1) % AUDIO_BUFFER_MAX_BUFFER_SIZE;
  data_number++;
  data_present = true;
}

// 获取过去的第N个数据
static AudioData *getAudioData(uint8_t last)
{
  return &data[(data_pointer + AUDIO_BUFFER_MAX_BUFFER_SIZE - 1 - last) % AUDIO_BUFFER_MAX_BUFFER_SIZE];
}

AudioData *audio::buffer::getAudioDataFront()
{
  return getAudioData(0);
}

AudioData *audio::buffer::getAudioDataFromNumber(uint32_t number)
{
  const uint32_t now = getAudioDataFront()->num;
  if ((int32_t)(number - now) > 0) // 目标序号在最新帧之后(回绕安全)
  {
    return nullptr;
  }
  const uint32_t age = now - number;
  if (age >= AUDIO_BUFFER_MAX_BUFFER_SIZE)
  {
    return nullptr;
  }
  return getAudioData(age);
}

uint8_t audio::buffer::getPointer()
{
  return data_pointer;
}

uint32_t audio::buffer::getNumber()
{
  return getAudioDataFront()->num;
}

static uint32_t wifiLastNumber = 0;
static bool wifiStarted = false;
uint8_t audio::buffer::getWiFiPacketFront(netbuf ***buffers)
{
  if (!data_present)
  {
    return 0;
  }

  AudioData *newest = getAudioDataFront();
  AudioData *audio = newest;
#ifdef BUILD_DEBUG
  const uint32_t backlog = wifiStarted ? newest->num - wifiLastNumber : 1;
  if (backlog > wifi_debug_stats.max_backlog)
  {
    wifi_debug_stats.max_backlog = backlog;
  }
#endif
  if (wifiStarted)
  {
    audio = getAudioDataFromNumber(wifiLastNumber + 1);
    if (audio == nullptr)
    {
#ifdef BUILD_DEBUG
      wifi_debug_stats.skipped_frames += newest->num - wifiLastNumber - 1;
#endif
      audio = newest;
    }
  }

  if (!wifiStarted || (int32_t)(audio->num - wifiLastNumber) > 0) // 回绕安全的序号比较
  {
    uint8_t part_max = (audio->size + PACKET_WIFI_AUDIO_DATA_MAX_SIZE - 1) /
                       PACKET_WIFI_AUDIO_DATA_MAX_SIZE;
    if (part_max == 0)
    {
      return 0;
    }
    *buffers = (netbuf **)malloc(sizeof(netbuf *) * part_max);
    if (*buffers == NULL)
    {
#ifdef BUILD_DEBUG
      wifi_debug_stats.allocation_errors++;
#endif
      LOGGER_WARN("Socket (netbuf **) malloc failed!");
      return 0;
    }
    // 创建每个包
    for (uint8_t part = 0; part < part_max; part++)
    {
      // 计算包大小
      uint16_t part_size;
      if (part != part_max - 1)
      {
        part_size = PACKET_WIFI_AUDIO_DATA_MAX_SIZE;
      }
      else
      {
        // 最后一个包不一定是满的
        part_size = audio->size - PACKET_WIFI_AUDIO_DATA_MAX_SIZE * (part_max - 1);
      }

      // 初始化结构体
      netbuf *buffer = NULL;
      buffer = netbuf_new();
      if (buffer == NULL)
      {
#ifdef BUILD_DEBUG
        wifi_debug_stats.allocation_errors++;
#endif
        // 释放之前已分配的资源
        for (uint8_t i = 0; i < part; i++)
        {
          netbuf_delete((*buffers)[i]);
        }
        free(*buffers);
        *buffers = NULL;
        LOGGER_WARN("Socket netbuf_new() malloc failed!");
        return 0;
      }
      (*buffers)[part] = buffer;

      // 复制数据
      Packet *packet = (Packet *)netbuf_alloc(buffer, PACKET_WIFI_AUDIO_HEAD_SIZE + part_size);
      if (packet == NULL)
      {
#ifdef BUILD_DEBUG
        wifi_debug_stats.allocation_errors++;
#endif
        // 释放当前buffer
        netbuf_delete((*buffers)[part]);
        // 释放之前已分配的buffers
        for (uint8_t i = 0; i < part; i++)
        {
          netbuf_delete((*buffers)[i]);
        }
        free(*buffers);
        *buffers = NULL;
        LOGGER_WARN("Socket netbuf_alloc() malloc failed!");
        return 0;
      }
      packet->type = PACKET_TYPE_WIFI_AUDIO;
      packet->packet.audioDataWiFi.size = part_size;
      packet->packet.audioDataWiFi.number = audio->num;
      packet->packet.audioDataWiFi.part = part;
      memcpy(packet->packet.audioDataWiFi.data, audio->data + PACKET_WIFI_AUDIO_DATA_MAX_SIZE * part, part_size);
    }
#ifdef BUILD_DEBUG
    wifi_debug_stats.frames++;
    wifi_debug_stats.parts += part_max;
    wifi_debug_stats.payload_bytes += audio->size;
#endif
    wifiLastNumber = audio->num; // 完整分包成功后才标记，分配失败可在下一轮重试。
    wifiStarted = true;
    return part_max;
  }
  else
  {
    return 0;
  }
}

#ifdef BUILD_DEBUG
void audio::buffer::getWiFiDebugStats(AudioTxBufferDebugStats &stats)
{
  stats = wifi_debug_stats;
  wifi_debug_stats = {};
}
#endif

static uint32_t bleLastNumber = 0;  // 最近已确认完成的帧号
static bool bleStarted = false;     // 是否已开始过发送(区分首帧选择逻辑)
static uint32_t bleSendNumber = 0;  // 正在发送的帧号
static uint32_t bleSendSize = 0;    // 正在发送的帧载荷字节数
static uint8_t bleSendPart = 0;     // 下一个待发送的分片序号
static uint8_t bleSendPartMax = 0;  // 正在发送帧的总分片数

bool audio::buffer::getBLEPacket(Packet &packet)
{
  if (!data_present)
  {
    return false;
  }

  // 当前帧已发完:选择下一帧(按序优先,积压超出历史窗口时跳到最新帧)
  if (bleSendPart >= bleSendPartMax)
  {
    AudioData *newest = getAudioDataFront();
    AudioData *audio = newest;
    if (bleStarted)
    {
      audio = getAudioDataFromNumber(bleLastNumber + 1);
      if (audio == nullptr)
      {
        // 积压丢失,跳到最新帧;若只是尚无新帧,下方判断会拒绝
        audio = newest;
      }
    }
    if (audio == nullptr || audio->size == 0 ||
        (bleStarted && (int32_t)(audio->num - bleLastNumber) <= 0)) // 回绕安全的序号比较
    {
      // 尚无新帧可发
      return false;
    }
    bleSendNumber = audio->num;
    bleSendSize = audio->size;
    bleSendPartMax = (audio->size + PACKET_BLE_AUDIO_DATA_MAX_SIZE - 1) / PACKET_BLE_AUDIO_DATA_MAX_SIZE;
    if (bleSendPartMax == 0)
    {
      bleSendPartMax = 1;
    }
    bleSendPart = 0;
  }

  // 按帧号重新定位帧数据(环形槽位可能已被写端覆盖,必须校验帧号)
  AudioData *audio = getAudioDataFromNumber(bleSendNumber);
  if (audio == nullptr || audio->num != bleSendNumber)
  {
    // 帧已滑出历史窗口,放弃当前帧,下一轮重新选帧
    bleSendPart = 0;
    bleSendPartMax = 0;
    return false;
  }

  // 填充分片(热路径零动态分配,缓冲由调用方提供)
  uint16_t part_size;
  if (bleSendPart != bleSendPartMax - 1)
  {
    part_size = PACKET_BLE_AUDIO_DATA_MAX_SIZE;
  }
  else
  {
    // 最后一个分片不一定是满的
    part_size = bleSendSize - PACKET_BLE_AUDIO_DATA_MAX_SIZE * (bleSendPartMax - 1);
  }
  packet.type = PACKET_TYPE_BLE_AUDIO;
  packet.packet.audioDataBLE.size = part_size;
  packet.packet.audioDataBLE.number = bleSendNumber;
  packet.packet.audioDataBLE.part = bleSendPart;
  memcpy(packet.packet.audioDataBLE.data, audio->data + PACKET_BLE_AUDIO_DATA_MAX_SIZE * bleSendPart, part_size);
  return true;
}

void audio::buffer::confirmBLEPacketSent()
{
  bleSendPart++;
  if (bleSendPart >= bleSendPartMax)
  {
    // 整帧全部分片发送完成
    bleLastNumber = bleSendNumber;
    bleStarted = true;
  }
}

void audio::buffer::restart()
{
  for (int i = 0; i < AUDIO_BUFFER_MAX_BUFFER_SIZE; i++)
  {
    data[i].num = 0;
    data[i].size = 0;
  }
  data_pointer = 0;
  data_number = 0;
  data_present = false;
  wifiLastNumber = 0;
  wifiStarted = false;
  bleLastNumber = 0;
  bleStarted = false;
  bleSendNumber = 0;
  bleSendSize = 0;
  bleSendPart = 0;
  bleSendPartMax = 0;
#ifdef BUILD_DEBUG
  wifi_debug_stats = {};
#endif
}

void audio::buffer::setup()
{
  LOGGER_INFO("Audio Buffer is starting...");
  data = (AudioData *)heap_caps_malloc(sizeof(AudioData) * AUDIO_BUFFER_MAX_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
  restart();
  LOGGER_INFO("Audio Buffer is started!");
}
