// Связь с агентом на ПК: UDP-обнаружение + HTTP-опрос.
#include "monitor.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_heap_caps.h>
#include <sys/time.h>
#include <time.h>

#include "settings.h"
#include "ui.h"

static const uint16_t UDP_PORT = 4210;

static WiFiUDP udp;
static SemaphoreHandle_t statsMux;
static PcStats st;
static WifiInfo wifi;
static uint32_t nextPoll = 0;
static uint32_t nextQuery = 0;
static uint32_t failCount = 0;
static bool wasOnline = false;

static volatile int reqVolume = -1;
static volatile int reqMute = -1;
static volatile bool reqSpeed = false;

// ---- связь по USB-кабелю (тот же порт, что и отладочный вывод)
static const size_t USB_BUF = 4096;
static char* usbBuf = nullptr;
static size_t usbLen = 0;
static bool usbOverflow = false;
static uint32_t usbLast = 0;      // millis() последнего принятого кадра
static bool usbWas = false;
static uint32_t usbFrames = 0;
static uint32_t nextHello = 0;

static bool usbActive() { return usbLast != 0 && (millis() - usbLast) < 3500; }

// Строки для агента начинаются с "@PCMON " — всё остальное он считает отладкой.
static void usbSend(const String& json) {
  String line = "@PCMON " + json + "\n";
  Serial.write((const uint8_t*)line.c_str(), line.length());
}

static void copyStr(char* dst, size_t n, const char* src) {
  if (!src) src = "";
  strlcpy(dst, src, n);
}

static void setAgent(const String& host, uint16_t port) {
  bool changed = false;
  settingsLock();
  if (gSet.agentHost != host || gSet.agentPort != port) {
    gSet.agentHost = host;
    gSet.agentPort = port;
    changed = true;
  }
  settingsUnlock();
  if (changed) {
    settingsSave();
    Serial.printf("[net] агент: %s:%u\n", host.c_str(), port);
  }
}

// ------------------------------------------------------------------ UDP
static void udpPoll() {
  int len = udp.parsePacket();
  if (len <= 0) return;
  char buf[160];
  int n = udp.read(buf, sizeof(buf) - 1);
  if (n <= 0) return;
  buf[n] = 0;

  // PCMON1|192.168.1.5|8123|DESKTOP
  if (strncmp(buf, "PCMON1|", 7) != 0) return;
  char* p1 = strchr(buf + 7, '|');
  if (!p1) return;
  *p1 = 0;
  char* p2 = strchr(p1 + 1, '|');
  if (p2) *p2 = 0;

  // Адрес берём из отправителя пакета: в тексте маяка может стоять адрес
  // другого адаптера ПК (VPN, виртуальная сеть), недоступный экрану.
  IPAddress ip = udp.remoteIP();
  if (ip == IPAddress(0, 0, 0, 0) && !ip.fromString(buf + 7)) return;
  int port = atoi(p1 + 1);
  if (port < 1 || port > 65535) port = 8123;
  String host = ip.toString();
  uint16_t ph = (uint16_t)port;

  String cur = settingsAgentHost();
  uint16_t cp = settingsAgentPort();
  if (cur != host || cp != ph) setAgent(host, ph);
}

static void sendQuery() {
  udp.beginPacket(IPAddress(255, 255, 255, 255), UDP_PORT);
  udp.write((const uint8_t*)"PCMON?", 6);
  udp.endPacket();
}

