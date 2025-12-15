#include "logger.h"
#include "config.h"
#include "module/power.h"
#include "module/led.h"

#include "driver/rtc_io.h"
#include "esp_sleep.h"

static void power_handle(void *arg)
{
  // 按钮状态
  bool buttonDown = false;
  unsigned long buttonLastTime;
  uint32_t buttonLastRGB;

  // 电池状态
  bool batteryCorrected = false;

  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(TASK_POWER_PERIOD);
  while (true)
  {
    xTaskDelayUntil(&xLastWakeTime, xFrequency);

    // 长按关机
    if (digitalRead(BUTTON_IO) == LOW)
    {
      if (buttonDown)
      {
        unsigned long now = millis();
        if (now - buttonLastTime > BUTTON_SHUTDOWN_TIME)
        {
          // 闪烁提示并深睡
          for (uint8_t i = 0; i < 3; i++)
          {
            led::black();
            delay(100);
            led::red();
            delay(100);
          }
          led::black();
          power::deepSleep();
        }
        else
        {
          // 颜色渐亮
          uint8_t tmp = (uint8_t)((now - buttonLastTime) * 1.0 / BUTTON_SHUTDOWN_TIME * 255);
          led::rgb(tmp, 0, 0);
        }
      }
      else
      {
        buttonDown = true;
        buttonLastTime = millis();
        // 显示按钮被按下的颜色
        buttonLastRGB = led::getLastColor();
      }
    }
    else
    {
      if (buttonDown)
      {
        buttonDown = false;
        led::rgb(buttonLastRGB);
      }
    }

    // 自动校准电池电压
    if (!batteryCorrected)
    {
      if (power::isUSBSupply() && !power::isCharging())
      {
        config::config.battery.isCorrected = true;
        config::config.battery.bias = BATTERY_MAX - power::getBATVoltage();
        config::save();
        batteryCorrected = true;
        logger::infoln("Battery voltage is corrected.");
      }
    }
  }
}

static void wakeUp()
{
  // 判断是怎么被唤醒的
  switch (esp_sleep_get_wakeup_cause())
  {
  case ESP_SLEEP_WAKEUP_TIMER:
  {
    logger::debugln("Power wake up from timer.");
    break;
  }

  case ESP_SLEEP_WAKEUP_EXT0:
  {
    if (power::isBATSupply())
    {
      // 检测电量是否充足
      if (power::getBATPercent() <= BATTERY_LOW_PERCENT)
      {
        for (uint8_t i = 0; i < 2; i++)
        {
          led::black();
          delay(100);
          led::red();
          delay(100);
        }
        led::black();
        power::deepSleep();
      }
    }
    logger::debugln("Power wake up from button.");
    break;
  }

  case ESP_SLEEP_WAKEUP_UNDEFINED:
  {
    led::green();
    logger::debugln("Power is normal started.");
    break;
  }

  default:
  {
    led::red();
    delay(3000);
    logger::debugln("Power unknow wake up for %d.", esp_sleep_get_wakeup_cause());
    break;
  }
  }
}

void power::deepSleep()
{
  // 设置按钮触发唤醒
  esp_sleep_enable_ext0_wakeup(BUTTON_IO, LOW);
  // 拉高引脚
  rtc_gpio_pulldown_dis(BUTTON_IO);
  rtc_gpio_pullup_en(BUTTON_IO);

  // 开始深睡
  esp_deep_sleep_start();
}

void power::setup()
{
  // 初始化按钮和充电指示
  pinMode(BUTTON_IO, INPUT_PULLUP);
  pinMode(CHARGING_IO, INPUT);

  // 初始化 ADC
  // 分辨率
  analogReadResolution(16);
  // 衰减 0~3100mV
  analogSetAttenuation(ADC_11db);

  // 检查唤醒状态
  wakeUp();

  xTaskCreatePinnedToCore(power_handle, "power_handle", TASK_POWER_STACK, NULL, TASK_POWER_PRIORITY, NULL, TASK_POWER_CORE);
}

double power::getVCCVoltage()
{
  return analogReadMilliVolts(ADC_VCC_IO) / 1000.0 * 3.0;
}

double power::getBATVoltage()
{
  return analogReadMilliVolts(ADC_BAT_IO) / 1000.0 * 2.0;
}

bool power::isUSBSupply()
{
  return (getVCCVoltage() > ADC_POWER_SUPPLY);
}

bool power::isBATSupply()
{
  return (getVCCVoltage() <= ADC_POWER_SUPPLY);
}

bool power::isCharging()
{
  return !digitalRead(CHARGING_IO);
}

double power::getBATPercent()
{
  double vol = getBATVoltage();
  // 如果进行了电压校准
  if (config::config.battery.isCorrected)
  {
    vol += config::config.battery.bias;
  }

  double percent;
  if (vol > BATTERY_MAX)
  {
    percent = 1.0;
  }
  else if (vol < BATTERY_MIN)
  {
    percent = 0.0;
  }
  else
  {
    percent = (vol - BATTERY_MIN) / (BATTERY_MAX - BATTERY_MIN);
  }
  return percent * 100.0;
}