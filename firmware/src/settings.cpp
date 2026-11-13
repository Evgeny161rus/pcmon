#include "settings.h"

#include <Preferences.h>

Settings gSet;
static Preferences prefs;
static SemaphoreHandle_t mux;

void settingsBegin() {
  mux = xSemaphoreCreateRecursiveMutex();
  prefs.begin("pcmon", false);
  gSet.wifiSsid = prefs.getString("ssid", "");
  gSet.wifiPass = prefs.getString("pass", "");
  gSet.agentHost = prefs.getString("agent", "");
  gSet.agentPort = prefs.getUShort("port", 8123);
  gSet.theme = prefs.getUChar("theme", 0);
  gSet.brightness = prefs.getUChar("bright", 85);
  gSet.dimSec = prefs.getUShort("dim", 120);
  gSet.sound = prefs.getBool("sound", true);
  gSet.sdLog = prefs.getBool("sdlog", false);
  gSet.skin = prefs.getUChar("skin", 0);
  gSet.flip = prefs.getBool("flip", false);
  gSet.alertOn = prefs.getBool("al_on", true);
  gSet.alertCpu = prefs.getUChar("al_cpu", 85);
  gSet.alertGpu = prefs.getUChar("al_gpu", 83);
  gSet.alertRam = prefs.getUChar("al_ram", 95);
  gSet.cityName = prefs.getString("c_name", "");
  gSet.cityGm = prefs.getString("c_gm", "");
  gSet.cityMail = prefs.getString("c_mail", "");
  if (gSet.agentPort == 0) gSet.agentPort = 8123;
}

bool settingsSave() {
  settingsLock();
  prefs.putString("ssid", gSet.wifiSsid);
  prefs.putString("pass", gSet.wifiPass);
  prefs.putString("agent", gSet.agentHost);
  prefs.putUShort("port", gSet.agentPort);
  prefs.putUChar("theme", gSet.theme);
  prefs.putUChar("bright", gSet.brightness);
  prefs.putUShort("dim", gSet.dimSec);
  prefs.putBool("sound", gSet.sound);
  prefs.putBool("sdlog", gSet.sdLog);
  prefs.putUChar("skin", gSet.skin);
  prefs.putBool("flip", gSet.flip);
  prefs.putBool("al_on", gSet.alertOn);
  prefs.putUChar("al_cpu", gSet.alertCpu);
  prefs.putUChar("al_gpu", gSet.alertGpu);
  prefs.putUChar("al_ram", gSet.alertRam);
  prefs.putString("c_name", gSet.cityName);
  prefs.putString("c_gm", gSet.cityGm);
  prefs.putString("c_mail", gSet.cityMail);
  settingsUnlock();
  return true;
}

void settingsLock() { xSemaphoreTakeRecursive(mux, portMAX_DELAY); }
void settingsUnlock() { xSemaphoreGiveRecursive(mux); }

String settingsAgentHost() {
  settingsLock();
  String s = gSet.agentHost;
  settingsUnlock();
  return s;
}

uint16_t settingsAgentPort() {
  settingsLock();
  uint16_t p = gSet.agentPort;
  settingsUnlock();
  return p;
}