// ------------------------------------------------------------------ HTTP
static bool postJson(const String& path, const String& body) {
  String host = settingsAgentHost();
  if (host.isEmpty()) return false;
  String url = "http://" + host + ":" + String(settingsAgentPort()) + path;

  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(900);
  http.setTimeout(1800);
  http.setReuse(false);
  if (!http.begin(client, url)) return false;
  http.addHeader("Content-Type", "application/json");
  int code = http.POST((uint8_t*)body.c_str(), body.length());
  bool ok = code == 200;
  if (ok) {
    JsonDocument doc;
    if (!deserializeJson(doc, http.getStream())) {
      xSemaphoreTake(statsMux, portMAX_DELAY);
      bool changed = false;
      if (doc["level"].is<int>()) {
        st.volume = doc["level"].as<int>();
        changed = true;
      }
      if (doc["muted"].is<bool>()) {
        st.muted = doc["muted"].as<bool>();
        changed = true;
      }
      if (changed) st.rev++;
      xSemaphoreGive(statsMux);
    }
  }
  http.end();
  return ok;
}

static void applyCommands() {
  int vol = reqVolume;
  if (vol >= 0) {
    reqVolume = -1;
    if (usbActive()) usbSend("{\"cmd\":\"volume\",\"level\":" + String(vol) + "}");
    else postJson("/api/volume", "{\"level\":" + String(vol) + "}");
  }
  int mute = reqMute;
  if (mute >= 0) {
    reqMute = -1;
    if (usbActive()) usbSend(String("{\"cmd\":\"mute\",\"mute\":") + (mute ? "true" : "false") + "}");
    else postJson("/api/volume", String("{\"mute\":") + (mute ? "true" : "false") + "}");
  }
  if (reqSpeed) {
    reqSpeed = false;
    if (usbActive()) usbSend("{\"cmd\":\"speedtest\"}");
    else postJson("/api/speedtest", "{}");
  }
}

static void parseStats(JsonDocument& doc, float linkMs, bool viaUsb) {
  PcStats n;
  n.online = true;
  n.viaUsb = viaUsb;
  n.seenMs = millis();
  n.rev = st.rev + 1;
  n.linkMs = linkMs;

  copyStr(n.host, sizeof(n.host), doc["host"] | "PC");
  copyStr(n.os, sizeof(n.os), doc["os"] | "");
  n.uptimeSec = doc["uptime"].as<uint32_t>();

  JsonObject cpu = doc["cpu"];
  n.cpu = cpu["load"] | 0.0f;
  n.cpuFreqGHz = cpu["freq"] | 0.0f;
  n.cpuTemp = cpu["temp"] | -1.0f;

  JsonObject ram = doc["ram"];
  n.ramPct = ram["percent"] | 0.0f;
  n.ramUsedGB = ram["used"] | 0.0f;
  n.ramTotalGB = ram["total"] | 0.0f;

  JsonObject gpu = doc["gpu"];
  n.hasGpu = gpu["present"] | false;
  copyStr(n.gpuName, sizeof(n.gpuName), gpu["name"] | "");
  n.gpuPct = gpu["load"] | 0.0f;
  n.gpuTemp = gpu["temp"] | -1.0f;
  n.vramPct = gpu["vram"] | 0.0f;
  n.vramUsedGB = gpu["vramUsed"] | 0.0f;
  n.vramTotalGB = gpu["vramTotal"] | 0.0f;

  JsonObject disk = doc["disk"];
  n.diskPct = disk["percent"] | 0.0f;
  n.diskUsedGB = disk["used"] | 0.0f;
  n.diskTotalGB = disk["total"] | 0.0f;
  n.diskReadMBs = disk["read"] | 0.0f;
  n.diskWriteMBs = disk["write"] | 0.0f;

  JsonObject net = doc["net"];
  n.netDownMbps = net["down"] | 0.0f;
  n.netUpMbps = net["up"] | 0.0f;
  n.netDownBytes = net["downTotal"].as<uint64_t>();
  n.netUpBytes = net["upTotal"].as<uint64_t>();

  JsonObject pcw = doc["wifi"];
  n.hasPcWifi = pcw["present"] | false;
  copyStr(n.pcSsid, sizeof(n.pcSsid), pcw["ssid"] | "");
  n.pcRssi = pcw["rssi"] | 0;
  n.pcWifiQ = pcw["quality"] | 0;
  n.pcLinkMbps = pcw["link"] | 0;

  n.volume = doc["volumeLevel"] | 0;
  n.muted = doc["muted"] | false;
  n.pingMs = doc["ping"] | 0.0f;

  JsonArray top = doc["top"];
  if (top.size() > 0) {
    copyStr(n.top1, sizeof(n.top1), top[0]["name"] | "");
    n.top1cpu = top[0]["cpu"] | 0.0f;
  }
  if (top.size() > 1) {
    copyStr(n.top2, sizeof(n.top2), top[1]["name"] | "");
    n.top2cpu = top[1]["cpu"] | 0.0f;
  }

  JsonObject sp = doc["speed"];
  n.speedTesting = sp["testing"] | false;
  n.speedProgress = sp["progress"] | 0.0f;
  n.speedDown = sp["down"] | 0.0f;
  n.speedUp = sp["up"] | 0.0f;
  n.speedPing = sp["ping"] | 0.0f;
  n.speedJitter = sp["jitter"] | 0.0f;
  n.speedAt = sp["at"].as<uint32_t>();
  copyStr(n.speedStage, sizeof(n.speedStage), sp["stage"] | "");
  copyStr(n.speedError, sizeof(n.speedError), sp["error"] | "");

  xSemaphoreTake(statsMux, portMAX_DELAY);
  st = n;
  xSemaphoreGive(statsMux);

  if (!wasOnline) {
    wasOnline = true;
    Serial.printf("[net] онлайн: %s (%s)\n", n.host, viaUsb ? "USB" : settingsAgentHost().c_str());
  }
  failCount = 0;
}

