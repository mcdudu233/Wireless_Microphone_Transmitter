#include "module/led.h"

#include "LiteLED.h"

LiteLED strip(LED_STRIP_WS2812, false);

void led::setup()
{
  strip.begin(LED_IO, LED_NUM);
  brightness(100);
  black();
}

void led::black()
{
  strip.setPixel(0, 0x000000);
  strip.show();
}

void led::red()
{
  strip.setPixel(0, 0xff0000);
  strip.show();
}

void led::green()
{
  strip.setPixel(0, 0x00ff00);
  strip.show();
}

void led::blue()
{
  strip.setPixel(0, 0x0000ff);
  strip.show();
}

void led::rgb(uint8_t r, uint8_t g, uint8_t b)
{
  strip.setPixel(0, rgb_from_values(r, g, b));
  strip.show();
}

void led::rgb(uint32_t rgb)
{
  strip.setPixel(0, rgb);
  strip.show();
}

void led::brightness(float v)
{
  strip.brightness((uint8_t)(v * 255.0 / 100.0));
  strip.show();
}