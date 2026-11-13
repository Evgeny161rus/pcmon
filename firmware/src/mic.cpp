// Микрофон: уровень, спектр и тон. БПФ на 1024 отсчёта (44,1 кГц, шаг ~43 Гц).
#include "mic.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cmath>
#include <cstring>

#include "audio.h"

namespace {
constexpr int N = 1024;
constexpr float RATE = 44100.0f;
// Сдвиг от dBFS к «шумомеру»: тихая комната (~-70 dBFS) ≈ 30 дБ. Мерка относительная.
constexpr float DB_OFFSET = 100.0f;

SemaphoreHandle_t mux = nullptr;
TaskHandle_t task = nullptr;
volatile bool active = false;
MicData shared;

float re[N], im[N], win[N];
int16_t pcm[N];
int bandLo[MIC_BANDS], bandHi[MIC_BANDS];

void fft(float* xr, float* xi) {
  for (int i = 1, j = 0; i < N; i++) {
    int bit = N >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      float t = xr[i]; xr[i] = xr[j]; xr[j] = t;
      t = xi[i]; xi[i] = xi[j]; xi[j] = t;
    }
  }
  for (int len = 2; len <= N; len <<= 1) {
    float ang = -2.0f * (float)M_PI / len;
    float wr = cosf(ang), wi = sinf(ang);
    for (int i = 0; i < N; i += len) {
      float cr = 1, ci = 0;
      for (int k = 0; k < len / 2; k++) {
        int a = i + k, b = i + k + len / 2;
        float tr = xr[b] * cr - xi[b] * ci;
        float ti = xr[b] * ci + xi[b] * cr;
        xr[b] = xr[a] - tr; xi[b] = xi[a] - ti;
        xr[a] += tr; xi[a] += ti;
        float nr = cr * wr - ci * wi;
        ci = cr * wi + ci * wr;
        cr = nr;
      }
    }
  }
}

void prepare() {
  for (int i = 0; i < N; i++) win[i] = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (N - 1));
  // полосы 60 Гц…16 кГц, логарифмически; каждая — минимум один бин
  float lo = 60.0f, hi = 16000.0f;
  for (int b = 0; b < MIC_BANDS; b++) {
    float f0 = lo * powf(hi / lo, (float)b / MIC_BANDS);
    float f1 = lo * powf(hi / lo, (float)(b + 1) / MIC_BANDS);
    int k0 = (int)(f0 * N / RATE), k1 = (int)(f1 * N / RATE);
    if (k0 < 1) k0 = 1;
    if (k1 <= k0) k1 = k0 + 1;
    bandLo[b] = k0;
    bandHi[b] = k1;
  }
}

