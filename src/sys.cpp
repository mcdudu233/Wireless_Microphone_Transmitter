#include "logger.h"
#include "config.h"
#include "sys.h"

SystemInfo systemInfo;

/* 每秒定时任务 */
static void system_handle(void *arg)
{
  // 记录CPU信息
  TaskStatus_t *tasks = NULL;
  TaskStatus_t *last_tasks = NULL;
  UBaseType_t tasks_size;
  UBaseType_t last_tasks_size;
  uint32_t tasktime;
  uint32_t last_tasktime;
  last_tasks_size = uxTaskGetNumberOfTasks();
  last_tasks = (TaskStatus_t *)heap_caps_malloc(sizeof(TaskStatus_t) * last_tasks_size, MALLOC_CAP_SPIRAM);
  last_tasks_size = uxTaskGetSystemState(last_tasks, last_tasks_size, &last_tasktime);

  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(TASK_SYSTEM_PERIOD);
  while (true)
  {
    // 精准延时
    xTaskDelayUntil(&xLastWakeTime, xFrequency);

    // 写入系统信息
    // CPU信息
    uint64_t idle0 = 0;
    uint64_t idle1 = 0;
    tasks_size = uxTaskGetNumberOfTasks();
    tasks = (TaskStatus_t *)heap_caps_malloc(sizeof(TaskStatus_t) * tasks_size, MALLOC_CAP_SPIRAM);
    tasks_size = uxTaskGetSystemState(tasks, tasks_size, &tasktime);
#ifdef SYSTEM_PRINT_INFORMATION
    printf("CPU info:\n");
    printf("  | Task | Run Time | Percentage |\n");
#endif
    for (int i = 0; i < tasks_size; i++)
    {
#ifdef SYSTEM_PRINT_INFORMATION
      uint32_t task_elapsed_time = tasks[i].ulRunTimeCounter;
      uint32_t percentage_time = task_elapsed_time * 100UL / 1000;
      printf("  | %s | %d | %d |\n", tasks[i].pcTaskName, task_elapsed_time, percentage_time);
#endif
      // 找到空闲任务
      if (strcmp(tasks[i].pcTaskName, "IDLE0") == 0)
      {
        idle0 += tasks[i].ulRunTimeCounter;
      }
      else if (strcmp(tasks[i].pcTaskName, "IDLE1") == 0)
      {
        idle1 += tasks[i].ulRunTimeCounter;
      }
    }
    for (int i = 0; i < last_tasks_size; i++)
    {
      if (strcmp(last_tasks[i].pcTaskName, "IDLE0") == 0)
      {
        idle0 -= last_tasks[i].ulRunTimeCounter;
      }
      else if (strcmp(last_tasks[i].pcTaskName, "IDLE1") == 0)
      {
        idle1 -= last_tasks[i].ulRunTimeCounter;
      }
    }
    systemInfo.cpu0Usage = 100.0 - (idle0 * 100.0 / (tasktime - last_tasktime));
    systemInfo.cpu1Usage = 100.0 - (idle1 * 100.0 / (tasktime - last_tasktime));
    free(last_tasks);
    last_tasks = tasks;
    last_tasks_size = tasks_size;
    last_tasktime = tasktime;
#ifdef SYSTEM_PRINT_INFORMATION
    printf("  CPU0:%f%, CPU1:%f%\n", systemInfo.cpu0Usage, systemInfo.cpu1Usage);
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
    printf("Memory Info:");
    printf("  IRAM: %d/%dKB\n", systemInfo.iramUsedSize / 1024, systemInfo.iramTotalSize / 1024);
    printf("  PSRAM: %d/%dKB\n", systemInfo.psramUsedSize / 1024, systemInfo.psramTotalSize / 1024);
#endif
  }
}

void sys::setup()
{
  xTaskCreatePinnedToCore(system_handle, "system_handle", TASK_SYSTEM_STACK, NULL, TASK_SYSTEM_PRIORITY, NULL, TASK_SYSTEM_CORE);
}