static void httpPoll() {
  String host = settingsAgentHost();
  if (host.isEmpty() || WiFi.status() != WL_CONNECTED) {
    xSemaphoreTake(statsMux, portMAX_DELAY);
    if (st.online) {
      st.online = false;
      st.rev++;
    }
    xSemaphoreGive(statsMux);
    wasOnline = false;
    return;
  }

  String url = "http://" + host + ":" + String(settingsAgentPort()) + "/api/stats";
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(900);
  http.setTimeout(1800);
  http.setReuse(false);
  bool ok = false;
  if (http.begin(client, url)) {
    uint32_t t0 = millis();
    int code = http.GET();
    float linkMs = (float)(millis() - t0);
    if (code == 200) {
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, http.getStream());
      if (!err && (doc["ok"] | false)) {
        parseStats(doc, linkMs, false);
        ok = true;
      }
    }
    http.end();
  }

  if (!ok) {
    if (++failCount >= 3) {
      xSemaphoreTake(statsMux, portMAX_DELAY);
      bool old = st.online;
      st.online = false;
      if (old) st.rev++;
      xSemaphoreGive(statsMux);
      if (wasOnline) {
        wasOnline = false;
        Serial.println("[net] агент не отвечает");
      }
    }
  }
}

// ------------------------------------------------------------------ прогноз от ПК
// Экран сам до сервера погоды может не достучаться, поэтому компаньон, пока ПК
// включён, присылает прогноз на двое суток по часам. Когда ПК выключен, заставка
// показывает строку для текущего часа. Прогноз хранится в памяти платы.
struct WxHour {
  int8_t temp, feels;
  uint8_t wind, code;
};
static const int WX_MAX = 48;
static WxHour wxH[WX_MAX];
static int wxN = 0;
static uint32_t wxT0 = 0;        // время первой строки, Unix
static uint32_t wxSavedMs = 0;

