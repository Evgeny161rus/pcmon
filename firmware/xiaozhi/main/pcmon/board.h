// Freenove / LCDWIKI ES3C28P — ESP32-S3 N16R8, 2.8" 320x240.
//
// Только аппаратные характеристики платы: дисплей, тач, звук, SD, АКБ, LED.
#pragma once

#define BOARD_FREENOVE_28

#ifdef BOARD_FREENOVE_28
#define LCD_W 320
#define LCD_H 240

// ---- ILI9341, SPI2 ----
#define LCD_SPI_SCK   12
#define LCD_SPI_MOSI  11
#define LCD_SPI_MISO  13
#define LCD_PIN_CS    10
#define LCD_PIN_DC    46
#define LCD_PIN_RST   -1
#define LCD_PIN_BL    45        // ШИМ подсветки
#define LCD_ROTATION  1         // альбомная ориентация
#define LCD_SPI_HZ    (40 * 1000 * 1000)

// ES3C28P — IPS-панель: без INVON цвета инвертированы.
#define LCD_INVERT    1

// ---- тач FT6336G на общей шине I2C ----
#define TOUCH_SDA     16
#define TOUCH_SCL     15
#define TOUCH_INT     17
#define TOUCH_RST     18
#define TOUCH_ADDR    0x38

// Калибровка, измеренная на этой плате:
//   x = A*rawY + B,  y = C*rawX + D
#define TOUCH_CAL_X_A (-0.961f)
#define TOUCH_CAL_X_B (314.3f)
#define TOUCH_CAL_Y_A (1.27f)
#define TOUCH_CAL_Y_B (-22.3f)

// ---- ES8311 + FM8002E ----
#define AUDIO_MCLK    4
#define AUDIO_BCLK    5
#define AUDIO_LRCK    7
#define AUDIO_DOUT    8
#define AUDIO_DIN     6
#define AUDIO_PA      1         // усилитель, активный LOW
#define AUDIO_CODEC   0x18

// ---- microSD, 1-bit SDMMC (на плате разведена только D0) ----
#define SD_CLK        38
#define SD_CMD        40
#define SD_D0         39

// ---- АКБ через делитель ----
#define BAT_PIN       9
#define BAT_DIVIDER   3.0f

// ---- WS2812 ----
#define LED_PIN       42

#endif
