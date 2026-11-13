// Интерфейс PC Monitor для 320x240: темы, графики, тач, звук, экран.
#include "ui.h"

#include <math.h>
#include <string.h>
#include <time.h>

#include "audio.h"
#include "board.h"
#include "display.h"
#include "monitor.h"
#include "portal.h"
#include "sdlog.h"
#include "alerts.h"
#include "settings.h"
#include "skins.h"

#include "fonts_px.h"
#include "font_medium.h"
#include "font_semibold.h"

// ------------------------------------------------------------------ макеты
static const int TOP_H = 26;
static const int CONTENT_Y = 28;
static const int CONTENT_H = 184;
static const int NAV_Y = 214;

struct Theme {
  const char* name;
  uint32_t bg1, bg2, card, card2, line, text, muted;
  uint32_t accent, accent2, good, warn, bad, chart, btnTxt;
  bool light;
};

// Палитра меню берётся от текущего скина: скин задаёт вид всего интерфейса.
static const Theme THEMES[13] = {
    {"Приборы", 0x0C0E11, 0x14181D, 0x171A1F, 0x20252B, 0x3A3F47, 0xFFFFFF, 0xAAB1BC,
     0xFF9F1C, 0x4CC9F0, 0x46E06A, 0xFFC53D, 0xE5484D, 0x1B2026, 0x14100A, false},
    {"Неон", 0x07060D, 0x120F24, 0x0F0D1C, 0x1B1830, 0x2E2850, 0xFFFFFF, 0xB9B3E0,
     0x22E3FF, 0xFF4DF5, 0x4DFFB0, 0xFFE14D, 0xFF4D6D, 0x151230, 0x07060D, false},
    {"Минимализм", 0xF4F2EE, 0xEAE6DE, 0xFFFFFF, 0xEFEBE4, 0xD9D5CC, 0x16161A, 0x5C5C66,
     0xC2410C, 0x1D4ED8, 0x15803D, 0xB45309, 0xB91C1C, 0xF7F4EF, 0xFFFFFF, true},
    {"Ретро-радио", 0x1C1A17, 0x26221C, 0x04100E, 0x0B1F1B, 0x3A342A, 0xE8DCC0, 0xC9C1B0,
     0x5EF2D6, 0xF3E6C4, 0x46E06A, 0xFFC53D, 0xFF5A4F, 0x06110F, 0x04100E, false},
    {"Часы", 0x000000, 0x0B0D10, 0x12151B, 0x1A1E26, 0x2A303C, 0xFFFFFF, 0x9AA2B1,
     0xFFA51E, 0x45C5FF, 0x2CE65C, 0xFFC53D, 0xFF3B6B, 0x12161E, 0x140E02, false},
    {"Терминал", 0x020A04, 0x041308, 0x061A0B, 0x0B2612, 0x174823, 0x35E85A, 0x28B846,
     0x35E85A, 0x72FF90, 0x35E85A, 0xB8FF4D, 0xFF5A4F, 0x05170A, 0x020A04, false},
    {"Плитки", 0x0B0C10, 0x12141C, 0x171A24, 0x202533, 0x2E3548, 0xFFFFFF, 0xA3ACB9,
     0x1F64F2, 0x763AF5, 0x0C7465, 0xB8530A, 0xE5484D, 0x151924, 0xFFFFFF, false},
    {"Лампы", 0x0E0905, 0x160E08, 0x1C120A, 0x26180E, 0x4A341E, 0xE6D7C3, 0xB8A58E,
     0xFF8A2B, 0xFFB366, 0x46E06A, 0xFFC53D, 0xFF5A4F, 0x1A1008, 0x160C04, false},
    {"Импульс", 0x07111D, 0x0B1928, 0x0D1B29, 0x112438, 0x1F4057, 0xEAF7FF, 0x8AA2B8,
     0x24D8FF, 0x8B6CFF, 0x42E6B5, 0xFFB454, 0xFF4E9B, 0x10283B, 0x07111D, false},
    {"Шина", 0x081018, 0x0D1922, 0x101C28, 0x122333, 0x25485A, 0xEAF5FF, 0x8295A8,
     0x8B6CFF, 0x24D8FF, 0x42E6B5, 0xFFB454, 0xFF5B7F, 0x11182C, 0x070B16, false},
    {"HUD", 0x0B0D12, 0x11151D, 0x121720, 0x1A202B, 0x27303D, 0xF4F7FC, 0x8995A7,
     0x42E6B5, 0x24D8FF, 0x42E6B5, 0xFFB454, 0xFF4E9B, 0x151C26, 0x0B0D12, false},
    {"Сканлайн", 0x080D12, 0x0E161D, 0x111A22, 0x17232C, 0x1D3440, 0xE9F6FF, 0x8297A5,
     0x42D8FF, 0xFF4E9B, 0x42E6B5, 0xFFB454, 0xFF5577, 0x10212A, 0x080D12, false},    {"Звук", 0x06070C, 0x0C0E18, 0x0F1220, 0x161A2C, 0x262B42, 0xF2F4FF, 0x8C92B0,
     0x22E3FF, 0xFF4DF5, 0x42E6B5, 0xFFB454, 0xFF4E6D, 0x10131F, 0x06070C, false},
};

static Theme TH;
static int themeIdx = 0;

static lv_font_t *fTiny, *fSmall, *fSmallB, *fMedB, *fBigB, *fHugeB, *fGiantB;

struct Gauge {
  lv_obj_t *card, *arc, *val, *title, *detail;
};

struct Widgets {
  Gauge gz[4];
  lv_obj_t *downLbl, *upLbl, *totDown, *totUp, *downChart, *upChart;
  lv_chart_series_t *downSer, *upSer;
  lv_obj_t *volVal, *volSlider, *volMuteIcon;

  lv_obj_t *chips[5], *chart, *now, *nowLbl, *info;
  lv_chart_series_t *ser1, *ser2;

  lv_obj_t *wq, *wdbm, *wbars[4], *wssid, *wip, *wlink, *wpc, *rssiChart;
  lv_chart_series_t *rssiSer;
  lv_obj_t *testBtn, *testLbl, *prog, *stage, *dlLbl, *ulLbl, *pingLbl, *lanLbl, *stime, *topLbl;

  lv_obj_t *themeSw[6], *brightSlider, *brightVal, *dimBtn[4];
  lv_obj_t *alertLbl;
  lv_obj_t *soundLbl, *sdLbl, *agentLbl, *agentDot, *infoSet, *flipLbl, *skinLbl;
};

static Widgets W;

static lv_obj_t *scr, *content, *overlay, *toast;
static lv_obj_t *titleLbl, *hostLbl, *timeLbl, *dateLbl;
static lv_obj_t *wBars[4], *rssiLbl, *batBox, *batFill, *batLbl, *powerLbl;
static lv_obj_t *tabObj[4], *tabIcon[4], *tabLbl[4];

static int page = 0;
static int gmode = 0;
static uint32_t lastRev = 0;
static uint32_t lastSeen = 0;
static uint32_t lastActivity = 0;
static uint32_t toastUntil = 0;
static uint32_t volHoldUntil = 0;
static uint32_t rebootAt = 0;
static bool asleep = false, dimmed = false;
static int pendingTheme = -1;
static bool pendingBuild = false;
// Главный экран — полноэкранный скин; касание открывает меню (страницы и настройки).
static bool home = true;
static bool pendingRebuild = false;
// Заставка: ПК молчит дольше 20 секунд (или включена принудительно для проверки).
static bool idleShown = false, idleForce = false;
static bool fcShown = false;      // экран прогноза погоды
static bool mediaShown = false;   // медиапанель
static bool nightShown = false;   // отчёт за 12 часов
static bool launchShown = false;  // быстрый запуск
static bool calShown = false;     // калибровка сенсора
static uint32_t fcAt = 0;
static uint32_t offlineSince = 0;
static int nightCap = 100;
static bool toastSkin = false;
static uint32_t gestureAt = 0;
static WifiInfo lastW;

static float hCpu[HIST_N], hRam[HIST_N], hGpu[HIST_N], hDisk[HIST_N];
static float hDown[HIST_N], hUp[HIST_N], hRssi[HIST_N];
static int hCount = 0;

static inline lv_color_t C(uint32_t v) { return lv_color_hex(v); }

// ------------------------------------------------------------------ шрифты
static lv_font_t* mkFont(const uint8_t* d, size_t len, int size, const lv_font_t* fb) {
  lv_font_t* f = lv_tiny_ttf_create_data_ex(d, len, size, LV_FONT_KERNING_NONE, 64);
  return f ? f : (lv_font_t*)fb;
}

static void loadFonts() {
  // Мелкий текст — заранее отрисованные растровые шрифты (чётче вектора на 320x240),
  // крупные цифры — вектор.
  fTiny = (lv_font_t*)&font_pm11;
  fSmall = (lv_font_t*)&font_pm12;
  fSmallB = (lv_font_t*)&font_pb12;
  fMedB = (lv_font_t*)&font_pb14;
  fBigB = (lv_font_t*)&font_pb16;
  fHugeB = mkFont(font_semibold, font_semibold_len, 22, &lv_font_montserrat_20);
  fGiantB = mkFont(font_semibold, font_semibold_len, 28, &lv_font_montserrat_28);
}

// ------------------------------------------------------------------ утилиты
static String fmtBytes(uint64_t b) {
  if (b >= 1099511627776ULL) return String((double)b / 1099511627776.0, 2) + " ТБ";
  if (b >= 1073741824ULL) return String((double)b / 1073741824.0, 1) + " ГБ";
  if (b >= 1048576ULL) return String((double)b / 1048576.0, 0) + " МБ";
  return String((unsigned long)(b / 1024)) + " КБ";
}

static String fmtGB(float gb) {
  if (gb >= 1000.0f) return String(gb / 1024.0f, 1) + "Т";
  if (gb >= 100.0f) return String((int)(gb + 0.5f)) + "Г";
  return String(gb, 1) + "Г";
}

static String fmtSpeed(float m) {
  if (m >= 1000.0f) return String(m / 1000.0f, 2) + " Гб";
  if (m >= 100.0f) return String(m, 0);
  if (m >= 10.0f) return String(m, 1);
  return String(m, 2);
}

static String fmtClock(uint32_t epoch) {
  if (epoch < 1700000000UL) return "—";
  time_t t = epoch;
  struct tm tm;
  localtime_r(&t, &tm);
  char b[24];
  strftime(b, sizeof(b), "%d.%m %H:%M", &tm);
  return String(b);
}

static String fmtUptime(uint32_t s) {
  uint32_t d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
  if (d) return String((unsigned)d) + "д " + String((unsigned)h) + "ч";
  if (h) return String((unsigned)h) + "ч " + String((unsigned)m) + "м";
  return String((unsigned)m) + "м";
}

static uint32_t loadColor(float v, int idx) {
  if (v >= 85.0f) return TH.bad;
  if (v >= 65.0f) return TH.warn;
  if (idx == 1) return TH.accent2;
  if (idx == 2) return TH.good;
  if (idx == 3) return TH.warn;
  return TH.accent;
}

