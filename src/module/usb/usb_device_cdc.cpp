#include "module/usb/usb.h"
#include "module/usb/usb_device_cdc.h"

#include "Arduino.h"
#include "tusb.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/rtc.h"
#include "esp_rom_sys.h"

USBCDCStream *USBCDCStream::_instance = nullptr;
USBCDCStream USBCDCSerial;

// Invoked when cdc when line state changed e.g connected/disconnected
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
  USBCDCStream::_line_state_callback(itf, dtr, rts);
}

// Invoked when CDC interface received data from host
void tud_cdc_rx_cb(uint8_t itf)
{
  USBCDCStream::_rx_callback(itf);
}

USBCDCStream::USBCDCStream(size_t rx_buffer_size)
    : _connected(false),
      _rx_buffer_size(rx_buffer_size),
      _rx_head(0), _rx_tail(0)
{
  // 分配环形缓冲区
  _rx_buffer = new uint8_t[rx_buffer_size];
  // 设置单例实例
  _instance = this;
}

USBCDCStream::~USBCDCStream()
{
  delete[] _rx_buffer;
  if (_instance == this)
  {
    _instance = nullptr;
  }
}

int USBCDCStream::available()
{
  size_t head = _rx_head;
  size_t tail = _rx_tail;

  if (head >= tail)
  {
    return head - tail;
  }
  else
  {
    return _rx_buffer_size - tail + head;
  }
}

int USBCDCStream::read()
{
  if (_rx_head == _rx_tail)
  {
    return -1; // 缓冲区为空
  }

  uint8_t data = _rx_buffer[_rx_tail];
  _rx_tail = (_rx_tail + 1) % _rx_buffer_size;
  return data;
}

int USBCDCStream::peek()
{
  if (_rx_head == _rx_tail)
  {
    return -1; // 缓冲区为空
  }
  return _rx_buffer[_rx_tail];
}

void USBCDCStream::flush()
{
  tud_cdc_write_flush();
}

size_t USBCDCStream::write(uint8_t ch)
{
  if (!_connected)
  {
    return 0;
  }

  uint32_t count = 0;
  while (count == 0)
  {
    count = tud_cdc_write(&ch, 1);
    if (count == 0)
    {
      delay(1); // 等待缓冲区空间
    }
  }
  tud_cdc_write_flush();
  return count;
}

size_t USBCDCStream::write(const uint8_t *buffer, size_t size)
{
  if (!_connected || size == 0)
  {
    return 0;
  }

  size_t total_written = 0;
  while (total_written < size)
  {
    uint32_t written = tud_cdc_write(buffer + total_written, size - total_written);
    if (written > 0)
    {
      total_written += written;
      tud_cdc_write_flush();
    }
    else
    {
      delay(1); // 等待缓冲区空间
    }
  }
  return total_written;
}

void USBCDCStream::clear()
{
  _rx_head = _rx_tail = 0;
}

int USBCDCStream::availableForWrite()
{
  return tud_cdc_write_available();
}

// 静态回调函数
void USBCDCStream::_rx_callback(uint8_t itf)
{
  if (_instance)
  {
    uint8_t buf[64];
    uint32_t count;

    if (tud_cdc_connected() && tud_cdc_available())
    {
      count = tud_cdc_read(buf, sizeof(buf));

      // 将数据存入环形缓冲区
      for (uint32_t i = 0; i < count; i++)
      {
        size_t next_head = (_instance->_rx_head + 1) % _instance->_rx_buffer_size;
        if (next_head != _instance->_rx_tail)
        { // 缓冲区未满
          _instance->_rx_buffer[_instance->_rx_head] = buf[i];
          _instance->_rx_head = next_head;
        }
        else
        {
          // 缓冲区满，丢弃数据
          break;
        }
      }
    }
  }
}

void USBCDCStream::_line_state_callback(uint8_t itf, bool dtr, bool rts)
{
  if (_instance)
  {
    if (rts)
    {
      if (!dtr)
      {
        // 重启进入下载模式
        REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
      }
      REG_WRITE(RTC_CNTL_OPTIONS0_REG, RTC_CNTL_SW_SYS_RST);
    }

    if (dtr)
    {
      // 终端连接
      _instance->_connected = true;
    }
    else
    {
      // 终端断开连接
      _instance->_connected = false;
    }
  }
}