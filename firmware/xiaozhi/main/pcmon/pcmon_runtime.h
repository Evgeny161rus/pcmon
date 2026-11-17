#pragma once

#include <stddef.h>
#include <stdint.h>

void pcmonPrepare();
void pcmonStart();
void pcmonSetInitTaskHandle(void* taskHandle);
void pcmonInitComplete();
void pcmonRequestAssistantView(bool visible);
void pcmonAudioFeed(const int16_t* samples, size_t count);
void pcmonMicSetActive(bool active);

