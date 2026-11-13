// Настройка Wi-Fi и адреса агента с телефона: точка доступа + веб-форма.
#include "portal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "alerts.h"
#include "monitor.h"
#include "settings.h"
#include "skins.h"
#include "ui.h"

static const char* AP_PASS = "12345678";
static WebServer server(80);
static DNSServer dns;
static String apName;
static bool apActive = false;
static bool wantReboot = false;

static const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="ru"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>PC Monitor — настройка</title>
<style>
:root{--bg:#0b1020;--card:#141e33;--line:#243452;--text:#f2f6ff;--mut:#8fa0bf;--acc:#4de1ff}
*{box-sizing:border-box}body{margin:0;padding:24px 14px;background:radial-gradient(120% 80% at 50% 0%,#1a2a52 0%,var(--bg) 55%);color:var(--text);font:15px/1.45 system-ui,Segoe UI,Roboto,sans-serif}
.wrap{max-width:520px;margin:0 auto}.logo{font-weight:800;letter-spacing:.18em;font-size:13px;color:var(--acc);margin-bottom:6px}
h1{font-size:26px;margin:0 0 4px}p.sub{color:var(--mut);margin:0 0 20px}
.card{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:18px;box-shadow:0 18px 50px #0006}
label{display:block;font-size:12px;color:var(--mut);margin:14px 0 6px;text-transform:uppercase;letter-spacing:.08em}
input,select{width:100%;padding:12px 13px;border-radius:11px;border:1px solid var(--line);background:#0e1729;color:var(--text);font-size:16px;outline:none}
input:focus,select:focus{border-color:var(--acc)}
.row{display:grid;grid-template-columns:1fr 1fr;gap:12px}
button{width:100%;margin-top:20px;padding:14px;border:0;border-radius:12px;background:linear-gradient(90deg,#4de1ff,#8b7bff);color:#07101f;font-weight:800;font-size:16px}
button.ghost{background:#1b2944;color:var(--acc);margin-top:10px;font-weight:600;font-size:14px;padding:10px}
.hint{color:var(--mut);font-size:12px;margin-top:8px}.ok{color:#51e08a}.warn{color:#ffc857}
.foot{color:var(--mut);font-size:12px;text-align:center;margin-top:16px}
</style></head><body><div class="wrap">
<div class="logo">PC MONITOR</div><h1>Настройка экрана</h1>
<p class="sub">Wi-Fi и адрес агента на компьютере</p>
<div class="card"><form method="POST" action="/save">
<label>Сеть Wi-Fi</label>
<input name="ssid" id="ssid" list="nets" value="%SSID%" placeholder="Имя сети" autocomplete="off">
<datalist id="nets"></datalist>
<button class="ghost" type="button" onclick="scan()">Найти сети рядом</button>
<label>Пароль</label>
<input name="pass" type="password" placeholder="оставьте пусто, чтобы не менять">
<label>Адрес агента (необязательно)</label>
<input name="agent" value="%AGENT%" placeholder="например 192.168.1.5 — или пусто для автопоиска">
<label>Порт агента</label>
<input name="port" value="%PORT%" inputmode="numeric">
<label>Скин</label>
<select name="theme">%THEMES%</select>
<button type="submit">Сохранить и перезагрузить</button>
<div class="hint">Экран сам найдёт агент в сети, если поле адреса пустое.</div>
</form></div>
<div class="foot">PC Monitor · ESP32-S3 · 320×240</div></div>
<script>
const names=["Аврора","Закат","Мята","Кибер","Норд","День"];
async function scan(){const b=document.querySelector('button.ghost');b.textContent='Сканирую…';
try{const r=await fetch('/scan');const a=await r.json();const dl=document.getElementById('nets');dl.innerHTML='';
a.sort((x,y)=>y.rssi-x.rssi).forEach(n=>{const o=document.createElement('option');o.value=n.ssid;o.label=n.rssi+' дБм';dl.appendChild(o)});
b.textContent=a.length?'Выберите сеть в поле выше':'Сети не найдены';}catch(e){b.textContent='Ошибка сканирования';}}
</script></body></html>)HTML";

static String htmlEscape(const String& s) {
  String r = s;
  r.replace("&", "&amp;");
  r.replace("\"", "&quot;");
  r.replace("<", "&lt;");
  r.replace(">", "&gt;");
  return r;
}

static String themeOptions(uint8_t cur) {
  String r;
  for (int i = 0; i < skinCount(); i++) {
    r += "<option value=\"";
    r += i;
    r += "\"";
    if (i == cur) r += " selected";
    r += ">";
    r += skinName(i);
    r += "</option>";
  }
  return r;
}

static void handleRoot() {
  settingsLock();
  String ssid = htmlEscape(gSet.wifiSsid);
  String agent = htmlEscape(gSet.agentHost);
  uint16_t port = gSet.agentPort;
  uint8_t theme = gSet.skin;
  settingsUnlock();

  String page = FPSTR(PAGE);
  page.replace("%SSID%", ssid);
  page.replace("%AGENT%", agent);
  page.replace("%PORT%", String(port));
  page.replace("%THEMES%", themeOptions(theme));
  server.send(200, "text/html; charset=utf-8", page);
}

static void handleScan() {
  int n = WiFi.scanNetworks(false, true);
  String json = "[";
  bool first = true;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    ssid.replace("\\", "\\\\");
    ssid.replace("\"", "\\\"");
    if (!first) json += ",";
    first = false;
    json += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
  }
  json += "]";
  WiFi.scanDelete();
  server.send(200, "application/json", json);
}

static void handleSave() {
  if (!server.hasArg("ssid")) {
    server.send(400, "text/plain", "no ssid");
    return;
  }
  settingsLock();
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (!ssid.isEmpty()) gSet.wifiSsid = ssid;
  if (!pass.isEmpty()) gSet.wifiPass = pass;
  gSet.agentHost = server.arg("agent");
  gSet.agentHost.trim();
  int port = server.arg("port").toInt();
  if (port < 1 || port > 65535) port = 8123;
  gSet.agentPort = (uint16_t)port;
  int theme = server.arg("theme").toInt();
  if (server.hasArg("theme") && theme >= 0 && theme < skinCount()) gSet.skin = (uint8_t)theme;
  settingsUnlock();
  settingsSave();
  wantReboot = true;

  server.send(200, "text/html; charset=utf-8",
              "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
              "<body style='background:#0b1020;color:#f2f6ff;font:16px system-ui;padding:40px;text-align:center'>"
              "<h2 style='color:#4de1ff'>Сохранено</h2><p>Экран перезагружается и подключается к сети.</p></body>");
}

static void handleStatus() {
  settingsLock();
  String agent = gSet.agentHost;
  uint16_t port = gSet.agentPort;
  settingsUnlock();
  String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  String json = "{\"ok\":true,\"ip\":\"" + ip + "\",\"rssi\":" + String(WiFi.RSSI()) +
                ",\"agent\":\"" + agent + "\",\"port\":" + String(port) +
                ",\"skin\":" + String(gSet.skin) + ",\"skins\":" + String(skinCount()) +
                ",\"flip\":" + String(gSet.flip ? 1 : 0) +
                ",\"city\":\"" + gSet.cityName + "\",\"gm\":\"" + gSet.cityGm + "\",\"mail\":\"" + gSet.cityMail + "\"" +
                ",\"alert\":" + String(gSet.alertOn ? 1 : 0) + ",\"tcpu\":" + String(gSet.alertCpu) +
                ",\"tgpu\":" + String(gSet.alertGpu) + ",\"tram\":" + String(gSet.alertRam) +
                ",\"alarm\":\"" + alertsText() + "\"" +
                ",\"heap\":" + String(ESP.getFreeHeap()) +
                ",\"psram\":" + String(ESP.getFreePsram()) +
                ",\"uptime\":" + String(millis() / 1000) + "}";
  server.send(200, "application/json", json);
}

// Настройки с ПК: /api/set?skin=1&flip=0&menu=2&home=1
static void handleSet() {
  bool reload = false;
  if (server.hasArg("flip")) {
    settingsLock();
    gSet.flip = server.arg("flip").toInt() != 0;
    settingsUnlock();
    reload = true;
  }
  if (reload) {
    settingsSave();
    uiReloadSettings();
  }
  // тревоги: /api/set?alert=1&tcpu=85&tgpu=83&tram=95
  bool al = false;
  settingsLock();
  if (server.hasArg("alert")) { gSet.alertOn = server.arg("alert").toInt() != 0; al = true; }
  if (server.hasArg("tcpu")) { gSet.alertCpu = (uint8_t)constrain(server.arg("tcpu").toInt(), 30, 110); al = true; }
  if (server.hasArg("tgpu")) { gSet.alertGpu = (uint8_t)constrain(server.arg("tgpu").toInt(), 30, 110); al = true; }
  if (server.hasArg("tram")) { gSet.alertRam = (uint8_t)constrain(server.arg("tram").toInt(), 30, 100); al = true; }
  settingsUnlock();
  if (al) settingsSave();
  if (server.hasArg("city") || server.hasArg("gm") || server.hasArg("mail")) {
    monitorSetCity(server.hasArg("city") ? server.arg("city").c_str() : nullptr,
                   server.hasArg("gm") ? server.arg("gm").c_str() : nullptr,
                   server.hasArg("mail") ? server.arg("mail").c_str() : nullptr);
  }
  if (server.hasArg("idle")) uiForceIdle(server.arg("idle").toInt() != 0);
  if (server.hasArg("fc")) uiShowForecast(server.arg("fc").toInt() != 0);
  if (server.hasArg("media")) uiShowMedia(server.arg("media").toInt() != 0);
  if (server.hasArg("night")) uiShowNight(server.arg("night").toInt() != 0);
  if (server.hasArg("launch")) uiShowLaunch(server.arg("launch").toInt() != 0);
  if (server.hasArg("skin")) uiSetSkin(server.arg("skin").toInt());
  if (server.hasArg("menu")) uiShowMenu(server.arg("menu").toInt());
  handleStatus();
}

// Снимок экрана в BMP (RGB565) — чтобы проверять интерфейс без фотоаппарата.
static void handleShot() {
  lv_draw_buf_t* b = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
  if (!b) {
    server.send(500, "text/plain", "no memory");
    return;
  }
  uint32_t w = b->header.w, h = b->header.h, stride = b->header.stride;
  uint32_t row = w * 2, dataLen = row * h;
  uint8_t hd[66] = {0};
  auto put32 = [&](int o, uint32_t v) { hd[o] = v; hd[o + 1] = v >> 8; hd[o + 2] = v >> 16; hd[o + 3] = v >> 24; };
  hd[0] = 'B'; hd[1] = 'M';
  put32(2, 66 + dataLen);
  put32(10, 66);
  put32(14, 40);
  put32(18, w);
  put32(22, (uint32_t)(-(int32_t)h));   // сверху вниз
  hd[26] = 1; hd[28] = 16;
  put32(30, 3);                         // BI_BITFIELDS
  put32(34, dataLen);
  put32(54, 0xF800); put32(58, 0x07E0); put32(62, 0x001F);
  server.setContentLength(66 + dataLen);
  server.send(200, "image/bmp", "");
  server.sendContent((const char*)hd, 66);
  const uint32_t rowsPerChunk = 8;
  static uint8_t chunk[320 * 2 * 8];
  for (uint32_t y = 0; y < h; y += rowsPerChunk) {
    uint32_t n = min(rowsPerChunk, h - y);
    for (uint32_t i = 0; i < n; i++) memcpy(chunk + i * row, b->data + (y + i) * stride, row);
    server.sendContent((const char*)chunk, n * row);
  }
  lv_draw_buf_destroy(b);
}

static void redirectPortal() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

void portalBegin() {
  uint8_t mac[6] = {0};
  WiFi.macAddress(mac);
  char buf[24];
  snprintf(buf, sizeof(buf), "PCMonitor-%02X%02X", mac[4], mac[5]);
  apName = buf;

  server.on("/", handleRoot);
  server.on("/scan", handleScan);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/api/status", handleStatus);
  server.on("/shot.bmp", handleShot);
  server.on("/api/set", handleSet);
  server.on("/favicon.ico", []() { server.send(204); });
  // captive portal
  server.on("/generate_204", redirectPortal);
  server.on("/hotspot-detect.html", redirectPortal);
  server.on("/connecttest.txt", redirectPortal);
  server.on("/ncsi.txt", redirectPortal);
  server.onNotFound([]() {
    if (apActive) redirectPortal();
    else server.send(404, "text/plain", "not found");
  });
  server.begin();
  Serial.printf("[portal] http://%s/ (AP %s)\n",
                WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "0.0.0.0",
                apName.c_str());
}

void portalStartAp() {
  if (apActive) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apName.c_str(), AP_PASS);
  delay(80);
  dns.start(53, "*", WiFi.softAPIP());
  apActive = true;
  Serial.printf("[portal] AP %s / %s, http://%s/\n", apName.c_str(), AP_PASS,
                WiFi.softAPIP().toString().c_str());
}

void portalLoop() {
  server.handleClient();
  if (apActive) dns.processNextRequest();
}

bool portalApActive() { return apActive; }
bool portalWantsReboot() { return wantReboot; }
String portalApName() { return apName; }
String portalApPass() { return AP_PASS; }
String portalUrl() {
  return apActive ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}
