#include <Arduino.h>




void setup()
{
  
}

void loop()
{
  srand(time(0));
  for (int i = 0; i < 256; i++){
    int R = rand() % 10;
    delay(1);
    int G = rand() % 10;
    delay(1);
    int B = rand() % 10;
    delay(1);
    led.SetPixelColor(0, RgbColor(R, G, B));
    led.Show();
    delay(100);
  }
}
