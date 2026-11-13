#pragma once
#include <stdint.h>

// Анализ звука с микрофона платы: уровень, спектр на 32 полосы, основной тон.
// Работает в своей задаче только пока включён (micSetActive(true)).

static const int MIC_BANDS = 32;
static const int MIC_HIST = 60;

struct MicData {
  bool ok = false;           // микрофон работает и данные свежие
  float db = 0;              // уровень, «дБ» (относительный, приведён к шкале шумомера)
  float peak = 0;            // максимум за минуту
  float avg = 0;             // среднее за минуту
  uint8_t bands[MIC_BANDS];  // 0..100 по каждой полосе (60 Гц…16 кГц, логарифмически)
  float toneHz = 0;          // основной тон, 0 — нет выраженного тона
  uint8_t hist[MIC_HIST];    // максимум уровня по секундам за последнюю минуту
  uint32_t seq = 0;          // растёт с каждым новым кадром
};

void micSetActive(bool on);
bool micGet(MicData& out);
// Имя ноты для частоты: «A4», «C#3»… (пустая строка, если частота вне диапазона).
void micNoteName(float hz, char* out, int n);
