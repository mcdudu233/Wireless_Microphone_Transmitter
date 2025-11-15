#include "config.h"
#include "logger.h"
#include "module/usb/usb.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_private/usb_phy.h"
#include "esp_timer.h"
#include "tusb.h"

static void tusb_handle(void *arg)
{
  while (true)
  {
    tud_task();
  }
}

static void tusb_mic_handle(void *pvParam)
{
  TickType_t xLastWakeTime = xTaskGetTickCount();
  while (true)
  {
    // if (s_uac_device->mic_active == false)
    // {
    //     // clear the notification
    //     ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    //     xLastWakeTime = xTaskGetTickCount();
    //     continue;
    // }
    // // clear the notification
    // // read data from the microphone chunk by chunk
    // size_t bytes_require = MIC_INTERVAL_MS * s_uac_device->mic_bytes_per_ms;
    // if (s_uac_device->user_cfg.input_cb)
    // {
    //     size_t bytes_read = 0;
    //     esp_err_t ret = s_uac_device->user_cfg.input_cb((uint8_t *)s_uac_device->mic_buf_write, bytes_require, &bytes_read, s_uac_device->user_cfg.cb_ctx);
    //     if (ret != ESP_OK)
    //     {
    //         ESP_LOGE(TAG, "Failed to read data from mic");
    //         continue;
    //     }
    //     int16_t *tmp_buf = s_uac_device->mic_buf_write;
    //     UAC_ENTER_CRITICAL();
    //     s_uac_device->mic_buf_write = s_uac_device->mic_buf_read;
    //     s_uac_device->mic_buf_read = tmp_buf;
    //     s_uac_device->mic_data_size = bytes_read;
    //     UAC_EXIT_CRITICAL();
    // }

    // vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(MIC_INTERVAL_MS));
  }
}

static usb_phy_handle_t phy_hdl;
static void usb_phy_init(void)
{
  // Configure USB PHY
  usb_phy_config_t phy_conf = {
      .controller = USB_PHY_CTRL_OTG,
      .otg_mode = USB_OTG_MODE_DEVICE,
      .otg_speed = USB_PHY_SPEED_HIGH,
  };
  usb_new_phy(&phy_conf, &phy_hdl);
}

void usb::setup()
{
  usb_phy_init();
  bool usb_init = tusb_init();
  if (!usb_init)
  {
    logger::warnln("USB Device Stack Init Fail");
    return;
  }
  xTaskCreatePinnedToCore(tusb_handle, "tusb_handle", TASK_TUSB_STACK, NULL, TASK_TUSB_PRIORITY, NULL, TASK_TUSB_CORE);
}

/*               USB设备回调              */
// Invoked when device is mounted
void tud_mount_cb(void)
{
  logger::infoln("USB mounted");
}
// Invoked when device is unmounted
void tud_umount_cb(void)
{
  logger::infoln("USB unmounted");
}
// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  logger::infoln("USB suspended");
}
// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
  logger::infoln("USB resumed");
}
/******************************************/