// Тревоги: перегрев процессора/видеокарты и переполнение памяти.
// Красная мигающая рамка с подписью, красный светодиод, звуковой сигнал.
#pragma once

#include <Arduino.h>

#include "monitor.h"

// Вызывать из основного цикла (тот же поток, что и LVGL).
void alertsTick(const PcStats& pc);

bool alertsActive();
String alertsText();      // "GPU 86°C" или пустая строка

// Касание экрана: убрать рамку и звук на 5 минут.
void alertsSnooze();
