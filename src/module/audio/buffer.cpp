#include "logger.h"
#include "module/audio/buffer.h"

// 循环缓冲区
static AudioData *data;
static uint8_t data_pointer;
static uint32_t data_number;

uint8_t *audio::buffer::getWritePointer(uint32_t packet_size)
{
  AudioData *buffer = &data[data_pointer];
  data_pointer = (data_pointer + 1) % AUDIO_ENCODER_MAX_BUFFER_SIZE;
  buffer->num = data_number++ % UINT32_MAX;
  buffer->size = packet_size;
  return buffer->data;
}

// 获取过去的第N个数据
static AudioData *getAudioData(uint8_t last)
{
  return &data[(data_pointer + AUDIO_ENCODER_MAX_BUFFER_SIZE - 1 - last) % AUDIO_ENCODER_MAX_BUFFER_SIZE];
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
    if ((now - number) >= AUDIO_ENCODER_MAX_BUFFER_SIZE)
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
uint8_t audio::buffer::getWiFiPacketFront(netbuf ***buffer)
{
  AudioData *audio = getAudioDataFront();
  if (audio->num > wifiLastNumber)
  {
    if (audio->size > WIFI_MAX_DATA_SIZE)
    {
      uint8_t part_max = audio->size / WIFI_MAX_DATA_SIZE + 1;
      uint16_t part_last_size = audio->size - WIFI_MAX_DATA_SIZE * (part_max - 1);
      // 分包
      *buffer = (netbuf **)malloc(sizeof(netbuf *) * part_max);
      for (int i = 0; i < part_max - 1; i++)
      {
        (*buffer)[i] = netbuf_new();
        AudioPacketWIFI *data = (AudioPacketWIFI *)netbuf_alloc((*buffer)[i], sizeof(AudioPacketWIFI));
        data->crc = 0;
        data->number = audio->num;
        data->part = 0;
        data->size = audio->size;
        memcpy(data->data, audio->data, WIFI_MAX_DATA_SIZE);
      }
      // 封装最后一个包
      (*buffer)[part_max - 1] = netbuf_new();
      AudioPacketWIFI *data = (AudioPacketWIFI *)netbuf_alloc((*buffer)[part_max - 1], sizeof(AudioPacketWIFI) - WIFI_MAX_DATA_SIZE + part_last_size);
      data->crc = 0;
      data->number = audio->num;
      data->part = 0;
      data->size = audio->size;
      memcpy(data->data, audio->data, part_last_size);
      return part_max;
    }
    else
    {
      *buffer = (netbuf **)malloc(sizeof(netbuf *));
      (*buffer)[0] = netbuf_new();
      AudioPacketWIFI *data = (AudioPacketWIFI *)netbuf_alloc((*buffer)[0], sizeof(AudioPacketWIFI) - WIFI_MAX_DATA_SIZE + audio->size);
      data->crc = 0;
      data->number = audio->num;
      data->part = 0;
      data->size = audio->size;
      memcpy(data->data, audio->data, audio->size);
      return 1;
    }
  }
  else
  {
    return 0;
  }
}

void audio::buffer::restart()
{
  // 刷新缓存
  for (int i = 0; i < AUDIO_ENCODER_MAX_BUFFER_SIZE; i++)
  {
    data[i].num = 0;
    data[i].size = 0;
  }
  data_pointer = 0;
  data_number = 0;
  wifiLastNumber = 0;
}

void audio::buffer::setup()
{
  data = (AudioData *)heap_caps_malloc(sizeof(AudioData) * AUDIO_ENCODER_MAX_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
  restart();
}