#include "logger.h"
#include "module/audio/buffer.h"

// 循环缓冲区
static AudioData *data;
static uint8_t data_pointer;
static uint32_t data_number;

#ifdef BUILD_DEBUG
static AudioTxBufferDebugStats wifi_debug_stats = {};
#endif

uint8_t *audio::buffer::getWritePointer(uint32_t packet_size)
{
  // 写入空闲槽位但暂不推进指针(此时包对读取端不可见),
  // 配合commitWrite发布完整数据, 避免发送任务读到写了一半的包(撕裂包产生杂音)
  AudioData *buffer = &data[data_pointer];
  buffer->num = data_number % UINT32_MAX;
  buffer->size = packet_size;
  return buffer->data;
}

void audio::buffer::commitWrite()
{
  data_pointer = (data_pointer + 1) % AUDIO_BUFFER_MAX_BUFFER_SIZE;
  data_number++;
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
  int32_t now = getAudioDataFront()->num;
  if (number > now)
  {
    return nullptr;
  }
  else if (number == now)
  {
    return getAudioDataFront();
  }
  else
  {
    if ((now - number) >= AUDIO_BUFFER_MAX_BUFFER_SIZE)
    {
      return nullptr;
    }
    return getAudioData(now - number);
  }
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
  if (data_number == 0)
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

  if (!wifiStarted || audio->num > wifiLastNumber)
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

static uint32_t bleLastNumber = 0;
uint8_t audio::buffer::getBLEPacketFront(Packet ***buffers)
{
  AudioData *audio = getAudioDataFront();
  if (audio->num > bleLastNumber)
  {
    bleLastNumber = audio->num; // 已发送

    uint8_t part_max = audio->size / PACKET_BLE_AUDIO_DATA_MAX_SIZE;
    if (audio->size % PACKET_BLE_AUDIO_DATA_MAX_SIZE != 0)
    {
      part_max += 1;
    }
    *buffers = (Packet **)malloc(sizeof(Packet *) * part_max);
    if (*buffers == NULL)
    {
      LOGGER_WARN("BLE (Packet **) malloc failed!");
      return 0;
    }
    // 创建每个包
    for (uint8_t part = 0; part < part_max; part++)
    {
      // 计算包大小
      uint16_t part_size;
      if (part != part_max - 1)
      {
        part_size = PACKET_BLE_AUDIO_DATA_MAX_SIZE;
      }
      else
      {
        // 最后一个包不一定是满的
        part_size = audio->size - PACKET_BLE_AUDIO_DATA_MAX_SIZE * (part_max - 1);
      }

      // 初始化结构体
      Packet *buffer = (Packet *)malloc(PACKET_BLE_AUDIO_HEAD_SIZE + part_size);
      if (buffer == NULL)
      {
        // 释放之前已分配的资源
        for (uint8_t i = 0; i < part; i++)
        {
          free((*buffers)[i]);
        }
        free(*buffers);
        *buffers = NULL;
        LOGGER_WARN("BLE Packet malloc failed!");
        return 0;
      }
      (*buffers)[part] = buffer;
      buffer->type = PACKET_TYPE_BLE_AUDIO;
      buffer->packet.audioDataBLE.size = part_size;
      buffer->packet.audioDataBLE.number = audio->num;
      buffer->packet.audioDataBLE.part = part;
      memcpy(buffer->packet.audioDataBLE.data, audio->data + PACKET_BLE_AUDIO_DATA_MAX_SIZE * part, part_size);
    }
    return part_max;
  }
  else
  {
    return 0;
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
  wifiLastNumber = 0;
  wifiStarted = false;
  bleLastNumber = 0;
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
