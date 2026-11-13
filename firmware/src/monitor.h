#pragma once

#include <Arduino.h>

#define HIST_N 120

struct PcStats {
  bool online = false;
  bool viaUsb = false;   // данные пришли по USB-кабелю, а не по Wi-Fi
  uint32_t seenMs = 0;
  uint32_t rev = 0;

  char host[33] = "";
  char os[48] = "";

  float cpu = 0;
  float cpuFreqGHz = 0;
  float cpuTemp = -1;
  float ramPct = 0;
  float ramUsedGB = 0;
  float ramTotalGB = 0;

  bool hasGpu = false;
  char gpuName[40] = "";
  float gpuPct = 0;
  float gpuTemp = -1;
  float vramPct = 0;
  float vramUsedGB = 0;
  float vramTotalGB = 0;

  float diskPct = 0;
  float diskUsedGB = 0;
  float diskTotalGB = 0;
  float diskReadMBs = 0;
  float diskWriteMBs = 0;

  float netDownMbps = 0;
  float netUpMbps = 0;
  uint64_t netDownBytes = 0;
  uint64_t netUpBytes = 0;

  bool hasPcWifi = false;
  char pcSsid[33] = "";
  int pcRssi = 0;
  int pcWifiQ = 0;
  int pcLinkMbps = 0;

  int volume = 0;
  bool muted = false;

  float pingMs = 0;     // интернет-пинг с ПК
  float linkMs = 0;     // отклик агента по LAN
  uint32_t uptimeSec = 0;

  bool speedTesting = false;
  float speedProgress = 0;
  float speedDown = 0;
  float speedUp = 0;
  float speedPing = 0;
  float speedJitter = 0;
  uint32_t speedAt = 0;
  char speedStage[32] = "";
  char speedError[48] = "";

  // что сейчас звучит на ПК
  char mediaApp[24] = "";
  char mediaTitle[72] = "";
  bool mediaPlaying = false;
  char top1[28] = "";
  float top1cpu = 0;
  char top2[28] = "";
  float top2cpu = 0;
};

struct WifiInfo {
  bool connected = false;
  bool apMode = false;
  int rssi = -127;
  int quality = 0;
  int channel = 0;
  char ssid[33] = "";
  char ip[16] = "";
  char apSsid[33] = "";
};

// Погода для заставки (open-meteo.com, обновляется раз в 20 минут по Wi-Fi)
struct Weather {
  bool ok = false;
  float temp = 0, feels = 0, wind = 0;
  int code = 0;          // код погоды WMO
  uint32_t atMs = 0;
};
// Последние 12 часов от ПК: 72 отрезка по 10 минут; -1 — ПК был выключен.
struct History {
  bool ok = false;
  uint32_t t0 = 0;
  uint16_t step = 600;
  int16_t cpu[72], gpu[72], ct[72], gt[72], net[72];
  int16_t hot = -1;
  uint32_t hotT = 0;
  uint16_t onMin = 0;
  char top[28] = "";
};
bool monitorHistory(History& out);

// Быстрый запуск: плитки от ПК (4–6), иконки 48×48 в формате LVGL RGB565A8.
static const int APP_MAX = 6;
static const int APP_ICON = 48;
struct AppTile {
  char name[40] = "";             // до 18 букв (UTF-8)
  bool confirm = false;          // действие с подтверждением (выключение, перезагрузка)
  const uint8_t* icon = nullptr;  // nullptr — иконка ещё не пришла
};
int monitorApps(AppTile* out, uint32_t* ver);   // число плиток, 0 — список ещё не получен
void monitorRequestLaunch(int i);

Weather monitorWeather();
// Прогноз от ПК на час now+ahead (0 = текущий час). false — прогноза на этот час нет.
bool monitorForecast(int ahead, Weather& out);

void monitorBegin();
void monitorWifiPoll();
PcStats monitorSnapshot();
WifiInfo monitorWifi();

void monitorRequestVolume(int level);
void monitorRequestMute(bool mute);
void monitorRequestSpeedtest();
// Медиаклавиша на ПК: 0 — предыдущий трек, 1 — плей/пауза, 2 — следующий.
void monitorRequestMedia(int key);
void monitorForgetAgent();
bool monitorAgentConfigured();
void monitorSetCity(const char* name, const char* gm, const char* mail);
