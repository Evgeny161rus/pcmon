#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>
#include <lvgl.h>

#include "../application.h"
#include "../audio_codecs/audio_codec.h"
#include "../boards/common/board.h"
#include "../boards/common/backlight.h"
#include "../display/display.h"
#include "assets/lang_config.h"
#include "audio.h"
#include "display.h"
#include "led.h"
#include "mic.h"
#include "portal.h"
#include "ui.h"

namespace {
int rawX = 0;
int rawY = 0;
bool rawDown = false;
bool touchHold = false;
bool calOn = false;
float calXA = 0, calXB = 0, calYA = 0, calYB = 0;
StreamBufferHandle_t micStream = nullptr;
volatile bool micActive = false;
}

// The monitor and XiaoZhi share the same LCD, codec, touch controller and LVGL.
bool displayInit() { return true; }
void displayBacklight(int pct) {
  if (auto* backlight = Board::GetInstance().GetBacklight())
    backlight->SetBrightness(static_cast<uint8_t>(constrain(pct, 0, 100)));
}
bool displayHasDimming() { return true; }
void displaySetFlip(bool flip) {
  auto* display = lv_display_get_default();
  if (display) lv_display_set_rotation(display, flip ? LV_DISPLAY_ROTATION_180 : LV_DISPLAY_ROTATION_0);
}
bool displayTouchRaw(int* rx, int* ry) {
  if (rx) *rx = rawX;
  if (ry) *ry = rawY;
  return rawDown;
}
void displaySetCal(bool on, float xa, float xb, float ya, float yb) {
  calOn = on;
  calXA = xa; calXB = xb; calYA = ya; calYB = yb;
}
void displayTouchHold(bool hold) { touchHold = hold; }

bool audioBegin() { return true; }
void audioBeep(uint16_t, uint16_t) {
  Application::GetInstance().Schedule([] {
    Application::GetInstance().PlaySound(Lang::Sounds::P3_EXCLAMATION);
  });
}
void audioClick() {
  Application::GetInstance().Schedule([] {
    Application::GetInstance().PlaySound(Lang::Sounds::P3_POPUP);
  });
}
void audioSetVolume(uint8_t percent) {
  if (auto* codec = Board::GetInstance().GetAudioCodec()) codec->SetOutputVolume(percent);
}
bool audioMicEnable(bool) { return true; }
size_t audioMicRead(int16_t* mono, size_t frames, uint32_t timeoutMs) {
  if (!micActive || !micStream || !mono || frames == 0) return 0;
  const size_t bytes = xStreamBufferReceive(micStream, mono, frames * sizeof(int16_t),
                                           pdMS_TO_TICKS(timeoutMs));
  return bytes / sizeof(int16_t);
}

void pcmonMicSetActive(bool active) {
  if (active && !micStream) micStream = xStreamBufferCreate(48000, sizeof(int16_t));
  micActive = active;
}
void pcmonAudioFeed(const int16_t* samples, size_t count) {
  if (!micActive || !micStream || !samples || !count) return;
  xStreamBufferSend(micStream, samples, count * sizeof(int16_t), 0);
}

void portalBegin() {}
void portalLoop() {}
void portalStartAp() {
  uiShowOverlay("Настройка Wi-Fi", "Wi-Fi управляется XiaoZhi.\nУдерживайте BOOT при запуске,\nчтобы открыть настройку сети.", true);
}
bool portalApActive() { return false; }
bool portalWantsReboot() { return false; }
String portalApName() { return ""; }
String portalApPass() { return ""; }
String portalUrl() { return ""; }

// GPIO 42 is XiaoZhi's own status LED; keep monitor status colors from
// replacing the assistant's state indicator.
void ledBegin() {}
void ledStatus(float, bool, bool, bool) {}

// Filled by the board's FT6336 input callback.
void pcmonSetRawTouch(int x, int y, bool down) {
  rawX = x; rawY = y; rawDown = down;
}
bool pcmonTouchSuppressed() { return touchHold; }
void pcmonCalibrateTouch(int rawx, int rawy, int* x, int* y) {
  if (calOn) {
    *x = static_cast<int>(calXA * rawy + calXB);
    *y = static_cast<int>(calYA * rawx + calYB);
  } else {
    *x = static_cast<int>(-0.961f * rawy + 314.3f);
    *y = static_cast<int>(1.27f * rawx - 22.3f);
  }
}

