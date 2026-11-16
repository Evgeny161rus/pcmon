#pragma once
#include <stddef.h>
#include <stdint.h>

#include <Arduino.h>

bool audioBegin();
void audioBeep(uint16_t hz, uint16_t ms);
void audioClick();
void audioSetVolume(uint8_t percent);
// Audio samples tapped from XiaoZhi's shared 24 kHz microphone stream.
bool audioMicEnable(bool on);
size_t audioMicRead(int16_t* mono, size_t frames, uint32_t timeoutMs);