void loop(void*) {
  prepare();
  float secMax = 0, avgAcc = 0;
  int avgN = 0;
  uint32_t secStart = millis();
  uint8_t hist[MIC_HIST] = {0};
  float minuteVals[MIC_HIST] = {0};
  int histPos = 0;
  float bandsS[MIC_BANDS] = {0};
  for (;;) {
    if (!active) {
      audioMicEnable(false);
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }
    if (!audioMicEnable(true)) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    size_t got = 0;
    while (got < (size_t)N && active) {
      size_t n = audioMicRead(pcm + got, N - got, 100);
      if (!n) break;
      got += n;
    }
    if (got < (size_t)N) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    // уровень (без постоянной составляющей)
    float mean = 0;
    for (int i = 0; i < N; i++) mean += pcm[i];
    mean /= N;
    double sum = 0;
    for (int i = 0; i < N; i++) {
      float v = (pcm[i] - mean) / 32768.0f;
      sum += v * v;
      re[i] = v * win[i];
      im[i] = 0;
    }
    float rms = sqrtf((float)(sum / N));
    float db = 20.0f * log10f(rms + 1e-7f) + DB_OFFSET;
    if (db < 20) db = 20;

    fft(re, im);
    // спектр: амплитуда бина в dBFS, полоса — максимум по своим бинам
    static float mag[N / 2];
    for (int k = 1; k < N / 2; k++) mag[k] = sqrtf(re[k] * re[k] + im[k] * im[k]) * (4.0f / N);
    uint8_t bands[MIC_BANDS];
    for (int b = 0; b < MIC_BANDS; b++) {
      float m = 0;
      for (int k = bandLo[b]; k < bandHi[b] && k < N / 2; k++) m = fmaxf(m, mag[k]);
      float d = 20.0f * log10f(m + 1e-7f);      // ~-100…0
      float v = (d + 80.0f) * (100.0f / 65.0f);  // -80 dBFS → 0, -15 → 100
      v = fminf(100.0f, fmaxf(0.0f, v));
      bandsS[b] = v > bandsS[b] ? v : bandsS[b] * 0.6f + v * 0.4f;
      bands[b] = (uint8_t)bandsS[b];
    }
    // основной тон 70…2000 Гц: заметно выше соседей и общего фона
    float tone = 0;
    {
      int k0 = (int)(70 * N / RATE), k1 = (int)(2000 * N / RATE);
      int best = 0;
      float bm = 0, total = 0;
      for (int k = k0; k <= k1; k++) {
        total += mag[k];
        if (mag[k] > bm) { bm = mag[k]; best = k; }
      }
      float meanMag = total / (k1 - k0 + 1);
      if (best > k0 && best < k1 && bm > meanMag * 8.0f && 20.0f * log10f(bm + 1e-7f) > -60.0f) {
        float a = mag[best - 1], b = mag[best], c = mag[best + 1];
        float den = a - 2 * b + c;
        float off = fabsf(den) > 1e-9f ? 0.5f * (a - c) / den : 0;
        tone = (best + off) * RATE / N;
      }
    }
    // минута: максимум каждой секунды и среднее
    secMax = fmaxf(secMax, db);
    avgAcc += db;
    avgN++;
    if (millis() - secStart >= 1000) {
      secStart = millis();
      hist[histPos] = (uint8_t)fminf(255.0f, secMax);
      minuteVals[histPos] = avgN ? avgAcc / avgN : 0;
      histPos = (histPos + 1) % MIC_HIST;
      secMax = 0;
      avgAcc = 0;
      avgN = 0;
    }
    float pk = 0, av = 0;
    int an = 0;
    for (int i = 0; i < MIC_HIST; i++) {
      pk = fmaxf(pk, (float)hist[i]);
      if (minuteVals[i] > 0) { av += minuteVals[i]; an++; }
    }
    xSemaphoreTake(mux, portMAX_DELAY);
    shared.ok = true;
    shared.db = db;
    shared.peak = fmaxf(pk, db);
    shared.avg = an ? av / an : db;
    memcpy(shared.bands, bands, sizeof(bands));
    shared.toneHz = tone;
    for (int i = 0; i < MIC_HIST; i++) shared.hist[i] = hist[(histPos + i) % MIC_HIST];
    shared.seq++;
    xSemaphoreGive(mux);
  }
}
}  // namespace

void micSetActive(bool on) {
  if (!mux) mux = xSemaphoreCreateMutex();
  if (!task) xTaskCreatePinnedToCore(loop, "mic", 6144, nullptr, 2, &task, 0);
  active = on;
  if (!on) {
    xSemaphoreTake(mux, portMAX_DELAY);
    shared.ok = false;
    xSemaphoreGive(mux);
  }
  if (on && task) xTaskNotifyGive(task);
}

bool micGet(MicData& out) {
  if (!mux) return false;
  xSemaphoreTake(mux, portMAX_DELAY);
  out = shared;
  xSemaphoreGive(mux);
  return out.ok;
}

void micNoteName(float hz, char* out, int n) {
  static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  if (hz < 20 || hz > 5000) {
    if (n) out[0] = 0;
    return;
  }
  int midi = (int)lroundf(69 + 12 * log2f(hz / 440.0f));
  snprintf(out, n, "%s%d", names[(midi % 12 + 12) % 12], midi / 12 - 1);
}
