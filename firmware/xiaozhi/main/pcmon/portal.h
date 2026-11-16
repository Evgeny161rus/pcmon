#pragma once

#include <Arduino.h>

void portalBegin();
void portalLoop();
void portalStartAp();
bool portalApActive();
bool portalWantsReboot();
String portalApName();
String portalApPass();
String portalUrl();
