#include "pcmon_runtime.h"

#include <Arduino.h>
#include <atomic>
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>

#include "alerts.h"
#include "led.h"
#include "monitor.h"
#include "sdlog.h"
#include "settings.h"
#include "ui.h"

namespace {
std::atomic<int> requestedView{-1};
uint32_t returnToMonitorAt = 0;

void pcmonTask(void*) {
  uint32_t lastSlow = 0;
  for (;;) {
    if (lvgl_port_lock(100)) {
      uiLoop();
      const int requested = requestedView.exchange(-1);
      if (requested == 1) {
        returnToMonitorAt = 0;
        uiSetAssistantVisible(true);
      } else if (requested == 0) {
        returnToMonitorAt = millis() + 6500;
      }
      if (returnToMonitorAt && static_cast<int32_t>(millis() - returnToMonitorAt) >= 0) {
        returnToMonitorAt = 0;
        uiSetAssistantVisible(false);
      }
      alertsTick(monitorSnapshot());
      lvgl_port_unlock();
    }

    const uint32_t now = millis();
    if (static_cast<int32_t>(now - lastSlow) >= 0) {
      lastSlow = now + 1000;
      monitorWifiPoll();
      const PcStats pc = monitorSnapshot();
      const WifiInfo wifi = monitorWifi();
      if (alertsActive()) ledStatus(pc.cpu, pc.online, wifi.apMode, pc.speedTesting);
      settingsLock();
      const bool logEnabled = gSet.sdLog;
      settingsUnlock();
      if (logEnabled) {
        if (!sdLogActive()) sdLogBegin();
        sdLogAppend(pc, wifi);
      } else if (sdLogActive()) {
        sdLogEnd();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
}  // namespace

void pcmonStart() {
  settingsBegin();
  Serial.setRxBufferSize(8192);
  Serial.begin(115200);
  ledBegin();
  if (lvgl_port_lock(3000)) {
    lv_obj_t* assistantScreen = lv_screen_active();
    uiInit();
    uiSetAssistantScreen(assistantScreen);
    uiSetAssistantVisible(false);
    lvgl_port_unlock();
  }
  monitorBegin();
  xTaskCreatePinnedToCore(pcmonTask, "pcmon_ui", 24576, nullptr, 1, nullptr, 0);
}

void pcmonRequestAssistantView(bool visible) {
  requestedView.store(visible ? 1 : 0);
}

