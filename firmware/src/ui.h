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
void uiShowForecast(bool on);   // открыть/закрыть экран прогноза
void uiShowMedia(bool on);      // открыть/закрыть медиапанель
void uiShowNight(bool on);      // открыть/закрыть отчёт за 12 часов
void uiShowLaunch(bool on);     // открыть/закрыть быстрый запуск
void uiShowMenu(int page);
void uiReloadSettings();
void uiRequest(int skin, int flip);   // из любого потока
