#pragma once

#include <Arduino.h>

void ledBegin();
void ledStatus(float cpu, bool online, bool apMode, bool testing);
