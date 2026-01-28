#include "logger.h"
#include "config.h"
#include "sys.h"

#include "module/power.h"

SystemInfo systemInfo;

/* 每秒定时任务 */
static void system_handle(void *arg)
{
  // 记录CPU信息
  TaskStatus_t *tasks = NULL;
  UBaseType_t tasks_size;
  uint32_t tasktime;

  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(TASK_SYSTEM_PERIOD);
  while (true)
  {
    // 精准延时
    xTaskDelayUntil(&xLastWakeTime, xFrequency);

#ifdef SYSTEM_PRINT_INFORMATION
    // 显示电池信息
    LOGGER_INFO("Battery Information:");
    LOGGER_INFO("USB_SUPPLY=%d, VCC=%F, BAT=%F, BAT=%F%, BAT_CHARGING=%d", power::isUSBSupply(), power::getVCCVoltage(), power::getBATVoltage(), power::getBATPercent(), power::isCharging());
#endif

    // 写入系统信息
    // CPU信息
    tasks_size = uxTaskGetNumberOfTasks();
    tasks = (TaskStatus_t *)malloc(sizeof(TaskStatus_t) * tasks_size);
    tasks_size = uxTaskGetSystemState(tasks, tasks_size, &tasktime);
    LOGGER_INFO("task=%d", tasktime);
#ifdef SYSTEM_PRINT_INFORMATION
    LOGGER_INFO("CPU info:\n");
    LOGGER_INFO("  | Task | Percentage | Stack High |\n");
#endif
    for (int i = 0; i < tasks_size; i++)
    {
#ifdef SYSTEM_PRINT_INFORMATION
      LOGGER_INFO("  | %s | %F | %d |\n", tasks[i].pcTaskName, tasks[i].ulRunTimeCounter * 100.0 / tasktime, uxTaskGetStackHighWaterMark(tasks[i].xHandle));
#endif
      // 找到空闲任务
      if (strcmp(tasks[i].pcTaskName, "IDLE0") == 0)
      {
        systemInfo.cpu0Usage = 100.0 - (tasks[i].ulRunTimeCounter * 100.0 / tasktime);
      }
      else if (strcmp(tasks[i].pcTaskName, "IDLE1") == 0)
      {
        systemInfo.cpu1Usage = 100.0 - (tasks[i].ulRunTimeCounter * 100.0 / tasktime);
      }
    }
    free(tasks);
#ifdef SYSTEM_PRINT_INFORMATION
    LOGGER_INFO("  CPU0:%F%, CPU1:%F%\n", systemInfo.cpu0Usage, systemInfo.cpu1Usage);
#endif

    // IRAM内存信息
    multi_heap_info_t heapInfo;
    heap_caps_get_info(&heapInfo, MALLOC_CAP_INTERNAL);
    systemInfo.iramUsedSize = heapInfo.total_allocated_bytes;
    systemInfo.iramTotalSize = systemInfo.iramUsedSize + heapInfo.total_free_bytes;

    // PSRAM内存信息
    heap_caps_get_info(&heapInfo, MALLOC_CAP_SPIRAM);
    systemInfo.psramUsedSize = heapInfo.total_allocated_bytes;
    systemInfo.psramTotalSize = systemInfo.psramUsedSize + heapInfo.total_free_bytes;

#ifdef SYSTEM_PRINT_INFORMATION
    LOGGER_INFO("Memory Info:");
    LOGGER_INFO("  IRAM: %d/%dKB\n", systemInfo.iramUsedSize / 1024, systemInfo.iramTotalSize / 1024);
    LOGGER_INFO("  PSRAM: %d/%dKB\n", systemInfo.psramUsedSize / 1024, systemInfo.psramTotalSize / 1024);
#endif
  }
}

void sys::setup()
{
  xTaskCreatePinnedToCore(system_handle, "system_handle", TASK_SYSTEM_STACK, NULL, TASK_SYSTEM_PRIORITY, NULL, TASK_SYSTEM_CORE);
}