static void handleForecast(JsonDocument& doc) {
  JsonArray h = doc["h"];
  uint32_t t0 = doc["t0"].as<uint32_t>();
  if (h.isNull() || t0 < 1700000000UL) return;
  WxHour tmp[WX_MAX];
  int n = 0;
  for (JsonArray row : h) {
    if (n >= WX_MAX) break;
    tmp[n].temp = (int8_t)constrain((int)(row[0] | 0), -100, 100);
    tmp[n].feels = (int8_t)constrain((int)(row[1] | 0), -100, 100);
    tmp[n].wind = (uint8_t)constrain((int)(row[2] | 0), 0, 250);
    tmp[n].code = (uint8_t)constrain((int)(row[3] | 0), 0, 250);
    n++;
  }
  if (n == 0) return;
  xSemaphoreTake(statsMux, portMAX_DELAY);
  memcpy(wxH, tmp, sizeof(WxHour) * n);
  wxN = n;
  wxT0 = t0;
  xSemaphoreGive(statsMux);
  if (!wxSavedMs || millis() - wxSavedMs > 3600000UL) {   // во флеш не чаще раза в час
    wxSavedMs = millis() ? millis() : 1;
    Preferences p;
    if (p.begin("pcmon_wx", false)) {
      p.putUInt("t0", t0);
      p.putBytes("h", tmp, sizeof(WxHour) * n);
      p.end();
    }
    Serial.printf("[weather] прогноз от ПК: %d ч\n", n);
  }
}

static void loadForecast() {
  Preferences p;
  if (!p.begin("pcmon_wx", true)) return;
  size_t len = p.getBytesLength("h");
  if (len >= sizeof(WxHour) && len <= sizeof(wxH)) {
    p.getBytes("h", wxH, len);
    wxN = len / sizeof(WxHour);
    wxT0 = p.getUInt("t0", 0);
  }
  p.end();
}

// ------------------------------------------------------------------ USB
static void handleUsbLine(char* line, size_t len) {
  if (line[0] != '{') return;
  JsonDocument doc;
  if (deserializeJson(doc, (const char*)line, len)) return;
  if (doc["set"].is<JsonObject>()) {
    // настройки из программы на ПК: {"set":{"skin":1,"flip":0,"call":"PCMON","city":"...","gm":"...","mail":"..."}}
    JsonObject s = doc["set"];
    if (!s["city"].isNull() || !s["gm"].isNull() || !s["mail"].isNull()) {
      monitorSetCity(s["city"] | (const char*)nullptr, s["gm"] | (const char*)nullptr, s["mail"] | (const char*)nullptr);
    }
    uiRequest(s["skin"] | -1, s["flip"] | -1, s["call"] | (const char*)nullptr);
    return;
  }
  if (doc["wx"] | 0) {      // почасовой прогноз от компаньона
    handleForecast(doc);
    return;
  }
  if (!(doc["ok"] | false)) return;

  // Часы берём у ПК: Wi-Fi и NTP при работе по кабелю не нужны.
  uint32_t t = doc["time"].as<uint32_t>();
  if (t > 1700000000UL) {
    time_t now = time(nullptr);
    long diff = (long)now - (long)t;
    if (now < 1700000000 || diff > 5 || diff < -5) {
      struct timeval tv = {(time_t)t, 0};
      settimeofday(&tv, nullptr);
    }
  }
  parseStats(doc, 0, true);
  usbLast = millis();
  if (usbLast == 0) usbLast = 1;
  usbWas = true;
  usbFrames++;
}

static void usbPoll() {
  if (!usbBuf) return;
  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c < 0) break;
    if (c == '\n') {
      if (!usbOverflow && usbLen > 0) {
        usbBuf[usbLen] = 0;
        handleUsbLine(usbBuf, usbLen);
      }
      usbLen = 0;
      usbOverflow = false;
    } else if (c != '\r') {
      if (usbLen < USB_BUF - 1) usbBuf[usbLen++] = (char)c;
      else usbOverflow = true;
    }
  }
}

// ------------------------------------------------------------------ погода (серверы РФ по Wi-Fi)
static Weather weather;
static volatile bool reqWeatherNow = false;

