#include "module/usb/usb.h"
#include "module/usb/usb_device_cdc.h"

#include "tusb.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/rtc.h"
#include "esp_rom_sys.h"

static bool cdc_connected = false;

// Invoked when cdc when line state changed e.g connected/disconnected
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
  (void)itf;

  if (rts)
  {
    // 重启进入下载模式
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    REG_WRITE(RTC_CNTL_OPTIONS0_REG, RTC_CNTL_SW_SYS_RST);
  }
  else
  {
    if (dtr)
    {
      // Terminal connected
      cdc_connected = true;
      const char *welcome = "CDC Serial Connected - Echo Enabled\r\n";
      tud_cdc_write_str(welcome);
      tud_cdc_write_flush();
    }
    else
    {
      // Terminal disconnected
      cdc_connected = false;
    }
  }
}

// Invoked when CDC interface received data from host
void tud_cdc_rx_cb(uint8_t itf)
{
  uint8_t buf[64];
  uint32_t count;

  // connected() check for DTR bit
  // Most but not all terminal client set this when making connection
  if (tud_cdc_connected())
  {
    if (tud_cdc_available()) // data is available
    {
      count = tud_cdc_n_read(itf, buf, sizeof(buf));
      (void)count;

      tud_cdc_n_write(itf, buf, count);
      tud_cdc_n_write_flush(itf);
      // dummy code to check that cdc serial is responding
    }
  }
}
