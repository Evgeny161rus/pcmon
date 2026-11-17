#pragma once

#include <stddef.h>
#include <stdint.h>

void pcmonStart();
void pcmonInitComplete();
void pcmonRequestAssistantView(bool visible);
void pcmonAudioFeed(const int16_t* samples, size_t count);
void pcmonMicSetActive(bool active);

