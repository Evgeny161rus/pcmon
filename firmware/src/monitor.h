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
void monitorForgetAgent();
bool monitorAgentConfigured();
void monitorSetCity(const char* name, const char* gm, const char* mail);
