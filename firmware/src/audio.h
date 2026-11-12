#pragma once

#include <Arduino.h>

bool audioBegin();
void audioBeep(uint16_t hz, uint16_t ms);
void audioClick();
void audioSetVolume(uint8_t percent);
