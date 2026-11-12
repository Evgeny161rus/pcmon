// Журнал показателей на microSD (1-bit SDMMC).
#include "sdlog.h"

#include <SD_MMC.h>
#include <time.h>

#include "board.h"

static bool active = false;
static uint32_t nextWrite = 0;

bool sdLogBegin() {
  if (active) return true;
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
  if (!SD_MMC.begin("/sdcard", true, false, 20000)) {
    Serial.println("[sd] карта не найдена");
    return false;
  }
  File f = SD_MMC.open("/sysmon.csv", FILE_APPEND);
  if (!f) {
    Serial.println("[sd] не удалось открыть /sysmon.csv");
    SD_MMC.end();
    return false;
  }
  if (f.size() == 0) {
    f.println("time,cpu,ram,gpu,disk,net_down_mbps,net_up_mbps,wifi_rssi,pc_wifi_rssi,volume");
  }
  f.close();
  active = true;
  nextWrite = 0;
  Serial.println("[sd] журнал включён: /sysmon.csv");
  return true;
}

void sdLogEnd() {
  if (!active) return;
  SD_MMC.end();
  active = false;
}

bool sdLogActive() { return active; }

void sdLogAppend(const PcStats& pc, const WifiInfo& wifi) {
  if (!active) return;
  uint32_t now = millis();
  if (now < nextWrite) return;
  nextWrite = now + 10000;

  File f = SD_MMC.open("/sysmon.csv", FILE_APPEND);
  if (!f) return;
  time_t t = time(nullptr);
  struct tm tm;
  localtime_r(&t, &tm);
  char ts[24];
  strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
  f.printf("%s,%.1f,%.1f,%.1f,%.1f,%.2f,%.2f,%d,%d,%d\n", ts, pc.cpu, pc.ramPct, pc.gpuPct,
           pc.diskPct, pc.netDownMbps, pc.netUpMbps, wifi.rssi, pc.pcRssi, pc.volume);
  f.close();
}
