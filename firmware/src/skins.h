// Скины главного экрана: каждый скин — своя раскладка и свои индикаторы.
#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "monitor.h"

int skinCount();
const char* skinName(int idx);

// Строит скин на экране scr (320x240). Все объекты некликабельны:
// касания ловит прозрачный слой, который создаёт ui.cpp поверх.
void skinBuild(lv_obj_t* scr, int idx);

// Заставка при выключенном ПК: часы, дата, погода. Цвета — от скина baseSkin.
void skinBuildIdle(lv_obj_t* scr, int baseSkin);

// Экран прогноза погоды (открывается касанием погоды). Цвета — от скина baseSkin.
void skinBuildForecast(lv_obj_t* scr, int baseSkin);

// Медиапанель. onKey: 0/1/2 — пред./плей-пауза/след., 3/4/5 — громкость −/выкл./+, 9 — закрыть.
void skinBuildMedia(lv_obj_t* scr, int baseSkin, void (*onKey)(int));

// Отчёт за последние 12 часов (открывается касанием часов).
void skinBuildNight(lv_obj_t* scr, int baseSkin);
bool skinClockHit(int x, int y);

// Касание в точке (x, y) попало в блок погоды текущего скина.
bool skinWeatherHit(int x, int y);

// Обновляет показания текущего скина.
void skinUpdate(const PcStats& pc, const WifiInfo& w);

// Забыть указатели на объекты (перед удалением экрана).
void skinClear();
