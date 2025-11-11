#include "module/led.h"

#include "NeoPixelBus.h"

static NeoPixelBus<NeoGrbFeature, NeoEsp32LcdX8Ws2812xMethod> led(LED_NUM, LED_IO);

void led::setup()
{
  led.Begin();
}