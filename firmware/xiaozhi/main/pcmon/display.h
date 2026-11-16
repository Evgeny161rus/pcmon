#pragma once

#include <lvgl.h>

bool displayInit();
void displayBacklight(int pct);
bool displayHasDimming();
void displaySetFlip(bool flip);   // поворот экрана и тача на 180°
// Калибровка сенсора: сырые координаты последнего касания (false — не нажато),
// и новая формула x = xa·rawY + xb, y = ya·rawX + yb (в координатах без поворота).
bool displayTouchRaw(int* rx, int* ry);
void displaySetCal(bool on, float xa, float xb, float ya, float yb);
void displayTouchHold(bool hold);   // во время калибровки касания не идут в интерфейс
