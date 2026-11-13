// ES8311 + FM8002E: короткие звуки интерфейса.
#include "audio.h"

#include <Wire.h>
#include <driver/i2s_std.h>

#include <cmath>

#include "board.h"

namespace {
constexpr uint32_t kRate = 44100;
i2s_chan_handle_t sTx = nullptr;
i2s_chan_handle_t sRx = nullptr;   // микрофон (АЦП ES8311, левый канал)
bool sRxOn = false;
bool sReady = false;

bool esWrite(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(AUDIO_CODEC);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

bool esRead(uint8_t reg, uint8_t& v) {
  Wire.beginTransmission(AUDIO_CODEC);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)AUDIO_CODEC, (uint8_t)1) != 1) return false;
  v = (uint8_t)Wire.read();
  return true;
}

bool codecInit() {
  esWrite(0x44, 0x08);
  esWrite(0x44, 0x08);
  if (!esWrite(0x0D, 0xFA)) return false;
  esWrite(0x01, 0x30);
  esWrite(0x02, 0x00);
  esWrite(0x03, 0x10);
  esWrite(0x16, 0x24);
  esWrite(0x04, 0x10);
  esWrite(0x05, 0x00);
  esWrite(0x0B, 0x00);
  esWrite(0x0C, 0x00);
  esWrite(0x10, 0x1F);
  esWrite(0x11, 0x7F);
  esWrite(0x00, 0x80);

  uint8_t reg = 0;
  if (!esRead(0x00, reg)) return false;
  esWrite(0x00, (uint8_t)(reg & ~0x40));
  esWrite(0x01, 0x3F);
  if (esRead(0x06, reg)) esWrite(0x06, (uint8_t)(reg & ~0x20));
  esWrite(0x13, 0x10);
  esWrite(0x1B, 0x0A);
  esWrite(0x1C, 0x6A);
  esWrite(0x44, 0x58);

  reg = 0;
  if (esRead(0x09, reg)) esWrite(0x09, (uint8_t)((reg & 0xFC) | 0x0C));
  if (esRead(0x0A, reg)) esWrite(0x0A, (uint8_t)((reg & 0xFC) | 0x0C));

  if (esRead(0x02, reg)) esWrite(0x02, (uint8_t)((reg & 0x07)));
  esWrite(0x05, 0x00);
  if (esRead(0x03, reg)) esWrite(0x03, (uint8_t)((reg & 0x80) | 0x10));
  if (esRead(0x04, reg)) esWrite(0x04, (uint8_t)((reg & 0x80) | 0x10));
  if (esRead(0x07, reg)) esWrite(0x07, (uint8_t)((reg & 0xC0)));
  esWrite(0x08, 0xFF);
  if (esRead(0x06, reg)) esWrite(0x06, (uint8_t)((reg & 0xE0) | 0x03));

  esWrite(0x00, 0x80);
  esWrite(0x01, 0x3F);
  esWrite(0x17, 0xBF);
  esWrite(0x0E, 0x02);
  esWrite(0x12, 0x00);
  esWrite(0x14, 0x1A);
  esWrite(0x0D, 0x01);
  esWrite(0x15, 0x40);
  esWrite(0x37, 0x08);
  esWrite(0x45, 0x00);
  esWrite(0x31, 0x00);
  esWrite(0x32, 0xB2);
  return true;
}
}  // namespace

bool audioBegin() {
  if (sReady) return true;

  Wire.beginTransmission(AUDIO_CODEC);
  if (Wire.endTransmission() != 0) {
    Serial.println("[audio] ES8311 не найден");
    return false;
  }

  pinMode(AUDIO_PA, OUTPUT);
  digitalWrite(AUDIO_PA, HIGH);

  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan.auto_clear = true;
  if (i2s_new_channel(&chan, &sTx, &sRx) != ESP_OK) return false;

  i2s_std_config_t cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kRate),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {(gpio_num_t)AUDIO_MCLK, (gpio_num_t)AUDIO_BCLK, (gpio_num_t)AUDIO_LRCK,
                   (gpio_num_t)AUDIO_DOUT, (gpio_num_t)AUDIO_DIN,
                   {.mclk_inv = false, .bclk_inv = false, .ws_inv = false}},
  };
  cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

  if (i2s_channel_init_std_mode(sTx, &cfg) != ESP_OK || i2s_channel_enable(sTx) != ESP_OK) {
    Serial.println("[audio] I2S не запустился");
    return false;
  }
  if (sRx && i2s_channel_init_std_mode(sRx, &cfg) != ESP_OK) {
    Serial.println("[audio] I2S RX не запустился");
    i2s_del_channel(sRx);
    sRx = nullptr;
  }

  sReady = codecInit();
  // FM8002E на этой плате включается низким уровнем.
  digitalWrite(AUDIO_PA, sReady ? LOW : HIGH);
  Serial.printf("[audio] ES8311 %s\n", sReady ? "ok" : "не настроен");
  return sReady;
}

void audioSetVolume(uint8_t percent) {
  if (percent > 100) percent = 100;
  uint8_t reg = (uint8_t)(percent * 255 / 100);
  if (sReady) esWrite(0x32, reg ? reg : 1);
}

void audioBeep(uint16_t hz, uint16_t ms) {
  if (!sReady || !ms) return;
  static int16_t buf[512 * 2];
  uint32_t total = (uint32_t)kRate * ms / 1000;
  if (total > 4096) total = 4096;
  float step = 2.0f * (float)M_PI * hz / kRate;
  float phase = 0;
  uint32_t done = 0;
  while (done < total) {
    uint32_t n = total - done;
    if (n > 512) n = 512;
    for (uint32_t i = 0; i < n; i++) {
      int16_t v = (int16_t)(sinf(phase) * 5000);
      buf[i * 2] = v;
      buf[i * 2 + 1] = v;
      phase += step;
      if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
    }
    size_t written = 0;
    i2s_channel_write(sTx, buf, n * 2 * sizeof(int16_t), &written, 50);
    done += n;
  }
}

void audioClick() { audioBeep(1400, 14); }

bool audioMicEnable(bool on) {
  if (!sReady || !sRx) return false;
  if (on == sRxOn) return true;
  esp_err_t e = on ? i2s_channel_enable(sRx) : i2s_channel_disable(sRx);
  if (e == ESP_OK) sRxOn = on;
  return e == ESP_OK;
}

size_t audioMicRead(int16_t* mono, size_t frames, uint32_t timeoutMs) {
  if (!sRxOn || !frames) return 0;
  static int16_t raw[1024 * 2];
  if (frames > 1024) frames = 1024;
  size_t got = 0;
  if (i2s_channel_read(sRx, raw, frames * 2 * sizeof(int16_t), &got, timeoutMs) != ESP_OK && !got) return 0;
  size_t n = got / (2 * sizeof(int16_t));
  for (size_t i = 0; i < n; i++) mono[i] = raw[i * 2];   // левый слот — АЦП микрофона
  return n;
}
