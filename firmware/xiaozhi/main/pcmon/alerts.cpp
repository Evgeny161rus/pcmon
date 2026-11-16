#include "alerts.h"

#include <esp32-hal-rgb-led.h>
#include <lvgl.h>

#include "audio.h"
#include "board.h"
#include "fonts_px.h"
#include "settings.h"

static bool active = false;
static char text[24] = "";
static int hotSec = 0, coolSec = 0;
static uint32_t nextEval = 0, nextBlink = 0, nextBeep = 0, snoozeUntil = 0;
static bool blinkOn = false;
static lv_obj_t* frame = nullptr;
static lv_obj_t* pill = nullptr;
static lv_obj_t* pillLbl = nullptr;

bool alertsActive() { return active; }
String alertsText() { return active ? String(text) : String(); }

static bool snoozed() { return snoozeUntil && (int32_t)(snoozeUntil - millis()) > 0; }

static void hideUi() {
  if (frame) lv_obj_delete_async(frame);
  if (pill) lv_obj_delete_async(pill);
  frame = pill = pillLbl = nullptr;
}

static void showUi() {
  if (!frame) {
    frame = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, LCD_W, LCD_H);
    lv_obj_set_pos(frame, 0, 0);
    lv_obj_set_style_border_width(frame, 5, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(0xFF2D2D), 0);
    lv_obj_remove_flag(frame, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

    pill = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, 200, 26);
    lv_obj_align(pill, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_set_style_bg_color(pill, lv_color_hex(0xC81E1E), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pill, 13, 0);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_CLICKABLE);
    pillLbl = lv_label_create(pill);
    lv_obj_set_style_text_font(pillLbl, &font_pb14, 0);
    lv_obj_set_style_text_color(pillLbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(pillLbl);
  }
  char b[48];
  snprintf(b, sizeof(b), "ТРЕВОГА: %s", text);
  lv_label_set_text(pillLbl, b);
}

void alertsSnooze() {
  if (!active || snoozed()) return;
  snoozeUntil = millis() + 5UL * 60UL * 1000UL;
  if (snoozeUntil == 0) snoozeUntil = 1;
  hideUi();
}

void alertsTick(const PcStats& pc) {
  uint32_t now = millis();

  if ((int32_t)(now - nextEval) >= 0) {
    nextEval = now + 1000;
    settingsLock();
    bool on = gSet.alertOn;
    int tCpu = gSet.alertCpu, tGpu = gSet.alertGpu, tRam = gSet.alertRam;
    settingsUnlock();

    char t[24] = "";
    bool hot = false, warm = false;   // warm — ещё не остыло ниже порога с запасом
    if (on && pc.online) {
      if (pc.cpuTemp > 0 && pc.cpuTemp >= tCpu) {
        hot = true;
        snprintf(t, sizeof(t), "CPU %.0f°C", pc.cpuTemp);
      } else if (pc.hasGpu && pc.gpuTemp > 0 && pc.gpuTemp >= tGpu) {
        hot = true;
        snprintf(t, sizeof(t), "GPU %.0f°C", pc.gpuTemp);
      } else if (pc.ramPct >= tRam) {
        hot = true;
        snprintf(t, sizeof(t), "ОЗУ %.0f%%", pc.ramPct);
      }
      warm = hot || (pc.cpuTemp > 0 && pc.cpuTemp >= tCpu - 3) ||
             (pc.hasGpu && pc.gpuTemp > 0 && pc.gpuTemp >= tGpu - 3) || pc.ramPct >= tRam - 2;
    }
    if (hot) {
      strlcpy(text, t, sizeof(text));
      coolSec = 0;
      if (++hotSec >= 3 && !active) {
        active = true;
        nextBeep = now;
        Serial.printf("[alert] %s\n", text);
      }
    } else {
      hotSec = 0;
      if (active && (!warm) && ++coolSec >= 3) {
        active = false;
        snoozeUntil = 0;
        hideUi();
        rgbLedWrite(LED_PIN, 0, 0, 0);
        Serial.println("[alert] норма");
      }
      if (warm) coolSec = 0;
    }
    if (active && !snoozed()) showUi();
  }

  if (!active) return;

  if ((int32_t)(now - nextBlink) >= 0) {
    nextBlink = now + 450;
    blinkOn = !blinkOn;
    rgbLedWrite(LED_PIN, blinkOn ? 90 : 0, 0, 0);
    if (frame) {
      if (blinkOn) lv_obj_remove_flag(frame, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(frame, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (!snoozed() && (int32_t)(now - nextBeep) >= 0) {
    nextBeep = now + 8000;
    settingsLock();
    bool snd = gSet.sound;
    settingsUnlock();
    if (snd) {
      audioBeep(1900, 110);
      audioBeep(1500, 110);
    }
  }
}
