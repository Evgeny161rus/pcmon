// ILI9341 320x240 + FT6336G + LVGL 9.
#include "display.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include <LovyanGFX.hpp>

#include "board.h"
#include "esp_heap_caps.h"
#if __has_include("esp_memory_utils.h")
#include "esp_memory_utils.h"
#else
#include "soc/soc_memory_layout.h"
#endif
#include "lvgl.h"
#include "ui.h"

class MonitorDisplay : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;

 public:
  MonitorDisplay() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = LCD_SPI_HZ;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = LCD_SPI_SCK;
      cfg.pin_mosi = LCD_SPI_MOSI;
      cfg.pin_miso = LCD_SPI_MISO;
      cfg.pin_dc = LCD_PIN_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = LCD_PIN_CS;
      cfg.pin_rst = LCD_PIN_RST;
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = LCD_PIN_BL;
      cfg.invert = false;
      cfg.freq = 20000;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};

static MonitorDisplay lcd;
static lv_display_t* disp;
static lv_indev_t* indev;
static int backlightPct = 80;
static bool flipped = false;

// ---------------------------------------------------------------- FT6336G
static bool ftRead(uint8_t reg, uint8_t* d, size_t n) {
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)TOUCH_ADDR, (int)n) != (int)n) return false;
  for (size_t i = 0; i < n; i++) d[i] = Wire.read();
  return true;
}

static void touchInit() {
  if (TOUCH_RST >= 0) {
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW);
    delay(10);
    digitalWrite(TOUCH_RST, HIGH);
    delay(80);
  }
  if (TOUCH_INT >= 0) pinMode(TOUCH_INT, INPUT);
  uint8_t n = 0;
  if (ftRead(0x02, &n, 1)) Serial.printf("[touch] FT6336 0x%02X, точек: %u\n", TOUCH_ADDR, n & 0x0F);
  else Serial.println("[touch] FT6336 не найден");
}

static bool wasPressed = false;
static bool swallow = false;
static bool calOn = false, holdTouch = false;
static float cXA = TOUCH_CAL_X_A, cXB = TOUCH_CAL_X_B, cYA = TOUCH_CAL_Y_A, cYB = TOUCH_CAL_Y_B;
static volatile int rawX = 0, rawY = 0;
static volatile bool rawDown = false;

bool displayTouchRaw(int* rx, int* ry) {
  if (rx) *rx = rawX;
  if (ry) *ry = rawY;
  return rawDown;
}

void displaySetCal(bool on, float xa, float xb, float ya, float yb) {
  calOn = on;
  if (on) {
    cXA = xa; cXB = xb; cYA = ya; cYB = yb;
  } else {
    cXA = TOUCH_CAL_X_A; cXB = TOUCH_CAL_X_B; cYA = TOUCH_CAL_Y_A; cYB = TOUCH_CAL_Y_B;
  }
}

void displayTouchHold(bool hold) { holdTouch = hold; }

static void touchRead(lv_indev_t*, lv_indev_data_t* data) {
  static int16_t lx = 160, ly = 120;
  static bool down = false;
  static uint32_t last = 0;

  if (millis() - last >= 15) {
    last = millis();
    uint8_t b[5];
    if (ftRead(0x02, b, 5)) {
      uint8_t n = b[0] & 0x0F;
      if (n > 0) {
        int rx = ((b[1] & 0x0F) << 8) | b[2];
        int ry = ((b[3] & 0x0F) << 8) | b[4];
        rawX = rx;
        rawY = ry;
        float fx = cXA * ry + cXB;
        float fy = cYA * rx + cYB;
        if (!calOn) {   // заводская формула; своя калибровка уже учитывает зеркалирование
#ifdef TOUCH_MIRROR_X
          fx = LCD_W - 1 - fx;
#endif
#ifdef TOUCH_MIRROR_Y
          fy = LCD_H - 1 - fy;
#endif
        }
        lx = (int16_t)constrain((int)lroundf(fx), 0, LCD_W - 1);
        ly = (int16_t)constrain((int)lroundf(fy), 0, LCD_H - 1);
        if (flipped) {
          lx = LCD_W - 1 - lx;
          ly = LCD_H - 1 - ly;
        }
      }
      down = n > 0;
    } else {
      down = false;
    }
    rawDown = down;
  }
  if (holdTouch) {
    data->point.x = lx;
    data->point.y = ly;
    data->state = LV_INDEV_STATE_RELEASED;
    wasPressed = down;
    return;
  }

  if (down && !wasPressed) {
    // Первое касание по погашенному экрану только будит его.
    swallow = uiScreenAsleep();
    uiTouchActivity();
  }
  if (down) uiTouchActivity();
  wasPressed = down;

  data->point.x = lx;
  data->point.y = ly;
  data->state = (down && !swallow) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  if (!down) swallow = false;
}

// ---------------------------------------------------------------- подсветка
void displayBacklight(int pct) {
  backlightPct = constrain(pct, 0, 100);
  lcd.setBrightness(backlightPct * 255 / 100);
}
bool displayHasDimming() { return true; }

void displaySetFlip(bool flip) {
  flipped = flip;
  lcd.setRotation(flip ? (LCD_ROTATION + 2) % 4 : LCD_ROTATION);
  if (disp) lv_obj_invalidate(lv_screen_active());
}

// ---------------------------------------------------------------- LVGL
static void flushCb(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  int w = lv_area_get_width(a);
  int h = lv_area_get_height(a);
  lcd.pushImage(a->x1, a->y1, w, h, (const uint16_t*)px);
  lcd.waitDisplay();
  lv_display_flush_ready(d);
}

static uint32_t tickCb() { return millis(); }

bool displayInit() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
  Wire.setTimeOut(3);
  touchInit();

  if (!lcd.init()) {
    Serial.println("[lcd] ILI9341 не инициализировался");
    return false;
  }
  lcd.setRotation(LCD_ROTATION);
  lcd.setColorDepth(16);
  lcd.setSwapBytes(true);
#if LCD_INVERT
  lcd.invertDisplay(true);
#endif
  lcd.fillScreen(0);
  lcd.setBrightness(0);

  lv_init();
  lv_tick_set_cb(tickCb);

  disp = lv_display_create(LCD_W, LCD_H);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

  const size_t bufPx = LCD_W * 40;
  const size_t bufBytes = bufPx * 2;
  uint8_t* b1 = (uint8_t*)heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  uint8_t* b2 = (uint8_t*)heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!b1) b1 = (uint8_t*)heap_caps_malloc(bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (!b2) b2 = (uint8_t*)heap_caps_malloc(bufBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (!b1) {
    Serial.println("[lcd] нет памяти под буфер LVGL");
    return false;
  }
  lv_display_set_buffers(disp, b1, b2, bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flushCb);

  indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchRead);

  Serial.printf("[lcd] %dx%d, буферы в %s, free int %u KB\n", LCD_W, LCD_H,
                esp_ptr_external_ram(b1) ? "PSRAM" : "RAM",
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
  return true;
}
