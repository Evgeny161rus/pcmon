// Пул памяти LVGL в PSRAM (см. LV_MEM_POOL_ALLOC в lv_conf.h).
// Внутренняя RAM нужна Wi-Fi, веб-серверу и AES/TLS.
#pragma once
#include <stddef.h>
#include <stdlib.h>
#include <esp_heap_caps.h>

static inline void* lvgl_pool_alloc(size_t n) {
  void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p ? p : malloc(n);
}
