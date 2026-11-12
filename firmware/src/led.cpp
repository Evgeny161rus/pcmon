// WS2812 на GPIO42 — индикатор состояния.
#include "led.h"

#include <esp32-hal-rgb-led.h>

#include "board.h"

void ledBegin() { rgbLedWrite(LED_PIN, 0, 0, 0); }

void ledStatus(float cpu, bool online, bool apMode, bool testing) {
  uint8_t r = 0, g = 0, b = 0;
  if (apMode) {
    r = 0;
    g = 18;
    b = 55;
  } else if (!online) {
    r = 35;
    g = 12;
    b = 0;
  } else if (testing) {
    r = 0;
    g = 45;
    b = 50;
  } else if (cpu >= 85) {
    r = 70;
    g = 4;
    b = 4;
  } else if (cpu >= 60) {
    r = 60;
    g = 35;
    b = 0;
  } else {
    r = 0;
    g = 45;
    b = 12;
  }
  rgbLedWrite(LED_PIN, r, g, b);
}
