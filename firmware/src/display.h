#pragma once

#include <lvgl.h>

bool displayInit();
void displayBacklight(int pct);
bool displayHasDimming();
void displaySetFlip(bool flip);   // поворот экрана и тача на 180°
