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

// Обновляет показания текущего скина.
void skinUpdate(const PcStats& pc, const WifiInfo& w);

// Забыть указатели на объекты (перед удалением экрана).
void skinClear();