static int ruWeatherCode(const char* s) {
  if (!s) return 0;
  if (strstr(s, "гроз") || strstr(s, "Гроз")) return 95;
  if (strstr(s, "снегопад") || strstr(s, "Снегопад") || strstr(s, "метел") || strstr(s, "Метел")) return 85;
  if (strstr(s, "снег") || strstr(s, "Снег")) return 71;
  if (strstr(s, "ливен") || strstr(s, "Ливен") || strstr(s, "сильный дождь")) return 80;
  if (strstr(s, "морос") || strstr(s, "Морос")) return 51;
  if (strstr(s, "дожд") || strstr(s, "Дожд") || strstr(s, "осадк")) return 61;
  if (strstr(s, "туман") || strstr(s, "Туман") || strstr(s, "дымк")) return 45;
  if (strstr(s, "пасмурн") || strstr(s, "Пасмурн")) return 3;
  if (strstr(s, "малооблач") || strstr(s, "Малооблач") || strstr(s, "небольшая облач") || strstr(s, "переменная облач")) return 1;
  if (strstr(s, "облач") || strstr(s, "Облач")) return 2;
  if (strstr(s, "ясно") || strstr(s, "Ясно")) return 0;
  return 2;
}

static bool parseIntAfter(const char* hay, const char* key, float& out) {
  const char* p = strstr(hay, key);
  if (!p) return false;
  p += strlen(key);
  while (*p && *p != '-' && *p != '+' && (*p < '0' || *p > '9')) p++;
  if (!*p) return false;
  out = (float)atof(p);
  return true;
}

static bool pollGismeteo(Weather& wn) {
  settingsLock();
  String slug = gSet.cityGm;
  settingsUnlock();
  if (slug.isEmpty()) return false;
  String url = "https://www.gismeteo.ru/" + slug + "/now/";
  NetworkClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(4500);
  http.setTimeout(6000);
  http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/124.0.0.0");
  http.addHeader("Accept-Encoding", "identity");
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    const size_t CAP = 262144;
    char* buf = (char*)heap_caps_malloc(CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf) {
      WiFiClient* s = http.getStreamPtr();
      size_t len = 0;
      uint32_t t0 = millis();
      while ((http.connected() || s->available()) && len < CAP - 1 && millis() - t0 < 6500) {
        int av = s->available();
        if (av > 0) {
          int r = s->readBytes(buf + len, min((size_t)av, CAP - 1 - len));
          if (r > 0) len += r;
        } else {
          vTaskDelay(pdMS_TO_TICKS(5));
        }
      }
      buf[len] = 0;
      float t = 0, f = 0, w = 0;
      int wcode = 2;
      bool gotT = false;
      const char* cw = strstr(buf, "\"cw\":{\"temperature\":");
      if (cw) {
        gotT = parseIntAfter(cw, "\"temperature\":", t);
        const char* d = strstr(cw, "\"description\":\"");
        if (d) {
          char desc[96] = {0};
          d += 15;
          int i = 0;
          while (d[i] && d[i] != '"' && i < 90) { desc[i] = d[i]; i++; }
          desc[i] = 0;
          wcode = ruWeatherCode(desc);
        }
      }
      const char* nw = strstr(buf, "class=\"now-weather\"");
      if (nw) {
        parseIntAfter(nw, "value=\"", t);
        gotT = true;
      }
      const char* nf = strstr(buf, "class=\"now-feel\"");
      if (nf) parseIntAfter(nf, "value=\"", f);
      else f = t;
      const char* nd = strstr(buf, "class=\"now-desc\"");
      if (nd) {
        char desc[96] = {0};
        strncpy(desc, nd, sizeof(desc) - 1);
        wcode = ruWeatherCode(desc);
      }
      const char* ni = strstr(buf, "class=\"now-info\"");
      if (ni) parseIntAfter(ni, "<speed-value value=\"", w);
      if (gotT) {
        wn.ok = true;
        wn.temp = t;
        wn.feels = f;
        wn.wind = w;
        wn.code = wcode;
        wn.atMs = millis();
        ok = true;
      }
      heap_caps_free(buf);
    }
  }
  http.end();
  return ok;
}

