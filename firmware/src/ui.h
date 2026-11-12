#pragma once

#include <Arduino.h>
#include <lvgl.h>

void uiInit();
void uiLoop();

void uiTouchActivity();
void uiActivity();
bool uiScreenAsleep();

void uiShowOverlay(const String& title, const String& body, bool closable);
void uiHideOverlay();
bool uiOverlayVisible();

void uiToast(const String& text);

// Управление с ПК (веб-запросы приходят в том же потоке, что и LVGL)
void uiSetSkin(int idx);
void uiForceIdle(bool on);   // показать заставку принудительно (для проверки)
void uiShowMenu(int page);
void uiReloadSettings();
void uiRequest(int skin, int flip, const char* call);   // из любого потока
