#include "pcmon_runtime.h"

#include <Arduino.h>
#include <atomic>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>
#include <lvgl.h>

#include "alerts.h"
#include "led.h"
#include "monitor.h"
#include "sdlog.h"
#include "settings.h"
#include "ui.h"

namespace {
static const char* TAG = "PCMon";
std::atomic<int> requestedView{-1};
std::atomic<bool> initFinished{false};
std::atomic<TaskHandle_t> initTaskHandle{nullptr};
uint32_t returnToMonitorAt = 0;
void pcmonTask(void*);
void pcmonPersistenceTask(void*);

void pcmonWorkersTask(void*) {
  while (!initFinished.load()) vTaskDelay(pdMS_TO_TICKS(20));
  // Let the setup task release its temporary internal-RAM stack.
  vTaskDelay(pdMS_TO_TICKS(100));
  TaskHandle_t initTask = initTaskHandle.load();
  if (initTask) vTaskDeleteWithCaps(initTask);
  BaseType_t persistResult = xTaskCreatePinnedToCore(
      pcmonPersistenceTask, "pcmon_store", 4096, nullptr, 1, nullptr, 0);
  if (persistResult != pdPASS) {
    ESP_LOGE(TAG, "Could not start PC Monitor storage task; internal heap=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
  }
  monitorStartTasks();
  BaseType_t result = xTaskCreatePinnedToCoreWithCaps(
      pcmonTask, "pcmon_ui", 8192, nullptr, 1, nullptr, 0,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (result != pdPASS) {
    ESP_LOGE(TAG, "Could not start PC Monitor UI task; internal heap=%u, PSRAM=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
  }
  vTaskSuspend(nullptr);
}

void pcmonPersistenceTask(void*) {
  for (;;) {
    settingsFlushPending();
    monitorSaveForecastIfNeeded();
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

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
  ESP_LOGI(TAG, "Starting PC Monitor; internal heap=%u, PSRAM=%u",
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
  initFinished.store(false);
  BaseType_t workerResult = xTaskCreatePinnedToCoreWithCaps(
      pcmonWorkersTask, "pcmon_start", 2048, nullptr, 1, nullptr, 0,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (workerResult != pdPASS) {
    ESP_LOGE(TAG, "Could not start PC Monitor worker coordinator; internal heap=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
  }
  if (lvgl_port_lock(3000)) {
    lv_obj_t* assistantScreen = lv_screen_active();
    uiInit();
    uiSetAssistantScreen(assistantScreen);
    uiSetAssistantVisible(false);
    lvgl_port_unlock();
    ESP_LOGI(TAG, "PC Monitor screen initialized");
  } else {
    ESP_LOGE(TAG, "Could not lock LVGL to initialize PC Monitor");
  }
}

void pcmonPrepare() {
  settingsBegin();
  Serial.setRxBufferSize(8192);
  Serial.begin(115200);
  ledBegin();
  monitorPrepare();
}

void pcmonInitComplete() {
  initFinished.store(true);
}

void pcmonSetInitTaskHandle(void* taskHandle) {
  initTaskHandle.store(static_cast<TaskHandle_t>(taskHandle));
}

void pcmonRequestAssistantView(bool visible) {
  requestedView.store(visible ? 1 : 0);
}