static bool pollMailRu(Weather& wn) {
  settingsLock();
  String slug = gSet.cityMail;
  settingsUnlock();
  if (slug.isEmpty()) return false;
  String url = "https://pogoda.mail.ru/prognoz/" + slug + "/";
  NetworkClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(4500);
  http.setTimeout(6000);
  http.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/124.0.0.0");
  http.addHeader("Accept-Encoding", "identity");
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    const size_t CAP = 262144;
    char* buf = (char*)heap_caps_malloc(CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf) {
      WiFiClient* s = http.getStreamPtr();
      size_t len = 0;
      uint32_t t0 = millis();
      while ((http.connected() || s->available()) && len < CAP - 1 && millis() - t0 < 6500) {
        int av = s->available();
        if (av > 0) {
          int r = s->readBytes(buf + len, min((size_t)av, CAP - 1 - len));
          if (r > 0) len += r;
        } else {
          vTaskDelay(pdMS_TO_TICKS(5));
        }
      }
      buf[len] = 0;
      const char* cur = strstr(buf, "\"current\":{");
      if (cur) {
        float t = 0, f = 0, w = 0;
        if (parseIntAfter(cur, "\"temperature\":", t)) {
          if (!parseIntAfter(cur, "\"tempe_comf\":\"", f)) f = t;
          parseIntAfter(cur, "\"wind_speed\":\"", w);
          char desc[96] = {0};
          const char* d = strstr(cur, "\"description\":\"");
          if (d) {
            d += 15;
            int i = 0;
            while (d[i] && d[i] != '"' && i < 90) { desc[i] = d[i]; i++; }
            desc[i] = 0;
          }
          wn.ok = true;
          wn.temp = t;
          wn.feels = f;
          wn.wind = w;
          wn.code = ruWeatherCode(desc);
          wn.atMs = millis();
          ok = true;
        }
      }
      heap_caps_free(buf);
    }
  }
  http.end();
  return ok;
}

static void weatherPoll() {
  Weather wn;
  const char* src = "Gismeteo";
  bool ok = pollGismeteo(wn);
  if (!ok) {
    src = "Mail.ru";
    ok = pollMailRu(wn);
  }
  if (ok) {
    xSemaphoreTake(statsMux, portMAX_DELAY);
    weather = wn;
    xSemaphoreGive(statsMux);
    Serial.printf("[weather] %s: %+.0f°C (ощущ. %+.0f°C, ветер %.0f м/с, код %d)\n",
                  src, wn.temp, wn.feels, wn.wind, wn.code);
  } else {
    Serial.println("[weather] ошибка опроса серверов РФ");
  }
}

Weather monitorWeather() {
  if (!statsMux) return weather;
  xSemaphoreTake(statsMux, portMAX_DELAY);
  Weather c = weather;
  if (c.ok && millis() - c.atMs > 2UL * 3600UL * 1000UL) c.ok = false;   // устарела
  time_t now = time(nullptr);
  if (!c.ok && wxN > 0 && now > 1700000000 && (uint32_t)now >= wxT0) {
    uint32_t i = ((uint32_t)now - wxT0) / 3600UL;
    if (i < (uint32_t)wxN) {
      c.ok = true;
      c.temp = wxH[i].temp;
      c.feels = wxH[i].feels;
      c.wind = wxH[i].wind;
      c.code = wxH[i].code;
    }
  }
  xSemaphoreGive(statsMux);
  return c;
}

