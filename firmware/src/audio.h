#pragma once
#include <stddef.h>
#include <stdint.h>

#include <Arduino.h>

bool audioBegin();
void audioBeep(uint16_t hz, uint16_t ms);
void audioClick();
void audioSetVolume(uint8_t percent);
// Микрофон: включить приём и читать моно-отсчёты 16 бит, 44,1 кГц.
bool audioMicEnable(bool on);
size_t audioMicRead(int16_t* mono, size_t frames, uint32_t timeoutMs);
