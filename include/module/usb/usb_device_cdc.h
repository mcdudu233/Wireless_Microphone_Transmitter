#pragma once

#include "Stream.h"

// 自定义串口
class USBCDCStream : public Stream
{
private:
  static USBCDCStream *_instance;
  bool _connected;

  // 环形缓冲区
  uint8_t *_rx_buffer;
  size_t _rx_buffer_size;
  volatile size_t _rx_head;
  volatile size_t _rx_tail;

public:
  static void _rx_callback(uint8_t itf);
  static void _line_state_callback(uint8_t itf, bool dtr, bool rts);

public:
  USBCDCStream(size_t rx_buffer_size = 256);
  virtual ~USBCDCStream();

  // Stream 方法
  int available() override;
  int read() override;
  int peek() override;

  // Print 方法
  size_t write(uint8_t) override;
  size_t write(const uint8_t *buffer, size_t size) override;
  using Print::write; // 继承其他write方法
  int availableForWrite() override;
  void flush() override;

  // 缓冲区管理
  void clear();

  // 额外功能方法
  bool connected() const { return _connected; }
  operator bool() { return connected(); }
};

extern USBCDCStream USBCDCSerial;