static void pushHist(float v, float* a) {
  if (hCount < HIST_N) a[hCount] = v;
  else {
    memmove(a, a + 1, (HIST_N - 1) * sizeof(float));
    a[HIST_N - 1] = v;
  }
}

static void pushHistory(const PcStats& pc, const WifiInfo& w) {
  pushHist(pc.cpu, hCpu);
  pushHist(pc.ramPct, hRam);
  pushHist(pc.hasGpu ? pc.gpuPct : 0.0f, hGpu);
  pushHist(pc.diskPct, hDisk);
  pushHist(pc.netDownMbps, hDown);
  pushHist(pc.netUpMbps, hUp);
  pushHist(w.connected ? (float)w.rssi : -127.0f, hRssi);
  if (hCount < HIST_N) hCount++;
}

static void rangeStats(const float* a, int cnt, float& mn, float& av, float& mx) {
  mn = 0;
  av = 0;
  mx = 0;
  if (cnt <= 0) return;
  mn = 1e30f;
  mx = -1e30f;
  double s = 0;
  for (int i = 0; i < cnt; i++) {
    float v = a[i];
    if (v < mn) mn = v;
    if (v > mx) mx = v;
    s += v;
  }
  av = (float)(s / cnt);
}

static float lastMax(const float* a, int cnt, int pts) {
  int start = cnt - pts;
  if (start < 0) start = 0;
  float mx = 0;
  for (int i = start; i < cnt; i++)
    if (a[i] > mx) mx = a[i];
  return mx;
}