bool monitorForecast(int ahead, Weather& out) {
  if (!statsMux) return false;
  time_t now = time(nullptr);
  if (now < 1700000000) return false;
  bool ok = false;
  xSemaphoreTake(statsMux, portMAX_DELAY);
  if (wxN > 0 && (uint32_t)now >= wxT0) {
    uint32_t i = ((uint32_t)now - wxT0) / 3600UL + (uint32_t)max(0, ahead);
    if (i < (uint32_t)wxN) {
      out.ok = true;
      out.temp = wxH[i].temp;
      out.feels = wxH[i].feels;
      out.wind = wxH[i].wind;
      out.code = wxH[i].code;
      out.atMs = millis();
      ok = true;
    }
  }
  xSemaphoreGive(statsMux);
  return ok;
}

// Погода качается в своей задаче: TLS-соединение и разбор страницы занимают
// до 10 секунд, и если делать это в задаче связи, USB-кадры от ПК не читаются,
// а экран на это время решает, что ПК пропал.
static void weatherTask(void*) {
  uint32_t next = 8000;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED) continue;
    if (!reqWeatherNow && (int32_t)(now - next) < 0) continue;
    reqWeatherNow = false;
    settingsLock();
    bool haveCity = !gSet.cityGm.isEmpty() || !gSet.cityMail.isEmpty();
    settingsUnlock();
    if (!haveCity) {
      next = millis() + 60UL * 1000UL;
      continue;
    }
    weatherPoll();
    xSemaphoreTake(statsMux, portMAX_DELAY);
    bool ok = weather.ok && millis() - weather.atMs < 60000UL;
    xSemaphoreGive(statsMux);
    // удачно — через 20 минут; неудачно — через 15, чтобы не долбить сайты
    next = millis() + (ok ? 20UL : 15UL) * 60UL * 1000UL;
  }
}

