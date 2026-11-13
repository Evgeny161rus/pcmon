#pragma once

#include <Arduino.h>

struct Settings {
  String wifiSsid;
  String wifiPass;
  String agentHost;       // пусто = автоматический поиск по UDP
  uint16_t agentPort = 8123;
  uint8_t theme = 0;
  uint8_t brightness = 85;
  uint16_t dimSec = 120;  // 0 = не гасить
  bool sound = true;
  bool sdLog = false;
  uint8_t skin = 0;       // скин главного экрана
  bool flip = false;      // экран перевёрнут на 180°
  bool alertOn = true;    // тревоги по перегреву и памяти
  // калибровка сенсора: x = calXA·rawY + calXB, y = calYA·rawX + calYB (без поворота); calOk=false — заводская
  bool calOk = false;
  float calXA = 0, calXB = 0, calYA = 0, calYB = 0;
  uint8_t alertCpu = 85;  // °C
  uint8_t alertGpu = 83;  // °C
  uint8_t alertRam = 95;  // %
  String cityName;
  String cityGm;
  String cityMail;
};

extern Settings gSet;

void settingsBegin();
bool settingsSave();

void settingsLock();
void settingsUnlock();

String settingsAgentHost();
uint16_t settingsAgentPort();