// ------------------------------------------------------------------ виджеты
static lv_obj_t* obj(lv_obj_t* p, int x, int y, int w, int h) {
  lv_obj_t* o = lv_obj_create(p);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_shadow_width(o, 0, 0);
  lv_obj_set_style_radius(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

static void setGrad(lv_obj_t* o, uint32_t c1, uint32_t c2, lv_grad_dir_t dir = LV_GRAD_DIR_VER) {
  lv_obj_set_style_bg_color(o, C(c1), 0);
  lv_obj_set_style_bg_grad_color(o, C(c2), 0);
  lv_obj_set_style_bg_grad_dir(o, dir, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
}

static lv_obj_t* card(lv_obj_t* p, int x, int y, int w, int h, int r = 10) {
  lv_obj_t* o = obj(p, x, y, w, h);
  setGrad(o, TH.card, TH.card2);
  lv_obj_set_style_radius(o, r, 0);
  lv_obj_set_style_border_width(o, 1, 0);
  lv_obj_set_style_border_color(o, C(TH.line), 0);
  lv_obj_set_style_shadow_width(o, 8, 0);
  lv_obj_set_style_shadow_color(o, C(0x000000), 0);
  lv_obj_set_style_shadow_opa(o, TH.light ? LV_OPA_10 : LV_OPA_30, 0);
  lv_obj_set_style_shadow_offset_y(o, 2, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

// Подпись в одну строку: длинный текст обрезается многоточием, а не переносится.
static lv_obj_t* one(lv_obj_t* l) {
  const lv_font_t* f = lv_obj_get_style_text_font(l, LV_PART_MAIN);
  lv_obj_set_height(l, lv_font_get_line_height(f) + 1);
  return l;
}

static lv_obj_t* txt(lv_obj_t* p, int x, int y, const char* s, const lv_font_t* f, uint32_t c,
                     int w = 0, lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
  lv_obj_t* l = lv_label_create(p);
  lv_label_set_text(l, s ? s : "");
  lv_obj_set_style_text_font(l, f ? f : fSmall, 0);
  lv_obj_set_style_text_color(l, C(c), 0);
  lv_obj_set_style_text_letter_space(l, 0, 0);
  lv_obj_set_style_text_line_space(l, 1, 0);
  if (w > 0) {
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(l, align, 0);
  }
  lv_obj_set_pos(l, x, y);
  lv_obj_remove_flag(l, LV_OBJ_FLAG_SCROLLABLE);
  return l;
}

static lv_obj_t* centerTxt(lv_obj_t* p, const char* s, const lv_font_t* f, uint32_t c) {
  lv_obj_t* l = txt(p, 0, 0, s, f, c);
  lv_obj_center(l);
  return l;
}

static lv_obj_t* ico(lv_obj_t* p, int x, int y, const char* s, uint32_t c, const lv_font_t* f = nullptr) {
  lv_obj_t* l = lv_label_create(p);
  lv_label_set_text(l, s);
  lv_obj_set_style_text_font(l, f ? f : &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(l, C(c), 0);
  lv_obj_set_pos(l, x, y);
  lv_obj_remove_flag(l, LV_OBJ_FLAG_SCROLLABLE);
  return l;
}

static lv_obj_t* makeBtn(lv_obj_t* p, int x, int y, int w, int h, uint32_t bg, uint32_t border, int r = 8) {
  lv_obj_t* b = obj(p, x, y, w, h);
  lv_obj_set_style_bg_color(b, C(bg), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(b, r, 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, C(border), 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(b, 6);   // кнопки мелкие: расширяем зону нажатия
  lv_obj_set_style_bg_color(b, C(TH.card2), LV_STATE_PRESSED);
  return b;
}

static void accentBtn(lv_obj_t* b) {
  lv_obj_set_style_bg_color(b, C(TH.accent), 0);
  lv_obj_set_style_bg_grad_color(b, C(TH.accent2), 0);
  lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(b, C(TH.accent), 0);
  lv_obj_set_style_text_color(b, C(TH.btnTxt), 0);
  lv_obj_set_style_bg_color(b, C(TH.accent2), LV_STATE_PRESSED);
}

static void makeBars(lv_obj_t* p, int x, int bottom, int bw, int gap, const int hs[4], lv_obj_t** out) {
  for (int i = 0; i < 4; i++) {
    out[i] = obj(p, x + i * (bw + gap), bottom - hs[i], bw, hs[i]);
    lv_obj_set_style_bg_color(out[i], C(TH.line), 0);
    lv_obj_set_style_bg_opa(out[i], LV_OPA_COVER, 0);
    lv_obj_set_style_radius(out[i], bw / 2, 0);
  }
}

static void setBars(lv_obj_t** bars, int q, uint32_t on) {
  int n = q >= 85 ? 4 : q >= 60 ? 3 : q >= 35 ? 2 : q >= 10 ? 1 : 0;
  if (q > 0 && n == 0) n = 1;
  for (int i = 0; i < 4; i++) {
    if (!bars[i]) continue;
    lv_obj_set_style_bg_color(bars[i], C(i < n ? on : TH.line), 0);
    lv_obj_set_style_bg_opa(bars[i], LV_OPA_COVER, 0);
  }
}

static void styleSlider(lv_obj_t* s, uint32_t ind) {
  lv_obj_set_style_bg_color(s, C(TH.card2), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s, C(ind), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_color(s, C(TH.accent2), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_dir(s, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s, C(ind), LV_PART_KNOB);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_set_style_border_width(s, 2, LV_PART_KNOB);
  lv_obj_set_style_border_color(s, C(TH.card), LV_PART_KNOB);
  lv_obj_set_style_pad_all(s, 2, LV_PART_KNOB);
}

// ------------------------------------------------------------------ графики
static lv_obj_t* newChart(lv_obj_t* p, int x, int y, int w, int h, int pts, bool grid, bool panel) {
  lv_obj_t* ch = lv_chart_create(p);
  lv_obj_set_pos(ch, x, y);
  lv_obj_set_size(ch, w, h);
  lv_chart_set_type(ch, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(ch, pts);
  lv_chart_set_update_mode(ch, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_div_line_count(ch, grid ? 3 : 0, grid ? 4 : 0);
  if (panel) {
    setGrad(ch, TH.card, TH.chart);
    lv_obj_set_style_border_width(ch, 1, 0);
    lv_obj_set_style_border_color(ch, C(TH.line), 0);
    lv_obj_set_style_radius(ch, 10, 0);
    lv_obj_set_style_pad_all(ch, 4, 0);
  } else {
    lv_obj_set_style_bg_opa(ch, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ch, 0, 0);
    lv_obj_set_style_radius(ch, 0, 0);
    lv_obj_set_style_pad_all(ch, 0, 0);
  }
  lv_obj_set_style_line_width(ch, 1, LV_PART_MAIN);
  lv_obj_set_style_line_color(ch, C(TH.line), LV_PART_MAIN);
  lv_obj_set_style_line_opa(ch, grid ? LV_OPA_40 : LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_line_width(ch, 2, LV_PART_ITEMS);
  lv_obj_set_style_line_rounded(ch, true, LV_PART_ITEMS);
  lv_obj_set_style_size(ch, 0, 0, LV_PART_INDICATOR);
  lv_obj_remove_flag(ch, LV_OBJ_FLAG_SCROLLABLE);
  return ch;
}

static lv_chart_series_t* addSer(lv_obj_t* ch, uint32_t color) {
  lv_chart_series_t* s = lv_chart_add_series(ch, C(color), LV_CHART_AXIS_PRIMARY_Y);
  if (s) lv_chart_set_all_values(ch, s, LV_CHART_POINT_NONE);
  return s;
}

static void setRange(lv_obj_t* ch, int mn, int mx) {
  if (!ch) return;
  if (mx <= mn) mx = mn + 1;
  lv_chart_set_axis_range(ch, LV_CHART_AXIS_PRIMARY_Y, mn, mx);
}

static void seedSeries(lv_obj_t* ch, lv_chart_series_t* ser, const float* arr, int cnt, int pts, float mul = 1.0f) {
  if (!ch || !ser) return;
  int start = cnt - pts;
  if (start < 0) start = 0;
  for (int i = 0; i < pts; i++) {
    int idx = start + i;
    int32_t v = LV_CHART_POINT_NONE;
    if (idx >= 0 && idx < cnt) v = (int32_t)lroundf(arr[idx] * mul);
    lv_chart_set_series_value_by_id(ch, ser, i, v);
  }
  lv_chart_refresh(ch);
}

static void arcExec(void* var, int32_t v) { lv_arc_set_value((lv_obj_t*)var, v); }

static void animArc(lv_obj_t* arc, int to) {
  if (!arc) return;
  lv_anim_del(arc, arcExec);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, arc);
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)arcExec);
  lv_anim_set_values(&a, lv_arc_get_value(arc), to);
  lv_anim_set_duration(&a, 300);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

static void fadeExec(void* var, int32_t v) { lv_obj_set_style_opa((lv_obj_t*)var, (lv_opa_t)v, 0); }

static void fadeIn(lv_obj_t* o) {
  if (!o) return;
  lv_obj_set_style_opa(o, 0, 0);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)fadeExec);
  lv_anim_set_values(&a, 0, 255);
  lv_anim_set_duration(&a, 130);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

static void click() {
  settingsLock();
  bool s = gSet.sound;
  settingsUnlock();
  if (s) audioClick();
}

// ------------------------------------------------------------------ вперёд
static void buildPage();
static void refreshAll(bool force);
static void updateNav();
static void pollBattery();
static void applyRequests();

// ------------------------------------------------------------------ колбэки
static void tabCb(lv_event_t* e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i == page) return;
  page = i;
  pendingBuild = true;
  click();
}

static void graphModeCb(lv_event_t* e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i == gmode) return;
  gmode = i;
  pendingBuild = true;
  click();
}

static void volChanged(lv_event_t*) {
  if (!W.volSlider) return;
  volHoldUntil = millis() + 2000;
  int v = lv_slider_get_value(W.volSlider);
  if (W.volVal) lv_label_set_text_fmt(W.volVal, "%d%%", v);
}

static void volReleased(lv_event_t*) {
  if (!W.volSlider) return;
  int v = lv_slider_get_value(W.volSlider);
  monitorRequestVolume(v);
  click();
}

static void volStep(lv_event_t* e) {
  int d = (int)(intptr_t)lv_event_get_user_data(e);
  PcStats pc = monitorSnapshot();
  int v = constrain(pc.volume + d, 0, 100);
  monitorRequestVolume(v);
  volHoldUntil = millis() + 2000;
  if (W.volSlider) lv_slider_set_value(W.volSlider, v, LV_ANIM_ON);
  if (W.volVal) lv_label_set_text_fmt(W.volVal, "%d%%", v);
  click();
}

static void volMuteCb(lv_event_t*) {
  PcStats pc = monitorSnapshot();
  monitorRequestMute(!pc.muted);
  volHoldUntil = millis() + 2000;
  click();
}

static void speedCb(lv_event_t*) {
  monitorRequestSpeedtest();
  uiToast("Тест скорости запущен");
  click();
}

static void themeCb(lv_event_t* e) {
  pendingTheme = (int)(intptr_t)lv_event_get_user_data(e);
  click();
}

static void brightChanged(lv_event_t*) {
  if (!W.brightSlider) return;
  int v = lv_slider_get_value(W.brightSlider);
  settingsLock();
  gSet.brightness = (uint8_t)constrain(v, 5, 100);
  settingsUnlock();
  displayBacklight(gSet.brightness);
  if (W.brightVal) lv_label_set_text_fmt(W.brightVal, "%d%%", gSet.brightness);
}

static void brightReleased(lv_event_t*) {
  settingsSave();
  click();
}

static void dimCb(lv_event_t* e) {
  static const uint16_t vals[4] = {30, 60, 300, 0};
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  settingsLock();
  gSet.dimSec = vals[i];
  settingsUnlock();
  settingsSave();
  pendingBuild = true;
  click();
}

static void soundCb(lv_event_t*) {
  settingsLock();
  gSet.sound = !gSet.sound;
  settingsUnlock();
  settingsSave();
  if (W.soundLbl) lv_label_set_text(W.soundLbl, gSet.sound ? "Звук: вкл" : "Звук: выкл");
  if (gSet.sound) audioClick();
}

static void sdCb(lv_event_t*) {
  settingsLock();
  bool want = !gSet.sdLog;
  settingsUnlock();
  if (want && !sdLogActive()) {
    if (!sdLogBegin()) {
      uiToast("Карта microSD не найдена");
      return;
    }
  }
  if (!want) sdLogEnd();
  settingsLock();
  gSet.sdLog = want;
  settingsUnlock();
  settingsSave();
  if (W.sdLbl) lv_label_set_text(W.sdLbl, want ? "SD-лог: вкл" : "SD-лог: выкл");
  uiToast(want ? "Журнал на SD включён" : "Журнал на SD выключен");
  click();
}

static void findAgentCb(lv_event_t*) {
  monitorForgetAgent();
  pendingBuild = true;
  uiToast("Ищу агент в сети…");
  click();
}

static void wifiSetupCb(lv_event_t*) {
  portalStartAp();
  uiShowOverlay("Точка доступа",
                "Сеть: " + portalApName() + "\nПароль: " + portalApPass() + "\nАдрес: http://" + portalUrl() + "/",
                true);
  click();
}

static void rebootCb(lv_event_t*) {
  rebootAt = millis() + 400;
  uiToast("Перезагрузка…");
  click();
}

static void overlayCloseCb(lv_event_t*) { uiHideOverlay(); }

static void goHomeCb(lv_event_t*) {
  home = true;
  pendingRebuild = true;
  click();
}

static void homeClickCb(lv_event_t*) {
  if (millis() - gestureAt < 600) return;   // это был свайп, а не касание
  // касание погоды открывает прогноз, касание прогноза — возвращает назад
  if (fcShown || nightShown) {
    fcShown = nightShown = false;
    pendingRebuild = true;
    click();
    return;
  }
  lv_point_t pt;
  lv_indev_get_point(lv_indev_active(), &pt);
  if (skinWeatherHit(pt.x, pt.y)) {
    fcShown = true;
    fcAt = millis();
    pendingRebuild = true;
    click();
    return;
  }
  if (skinClockHit(pt.x, pt.y)) {   // касание часов — отчёт за 12 часов
    nightShown = true;
    fcAt = millis();
    pendingRebuild = true;
    click();
    return;
  }
  home = false;
  pendingRebuild = true;
  click();
}

static void nextSkin(int dir) {
  int n = skinCount();
  settingsLock();
  gSet.skin = (uint8_t)((gSet.skin + n + dir) % n);
  settingsUnlock();
  settingsSave();
}

// Кнопки медиапанели
static void mediaKey(int k) {
  click();
  if (k >= 0 && k <= 2) {
    monitorRequestMedia(k);
  } else if (k == 3 || k == 5) {
    PcStats pc = monitorSnapshot();
    monitorRequestVolume(constrain(pc.volume + (k == 3 ? -5 : 5), 0, 100));
  } else if (k == 4) {
    monitorRequestMute(!monitorSnapshot().muted);
  } else if (k == 9) {
    mediaShown = launchShown = false;
    pendingRebuild = true;
  } else if (k >= 100 && k < 100 + APP_MAX) {
    monitorRequestLaunch(k - 100);
  }
}

static void mediaGestureCb(lv_event_t*) {
  lv_indev_t* in = lv_indev_active();
  lv_dir_t d = lv_indev_get_gesture_dir(in);
  if (d != LV_DIR_TOP && d != LV_DIR_BOTTOM) return;
  mediaShown = launchShown = false;
  pendingRebuild = true;
  lv_indev_wait_release(in);
  click();
}

static void homeGestureCb(lv_event_t*) {
  lv_indev_t* in = lv_indev_active();
  lv_dir_t d = lv_indev_get_gesture_dir(in);
  if (d == LV_DIR_TOP || d == LV_DIR_BOTTOM) {   // свайп вверх — медиапанель, вниз — быстрый запуск
    gestureAt = millis();
    fcShown = nightShown = false;
    mediaShown = d == LV_DIR_TOP;
    launchShown = d == LV_DIR_BOTTOM;
    pendingRebuild = true;
    lv_indev_wait_release(in);
    click();
    return;
  }
  if (d != LV_DIR_LEFT && d != LV_DIR_RIGHT) return;
  gestureAt = millis();
  nextSkin(d == LV_DIR_LEFT ? 1 : -1);
  toastSkin = true;
  pendingRebuild = true;
  lv_indev_wait_release(in);
  click();
}

static void flipCb(lv_event_t*) {
  settingsLock();
  gSet.flip = !gSet.flip;
  bool f = gSet.flip;
  settingsUnlock();
  settingsSave();
  displaySetFlip(f);
  if (W.flipLbl) lv_label_set_text(W.flipLbl, f ? "Экран: 180°" : "Экран: 0°");
  click();
}

static void alertCb(lv_event_t*) {
  settingsLock();
  gSet.alertOn = !gSet.alertOn;
  bool on = gSet.alertOn;
  settingsUnlock();
  settingsSave();
  if (W.alertLbl) lv_label_set_text(W.alertLbl, on ? "Тревоги: вкл" : "Тревоги: выкл");
  click();
}

// ------------------------------------------------------------------ страницы
static Gauge makeGauge(lv_obj_t* p, int x, int y, int w, int h, const char* title) {
  Gauge g;
  g.card = card(p, x, y, w, h);
  g.arc = lv_arc_create(g.card);
  lv_obj_set_size(g.arc, 58, 58);
  lv_obj_align(g.arc, LV_ALIGN_TOP_MID, 0, 5);
  lv_arc_set_range(g.arc, 0, 100);
  lv_arc_set_bg_angles(g.arc, 135, 405);
  lv_arc_set_value(g.arc, 0);
  lv_obj_remove_flag(g.arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(g.arc, LV_OPA_TRANSP, 0);
  lv_obj_set_style_arc_width(g.arc, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_color(g.arc, C(TH.card2), LV_PART_MAIN);
  lv_obj_set_style_arc_width(g.arc, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(g.arc, C(TH.accent), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(g.arc, true, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(g.arc, LV_OPA_TRANSP, LV_PART_KNOB);
  g.val = txt(g.card, 0, 28, "—", fMedB, TH.text, w, LV_TEXT_ALIGN_CENTER);
  g.title = txt(g.card, 0, 61, title, fTiny, TH.muted, w, LV_TEXT_ALIGN_CENTER);
  g.detail = one(txt(g.card, 2, 75, "", fTiny, TH.muted, w - 4, LV_TEXT_ALIGN_CENTER));
  return g;
}

static void setGauge(Gauge& g, float pct, const char* val, const char* det, uint32_t col) {
  if (!g.arc) return;
  animArc(g.arc, constrain((int)lroundf(pct), 0, 100));
  lv_obj_set_style_arc_color(g.arc, C(col), LV_PART_INDICATOR);
  if (g.val) lv_label_set_text(g.val, val);
  if (g.detail) lv_label_set_text(g.detail, det);
}

static void buildOverview() {
  static const char* titles[4] = {"CPU", "ОЗУ", "GPU", "ДИСК"};
  for (int i = 0; i < 4; i++) W.gz[i] = makeGauge(content, 4 + i * 78, 0, 74, 92, titles[i]);

  lv_obj_t* net = card(content, 4, 98, 198, 82);
  ico(net, 6, 9, LV_SYMBOL_DOWNLOAD, TH.accent, &lv_font_montserrat_12);
  W.downLbl = txt(net, 20, 5, "—", fMedB, TH.accent);
  txt(net, 64, 8, "Мбит/с", fTiny, TH.muted);
  ico(net, 6, 31, LV_SYMBOL_UPLOAD, TH.accent2, &lv_font_montserrat_12);
  W.upLbl = txt(net, 20, 27, "—", fMedB, TH.accent2);
  txt(net, 64, 30, "Мбит/с", fTiny, TH.muted);
  W.totDown = one(txt(net, 6, 50, "—", fTiny, TH.muted, 98));
  W.totUp = one(txt(net, 6, 64, "—", fTiny, TH.muted, 98));
  W.downChart = newChart(net, 110, 5, 82, 34, 60, false, false);
  W.downSer = addSer(W.downChart, TH.accent);
  W.upChart = newChart(net, 110, 43, 82, 34, 60, false, false);
  W.upSer = addSer(W.upChart, TH.accent2);

  lv_obj_t* vol = card(content, 206, 98, 110, 82);
  txt(vol, 0, 1, "ГРОМКОСТЬ", fTiny, TH.muted, 110, LV_TEXT_ALIGN_CENTER);
  W.volVal = txt(vol, 0, 16, "—", fHugeB, TH.text, 110, LV_TEXT_ALIGN_CENTER);
  W.volSlider = lv_slider_create(vol);
  lv_obj_set_pos(W.volSlider, 9, 44);
  lv_obj_set_size(W.volSlider, 92, 8);
  lv_slider_set_range(W.volSlider, 0, 100);
  lv_slider_set_value(W.volSlider, 0, LV_ANIM_OFF);
  styleSlider(W.volSlider, TH.accent);
  lv_obj_add_event_cb(W.volSlider, volChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(W.volSlider, volReleased, LV_EVENT_RELEASED, nullptr);

  lv_obj_t* minus = makeBtn(vol, 9, 58, 28, 20, TH.card2, TH.line);
  centerTxt(minus, "−", fMedB, TH.text);
  lv_obj_add_event_cb(minus, volStep, LV_EVENT_CLICKED, (void*)(intptr_t)-5);

  lv_obj_t* mute = makeBtn(vol, 41, 58, 28, 20, TH.card2, TH.line);
  W.volMuteIcon = centerTxt(mute, LV_SYMBOL_VOLUME_MAX, &lv_font_montserrat_12, TH.text);
  lv_obj_add_event_cb(mute, volMuteCb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* plus = makeBtn(vol, 73, 58, 28, 20, TH.card2, TH.line);
  centerTxt(plus, "+", fMedB, TH.text);
  lv_obj_add_event_cb(plus, volStep, LV_EVENT_CLICKED, (void*)(intptr_t)5);
}

static void buildGraph() {
  static const char* names[5] = {"CPU", "ОЗУ", "GPU", "ДИСК", "СЕТЬ"};
  for (int i = 0; i < 5; i++) {
    bool act = i == gmode;
    lv_obj_t* b = makeBtn(content, 4 + i * 62, 0, 60, 22, act ? TH.accent : TH.card, act ? TH.accent : TH.line, 10);
    lv_obj_t* l = centerTxt(b, names[i], fTiny, act ? TH.btnTxt : TH.text);
    (void)l;
    lv_obj_add_event_cb(b, graphModeCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    W.chips[i] = b;
  }

  W.chart = newChart(content, 4, 26, 312, 120, HIST_N, true, true);
  uint32_t c1 = gmode == 0 ? TH.accent : gmode == 1 ? TH.accent2 : gmode == 2 ? TH.good : gmode == 3 ? TH.warn : TH.accent;
  W.ser1 = addSer(W.chart, c1);
  if (gmode == 4) W.ser2 = addSer(W.chart, TH.accent2);

  W.now = card(content, 244, 30, 68, 18, 9);
  lv_obj_set_style_border_color(W.now, C(c1), 0);
  W.nowLbl = centerTxt(W.now, "—", fSmallB, c1);

  W.info = txt(content, 4, 152, "нет данных", fTiny, TH.muted, 312);
  lv_label_set_long_mode(W.info, LV_LABEL_LONG_WRAP);
}

static void buildNetwork() {
  lv_obj_t* wc = card(content, 4, 0, 148, 182);
  ico(wc, 8, 4, LV_SYMBOL_WIFI, TH.accent, &lv_font_montserrat_12);
  txt(wc, 24, 5, "WI-FI ЭКРАНА", fTiny, TH.muted);
  W.wq = txt(wc, 8, 18, "—", fGiantB, TH.accent);
  W.wdbm = one(txt(wc, 8, 50, "нет сети", fSmall, TH.text, 132));
  static const int hs[4] = {7, 11, 15, 19};
  makeBars(wc, 8, 84, 5, 4, hs, W.wbars);
  W.wssid = one(txt(wc, 8, 88, "", fSmall, TH.text, 132));
  W.wip = one(txt(wc, 8, 104, "", fTiny, TH.muted, 132));
  W.wlink = one(txt(wc, 8, 118, "", fTiny, TH.muted, 132));
  W.wpc = one(txt(wc, 8, 132, "", fTiny, TH.muted, 132));
  W.rssiChart = newChart(wc, 8, 150, 132, 26, 60, false, false);
  W.rssiSer = addSer(W.rssiChart, TH.accent);

  lv_obj_t* nc = card(content, 156, 0, 160, 182);
  ico(nc, 8, 4, LV_SYMBOL_GPS, TH.accent2, &lv_font_montserrat_12);
  txt(nc, 24, 5, "ИНТЕРНЕТ", fTiny, TH.muted);
  W.testBtn = makeBtn(nc, 8, 18, 144, 26, TH.accent, TH.accent, 12);
  accentBtn(W.testBtn);
  W.testLbl = centerTxt(W.testBtn, "ТЕСТ СКОРОСТИ", fSmallB, TH.btnTxt);
  lv_obj_add_event_cb(W.testBtn, speedCb, LV_EVENT_CLICKED, nullptr);

  W.prog = lv_bar_create(nc);
  lv_obj_set_pos(W.prog, 8, 50);
  lv_obj_set_size(W.prog, 144, 5);
  lv_bar_set_range(W.prog, 0, 100);
  lv_bar_set_value(W.prog, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(W.prog, C(TH.card2), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(W.prog, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(W.prog, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(W.prog, C(TH.accent), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_color(W.prog, C(TH.accent2), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_dir(W.prog, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
  lv_obj_set_style_radius(W.prog, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);

  W.stage = one(txt(nc, 8, 56, "готов к тесту", fTiny, TH.muted, 144));
  ico(nc, 8, 74, LV_SYMBOL_DOWNLOAD, TH.accent, &lv_font_montserrat_14);
  W.dlLbl = txt(nc, 24, 70, "—", fHugeB, TH.accent);
  txt(nc, 24, 94, "Мбит/с ↓", fTiny, TH.muted);
  ico(nc, 92, 74, LV_SYMBOL_UPLOAD, TH.accent2, &lv_font_montserrat_12);
  W.ulLbl = txt(nc, 106, 74, "—", fBigB, TH.accent2);
  txt(nc, 106, 94, "Мбит/с ↑", fTiny, TH.muted);
  W.pingLbl = one(txt(nc, 8, 108, "пинг —", fSmall, TH.text, 144));
  W.lanLbl = one(txt(nc, 8, 123, "LAN —", fTiny, TH.muted, 144));
  W.stime = one(txt(nc, 8, 137, "ещё не тестировали", fTiny, TH.muted, 144));
  W.topLbl = txt(nc, 8, 151, "", fTiny, TH.muted, 144);
  lv_label_set_long_mode(W.topLbl, LV_LABEL_LONG_WRAP);
}

static void buildSettings() {
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(content, 4, 0);
  lv_obj_set_style_pad_row(content, 4, 0);
  lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_bg_color(content, C(TH.accent), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(content, LV_OPA_50, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(content, 3, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(content, 2, LV_PART_SCROLLBAR);

  txt(content, 2, 0, "СКИН", fTiny, TH.muted);
  lv_obj_t* tr = obj(content, 0, 0, 312, 36);
  lv_obj_set_flex_flow(tr, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(tr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  {
    const int n = skinCount();
    lv_obj_t* prev = makeBtn(tr, 0, 0, 52, 32, TH.card, TH.line, 8);
    lv_obj_center(ico(prev, 0, 0, LV_SYMBOL_LEFT, TH.accent, &lv_font_montserrat_14));
    lv_obj_add_event_cb(prev, themeCb, LV_EVENT_CLICKED, (void*)(intptr_t)((themeIdx + n - 1) % n));
    lv_obj_t* mid = makeBtn(tr, 0, 0, 196, 32, TH.card2, TH.accent, 8);
    lv_obj_remove_flag(mid, LV_OBJ_FLAG_CLICKABLE);
    centerTxt(mid, skinName(themeIdx), fMedB, TH.text);
    lv_obj_t* next = makeBtn(tr, 0, 0, 52, 32, TH.card, TH.line, 8);
    lv_obj_center(ico(next, 0, 0, LV_SYMBOL_RIGHT, TH.accent, &lv_font_montserrat_14));
    lv_obj_add_event_cb(next, themeCb, LV_EVENT_CLICKED, (void*)(intptr_t)((themeIdx + 1) % n));
  }

  lv_obj_t* br = card(content, 0, 0, 312, 30);
  ico(br, 8, 7, LV_SYMBOL_EYE_OPEN, TH.accent, &lv_font_montserrat_14);
  txt(br, 28, 9, "ЯРКОСТЬ", fTiny, TH.muted);
  W.brightSlider = lv_slider_create(br);
  lv_obj_set_pos(W.brightSlider, 92, 11);
  lv_obj_set_size(W.brightSlider, 170, 8);
  lv_slider_set_range(W.brightSlider, 5, 100);
  lv_slider_set_value(W.brightSlider, gSet.brightness, LV_ANIM_OFF);
  styleSlider(W.brightSlider, TH.accent);
  lv_obj_add_event_cb(W.brightSlider, brightChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(W.brightSlider, brightReleased, LV_EVENT_RELEASED, nullptr);
  W.brightVal = txt(br, 268, 9, "", fSmallB, TH.text, 38, LV_TEXT_ALIGN_RIGHT);

  lv_obj_t* dr = card(content, 0, 0, 312, 30);
  ico(dr, 8, 7, LV_SYMBOL_POWER, TH.accent2, &lv_font_montserrat_14);
  txt(dr, 28, 9, "ЭКРАН", fTiny, TH.muted);
  static const uint16_t vals[4] = {30, 60, 300, 0};
  static const char* names[4] = {"30 с", "1 мин", "5 мин", "не гас"};
  for (int i = 0; i < 4; i++) {
    bool act = gSet.dimSec == vals[i];
    lv_obj_t* b = makeBtn(dr, 78 + i * 58, 4, 54, 22, act ? TH.accent : TH.card2, act ? TH.accent : TH.line, 8);
    centerTxt(b, names[i], fTiny, act ? TH.btnTxt : TH.text);
    lv_obj_add_event_cb(b, dimCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    W.dimBtn[i] = b;
  }

  lv_obj_t* tg = obj(content, 0, 0, 312, 28);
  lv_obj_t* sb = makeBtn(tg, 0, 0, 152, 28, TH.card, TH.line);
  W.soundLbl = centerTxt(sb, gSet.sound ? "Звук: вкл" : "Звук: выкл", fSmallB, TH.text);
  lv_obj_add_event_cb(sb, soundCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* db = makeBtn(tg, 160, 0, 152, 28, TH.card, TH.line);
  W.sdLbl = centerTxt(db, gSet.sdLog ? "SD-лог: вкл" : "SD-лог: выкл", fSmallB, TH.text);
  lv_obj_add_event_cb(db, sdCb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* rg = obj(content, 0, 0, 312, 28);
  lv_obj_t* fbtn = makeBtn(rg, 0, 0, 152, 28, TH.card, TH.line);
  W.flipLbl = centerTxt(fbtn, gSet.flip ? "Экран: 180°" : "Экран: 0°", fSmallB, TH.text);
  lv_obj_add_event_cb(fbtn, flipCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* abtn = makeBtn(rg, 160, 0, 152, 28, TH.card, TH.line);
  W.alertLbl = centerTxt(abtn, gSet.alertOn ? "Тревоги: вкл" : "Тревоги: выкл", fSmallB, TH.text);
  lv_obj_add_event_cb(abtn, alertCb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* ag = card(content, 0, 0, 312, 48);
  W.agentDot = obj(ag, 8, 7, 8, 8);
  lv_obj_set_style_radius(W.agentDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(W.agentDot, LV_OPA_COVER, 0);
  txt(ag, 22, 5, "АГЕНТ НА ПК", fTiny, TH.muted);
  W.agentLbl = txt(ag, 8, 18, "", fSmall, TH.text, 190);
  lv_obj_t* fb = makeBtn(ag, 204, 4, 50, 20, TH.card2, TH.line);
  centerTxt(fb, "Найти", fTiny, TH.text);
  lv_obj_add_event_cb(fb, findAgentCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* wb = makeBtn(ag, 258, 4, 48, 20, TH.card2, TH.line);
  centerTxt(wb, "Wi-Fi", fTiny, TH.text);
  lv_obj_add_event_cb(wb, wifiSetupCb, LV_EVENT_CLICKED, nullptr);
  txt(ag, 204, 28, "порт 8123 · UDP 4210", fTiny, TH.muted, 102);

  W.infoSet = txt(content, 0, 0, "", fTiny, TH.muted, 312);
  lv_label_set_long_mode(W.infoSet, LV_LABEL_LONG_WRAP);

  lv_obj_t* rb = makeBtn(content, 0, 0, 140, 28, TH.bad, TH.bad, 12);
  centerTxt(rb, "ПЕРЕЗАГРУЗКА", fSmallB, TH.btnTxt);
  lv_obj_add_event_cb(rb, rebootCb, LV_EVENT_CLICKED, nullptr);
}

// ------------------------------------------------------------------ сборка
static void buildTop() {
  lv_obj_t* bar = obj(scr, 0, 0, LCD_W, TOP_H);
  lv_obj_set_style_bg_color(bar, C(TH.bg1), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(bar, C(TH.line), 0);

  titleLbl = txt(bar, 6, 0, "PC MONITOR", fSmallB, TH.accent);
  lv_obj_set_style_text_letter_space(titleLbl, 1, 0);
  hostLbl = one(txt(bar, 6, 12, "нет связи", fTiny, TH.muted, 104));
  timeLbl = txt(bar, 116, 0, "--:--", fSmallB, TH.text, 70, LV_TEXT_ALIGN_CENTER);
  dateLbl = txt(bar, 116, 12, "", fTiny, TH.muted, 70, LV_TEXT_ALIGN_CENTER);

  static const int hs[4] = {4, 7, 10, 13};
  makeBars(bar, 196, 20, 3, 3, hs, wBars);
  rssiLbl = txt(bar, 216, 6, "—", fTiny, TH.muted, 28);

  batBox = obj(bar, 250, 7, 22, 10);
  lv_obj_set_style_border_width(batBox, 1, 0);
  lv_obj_set_style_border_color(batBox, C(TH.muted), 0);
  lv_obj_set_style_radius(batBox, 2, 0);
  batFill = obj(batBox, 1, 1, 18, 6);
  lv_obj_set_style_bg_color(batFill, C(TH.good), 0);
  lv_obj_set_style_bg_opa(batFill, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(batFill, 1, 0);
  lv_obj_t* cap = obj(bar, 272, 9, 2, 4);
  lv_obj_set_style_bg_color(cap, C(TH.muted), 0);
  lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, 0);
  batLbl = txt(bar, 278, 6, "", fTiny, TH.muted, 34);
  powerLbl = txt(bar, 250, 6, "USB", fTiny, TH.muted, 60);
  lv_obj_add_flag(powerLbl, LV_OBJ_FLAG_HIDDEN);
}

static void buildNav() {
  lv_obj_t* nav = obj(scr, 0, NAV_Y, LCD_W, LCD_H - NAV_Y);
  lv_obj_set_style_bg_color(nav, C(TH.bg1), 0);
  lv_obj_set_style_bg_opa(nav, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(nav, 1, 0);
  lv_obj_set_style_border_side(nav, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_color(nav, C(TH.line), 0);

  static const char* icons[4] = {LV_SYMBOL_HOME, LV_SYMBOL_BARS, LV_SYMBOL_WIFI, LV_SYMBOL_SETTINGS};
  static const char* names[4] = {"Обзор", "Графики", "Сеть", "Ещё"};
  lv_obj_t* hb = obj(nav, 2, 2, 40, 22);
  lv_obj_set_style_radius(hb, 8, 0);
  lv_obj_set_style_bg_color(hb, C(TH.card2), 0);
  lv_obj_set_style_bg_opa(hb, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(hb, 1, 0);
  lv_obj_set_style_border_color(hb, C(TH.accent), 0);
  lv_obj_add_flag(hb, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(hb, 12);
  lv_obj_t* hi = ico(hb, 0, 0, LV_SYMBOL_LEFT, TH.accent, &lv_font_montserrat_12);
  lv_obj_center(hi);
  lv_obj_add_event_cb(hb, goHomeCb, LV_EVENT_CLICKED, nullptr);

  for (int i = 0; i < 4; i++) {
    lv_obj_t* t = obj(nav, 44 + i * 69, 2, 68, 22);
    lv_obj_set_style_radius(t, 8, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(t, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(t, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(t, 4, 0);
    tabIcon[i] = ico(t, 0, 0, icons[i], TH.muted, &lv_font_montserrat_12);
    tabLbl[i] = txt(t, 0, 0, names[i], fTiny, TH.muted);
    lv_obj_set_ext_click_area(t, 12);  // нижний ряд у самого края стекла
    lv_obj_add_event_cb(t, tabCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    tabObj[i] = t;
  }
  updateNav();
}

static void updateNav() {
  for (int i = 0; i < 4; i++) {
    if (!tabObj[i]) continue;
    bool act = i == page;
    lv_obj_set_style_bg_color(tabObj[i], C(act ? TH.card2 : TH.bg1), 0);
    lv_obj_set_style_bg_grad_color(tabObj[i], C(act ? TH.card : TH.bg1), 0);
    lv_obj_set_style_bg_grad_dir(tabObj[i], act ? LV_GRAD_DIR_VER : LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_border_width(tabObj[i], act ? 1 : 0, 0);
    lv_obj_set_style_border_color(tabObj[i], C(act ? TH.accent : TH.line), 0);
    if (tabIcon[i]) lv_obj_set_style_text_color(tabIcon[i], C(act ? TH.accent : TH.muted), 0);
    if (tabLbl[i]) lv_obj_set_style_text_color(tabLbl[i], C(act ? TH.text : TH.muted), 0);
  }
}

// ------------------------------------------------------------------ калибровка сенсора
// 4 крестика по углам; для каждого — среднее сырых координат, затем прямые по МНК:
// x = a·rawY + b, y = c·rawX + d (в координатах экрана без поворота).
static const int CAL_PT[4][2] = {{30, 30}, {290, 30}, {290, 210}, {30, 210}};
static struct {
  lv_timer_t* timer;
  lv_obj_t *cross, *msg;
  int idx, n;
  long sx, sy;
  bool waitUp;
  float raw[4][2];
  uint32_t doneAt;
} CAL;

static void calPlace() {
  lv_obj_set_pos(CAL.cross, CAL_PT[CAL.idx][0] - 15, CAL_PT[CAL.idx][1] - 15);
  lv_label_set_text_fmt(CAL.msg, "Калибровка сенсора\nКоснитесь центра крестика (%d из 4)", CAL.idx + 1);
}

static bool calFit() {
  bool fl = gSet.flip;
  float mx = 0, my = 0, mrx = 0, mry = 0;
  float ux[4], uy[4];
  for (int i = 0; i < 4; i++) {
    ux[i] = fl ? LCD_W - 1 - CAL_PT[i][0] : CAL_PT[i][0];
    uy[i] = fl ? LCD_H - 1 - CAL_PT[i][1] : CAL_PT[i][1];
    mx += ux[i] / 4; my += uy[i] / 4; mrx += CAL.raw[i][0] / 4; mry += CAL.raw[i][1] / 4;
  }
  float cxy = 0, vy = 0, cyx = 0, vx = 0;
  for (int i = 0; i < 4; i++) {
    cxy += (CAL.raw[i][1] - mry) * (ux[i] - mx);
    vy += (CAL.raw[i][1] - mry) * (CAL.raw[i][1] - mry);
    cyx += (CAL.raw[i][0] - mrx) * (uy[i] - my);
    vx += (CAL.raw[i][0] - mrx) * (CAL.raw[i][0] - mrx);
  }
  if (vy < 1 || vx < 1) return false;
  float a = cxy / vy, c = cyx / vx;
  if (fabsf(a) < 0.3f || fabsf(a) > 3 || fabsf(c) < 0.3f || fabsf(c) > 3) return false;
  settingsLock();
  gSet.calOk = true;
  gSet.calXA = a;
  gSet.calXB = mx - a * mry;
  gSet.calYA = c;
  gSet.calYB = my - c * mrx;
  settingsUnlock();
  settingsSave();
  displaySetCal(true, gSet.calXA, gSet.calXB, gSet.calYA, gSet.calYB);
  return true;
}

static void calTick(lv_timer_t*) {
  if (CAL.doneAt) {
    if (millis() - CAL.doneAt > 1800) {
      displayTouchHold(false);
      calShown = false;
      pendingRebuild = true;
      CAL.doneAt = 0;
    }
    return;
  }
  int rx, ry;
  bool down = displayTouchRaw(&rx, &ry);
  uiActivity();
  if (CAL.waitUp) {
    if (!down) CAL.waitUp = false;
    return;
  }
  if (down) {
    CAL.sx += rx;
    CAL.sy += ry;
    CAL.n++;
    return;
  }
  if (CAL.n < 3) return;
  CAL.raw[CAL.idx][0] = (float)CAL.sx / CAL.n;
  CAL.raw[CAL.idx][1] = (float)CAL.sy / CAL.n;
  CAL.sx = CAL.sy = 0;
  CAL.n = 0;
  click();
  if (++CAL.idx < 4) {
    calPlace();
    return;
  }
  bool ok = calFit();
  lv_obj_add_flag(CAL.cross, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(CAL.msg, ok ? "Готово! Сенсор откалиброван" : "Не получилось — запустите калибровку ещё раз");
  CAL.doneAt = millis();
}

static void buildCalibration() {
  lv_obj_set_style_bg_color(scr, C(0x0B0D12), 0);
  lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_NONE, 0);
  CAL.msg = lv_label_create(scr);
  lv_obj_set_width(CAL.msg, 280);
  lv_obj_set_style_text_align(CAL.msg, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(CAL.msg, C(0xF2F4F7), 0);
  lv_obj_set_style_text_font(CAL.msg, fMedB, 0);
  lv_obj_align(CAL.msg, LV_ALIGN_CENTER, 0, 0);
  CAL.cross = obj(scr, 0, 0, 30, 30);
  lv_obj_set_style_bg_opa(CAL.cross, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(CAL.cross, 2, 0);
  lv_obj_set_style_border_color(CAL.cross, C(0xFF9F1C), 0);
  lv_obj_set_style_radius(CAL.cross, LV_RADIUS_CIRCLE, 0);
  lv_obj_t* h = obj(CAL.cross, -2, 12, 30, 2);
  lv_obj_set_style_bg_color(h, C(0xFF9F1C), 0);
  lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
  lv_obj_t* v = obj(CAL.cross, 12, -2, 2, 30);
  lv_obj_set_style_bg_color(v, C(0xFF9F1C), 0);
  lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);
  CAL.idx = CAL.n = 0;
  CAL.sx = CAL.sy = 0;
  CAL.waitUp = true;
  CAL.doneAt = 0;
  calPlace();
  displayTouchHold(true);
  CAL.timer = lv_timer_create(calTick, 20, nullptr);
}

static void buildAll() {
  settingsLock();
  themeIdx = constrain((int)gSet.skin, 0, skinCount() - 1);
  settingsUnlock();
  TH = THEMES[themeIdx];
  scr = lv_obj_create(NULL);
  lv_obj_set_size(scr, LCD_W, LCD_H);
  setGrad(scr, TH.bg1, TH.bg2);
  lv_obj_set_style_radius(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_shadow_width(scr, 0, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_screen_load(scr);
  if (CAL.timer) {
    lv_timer_delete(CAL.timer);
    CAL.timer = nullptr;
  }
  if (calShown) {
    buildCalibration();
    return;
  }
  if (home) {
    settingsLock();
    int s = gSet.skin;
    settingsUnlock();
    if (launchShown) {
      skinBuildLaunch(scr, s, mediaKey);
      lv_obj_add_event_cb(scr, mediaGestureCb, LV_EVENT_GESTURE, nullptr);
      skinUpdate(monitorSnapshot(), monitorWifi());
      return;
    }
    if (mediaShown) {
      skinBuildMedia(scr, s, mediaKey);
      lv_obj_add_event_cb(scr, mediaGestureCb, LV_EVENT_GESTURE, nullptr);
      skinUpdate(monitorSnapshot(), monitorWifi());
      return;
    }
    if (nightShown) skinBuildNight(scr, s);
    else if (fcShown) skinBuildForecast(scr, s);
    else if (idleShown) skinBuildIdle(scr, s);
    else skinBuild(scr, s);
    // прозрачный слой поверх скина: касание — меню, свайп — другой скин
    lv_obj_t* c = obj(scr, 0, 0, LCD_W, LCD_H);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(c, homeClickCb, LV_EVENT_SHORT_CLICKED, nullptr);
    lv_obj_add_event_cb(c, homeGestureCb, LV_EVENT_GESTURE, nullptr);
    skinUpdate(monitorSnapshot(), monitorWifi());
    return;
  }
  buildTop();
  buildNav();
  buildPage();
}

static void rebuildAll() {
  uiHideOverlay();
  if (toast) {
    lv_obj_del(toast);
    toast = nullptr;
  }
  lv_anim_del(NULL, NULL);
  skinClear();
  lv_obj_t* old = scr;
  scr = nullptr;
  content = nullptr;
  W = {};
  titleLbl = hostLbl = timeLbl = dateLbl = rssiLbl = batBox = batFill = batLbl = powerLbl = nullptr;
  for (int i = 0; i < 4; i++) wBars[i] = tabObj[i] = tabIcon[i] = tabLbl[i] = nullptr;
  buildAll();              // новый экран уже загружен — старый можно удалять
  if (old) lv_obj_del(old);
}

static void buildPage() {
  lv_anim_del(NULL, arcExec);
  lv_anim_del(NULL, fadeExec);
  if (content) lv_obj_del(content);
  W = {};
  content = obj(scr, 0, CONTENT_Y, LCD_W, CONTENT_H);
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
  switch (page) {
    case 1: buildGraph(); break;
    case 2: buildNetwork(); break;
    case 3: buildSettings(); break;
    default: buildOverview(); break;
  }
  updateNav();
  fadeIn(content);
  refreshAll(true);
}

// ------------------------------------------------------------------ обновление
static int lipoPercent(float v) {
  static const float tbl[][2] = {{4.15f, 100}, {4.05f, 90}, {3.97f, 80}, {3.90f, 70}, {3.84f, 60}, {3.80f, 50},
                                 {3.77f, 40}, {3.74f, 30}, {3.70f, 20}, {3.65f, 10}, {3.50f, 3}, {3.30f, 0}};
  if (v >= tbl[0][0]) return 100;
  for (int i = 1; i < 12; i++) {
    if (v >= tbl[i][0]) {
      float k = (v - tbl[i][0]) / (tbl[i - 1][0] - tbl[i][0]);
      return (int)lroundf(tbl[i][1] + k * (tbl[i - 1][1] - tbl[i][1]));
    }
  }
  return 0;
}

static void pollBattery() {
  static uint32_t next = 0;
  if ((int32_t)(millis() - next) < 0) return;
  next = millis() + 5000;
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(BAT_PIN);
  float v = mv / 8.0f / 1000.0f * BAT_DIVIDER;
  int pct = -1;
  if (v > 2.7f && v < 4.35f) pct = lipoPercent(v);
  if (!batBox) return;
  if (pct >= 0) {
    lv_obj_remove_flag(batBox, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(batFill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(powerLbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(batFill, max(2, 18 * pct / 100));
    lv_obj_set_style_bg_color(batFill, C(pct > 50 ? TH.good : pct > 20 ? TH.warn : TH.bad), 0);
    lv_label_set_text_fmt(batLbl, "%d%%", pct);
  } else {
    lv_obj_add_flag(batBox, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(batFill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(powerLbl, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(batLbl, "");
  }
}

static void refreshTop(const PcStats& pc, const WifiInfo& w) {
  if (!titleLbl) return;
  if (pc.online) {
    lv_obj_set_style_text_color(titleLbl, C(TH.accent), 0);
    lv_label_set_text(hostLbl, pc.host);
  } else if (w.connected) {
    lv_obj_set_style_text_color(titleLbl, C(TH.warn), 0);
    lv_label_set_text(hostLbl, monitorAgentConfigured() ? "агент молчит" : "ищу агент…");
  } else {
    lv_obj_set_style_text_color(titleLbl, C(TH.muted), 0);
    lv_label_set_text(hostLbl, w.apMode ? w.apSsid : "жду ПК по USB");
  }

  time_t t = time(nullptr);
  if (t > 1700000000) {
    struct tm tm;
    localtime_r(&t, &tm);
    char b[8];
    strftime(b, sizeof(b), "%H:%M", &tm);
    lv_label_set_text(timeLbl, b);
    strftime(b, sizeof(b), "%d.%m", &tm);
    lv_label_set_text(dateLbl, b);
  } else {
    lv_label_set_text(timeLbl, "--:--");
    lv_label_set_text(dateLbl, "");
  }

  int q = w.connected ? w.quality : 0;
  setBars(wBars, q, TH.accent);
  if (w.connected) lv_label_set_text_fmt(rssiLbl, "%d", w.rssi);
  else if (w.apMode) lv_label_set_text(rssiLbl, "AP");
  else lv_label_set_text(rssiLbl, "—");
  pollBattery();
}

static void refreshOverview(const PcStats& pc) {
  char v[16], d[24];
  bool on = pc.online;

  snprintf(v, sizeof(v), on ? "%.0f%%" : "—", pc.cpu);
  if (pc.cpuTemp > 0) snprintf(d, sizeof(d), "%.0f°C", pc.cpuTemp);
  else if (pc.cpuFreqGHz > 0.1f) snprintf(d, sizeof(d), "%.1f ГГц", pc.cpuFreqGHz);
  else strcpy(d, on ? "нагрузка" : "—");
  setGauge(W.gz[0], on ? pc.cpu : 0, v, d, loadColor(pc.cpu, 0));

  snprintf(v, sizeof(v), on ? "%.0f%%" : "—", pc.ramPct);
  if (pc.ramTotalGB > 0) snprintf(d, sizeof(d), "%s/%s", fmtGB(pc.ramUsedGB).c_str(), fmtGB(pc.ramTotalGB).c_str());
  else strcpy(d, on ? "память" : "—");
  setGauge(W.gz[1], on ? pc.ramPct : 0, v, d, loadColor(pc.ramPct, 1));

  snprintf(v, sizeof(v), on && pc.hasGpu ? "%.0f%%" : "—", pc.gpuPct);
  if (!pc.hasGpu) strcpy(d, "нет GPU");
  else if (pc.gpuTemp > 0) snprintf(d, sizeof(d), "%.0f°C VRAM%.0f%%", pc.gpuTemp, pc.vramPct);
  else snprintf(d, sizeof(d), "VRAM %.0f%%", pc.vramPct);
  setGauge(W.gz[2], on && pc.hasGpu ? pc.gpuPct : 0, v, d, loadColor(pc.gpuPct, 2));

  snprintf(v, sizeof(v), on ? "%.0f%%" : "—", pc.diskPct);
  if (pc.diskTotalGB > 0) snprintf(d, sizeof(d), "%s/%s", fmtGB(pc.diskUsedGB).c_str(), fmtGB(pc.diskTotalGB).c_str());
  else strcpy(d, on ? "диск" : "—");
  setGauge(W.gz[3], on ? pc.diskPct : 0, v, d, loadColor(pc.diskPct, 3));

  if (W.downLbl) lv_label_set_text_fmt(W.downLbl, on ? "%.1f" : "—", pc.netDownMbps);
  if (W.upLbl) lv_label_set_text_fmt(W.upLbl, on ? "%.1f" : "—", pc.netUpMbps);
  if (W.totDown) lv_label_set_text(W.totDown, ("↓ " + fmtBytes(pc.netDownBytes)).c_str());
  if (W.totUp) lv_label_set_text(W.totUp, ("↑ " + fmtBytes(pc.netUpBytes)).c_str());

  if (W.downChart && W.downSer) {
    int hi = max(1, (int)ceil(lastMax(hDown, hCount, 60) * 1.2f));
    setRange(W.downChart, 0, hi);
    seedSeries(W.downChart, W.downSer, hDown, hCount, 60);
  }
  if (W.upChart && W.upSer) {
    int hi = max(1, (int)ceil(lastMax(hUp, hCount, 60) * 1.2f));
    setRange(W.upChart, 0, hi);
    seedSeries(W.upChart, W.upSer, hUp, hCount, 60);
  }

  if (W.volVal && W.volSlider) {
    if (!lv_slider_is_dragged(W.volSlider) && (int32_t)(millis() - volHoldUntil) > 0) {
      lv_slider_set_value(W.volSlider, pc.volume, LV_ANIM_ON);
      lv_label_set_text_fmt(W.volVal, "%d%%", pc.volume);
    }
    lv_obj_set_style_text_color(W.volVal, C(pc.muted ? TH.muted : TH.text), 0);
    if (W.volMuteIcon) {
      lv_label_set_text(W.volMuteIcon, pc.muted ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX);
      lv_obj_set_style_text_color(W.volMuteIcon, C(pc.muted ? TH.bad : TH.text), 0);
    }
  }
}

static void refreshGraph(const PcStats& pc) {
  if (!W.chart || !W.ser1) return;
  const float* src = hCpu;
  float cur = 0;
  if (gmode == 0) { src = hCpu; cur = pc.cpu; }
  else if (gmode == 1) { src = hRam; cur = pc.ramPct; }
  else if (gmode == 2) { src = hGpu; cur = pc.gpuPct; }
  else if (gmode == 3) { src = hDisk; cur = pc.diskPct; }
  else { src = hDown; cur = pc.netDownMbps; }

  if (gmode == 4) {
    float mx = max(lastMax(hDown, hCount, HIST_N), lastMax(hUp, hCount, HIST_N));
    setRange(W.chart, 0, max(10, (int)ceil(mx * 1.15f)));
    seedSeries(W.chart, W.ser1, hDown, hCount, HIST_N);
    seedSeries(W.chart, W.ser2, hUp, hCount, HIST_N);
    if (W.nowLbl) lv_label_set_text_fmt(W.nowLbl, "↓%.0f", cur);
  } else {
    setRange(W.chart, 0, 100);
    seedSeries(W.chart, W.ser1, src, hCount, HIST_N);
    if (W.nowLbl) lv_label_set_text_fmt(W.nowLbl, "%.0f%%", cur);
  }

  float mn, av, mx;
  rangeStats(src, hCount, mn, av, mx);
  String s;
  if (hCount == 0) {
    s = "нет данных — жду ответа агента";
  } else if (gmode == 4) {
    float umn, uav, umx;
    rangeStats(hUp, hCount, umn, uav, umx);
    s = "↓ " + String(mn, 1) + " / " + String(av, 1) + " / " + String(mx, 1) + " Мбит/с\n";
    s += "↑ " + String(umn, 1) + " / " + String(uav, 1) + " / " + String(umx, 1) + " Мбит/с";
  } else {
    s = "мин " + String(mn, 0) + "%   сред " + String(av, 0) + "%   макс " + String(mx, 0) + "%\n";
    if (gmode == 0) {
      s += pc.online && pc.top1[0] ? String("Топ: ") + pc.top1 + " " + String(pc.top1cpu, 1) + "%" : "процессы недоступны";
    } else if (gmode == 1) {
      s += pc.ramTotalGB > 0 ? String(pc.ramUsedGB, 1) + " / " + String(pc.ramTotalGB, 1) + " ГБ" : "";
    } else if (gmode == 2) {
      if (!pc.hasGpu) s += "видеокарта не найдена";
      else s = s + (pc.gpuTemp > 0 ? String(pc.gpuTemp, 0) + "°C · " : "") + "VRAM " + String(pc.vramUsedGB, 1) + "/" + String(pc.vramTotalGB, 1) + " ГБ";
    } else {
      s += pc.diskTotalGB > 0 ? String(pc.diskUsedGB, 0) + " / " + String(pc.diskTotalGB, 0) + " ГБ · " : "";
      s += "R " + String(pc.diskReadMBs, 1) + " · W " + String(pc.diskWriteMBs, 1) + " МБ/с";
    }
  }
  if (W.info) lv_label_set_text(W.info, s.c_str());
}

static void refreshNetworkPage(const PcStats& pc, const WifiInfo& w) {
  if (W.wq) {
    if (w.connected) {
      lv_label_set_text_fmt(W.wq, "%d%%", w.quality);
      lv_label_set_text_fmt(W.wdbm, "%d дБм · %d%%", w.rssi, w.quality);
      lv_label_set_text(W.wssid, w.ssid[0] ? w.ssid : "скрытая сеть");
      lv_label_set_text_fmt(W.wip, "IP %s", w.ip);
      lv_label_set_text_fmt(W.wlink, "канал %d · LAN %.0f мс", w.channel, pc.linkMs);
    } else if (w.apMode) {
      lv_label_set_text(W.wq, "AP");
      lv_label_set_text(W.wdbm, "точка доступа");
      lv_label_set_text(W.wssid, w.apSsid);
      lv_label_set_text_fmt(W.wip, "IP %s", w.ip);
      lv_label_set_text(W.wlink, "настройте Wi-Fi");
    } else {
      lv_label_set_text(W.wq, "—");
      lv_label_set_text(W.wdbm, "нет сети");
      lv_label_set_text(W.wssid, "");
      lv_label_set_text(W.wip, "");
      lv_label_set_text(W.wlink, "");
    }
    setBars(W.wbars, w.connected ? w.quality : 0, TH.accent);
  }

  if (W.wpc) {
    if (!pc.online) lv_label_set_text(W.wpc, "ПК: нет связи");
    else if (pc.hasPcWifi) lv_label_set_text_fmt(W.wpc, "ПК: %d дБм · %d Мбит/с", pc.pcRssi, pc.pcLinkMbps);
    else lv_label_set_text(W.wpc, "ПК: провод / нет данных");
  }

  if (W.rssiChart && W.rssiSer) {
    setRange(W.rssiChart, -100, -30);
    seedSeries(W.rssiChart, W.rssiSer, hRssi, hCount, 60);
  }

  if (W.testLbl) {
    if (pc.speedTesting) {
      lv_label_set_text(W.testLbl, "ИДЁТ ТЕСТ…");
      lv_obj_set_style_bg_color(W.testBtn, C(TH.card2), 0);
      lv_obj_set_style_bg_grad_dir(W.testBtn, LV_GRAD_DIR_NONE, 0);
      lv_obj_set_style_text_color(W.testLbl, C(TH.muted), 0);
    } else {
      lv_label_set_text(W.testLbl, "ТЕСТ СКОРОСТИ");
      accentBtn(W.testBtn);
      lv_obj_set_style_text_color(W.testLbl, C(TH.btnTxt), 0);
    }
  }
  if (W.prog) lv_bar_set_value(W.prog, pc.speedTesting ? (int)(pc.speedProgress * 100) : 0, LV_ANIM_ON);
  if (W.stage) {
    if (pc.speedError[0]) lv_label_set_text(W.stage, pc.speedError);
    else if (pc.speedTesting) lv_label_set_text(W.stage, pc.speedStage[0] ? pc.speedStage : "измерение…");
    else lv_label_set_text(W.stage, "готов к тесту");
    lv_obj_set_style_text_color(W.stage, C(pc.speedError[0] ? TH.bad : TH.muted), 0);
  }
  if (W.dlLbl) lv_label_set_text(W.dlLbl, pc.speedDown > 0 ? fmtSpeed(pc.speedDown).c_str() : "—");
  if (W.ulLbl) lv_label_set_text(W.ulLbl, pc.speedUp > 0 ? fmtSpeed(pc.speedUp).c_str() : "—");
  if (W.pingLbl) {
    if (pc.speedPing > 0) lv_label_set_text_fmt(W.pingLbl, "пинг %.0f мс · джиттер %.0f мс", pc.speedPing, pc.speedJitter);
    else lv_label_set_text(W.pingLbl, "пинг —");
  }
  if (W.lanLbl) {
    if (pc.online) lv_label_set_text_fmt(W.lanLbl, "LAN %.0f мс · аптайм %s", pc.linkMs, fmtUptime(pc.uptimeSec).c_str());
    else lv_label_set_text(W.lanLbl, "LAN —");
  }
  if (W.stime) lv_label_set_text(W.stime, pc.speedAt ? ("тест: " + fmtClock(pc.speedAt)).c_str() : "ещё не тестировали");
  if (W.topLbl) {
    if (pc.online && pc.top1[0]) {
      String s = String("Топ CPU: ") + pc.top1 + " " + String(pc.top1cpu, 1) + "%";
      if (pc.top2[0]) s += "\n" + String(pc.top2) + " " + String(pc.top2cpu, 1) + "%";
      lv_label_set_text(W.topLbl, s.c_str());
    } else {
      lv_label_set_text(W.topLbl, "");
    }
  }
}

static void refreshSettingsPage(const PcStats& pc, const WifiInfo& w) {
  if (!W.agentLbl) return;
  settingsLock();
  String host = gSet.agentHost;
  uint16_t port = gSet.agentPort;
  settingsUnlock();
  if (pc.online && pc.viaUsb) lv_label_set_text(W.agentLbl, "USB-кабель");
  else if (host.length()) lv_label_set_text_fmt(W.agentLbl, "%s:%u", host.c_str(), (unsigned)port);
  else lv_label_set_text(W.agentLbl, "автопоиск по UDP");
  if (W.agentDot) lv_obj_set_style_bg_color(W.agentDot, C(pc.online ? TH.good : TH.bad), 0);
  if (W.brightVal) lv_label_set_text_fmt(W.brightVal, "%d%%", gSet.brightness);

  String s;
  s += "IP: ";
  s += w.connected || w.apMode ? w.ip : "—";
  s += "  ·  Wi-Fi: ";
  s += w.connected ? String(w.rssi) + " дБм" : (w.apMode ? String("AP ") + w.apSsid : "нет");
  s += "\nheap ";
  s += String(ESP.getFreeHeap() / 1024);
  s += "K · PSRAM ";
  s += String(ESP.getFreePsram() / 1024);
  s += "K · экран ";
  s += String(millis() / 1000);
  s += " с · v1.7";
  if (W.infoSet) lv_label_set_text(W.infoSet, s.c_str());
}

static void refreshPage(const PcStats& pc, const WifiInfo& w) {
  switch (page) {
    case 1: refreshGraph(pc); break;
    case 2: refreshNetworkPage(pc, w); break;
    case 3: refreshSettingsPage(pc, w); break;
    default: refreshOverview(pc); break;
  }
}

static void refreshAll(bool force) {
  PcStats pc = monitorSnapshot();
  WifiInfo w = monitorWifi();
  refreshTop(pc, w);
  refreshPage(pc, w);
  lastW = w;
  lastRev = pc.rev;
  lastSeen = pc.seenMs;
  (void)force;
}

static void handlePending() {
  if (pendingTheme >= 0) {
    int t = pendingTheme;
    pendingTheme = -1;
    settingsLock();
    gSet.skin = (uint8_t)constrain(t, 0, skinCount() - 1);
    settingsUnlock();
    settingsSave();
    rebuildAll();
    uiToast(String("Скин: ") + TH.name);
    return;
  }
  if (pendingRebuild) {
    pendingRebuild = false;
    pendingBuild = false;
    rebuildAll();
    if (toastSkin) {
      toastSkin = false;
      uiToast(String("Скин: ") + skinName(gSet.skin));
    }
    return;
  }
  if (pendingBuild) {
    pendingBuild = false;
    if (!home) buildPage();
  }
}

static bool homeBlinkOnly() {
  settingsLock();
  int s = gSet.skin;
  settingsUnlock();
  return home && !idleShown && !fcShown && !mediaShown && !nightShown && !launchShown && s == 5;
}

// ------------------------------------------------------------------ публичное
void uiInit() {
  loadFonts();
  settingsLock();
  themeIdx = constrain((int)gSet.skin, 0, skinCount() - 1);
  settingsUnlock();
  TH = THEMES[themeIdx];
  analogSetPinAttenuation(BAT_PIN, ADC_11db);
  lastActivity = millis();
  displaySetFlip(gSet.flip);
  if (gSet.calOk) displaySetCal(true, gSet.calXA, gSet.calXB, gSet.calYA, gSet.calYB);
  buildAll();
  displayBacklight(gSet.brightness);
}

void uiLoop() {
  uint32_t now = millis();
  applyRequests();
  handlePending();
  if (rebootAt && (int32_t)(now - rebootAt) >= 0) ESP.restart();
  if (toast && (int32_t)(now - toastUntil) >= 0) {
    lv_anim_del(toast, fadeExec);
    lv_obj_del(toast);
    toast = nullptr;
  }

  static uint32_t lastClock = 0;
  bool clockTick = (int32_t)(now - lastClock) >= 0;
  if (clockTick) lastClock = now + (homeBlinkOnly() ? 500 : 1000);

  PcStats pc = monitorSnapshot();
  WifiInfo w = monitorWifi();
  bool pcDirty = pc.rev != lastRev;
  bool wifiDirty = w.connected != lastW.connected || w.apMode != lastW.apMode || w.rssi != lastW.rssi ||
                   w.quality != lastW.quality || w.channel != lastW.channel || strcmp(w.ssid, lastW.ssid) ||
                   strcmp(w.ip, lastW.ip) || strcmp(w.apSsid, lastW.apSsid);

  if (pcDirty) {
    lastRev = pc.rev;
    if (pc.online && pc.seenMs != lastSeen) {
      lastSeen = pc.seenMs;
      pushHistory(pc, w);
    }
  }

  // заставка при выключенном ПК
  if (pc.online) offlineSince = 0;
  else if (!offlineSince) offlineSince = now ? now : 1;
  bool wantIdle = idleForce || (offlineSince && now - offlineSince > 20000UL);
  if (home && wantIdle != idleShown) {
    idleShown = wantIdle;
    if (!fcShown && !mediaShown && !nightShown && !launchShown) pendingRebuild = true;
  }
  // медиапанель закрывается через 30 с без касаний
  if ((mediaShown || launchShown) && (!home || now - lastActivity > 30000UL)) {
    mediaShown = launchShown = false;
    if (home) pendingRebuild = true;
  }
  // прогноз сам закрывается через 30 с
  if ((fcShown || nightShown) && (!home || now - fcAt > 30000UL)) {
    fcShown = nightShown = false;
    if (home) pendingRebuild = true;
  }
  // ночью (23:00–07:00) заставка светит не ярче 10 %
  if (clockTick) {
    int cap = 100;
    time_t tt = time(nullptr);
    if (idleShown && home && tt > 1700000000) {
      struct tm tmv;
      localtime_r(&tt, &tmv);
      if (tmv.tm_hour >= 23 || tmv.tm_hour < 7) cap = 10;
    }
    if (cap != nightCap) {
      nightCap = cap;
      if (!dimmed && !asleep) {
        settingsLock();
        int b = gSet.brightness;
        settingsUnlock();
        displayBacklight(min(b, nightCap));
      }
    }
  }

  if (home) {
    if (pcDirty || wifiDirty || clockTick) {
      skinUpdate(pc, w);
      lastW = w;
    }
  } else {
    if (pcDirty || wifiDirty || clockTick) {
      refreshTop(pc, w);
      lastW = w;
    }
    if (pcDirty || wifiDirty) refreshPage(pc, w);
    // меню само закрывается через 30 с без касаний
    if (!overlay && now - lastActivity > 30000UL) {
      home = true;
      pendingRebuild = true;
    }
  }

  settingsLock();
  uint16_t dimSec = gSet.dimSec;
  uint8_t bright = gSet.brightness;
  settingsUnlock();
  if (dimSec > 0 && !asleep) {
    uint32_t idle = now - lastActivity;
    if (!dimmed && idle > (uint32_t)dimSec * 1000UL) {
      dimmed = true;
      displayBacklight(max(6, bright / 4));
    }
    if (idle > ((uint32_t)dimSec + 30) * 1000UL) {
      asleep = true;
      displayBacklight(0);
    }
  }
}

void uiActivity() {
  lastActivity = millis();
  if (dimmed || asleep) {
    dimmed = false;
    asleep = false;
    settingsLock();
    int b = gSet.brightness;
    settingsUnlock();
    displayBacklight(min(b, nightCap));
  }
}

void uiShowNight(bool on) {
  nightShown = on;
  fcShown = mediaShown = launchShown = false;
  fcAt = millis();
  home = true;
  pendingRebuild = true;
}

void uiShowScreen(const char* n) {
  if (!strcmp(n, "fc")) uiShowForecast(true);
  else if (!strcmp(n, "media")) uiShowMedia(true);
  else if (!strcmp(n, "night")) uiShowNight(true);
  else if (!strcmp(n, "launch")) uiShowLaunch(true);
  else if (!strcmp(n, "idle")) uiForceIdle(true);
  else if (!strcmp(n, "cal")) {
    calShown = true;
    home = true;
    pendingRebuild = true;
  } else if (!strcmp(n, "calreset")) {
    settingsLock();
    gSet.calOk = false;
    settingsUnlock();
    settingsSave();
    displaySetCal(false, 0, 0, 0, 0);
  }
  else if (!strcmp(n, "home")) {
    fcShown = nightShown = mediaShown = launchShown = false;
    uiForceIdle(false);
  }
}

void uiShowLaunch(bool on) {
  launchShown = on;
  fcShown = nightShown = mediaShown = false;
  home = true;
  pendingRebuild = true;
  uiActivity();
}

void uiShowMedia(bool on) {
  mediaShown = on;
  fcShown = nightShown = launchShown = false;
  home = true;
  pendingRebuild = true;
  uiActivity();
}

void uiShowForecast(bool on) {
  fcShown = on;
  nightShown = mediaShown = launchShown = false;
  fcAt = millis();
  home = true;
  pendingRebuild = true;
}

void uiForceIdle(bool on) {
  idleForce = on;
  home = true;
  pendingRebuild = true;
  uiActivity();
}

void uiReloadSettings();
void uiSetSkin(int idx);

// Запрос настроек из потока связи (USB): применяется в uiLoop.
static volatile int reqSkin = -1, reqFlip = -1;
void uiRequest(int skin, int flip) {
  if (flip >= 0) reqFlip = flip ? 1 : 0;
  if (skin >= 0) reqSkin = skin;
}

static void applyRequests() {
  if (reqFlip >= 0) {
    settingsLock();
    gSet.flip = reqFlip != 0;
    settingsUnlock();
    reqFlip = -1;
    settingsSave();
    uiReloadSettings();
  }
  if (reqSkin >= 0) {
    int s = reqSkin;
    reqSkin = -1;
    uiSetSkin(s);
  }
}

void uiSetSkin(int idx) {
  settingsLock();
  gSet.skin = (uint8_t)constrain(idx, 0, skinCount() - 1);
  settingsUnlock();
  settingsSave();
  fcShown = nightShown = mediaShown = launchShown = false;   // смена скина — сразу на главный экран
  home = true;
  pendingRebuild = true;
  uiActivity();
}

void uiShowMenu(int pg) {
  page = constrain(pg, 0, 3);
  home = false;
  pendingRebuild = true;
  uiActivity();
}

void uiReloadSettings() {
  settingsLock();
  bool f = gSet.flip;
  settingsUnlock();
  displaySetFlip(f);
  pendingRebuild = true;
  uiActivity();
}

void uiTouchActivity() {
  uiActivity();
  alertsSnooze();   // касание снимает тревогу на 5 минут
}
bool uiScreenAsleep() { return asleep; }

void uiShowOverlay(const String& title, const String& body, bool closable) {
  uiHideOverlay();
  overlay = obj(lv_layer_top(), 0, 0, LCD_W, LCD_H);
  setGrad(overlay, TH.bg1, TH.bg2);
  lv_obj_set_style_border_width(overlay, 0, 0);
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* c = card(overlay, 20, 46, 280, 148, 14);
  txt(c, 0, 14, title.c_str(), fBigB, TH.accent, 280, LV_TEXT_ALIGN_CENTER);
  lv_obj_t* b = txt(c, 20, 42, body.c_str(), fSmall, TH.text, 240, LV_TEXT_ALIGN_CENTER);
  lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
  if (closable) {
    lv_obj_t* btn = makeBtn(c, 90, 108, 100, 28, TH.accent, TH.accent, 12);
    accentBtn(btn);
    centerTxt(btn, "Понятно", fSmallB, TH.btnTxt);
    lv_obj_add_event_cb(btn, overlayCloseCb, LV_EVENT_CLICKED, nullptr);
  }
  fadeIn(overlay);
}

void uiHideOverlay() {
  if (!overlay) return;
  lv_anim_del(overlay, fadeExec);
  lv_obj_del(overlay);
  overlay = nullptr;
}

bool uiOverlayVisible() { return overlay != nullptr; }

void uiToast(const String& text) {
  if (toast) {
    lv_anim_del(toast, fadeExec);
    lv_obj_del(toast);
    toast = nullptr;
  }
  toast = obj(lv_layer_top(), 50, 178, 220, 26);
  setGrad(toast, TH.card, TH.card2);
  lv_obj_set_style_radius(toast, 13, 0);
  lv_obj_set_style_border_width(toast, 1, 0);
  lv_obj_set_style_border_color(toast, C(TH.accent), 0);
  lv_obj_set_style_shadow_width(toast, 10, 0);
  lv_obj_set_style_shadow_color(toast, C(0), 0);
  lv_obj_set_style_shadow_opa(toast, LV_OPA_30, 0);
  lv_obj_remove_flag(toast, LV_OBJ_FLAG_CLICKABLE);
  centerTxt(toast, text.c_str(), fTiny, TH.text);
  toastUntil = millis() + 2200;
  fadeIn(toast);
}
