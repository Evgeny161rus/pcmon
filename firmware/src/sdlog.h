#pragma once

#include <Arduino.h>

#include "monitor.h"

bool sdLogBegin();
void sdLogEnd();
bool sdLogActive();
void sdLogAppend(const PcStats& pc, const WifiInfo& wifi);
