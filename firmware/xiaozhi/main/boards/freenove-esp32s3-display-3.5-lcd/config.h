#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// ============= I2S =============
#define AUDIO_I2S_GPIO_MCLK      GPIO_NUM_17  //MCLK
#define AUDIO_I2S_GPIO_BCLK      GPIO_NUM_18  //SCK
#define AUDIO_I2S_GPIO_DIN       GPIO_NUM_16  //DIN
#define AUDIO_I2S_GPIO_WS        GPIO_NUM_21  //LRC
#define AUDIO_I2S_GPIO_DOUT      GPIO_NUM_15  //DOUT
#define AUDIO_CODEC_PA_PIN       GPIO_NUM_1   //PA

// ============= AUDIO =============
#define AUDIO_CODEC_I2C_NUM      I2C_NUM_0
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_39
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_38
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR

// ============= Basic =============
#define BOOT_BUTTON_GPIO         GPIO_NUM_0
#define BUILTIN_LED_GPIO         GPIO_NUM_40

// ============= Display =============
#define DISPLAY_BACKLIGHT_PIN    GPIO_NUM_41 

#define DISPLAY_RST_PIN       GPIO_NUM_NC
#define DISPLAY_CS_PIN        GPIO_NUM_10
#define DISPLAY_SCK_PIN       GPIO_NUM_12

#define DISPLAY_QSPI_D0_PIN   GPIO_NUM_11
#define DISPLAY_QSPI_D1_PIN   GPIO_NUM_13
#define DISPLAY_QSPI_D2_PIN   GPIO_NUM_14
#define DISPLAY_QSPI_D3_PIN   GPIO_NUM_9
#define DISPLAY_SPI_SCLK_HZ   (40 * 1000 * 1000)

#define LCD_SPI_HOST          SPI2_HOST

#define LCD_TYPE_ST77922

#define DISPLAY_WIDTH         480
#define DISPLAY_HEIGHT        320

#define DISPLAY_SWAP_XY       false
#define DISPLAY_MIRROR_X      false
#define DISPLAY_MIRROR_Y      true
#define DISPLAY_INVERT_COLOR  true
#define DISPLAY_RGB_ORDER     LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X      0
#define DISPLAY_OFFSET_Y      0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

#define DISPLAY_SPI_MODE      0

#endif