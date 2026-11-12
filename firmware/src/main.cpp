/*
  PC Monitor — ESP32-S3 (Freenove ES3C28P, 2.8" 320x240).

  Экран показывает состояние Windows-ПК: CPU, ОЗУ, GPU, диск, сеть,
  скорость интернета, качество Wi-Fi и управляет системной громкостью.
  Данные отдаёт агент sysmon_agent.py по локальной сети (HTTP + UDP-обнаружение).
*/
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "alerts.h"
#include "audio.h"
#include "display.h"
#include "led.h"
#include "monitor.h"
#include "portal.h"
#include "sdlog.h"
#include "settings.h"
#include "ui.h"

static const char* FW_VERSION = "1.3.0";

enum Phase { NO_CREDS, CONNECTING, CONNECTED, FAILED_AP };
static Phase phase = NO_CREDS;
static uint32_t connectStart = 0;
static uint32_t lastSlow = 0;
static uint32_t portalRebootAt = 0;

static String savedSsid() {
  settingsLock();
  String s = gSet.wifiSsid;
  settingsUnlock();
  return s;
}

static void showApOverlay(const String& reason) {
  String body = "Точка доступа: " + portalApName() + "\n";
  body += "Пароль: " + portalApPass() + "\n";
  body += "Адрес: http://" + WiFi.softAPIP().toString() + "/";
  if (reason.length()) body = reason + "\n\n" + body;
  uiShowOverlay("Настройка Wi-Fi", body, true);
}

static void onConnected() {
  configTzTime("MSK-3", "ntp0.NL.net", "ntp1.vniiftri.ru", "pool.ntp.org");
  uiHideOverlay();
  uiToast(String("IP ") + WiFi.localIP().toString());
  Serial.printf("[wifi] подключено, IP %s, RSSI %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

static void wifiStart() {
  String ssid = savedSsid();
  settingsLock();
  String pass = gSet.wifiPass;
  settingsUnlock();

  WiFi.persistent(false);
  WiFi.setSleep(false);

  if (ssid.length() == 0) {
    // Wi-Fi не настроен — это нормально: показатели идут по USB-кабелю.
    // Точка доступа для настройки включается кнопкой «Wi-Fi» на странице «Ещё».
    phase = NO_CREDS;
    return;
  }

  phase = CONNECTING;
  connectStart = millis();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uiShowOverlay("Подключение", "Wi-Fi: " + ssid + "\nИдёт подключение к сети…", false);
}

static void wifiTick() {
  if (phase == CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      phase = CONNECTED;
      onConnected();
    } else if (millis() - connectStart > 20000) {
      phase = FAILED_AP;
      portalStartAp();
      showApOverlay("Не удалось подключиться к «" + savedSsid() + "».");
    }
  } else if (phase == CONNECTED) {
    if (WiFi.status() != WL_CONNECTED) {
      phase = CONNECTING;
      connectStart = millis();
      uiShowOverlay("Связь", "Wi-Fi пропал.\nПереподключение…", false);
    }
  } else {
    if (WiFi.status() == WL_CONNECTED) {
      phase = CONNECTED;
      onConnected();
    } else if (phase == FAILED_AP && millis() - connectStart > 30000) {
      connectStart = millis();
      settingsLock();
      String pass = gSet.wifiPass;
      settingsUnlock();
      WiFi.disconnect();
      WiFi.begin(savedSsid().c_str(), pass.c_str());
    }
  }
  monitorWifiPoll();
}

static void slowTick() {
  PcStats pc = monitorSnapshot();
  WifiInfo w = monitorWifi();
  if (!alertsActive()) ledStatus(pc.cpu, pc.online, w.apMode, pc.speedTesting);

  settingsLock();
  bool log = gSet.sdLog;
  settingsUnlock();
  if (log) {
    if (!sdLogActive()) sdLogBegin();
    sdLogAppend(pc, w);
  } else if (sdLogActive()) {
    sdLogEnd();
  }
}

void setup() {
  // По этому же порту агент присылает показатели: кадр ~1 КБ раз в секунду.
  Serial.setRxBufferSize(8192);
  Serial.setTxTimeoutMs(0);   // не ждать, если на ПК никто не читает порт
  Serial.begin(115200);
  setenv("TZ", "MSK-3", 1);
  tzset();
  delay(200);
  Serial.printf("\n[boot] PC Monitor %s\n", FW_VERSION);

  settingsBegin();
  if (!displayInit()) {
    Serial.println("[boot] дисплей не запустился");
    for (;;) delay(1000);
  }
  uiInit();
  audioBegin();
  ledBegin();
  // Сетевой стек (lwIP) должен быть запущен до WebServer.begin() и udp.begin(),
  // иначе на ядре 3.x прошивка падает с assert "Invalid mbox".
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  portalBegin();
  monitorBegin();
  wifiStart();

  settingsLock();
  int b = gSet.brightness;
  settingsUnlock();
  displayBacklight(b);
}

void loop() {
  portalLoop();
  uiLoop();
  alertsTick(monitorSnapshot());
  lv_timer_handler();
  delay(5);

  uint32_t now = millis();
  if ((int32_t)(now - lastSlow) >= 0) {
    lastSlow = now + 1000;
    wifiTick();
    slowTick();
  }

  if (portalWantsReboot()) {
    if (portalRebootAt == 0) portalRebootAt = millis() + 800;
    if ((int32_t)(millis() - portalRebootAt) >= 0) ESP.restart();
  }
}
