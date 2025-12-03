#pragma once

#include "cinttypes"

#define SYSTEM_PRINT_INFORMATION

struct SystemInfo
{
  float cpu0Usage;
  float cpu1Usage;
  uint32_t iramUsedSize;
  uint32_t iramTotalSize;
  uint32_t psramUsedSize;
  uint32_t psramTotalSize;
};

extern SystemInfo systemInfo;

namespace sys
{
  void setup();
}