// ------------------------------------------------------------------ task
static void netTask(void*) {
  udp.begin(UDP_PORT);
  Serial.printf("[net] UDP %u, опрос агента\n", UDP_PORT);
  for (;;) {
    usbPoll();
    bool usb = usbActive();
    if (!usb && usbWas) {
      // кабель отключили или агент остановлен
      usbWas = false;
      xSemaphoreTake(statsMux, portMAX_DELAY);
      st.online = false;
      st.rev++;
      xSemaphoreGive(statsMux);
      wasOnline = false;
      Serial.println("[usb] агент замолчал");
    }
    bool wifiUp = WiFi.status() == WL_CONNECTED;
    if (wifiUp) udpPoll();
    applyCommands();

    uint32_t now = millis();
    if (now >= nextPoll) {
      nextPoll = now + 1000;
      if (!usb) httpPoll();
    }
    if (now >= nextQuery) {
      nextQuery = now + 5000;
      if (!usb && wifiUp && (settingsAgentHost().isEmpty() || !wasOnline)) sendQuery();
    }
    if (now >= nextHello) {
      nextHello = now + 5000;
      settingsLock();
      String hello = "{\"hello\":1,\"fw\":\"1.4.0\",\"ip\":\"" + WiFi.localIP().toString() + "\",\"skin\":" + String(gSet.skin) +
                     ",\"flip\":" + String(gSet.flip ? 1 : 0) + ",\"call\":\"" + gSet.callsign + "\",\"city\":\"" + gSet.cityName +
                     "\",\"gm\":\"" + gSet.cityGm + "\",\"mail\":\"" + gSet.cityMail + "\",\"rx\":" + String(usbFrames) + "}";
      settingsUnlock();
      usbSend(hello);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void monitorBegin() {
  statsMux = xSemaphoreCreateMutex();
  memset(&st, 0, sizeof(st));
  usbBuf = (char*)malloc(USB_BUF);
  loadForecast();
  xTaskCreatePinnedToCore(netTask, "net", 16384, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(weatherTask, "weather", 16384, nullptr, 1, nullptr, 0);
}

void monitorWifiPoll() {
  WifiInfo w;
  wifi_mode_t mode = WiFi.getMode();
  w.apMode = (mode & WIFI_MODE_AP) != 0;
  if (WiFi.status() == WL_CONNECTED) {
    w.connected = true;
    w.rssi = WiFi.RSSI();
    w.quality = w.rssi <= -100 ? 0 : (w.rssi >= -50 ? 100 : 2 * (w.rssi + 100));
    w.channel = WiFi.channel();
    copyStr(w.ssid, sizeof(w.ssid), WiFi.SSID().c_str());
    copyStr(w.ip, sizeof(w.ip), WiFi.localIP().toString().c_str());
  } else {
    w.rssi = -127;
    copyStr(w.ip, sizeof(w.ip), WiFi.softAPIP().toString().c_str());
  }
  if (w.apMode) copyStr(w.apSsid, sizeof(w.apSsid), WiFi.softAPSSID().c_str());
  xSemaphoreTake(statsMux, portMAX_DELAY);
  wifi = w;
  xSemaphoreGive(statsMux);
}

PcStats monitorSnapshot() {
  if (!statsMux) return st;
  xSemaphoreTake(statsMux, portMAX_DELAY);
  PcStats c = st;
  xSemaphoreGive(statsMux);
  return c;
}

WifiInfo monitorWifi() {
  if (!statsMux) return wifi;
  xSemaphoreTake(statsMux, portMAX_DELAY);
  WifiInfo c = wifi;
  xSemaphoreGive(statsMux);
  return c;
}

void monitorRequestVolume(int level) {
  reqVolume = constrain(level, 0, 100);
  // Оптимистично обновляем UI, чтобы ползунок не дёргался до ответа.
  if (statsMux) {
    xSemaphoreTake(statsMux, portMAX_DELAY);
    st.volume = reqVolume;
    st.muted = false;
    st.rev++;
    xSemaphoreGive(statsMux);
  }
}

void monitorRequestMute(bool mute) {
  reqMute = mute ? 1 : 0;
  if (statsMux) {
    xSemaphoreTake(statsMux, portMAX_DELAY);
    st.muted = mute;
    st.rev++;
    xSemaphoreGive(statsMux);
  }
}

void monitorRequestSpeedtest() { reqSpeed = true; }

void monitorForgetAgent() {
  settingsLock();
  gSet.agentHost = "";
  settingsUnlock();
  settingsSave();
  xSemaphoreTake(statsMux, portMAX_DELAY);
  st.online = false;
  st.rev++;
  xSemaphoreGive(statsMux);
  wasOnline = false;
  nextQuery = 0;
}

bool monitorAgentConfigured() { return !settingsAgentHost().isEmpty(); }

void monitorSetCity(const char* name, const char* gm, const char* mail) {
  bool ch = false;
  settingsLock();
  if (name && name[0]) {
    String n = name;
    n.trim();
    n.replace("\"", "");
    n.replace("\\", "");
    if (n.length() > 36) n = n.substring(0, 36);
    if (gSet.cityName != n) { gSet.cityName = n; ch = true; }
  }
  if (gm) {
    String g = gm;
    g.trim();
    g.replace("\"", "");
    if (g.length() > 64) g = g.substring(0, 64);
    if (gSet.cityGm != g) { gSet.cityGm = g; ch = true; }
  }
  if (mail) {
    String m = mail;
    m.trim();
    m.replace("\"", "");
    if (m.length() > 64) m = m.substring(0, 64);
    if (gSet.cityMail != m) { gSet.cityMail = m; ch = true; }
  }
  settingsUnlock();
  if (ch) {
    settingsSave();
    xSemaphoreTake(statsMux, portMAX_DELAY);
    weather.ok = false;
    wxN = 0;
    xSemaphoreGive(statsMux);
    reqWeatherNow = true;
    Serial.printf("[weather] город: %s (gm=%s, mail=%s)\n",
                  gSet.cityName.c_str(), gSet.cityGm.c_str(), gSet.cityMail.c_str());
  }
}
