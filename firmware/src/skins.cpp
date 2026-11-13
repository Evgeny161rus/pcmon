// Скины главного экрана PC Monitor (320x240).
//   0 «Приборы»     — два стрелочных циферблата
//   1 «Неон»        — три светящихся кольца
//   2 «Минимализм»  — светлый фон, одна крупная цифра
//   3 «Ретро-радио» — стрелочный индикатор, табло, позывной
//   4 «Часы»        — крупные часы и четыре цветных кольца
//   5 «Терминал»    — зелёная консоль sysmon --live
//   6 «Плитки»      — четыре яркие карточки и строка состояния
//   7 «Лампы»       — газоразрядные индикаторы ИН-14
//   8 «Импульс»     — центральное кольцо CPU и быстрые метрики
//   9 «Шина»        — схема узлов вокруг центрального CPU
//  10 «HUD»         — сегментированные шкалы телеметрии
//  11 «Сканлайн»    — история CPU и системные показатели
#include "skins.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include "settings.h"

#include "fonts_px.h"
#include "font_medium.h"
#include "font_semibold.h"

static const char* NAMES[] = {
    "Приборы", "Неон", "Минимализм", "Ретро-радио",
    "Часы", "Терминал", "Плитки", "Лампы",
    "Импульс", "Шина", "HUD", "Сканлайн",
};
static const int SKIN_N = 12;

int skinCount() { return SKIN_N; }
const char* skinName(int idx) { return NAMES[constrain(idx, 0, SKIN_N - 1)]; }

// ------------------------------------------------------------------ шрифты
static lv_font_t *f8, *f9, *f11, *f9b, *f14b, *f16b, *fMono, *f20b, *f24, *f30b, *f36b, *f44b, *f64b, *f76b, *f84b;

static lv_font_t* mk(const uint8_t* d, size_t n, int px, int cache, const lv_font_t* fb) {
  lv_font_t* f = lv_tiny_ttf_create_data_ex(d, n, px, LV_FONT_KERNING_NONE, cache);
  return f ? f : (lv_font_t*)fb;
}

static void fonts() {
  if (f8) return;
  // мелкий текст — растровые шрифты 11–16 px (имена переменных исторические)
  f8 = (lv_font_t*)&font_pm11;
  f9 = (lv_font_t*)&font_pm12;
  f11 = (lv_font_t*)&font_pm13;
  f9b = (lv_font_t*)&font_pb12;
  f14b = (lv_font_t*)&font_pb14;
  f16b = (lv_font_t*)&font_pb16;
  fMono = (lv_font_t*)&font_mono16;
  f20b = mk(font_semibold, font_semibold_len, 20, 48, &lv_font_montserrat_20);
  f24 = mk(font_medium, font_medium_len, 24, 16, &lv_font_montserrat_20);
  f30b = mk(font_semibold, font_semibold_len, 30, 24, &lv_font_montserrat_28);
  f36b = mk(font_semibold, font_semibold_len, 36, 16, &lv_font_montserrat_28);
  f44b = mk(font_semibold, font_semibold_len, 44, 24, &lv_font_montserrat_28);
  f64b = mk(font_semibold, font_semibold_len, 64, 16, &lv_font_montserrat_28);
  f76b = mk(font_semibold, font_semibold_len, 76, 16, &lv_font_montserrat_28);
  f84b = mk(font_semibold, font_semibold_len, 84, 14, &lv_font_montserrat_28);
}

// ------------------------------------------------------------------ состояние
struct Bar {
  lv_obj_t* fill;
  int w;
};

struct State {
  int idx = -1;
  // общие
  lv_obj_t *host, *clock, *date, *net, *net2;
  // приборы / ретро
  lv_obj_t *sc[2], *needle[2], *val[2], *sub[2];
  lv_obj_t *cellVal[4];
  Bar cellBar[4];
  // неон / часы
  lv_obj_t *ring[4], *glow[4], *big, *bigSub, *rowVal[4], *rowSub[4], *chart;
  lv_chart_series_t* ser;
  // минимализм
  lv_obj_t *pct;
  Bar mbar[3];
  // ретро
  lv_obj_t *vfd[3], *led[2][12], *ledLbl[2], *rx, *tx, *vol, *volArc, *call;
  // терминал / лампы
  lv_obj_t *term[9], *cursor;
  lv_obj_t *nixClk[4], *nixVal[3][2];
  // заставка
  lv_obj_t *wTemp, *wDesc, *wSub;
  lv_obj_t* vfdLbl[3];
  // погода в скине: значок, температура, описание и до 4 ячеек прогноза
  struct WxPal { uint32_t sun, cloud, drop, bg; };
  struct WxView {
    lv_obj_t *icon = nullptr, *temp = nullptr, *desc = nullptr, *time = nullptr;
    int s = 0, code = -100;
    bool night = false;
  };
  WxPal wxPal = {0xFFC83D, 0xD5DCE6, 0x4CC9F0, 0x000000};
  WxView wx;
  WxView fc[4];
  int fcN = 0, fcStep = 3;
  bool wxLong = false;   // описание с «ощущается»
};

static State S;
static float hist[48];
static int histN = 0;
static uint32_t histSeen = 0;

void skinClear() {
  S = State();
}

static inline lv_color_t C(uint32_t v) { return lv_color_hex(v); }

// ------------------------------------------------------------------ кирпичики
static lv_obj_t* box(lv_obj_t* p, int x, int y, int w, int h, uint32_t bg, int r = 0, uint32_t border = 0, int bw = 0) {
  lv_obj_t* o = lv_obj_create(p);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, C(bg), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, r, 0);
  if (bw > 0) {
    lv_obj_set_style_border_width(o, bw, 0);
    lv_obj_set_style_border_color(o, C(border), 0);
  }
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

static lv_obj_t* lab(lv_obj_t* p, int x, int y, int w, const char* t, const lv_font_t* f, uint32_t c,
                     lv_text_align_t a = LV_TEXT_ALIGN_LEFT) {
  lv_obj_t* l = lv_label_create(p);
  lv_label_set_text(l, t ? t : "");
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, C(c), 0);
  lv_obj_set_style_text_letter_space(l, 0, 0);
  if (w > 0) {
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(l, a, 0);
  }
  lv_obj_set_pos(l, x, y);
  return l;
}

static Bar bar(lv_obj_t* p, int x, int y, int w, int h, uint32_t track, uint32_t fill) {
  box(p, x, y, w, h, track, h / 2);
  Bar b;
  b.fill = box(p, x, y, 1, h, fill, h / 2);
  b.w = w;
  return b;
}

static void setBar(Bar& b, float pct) {
  if (!b.fill) return;
  int w = (int)lroundf(b.w * constrain(pct, 0.0f, 100.0f) / 100.0f);
  lv_obj_set_width(b.fill, max(1, w));
}

static void setText(lv_obj_t* l, const char* t) {
  if (l && strcmp(lv_label_get_text(l), t) != 0) lv_label_set_text(l, t);
}

static void setFmt(lv_obj_t* l, const char* fmt, ...) {
  if (!l) return;
  char b[64];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  setText(l, b);
}

static lv_obj_t* arcRing(lv_obj_t* p, int cx, int cy, int r, int w, uint32_t track, uint32_t col, lv_opa_t trackOpa,
                         lv_opa_t opa) {
  lv_obj_t* a = lv_arc_create(p);
  int size = 2 * r + w;
  lv_obj_set_size(a, size, size);
  lv_obj_set_pos(a, cx - size / 2, cy - size / 2);
  lv_arc_set_bg_angles(a, 135, 45);
  lv_arc_set_range(a, 0, 100);
  lv_arc_set_value(a, 0);
  lv_obj_remove_style(a, NULL, LV_PART_KNOB);
  lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(a, 0, 0);
  lv_obj_set_style_arc_width(a, w, LV_PART_MAIN);
  lv_obj_set_style_arc_color(a, C(track), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(a, trackOpa, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, w, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(a, C(col), LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(a, opa, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
  return a;
}

static lv_obj_t* sparkline(lv_obj_t* p, int x, int y, int w, int h, uint32_t col, lv_chart_series_t** ser) {
  lv_obj_t* ch = lv_chart_create(p);
  lv_obj_remove_style_all(ch);
  lv_obj_set_pos(ch, x, y);
  lv_obj_set_size(ch, w, h);
  lv_chart_set_type(ch, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(ch, 48);
  lv_chart_set_div_line_count(ch, 0, 0);
  lv_chart_set_axis_range(ch, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
  lv_obj_set_style_line_width(ch, 2, LV_PART_ITEMS);
  lv_obj_set_style_line_rounded(ch, true, LV_PART_ITEMS);
  lv_obj_set_style_size(ch, 0, 0, LV_PART_INDICATOR);
  lv_obj_remove_flag(ch, LV_OBJ_FLAG_CLICKABLE);
  *ser = lv_chart_add_series(ch, C(col), LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_all_values(ch, *ser, LV_CHART_POINT_NONE);
  return ch;
}

static void feedChart() {
  if (!S.chart || !S.ser) return;
  // шкала подстраивается под нагрузку, чтобы при 5 % линия не лежала на дне
  float mx = 0;
  for (int i = 0; i < histN; i++) mx = max(mx, hist[i]);
  lv_chart_set_axis_range(S.chart, LV_CHART_AXIS_PRIMARY_Y, 0, min(100, max(20, (int)ceilf(mx * 1.3f))));
  for (int i = 0; i < 48; i++) {
    int idx = histN - 48 + i;
    lv_chart_set_series_value_by_id(S.chart, S.ser, i, idx >= 0 ? (int32_t)lroundf(hist[idx]) : LV_CHART_POINT_NONE);
  }
  lv_chart_refresh(S.chart);
}

// Стили красной зоны шкалы (общие для приборов).
static lv_style_t stRedMain, stRedTick, stDarkRedTick;
static bool stylesReady = false;

static void styles() {
  if (stylesReady) return;
  stylesReady = true;
  lv_style_init(&stRedMain);
  lv_style_set_arc_color(&stRedMain, C(0xE5484D));
  lv_style_set_arc_width(&stRedMain, 3);
  lv_style_init(&stRedTick);
  lv_style_set_line_color(&stRedTick, C(0xE5484D));
  lv_style_set_text_color(&stRedTick, C(0xFF7A7F));
  lv_style_init(&stDarkRedTick);
  lv_style_set_line_color(&stDarkRedTick, C(0xC0271D));
  lv_style_set_text_color(&stDarkRedTick, C(0xA01D15));
}

static lv_obj_t* roundScale(lv_obj_t* p, int size, int angle, int rotation, uint32_t line, uint32_t minor, uint32_t text,
                            int redFrom, const lv_style_t* redTick) {
  lv_obj_t* sc = lv_scale_create(p);
  lv_obj_set_size(sc, size, size);
  lv_scale_set_mode(sc, LV_SCALE_MODE_ROUND_INNER);
  lv_scale_set_range(sc, 0, 100);
  lv_scale_set_total_tick_count(sc, 21);
  lv_scale_set_major_tick_every(sc, 4);
  lv_scale_set_angle_range(sc, angle);
  lv_scale_set_rotation(sc, rotation);
  lv_scale_set_label_show(sc, true);
  lv_obj_set_style_bg_opa(sc, LV_OPA_TRANSP, 0);
  lv_obj_set_style_arc_width(sc, 2, LV_PART_MAIN);
  lv_obj_set_style_arc_color(sc, C(line), LV_PART_MAIN);
  lv_obj_set_style_length(sc, 4, LV_PART_ITEMS);
  lv_obj_set_style_line_width(sc, 1, LV_PART_ITEMS);
  lv_obj_set_style_line_color(sc, C(minor), LV_PART_ITEMS);
  lv_obj_set_style_length(sc, 8, LV_PART_INDICATOR);
  lv_obj_set_style_line_width(sc, 2, LV_PART_INDICATOR);
  lv_obj_set_style_line_color(sc, C(line), LV_PART_INDICATOR);
  lv_obj_set_style_text_font(sc, f8, LV_PART_INDICATOR);
  lv_obj_set_style_text_color(sc, C(text), LV_PART_INDICATOR);
  lv_obj_remove_flag(sc, LV_OBJ_FLAG_CLICKABLE);

  lv_scale_section_t* sec = lv_scale_add_section(sc);
  lv_scale_set_section_range(sc, sec, redFrom, 100);
  lv_scale_set_section_style_main(sc, sec, &stRedMain);
  lv_scale_set_section_style_indicator(sc, sec, redTick);
  lv_scale_set_section_style_items(sc, sec, redTick);
  return sc;
}

static lv_obj_t* needleLine(lv_obj_t* sc, uint32_t col, int w) {
  lv_obj_t* n = lv_line_create(sc);
  lv_obj_set_style_line_width(n, w, 0);
  lv_obj_set_style_line_color(n, C(col), 0);
  lv_obj_set_style_line_rounded(n, true, 0);
  return n;
}


// ------------------------------------------------------------------ погода
static const char* weatherText(int c) {
  if (c == 0) return "Ясно";
  if (c == 1) return "Малооблачно";
  if (c == 2) return "Облачно";
  if (c == 3) return "Пасмурно";
  if (c == 45 || c == 48) return "Туман";
  if (c >= 51 && c <= 57) return "Морось";
  if (c >= 61 && c <= 67) return "Дождь";
  if (c >= 71 && c <= 77) return "Снег";
  if (c >= 80 && c <= 82) return "Ливень";
  if (c == 85 || c == 86) return "Снегопад";
  if (c >= 95) return "Гроза";
  return "Погода";
}

static int clampi(float v) { return (int)lroundf(v); }

static lv_obj_t* disc(lv_obj_t* p, float x, float y, float d, uint32_t c) {
  return box(p, clampi(x), clampi(y), max(2, clampi(d)), max(2, clampi(d)), c, LV_RADIUS_CIRCLE);
}

static void drawCloud(lv_obj_t* p, float ox, float oy, float k, uint32_t c) {
  box(p, clampi(ox + k * 0.04f), clampi(oy + k * 0.42f), clampi(k * 0.92f), max(3, clampi(k * 0.36f)), c, LV_RADIUS_CIRCLE);
  disc(p, ox + k * 0.12f, oy + k * 0.24f, k * 0.42f, c);
  disc(p, ox + k * 0.36f, oy + k * 0.06f, k * 0.54f, c);
}

static void drawSun(lv_obj_t* p, float cx, float cy, float d, uint32_t c, bool rays) {
  lv_obj_t* o = disc(p, cx - d / 2, cy - d / 2, d, c);
  lv_obj_set_style_shadow_width(o, max(4, clampi(d * 0.6f)), 0);
  lv_obj_set_style_shadow_color(o, C(c), 0);
  lv_obj_set_style_shadow_opa(o, LV_OPA_50, 0);
  if (!rays) return;
  float L = d * 0.30f, t = max(2.0f, d * 0.13f), R = d * 0.62f;
  box(p, clampi(cx - t / 2), clampi(cy - R - L), clampi(t), clampi(L), c, LV_RADIUS_CIRCLE);
  box(p, clampi(cx - t / 2), clampi(cy + R), clampi(t), clampi(L), c, LV_RADIUS_CIRCLE);
  box(p, clampi(cx - R - L), clampi(cy - t / 2), clampi(L), clampi(t), c, LV_RADIUS_CIRCLE);
  box(p, clampi(cx + R), clampi(cy - t / 2), clampi(L), clampi(t), c, LV_RADIUS_CIRCLE);
  float q = (R + L * 0.4f) * 0.7071f, dd = t * 1.15f;
  disc(p, cx - q - dd / 2, cy - q - dd / 2, dd, c);
  disc(p, cx + q - dd / 2, cy - q - dd / 2, dd, c);
  disc(p, cx - q - dd / 2, cy + q - dd / 2, dd, c);
  disc(p, cx + q - dd / 2, cy + q - dd / 2, dd, c);
}

static void drawMoon(lv_obj_t* p, float cx, float cy, float d, uint32_t c, uint32_t bg) {
  lv_obj_t* o = disc(p, cx - d / 2, cy - d / 2, d, c);
  lv_obj_set_style_shadow_width(o, max(3, clampi(d * 0.4f)), 0);
  lv_obj_set_style_shadow_color(o, C(c), 0);
  lv_obj_set_style_shadow_opa(o, LV_OPA_30, 0);
  disc(p, cx - d / 2 + d * 0.34f, cy - d / 2 - d * 0.16f, d * 0.86f, bg);
}

static lv_point_precise_t boltPts[12][4];
static int boltN = 0;

static void drawIcon(lv_obj_t* cont, int s, int code, bool night, const State::WxPal& p) {
  lv_obj_clean(cont);
  if (code < 0) return;
  float k = (float)s;
  bool clear = code == 0, partly = code == 1 || code == 2;
  if (clear) {
    if (night) drawMoon(cont, k * 0.5f, k * 0.5f, k * 0.58f, p.sun, p.bg);
    else drawSun(cont, k * 0.5f, k * 0.5f, k * 0.42f, p.sun, true);
    return;
  }
  if (partly) {
    if (night) drawMoon(cont, k * 0.66f, k * 0.30f, k * 0.44f, p.sun, p.bg);
    else drawSun(cont, k * 0.64f, k * 0.32f, k * 0.34f, p.sun, code == 1);
    drawCloud(cont, 0, k * 0.26f, k * 0.80f, p.cloud);
    return;
  }
  bool fog = code == 45 || code == 48;
  bool precip = !fog && code >= 51;
  drawCloud(cont, k * 0.02f, precip || fog ? -k * 0.06f : k * 0.08f, k * 0.96f, p.cloud);
  if (fog) {
    box(cont, clampi(k * 0.08f), clampi(k * 0.70f), clampi(k * 0.84f), max(2, clampi(k * 0.08f)), p.cloud, LV_RADIUS_CIRCLE);
    box(cont, clampi(k * 0.20f), clampi(k * 0.84f), clampi(k * 0.66f), max(2, clampi(k * 0.08f)), p.cloud, LV_RADIUS_CIRCLE);
    return;
  }
  if (!precip) return;
  bool snow = (code >= 71 && code <= 77) || code == 85 || code == 86;
  bool storm = code >= 95;
  if (snow) {
    float d = max(3.0f, k * 0.13f);
    disc(cont, k * 0.22f, k * 0.70f, d, p.drop);
    disc(cont, k * 0.46f, k * 0.78f, d, p.drop);
    disc(cont, k * 0.70f, k * 0.70f, d, p.drop);
    disc(cont, k * 0.34f, k * 0.88f, d * 0.8f, p.drop);
    disc(cont, k * 0.58f, k * 0.90f, d * 0.8f, p.drop);
    return;
  }
  float w = max(2.0f, k * 0.09f), h = k * 0.20f;
  int n = (code >= 51 && code <= 57) ? 2 : (code >= 80 && code <= 82) ? 4 : 3;
  if (storm) n = 2;
  for (int i = 0; i < n; i++) {
    float x = n == 4 ? k * (0.16f + i * 0.20f) : n == 2 ? k * (0.28f + i * 0.40f) : k * (0.22f + i * 0.26f);
    float y = k * (0.68f + ((i & 1) ? 0.10f : 0.0f));
    box(cont, clampi(x), clampi(y), clampi(w), clampi(n == 2 && !storm ? h * 0.6f : h), p.drop, LV_RADIUS_CIRCLE);
  }
  if (storm) {
    lv_point_precise_t* b = boltPts[boltN++ % 12];
    b[0] = {(lv_value_precise_t)(k * 0.58f), (lv_value_precise_t)(k * 0.56f)};
    b[1] = {(lv_value_precise_t)(k * 0.42f), (lv_value_precise_t)(k * 0.78f)};
    b[2] = {(lv_value_precise_t)(k * 0.58f), (lv_value_precise_t)(k * 0.78f)};
    b[3] = {(lv_value_precise_t)(k * 0.44f), (lv_value_precise_t)(k * 0.98f)};
    lv_obj_t* l = lv_line_create(cont);
    lv_line_set_points(l, b, 4);
    lv_obj_set_style_line_width(l, max(2, clampi(k * 0.09f)), 0);
    lv_obj_set_style_line_color(l, C(p.sun), 0);
    lv_obj_set_style_line_rounded(l, true, 0);
  }
}

// Контейнер значка погоды размером s×s.
static lv_obj_t* wxIconBox(lv_obj_t* p, int x, int y, int s) {
  lv_obj_t* o = lv_obj_create(p);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, s, s);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_clip_corner(o, false, 0);
  return o;
}

static void wxPal(uint32_t sun, uint32_t cloud, uint32_t drop, uint32_t bg) { S.wxPal = {sun, cloud, drop, bg}; }

// Текущая погода: значок, температура (и описание, если есть место).
static void wxNow(lv_obj_t* p, int x, int y, int s, lv_obj_t* temp, lv_obj_t* desc = nullptr) {
  S.wx.icon = s > 0 ? wxIconBox(p, x, y, s) : nullptr;
  S.wx.s = s;
  S.wx.temp = temp;
  S.wx.desc = desc;
}

// Ячейка прогноза: время сверху, значок, температура снизу.
static void wxSlot(lv_obj_t* p, int x, int y, int w, int s, const lv_font_t* ft, const lv_font_t* fv, uint32_t tc,
                   uint32_t vc) {
  if (S.fcN >= 4) return;
  State::WxView& v = S.fc[S.fcN++];
  v.time = lab(p, x, y, w, "", ft, tc, LV_TEXT_ALIGN_CENTER);
  int th = lv_font_get_line_height(ft);
  v.icon = wxIconBox(p, x + (w - s) / 2, y + th + 1, s);
  v.s = s;
  v.temp = lab(p, x, y + th + s + 2, w, "", fv, vc, LV_TEXT_ALIGN_CENTER);
}

static bool isNight(int hour) { return hour >= 19 || hour < 7; }

static void wxShow(State::WxView& v, const Weather& w, bool night) {
  int code = w.ok ? w.code : -1;
  if (v.icon && (code != v.code || night != v.night)) drawIcon(v.icon, v.s, code, night, S.wxPal);
  v.code = code;
  v.night = night;
  if (v.temp) {
    if (w.ok) setFmt(v.temp, "%+.0f°", w.temp);
    else setText(v.temp, "");
  }
}

static void updWeather() {
  if (!S.wx.icon && !S.wx.temp && !S.fcN) return;
  time_t t = time(nullptr);
  struct tm tm = {};
  if (t > 1700000000) localtime_r(&t, &tm);
  Weather w = monitorWeather();
  wxShow(S.wx, w, isNight(tm.tm_hour));
  if (S.wx.desc) {
    if (!w.ok) setText(S.wx.desc, "");
    else if (S.wxLong) setFmt(S.wx.desc, "%s · ощущ. %+.0f°", weatherText(w.code), w.feels);
    else setText(S.wx.desc, weatherText(w.code));
  }
  for (int i = 0; i < S.fcN; i++) {
    int ahead = (i + 1) * S.fcStep;
    Weather f;
    bool ok = monitorForecast(ahead, f);
    int hh = (tm.tm_hour + ahead) % 24;
    if (ok) setFmt(S.fc[i].time, "%02d:00", hh);
    else {
      setText(S.fc[i].time, "");
      f.ok = false;
    }
    wxShow(S.fc[i], f, isNight(hh));
  }
}

// ------------------------------------------------------------------ 0. Приборы
static void buildAnalog(lv_obj_t* scr) {
  lv_obj_set_style_bg_color(scr, C(0x0C0E11), 0);
  wxPal(0xFFB547, 0xC9CED6, 0x4CC9F0, 0x0C0E11);
  wxNow(scr, 8, 2, 22, lab(scr, 34, 5, 56, "", f14b, 0xFFFFFF));
  S.clock = lab(scr, 120, 5, 80, "--:--", f14b, 0xFFFFFF, LV_TEXT_ALIGN_CENTER);
  S.net = lab(scr, 186, 6, 128, "", f9b, 0xFFFFFF, LV_TEXT_ALIGN_RIGHT);

  static const int cxs[2] = {82, 238};
  static const char* names[2] = {"CPU", "GPU"};
  const int cy = 106;
  for (int i = 0; i < 2; i++) {
    int cx = cxs[i];
    lv_obj_t* face = box(scr, cx - 71, cy - 71, 142, 142, 0x171A1F, LV_RADIUS_CIRCLE, 0x3A3F47, 2);
    S.sc[i] = roundScale(face, 130, 270, 135, 0xE6E9EE, 0x7D8590, 0xC9CED6, 85, &stRedTick);
    lv_obj_center(S.sc[i]);
    S.needle[i] = needleLine(S.sc[i], 0xFF9F1C, 3);
    lv_scale_set_line_needle_value(S.sc[i], S.needle[i], 50, 0);
    lv_obj_t* hub = box(face, 0, 0, 14, 14, 0x23272D, LV_RADIUS_CIRCLE, 0x4A505A, 2);
    lv_obj_center(hub);
    lab(scr, cx - 20, cy + 11, 40, names[i], f9b, 0xFF9F1C, LV_TEXT_ALIGN_CENTER);
    S.val[i] = lab(scr, cx - 40, cy + 24, 80, "—", f24, 0xFFFFFF, LV_TEXT_ALIGN_CENTER);
    S.sub[i] = lab(scr, cx - 44, cy + 50, 88, "", f8, 0xAAB1BC, LV_TEXT_ALIGN_CENTER);
  }

  static const char* cells[3] = {"ТЕМП. CPU", "ТЕМП. GPU", "ОЗУ, ГБ"};
  static const uint32_t cols[3] = {0xFF9F1C, 0xFF9F1C, 0x4CC9F0};
  for (int i = 0; i < 3; i++) {
    int x = 12 + i * 100;
    lv_obj_t* c = box(scr, x, 186, 96, 46, 0x171A1F, 6, 0x2B2F35, 1);
    lab(c, 7, 2, 84, cells[i], f8, 0xAAB1BC);
    S.cellVal[i] = lab(c, 7, 15, 84, "—", f16b, 0xFFFFFF);
    S.cellBar[i] = bar(c, 7, 38, 80, 3, 0x2B2F35, cols[i]);
  }
}

static void gpuShort(const PcStats& pc, char* out, size_t n) {
  const char* s = pc.gpuName;
  if (!strncmp(s, "NVIDIA ", 7)) s += 7;
  if (!strncmp(s, "GeForce ", 8)) s += 8;
  if (!strncmp(s, "AMD ", 4)) s += 4;
  if (!strncmp(s, "Radeon ", 7)) s += 7;
  strlcpy(out, s, n);
}

static void updAnalog(const PcStats& pc) {
  bool on = pc.online;
  float v[2] = {on ? pc.cpu : 0, on && pc.hasGpu ? pc.gpuPct : 0};
  for (int i = 0; i < 2; i++) {
    if (S.sc[i] && S.needle[i]) lv_scale_set_line_needle_value(S.sc[i], S.needle[i], 50, (int32_t)lroundf(v[i]));
  }
  if (on) setFmt(S.val[0], "%.0f%%", pc.cpu);
  else setText(S.val[0], "—");
  if (on && pc.hasGpu) setFmt(S.val[1], "%.0f%%", pc.gpuPct);
  else setText(S.val[1], "—");
  if (on && pc.cpuFreqGHz > 0.1f) setFmt(S.sub[0], "%.1f ГГц", pc.cpuFreqGHz);
  else setText(S.sub[0], "");
  char g[24];
  gpuShort(pc, g, sizeof(g));
  setText(S.sub[1], on && pc.hasGpu ? g : "");

  if (on && pc.cpuTemp > 0) setFmt(S.cellVal[0], "%.0f°", pc.cpuTemp);
  else setText(S.cellVal[0], "—");
  setBar(S.cellBar[0], on && pc.cpuTemp > 0 ? pc.cpuTemp : 0);
  if (on && pc.gpuTemp > 0) setFmt(S.cellVal[1], "%.0f°", pc.gpuTemp);
  else setText(S.cellVal[1], "—");
  setBar(S.cellBar[1], on && pc.gpuTemp > 0 ? pc.gpuTemp : 0);
  if (on) setFmt(S.cellVal[2], "%.1f / %.0f", pc.ramUsedGB, pc.ramTotalGB);
  else setText(S.cellVal[2], "—");
  setBar(S.cellBar[2], on ? pc.ramPct : 0);

  if (on) setFmt(S.net, "↓ %.1f ↑ %.1f Мбит/с", pc.netDownMbps, pc.netUpMbps);
  else setText(S.net, "");
}

// ------------------------------------------------------------------ 1. Неон
static const uint32_t NEON[3] = {0x22E3FF, 0xFF4DF5, 0xFFE14D};

static void buildNeon(lv_obj_t* scr) {
  lv_obj_set_style_bg_color(scr, C(0x07060D), 0);
  const int cx = 104, cy = 126;
  static const int rs[3] = {78, 62, 46};
  for (int i = 0; i < 3; i++) {
    S.glow[i] = arcRing(scr, cx, cy, rs[i], 14, 0x000000, NEON[i], LV_OPA_TRANSP, LV_OPA_20);
    S.ring[i] = arcRing(scr, cx, cy, rs[i], 6, 0x1B1830, NEON[i], LV_OPA_COVER, LV_OPA_COVER);
  }
  S.big = lab(scr, cx - 36, cy - 22, 72, "—", f30b, NEON[0], LV_TEXT_ALIGN_CENTER);
  S.bigSub = lab(scr, cx - 38, cy + 13, 76, "CPU", f8, 0xB9B3E0, LV_TEXT_ALIGN_CENTER);

  wxPal(0xFFE14D, 0x22E3FF, 0xFF4DF5, 0x07060D);
  wxNow(scr, 10, 4, 24, lab(scr, 40, 7, 60, "", f16b, 0xFFFFFF), lab(scr, 100, 9, 96, "", f9, 0xB9B3E0));
  S.clock = lab(scr, 200, 6, 110, "--:--", f20b, 0xFFFFFF, LV_TEXT_ALIGN_RIGHT);

  static const char* rows[3] = {"GPU", "RAM", "NET"};
  static const uint32_t rc[3] = {0xFF4DF5, 0xFFE14D, 0x22E3FF};
  for (int i = 0; i < 3; i++) {
    int y = 44 + i * 40;
    box(scr, 200, y, 3, 30, rc[i], 1);
    box(scr, 200, y + 36, 110, 1, 0x1B1830);
    lab(scr, 210, y + 2, 40, rows[i], f9b, 0xB9B3E0);
    S.rowVal[i] = lab(scr, 236, y - 2, 74, "—", f16b, rc[i], LV_TEXT_ALIGN_RIGHT);
    S.rowSub[i] = lab(scr, 210, y + 19, 104, "", f8, 0xB9B3E0);
  }
  S.chart = sparkline(scr, 200, 170, 110, 50, NEON[0], &S.ser);
  lab(scr, 200, 222, 110, "CPU · история", f8, 0xB9B3E0, LV_TEXT_ALIGN_RIGHT);

  static const char* leg[3] = {"CPU", "GPU", "RAM"};
  for (int i = 0; i < 3; i++) {
    box(scr, 19 + i * 56, 226, 6, 6, NEON[i], 3);
    lab(scr, 29 + i * 56, 222, 40, leg[i], f8, 0xB9B3E0);
  }
}

static void updNeon(const PcStats& pc) {
  bool on = pc.online;
  float v[3] = {on ? pc.cpu : 0, on && pc.hasGpu ? pc.gpuPct : 0, on ? pc.ramPct : 0};
  for (int i = 0; i < 3; i++) {
    int iv = (int)lroundf(v[i]);
    if (S.ring[i] && lv_arc_get_value(S.ring[i]) != iv) {
      lv_arc_set_value(S.ring[i], iv);
      lv_arc_set_value(S.glow[i], iv);
    }
  }
  if (on) setFmt(S.big, "%.0f%%", pc.cpu);
  else setText(S.big, "—");
  if (on && pc.cpuTemp > 0) setFmt(S.bigSub, "CPU · %.0f°C", pc.cpuTemp);
  else setText(S.bigSub, "CPU");

  if (on && pc.hasGpu) {
    setFmt(S.rowVal[0], "%.0f%%", pc.gpuPct);
    if (pc.gpuTemp > 0) setFmt(S.rowSub[0], "%.0f°C · VRAM %.0f%%", pc.gpuTemp, pc.vramPct);
    else setFmt(S.rowSub[0], "VRAM %.0f%%", pc.vramPct);
  } else {
    setText(S.rowVal[0], "—");
    setText(S.rowSub[0], on ? "нет видеокарты" : "");
  }
  if (on) {
    setFmt(S.rowVal[1], "%.0f%%", pc.ramPct);
    setFmt(S.rowSub[1], "%.1f / %.0f ГБ", pc.ramUsedGB, pc.ramTotalGB);
    setFmt(S.rowVal[2], "%.1f", pc.netDownMbps);
    setFmt(S.rowSub[2], "↓ Мбит/с · ↑ %.1f", pc.netUpMbps);
  } else {
    for (int i = 1; i < 3; i++) {
      setText(S.rowVal[i], "—");
      setText(S.rowSub[i], "");
    }
  }
  feedChart();
}

// ------------------------------------------------------------------ 2.  Минимализм
static void buildMinimal(lv_obj_t* scr) {
  const uint32_t INK = 0x16161A, MUT = 0x5C5C66, ACC = 0xC2410C, LINE = 0xD9D5CC;
  lv_obj_set_style_bg_color(scr, C(0xF4F2EE), 0);
  lab(scr, 20, 12, 120, "ПРОЦЕССОР", f8, MUT);
  S.big = lab(scr, 16, 24, 0, "—", f84b, INK);
  S.pct = lab(scr, 120, 84, 0, "%", f24, MUT);
  S.bigSub = lab(scr, 20, 126, 150, "", f11, INK);
  S.chart = sparkline(scr, 20, 146, 140, 44, ACC, &S.ser);

  static const char* rows[3] = {"ВИДЕОКАРТА", "ПАМЯТЬ", "СЕТЬ, МБИТ/С"};
  for (int i = 0; i < 3; i++) {
    int y = 18 + i * 60;
    lab(scr, 188, y - 4, 116, rows[i], f8, MUT);
    S.rowVal[i] = lab(scr, 188, y + 12, 70, "—", f20b, INK);
    S.rowSub[i] = lab(scr, 232, y + 20, 68, "", f9, MUT, LV_TEXT_ALIGN_RIGHT);
    S.mbar[i] = bar(scr, 188, y + 41, 112, 2, LINE, ACC);
  }
  box(scr, 20, 204, 280, 1, LINE);
  S.clock = lab(scr, 20, 213, 0, "--:--", f14b, INK);
  S.date = lab(scr, 68, 215, 110, "", f9, MUT);
  wxPal(0xE8890C, 0x8C8C96, 0x2F6FD6, 0xF4F2EE);
  wxNow(scr, 236, 208, 24, lab(scr, 260, 211, 40, "", f14b, INK, LV_TEXT_ALIGN_RIGHT),
        lab(scr, 150, 215, 82, "", f9, MUT, LV_TEXT_ALIGN_RIGHT));
}

static void updMinimal(const PcStats& pc) {
  bool on = pc.online;
  if (on) setFmt(S.big, "%.0f", pc.cpu);
  else setText(S.big, "—");
  if (S.big && S.pct) {
    lv_obj_update_layout(S.big);
    lv_obj_align_to(S.pct, S.big, LV_ALIGN_OUT_RIGHT_BOTTOM, 3, -20);
  }
  if (!on) setText(S.bigSub, "нет связи с ПК");
  else if (pc.cpuTemp > 0) setFmt(S.bigSub, "%.0f°C · %.1f ГГц", pc.cpuTemp, pc.cpuFreqGHz);
  else setFmt(S.bigSub, "%.1f ГГц", pc.cpuFreqGHz);

  if (on && pc.hasGpu) {
    setFmt(S.rowVal[0], "%.0f%%", pc.gpuPct);
    if (pc.gpuTemp > 0) setFmt(S.rowSub[0], "%.0f°C", pc.gpuTemp);
    else setText(S.rowSub[0], "");
  } else {
    setText(S.rowVal[0], "—");
    setText(S.rowSub[0], "");
  }
  setBar(S.mbar[0], on && pc.hasGpu ? pc.gpuPct : 0);
  if (on) {
    setFmt(S.rowVal[1], "%.1f", pc.ramUsedGB);
    setFmt(S.rowSub[1], "из %.0f ГБ", pc.ramTotalGB);
    setFmt(S.rowVal[2], "%.1f", pc.netDownMbps);
    setFmt(S.rowSub[2], "↑ %.1f", pc.netUpMbps);
  } else {
    for (int i = 1; i < 3; i++) {
      setText(S.rowVal[i], "—");
      setText(S.rowSub[i], "");
    }
  }
  setBar(S.mbar[1], on ? pc.ramPct : 0);
  // сеть: шкала до 100 Мбит/с
  setBar(S.mbar[2], on ? min(100.0f, pc.netDownMbps) : 0);
  feedChart();
}

// ------------------------------------------------------------------ 3. Ретро-радио
static void buildRetro(lv_obj_t* scr) {
  const uint32_t VFD = 0x5EF2D6, DIM = 0x48B8A4, PANEL = 0x04100E, EDGE = 0x0A0908;
  lv_obj_set_style_bg_color(scr, C(0x1C1A17), 0);

  // окно стрелочного прибора с подсветкой
  lv_obj_t* win = box(scr, 10, 10, 190, 120, 0xF3E6C4, 6, EDGE, 2);
  lv_obj_set_style_bg_grad_color(win, C(0xE6D19C), 0);
  lv_obj_set_style_bg_grad_dir(win, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_clip_corner(win, true, 0);
  S.sc[0] = roundScale(win, 212, 110, 215, 0x1C1A17, 0x1C1A17, 0x1C1A17, 80, &stDarkRedTick);
  lv_obj_set_pos(S.sc[0], 93 - 106, 142 - 106);
  S.needle[0] = needleLine(S.sc[0], 0x7A1410, 2);
  lv_scale_set_line_needle_value(S.sc[0], S.needle[0], 100, 0);
  // подписи в верхних углах: внизу по ним ходит стрелка
  lab(win, 6, 3, 100, "НАГРУЗКА CPU", f8, 0x1C1A17);
  S.sub[0] = lab(win, 100, 3, 80, "", f8, 0x4A4030, LV_TEXT_ALIGN_RIGHT);

  // люминесцентное табло
  lv_obj_t* vp = box(scr, 208, 10, 102, 120, PANEL, 6, EDGE, 2);
  static const char* rows[3] = {"GPU", "t° CPU", "ПОГОДА"};
  for (int i = 0; i < 3; i++) {
    int y = 6 + i * 37;
    S.vfdLbl[i] = lab(vp, 8, y - 2, 84, rows[i], f8, DIM);
    S.vfd[i] = lab(vp, 8, y + 10, 82, "—", f20b, VFD, LV_TEXT_ALIGN_RIGHT);
    if (i < 2) box(vp, 8, y + 34, 82, 1, 0x12302B);
  }

  // часы
  lv_obj_t* cp = box(scr, 10, 140, 190, 46, PANEL, 6, EDGE, 2);
  lab(cp, 8, 5, 40, "МСК", f8, DIM);
  S.clock = lab(cp, 6, 5, 174, "--:--:--", f30b, VFD, LV_TEXT_ALIGN_RIGHT);

  // светодиодные линейки
  static const int ly[2] = {150, 178};
  for (int r = 0; r < 2; r++) {
    S.ledLbl[r] = lab(scr, 208, ly[r] - 14, 102, "", f8, 0xC9C1B0);
    for (int i = 0; i < 12; i++) S.led[r][i] = box(scr, 208 + (i * 17) / 2, ly[r], 6, 9, 0x2C2924, 1);
  }

  // позывной, RX/TX, громкость
  lv_obj_t* plate = box(scr, 10, 196, 66, 34, 0x2A2620, 4, 0x4A443A, 1);
  settingsLock();
  String call = gSet.callsign;
  settingsUnlock();
  S.call = lab(plate, 0, 8, 64, call.c_str(), f14b, 0xE8DCC0, LV_TEXT_ALIGN_CENTER);

  lv_obj_t* rp = box(scr, 84, 196, 116, 34, PANEL, 4, EDGE, 2);
  lab(rp, 6, 4, 20, "RX", f8, DIM);
  S.rx = lab(rp, 22, 1, 50, "—", f14b, VFD);
  lab(rp, 6, 18, 20, "TX", f8, DIM);
  S.tx = lab(rp, 22, 15, 50, "—", f14b, VFD);
  lab(rp, 64, 8, 46, "Мбит/с", f8, DIM, LV_TEXT_ALIGN_RIGHT);

  lab(scr, 208, 196, 56, "ГРОМК.", f8, 0xC9C1B0);
  S.vol = lab(scr, 208, 211, 56, "—", f14b, 0xE8DCC0);
  S.volArc = arcRing(scr, 286, 213, 14, 4, 0x4A443A, 0xE8DCC0, LV_OPA_COVER, LV_OPA_COVER);
}

static void setLeds(int row, float pct, bool on) {
  int lit = on ? (int)lroundf(constrain(pct, 0.0f, 100.0f) * 12.0f / 100.0f) : 0;
  for (int i = 0; i < 12; i++) {
    if (!S.led[row][i]) continue;
    uint32_t col = i < lit ? (i < 8 ? 0x46E06A : i < 10 ? 0xFFC53D : 0xFF5A4F) : 0x2C2924;
    lv_obj_set_style_bg_color(S.led[row][i], C(col), 0);
  }
}

static void updRetro(const PcStats& pc) {
  bool on = pc.online;
  if (S.sc[0] && S.needle[0])
    lv_scale_set_line_needle_value(S.sc[0], S.needle[0], 100, on ? (int32_t)lroundf(pc.cpu) : 0);
  if (!on) setText(S.sub[0], "нет связи");
  else if (pc.cpuTemp > 0) setFmt(S.sub[0], "%.0f°C · %.1f ГГц", pc.cpuTemp, pc.cpuFreqGHz);
  else setFmt(S.sub[0], "%.1f ГГц", pc.cpuFreqGHz);

  if (on && pc.hasGpu) setFmt(S.vfd[0], "%.0f%%", pc.gpuPct);
  else setText(S.vfd[0], "—");
  if (on && pc.cpuTemp > 0) {
    setText(S.vfdLbl[1], "t° CPU");
    setFmt(S.vfd[1], "%.0f°", pc.cpuTemp);
  } else if (on && pc.gpuTemp > 0) {
    setText(S.vfdLbl[1], "t° GPU");
    setFmt(S.vfd[1], "%.0f°", pc.gpuTemp);
  } else {
    setText(S.vfdLbl[1], "t° CPU");
    setText(S.vfd[1], "—");
  }
  Weather w = monitorWeather();
  if (w.ok) {
    setFmt(S.vfd[2], "%+.0f°", w.temp);
    setFmt(S.vfdLbl[2], "%s", weatherText(w.code));
  } else {
    setText(S.vfd[2], "—");
    setText(S.vfdLbl[2], "ПОГОДА");
  }

  if (on) {
    setFmt(S.ledLbl[0], "ОЗУ %.0f%%", pc.ramPct);
    setFmt(S.ledLbl[1], "VRAM %.0f%%", pc.vramPct);
    setFmt(S.rx, "%.1f", pc.netDownMbps);
    setFmt(S.tx, "%.1f", pc.netUpMbps);
    setFmt(S.vol, pc.muted ? "MUTE" : "%d", pc.volume);
  } else {
    setText(S.ledLbl[0], "ОЗУ");
    setText(S.ledLbl[1], "VRAM");
    setText(S.rx, "—");
    setText(S.tx, "—");
    setText(S.vol, "—");
  }
  setLeds(0, pc.ramPct, on);
  setLeds(1, pc.vramPct, on && pc.hasGpu);
  if (S.volArc) lv_arc_set_value(S.volArc, on && !pc.muted ? pc.volume : 0);
}

// ------------------------------------------------------------------ 4. Часы
static lv_obj_t* fullRing(lv_obj_t* p, int cx, int cy, int r, int w, uint32_t track, uint32_t col) {
  lv_obj_t* a = arcRing(p, cx, cy, r, w, track, col, LV_OPA_COVER, LV_OPA_COVER);
  lv_arc_set_bg_angles(a, 0, 360);
  lv_arc_set_rotation(a, 270);
  return a;
}

static void buildClockRings(lv_obj_t* scr) {
  lv_obj_set_style_bg_color(scr, C(0x000000), 0);
  S.clock = lab(scr, 0, 12, 320, "--:--", f84b, 0xFFFFFF, LV_TEXT_ALIGN_CENTER);
  S.net = lab(scr, 0, 104, 160, "", f14b, 0xCED4DA, LV_TEXT_ALIGN_RIGHT);
  wxPal(0xFFA51E, 0xDDE3EA, 0x45C5FF, 0x000000);
  wxNow(scr, 172, 98, 26, lab(scr, 202, 104, 50, "", f14b, 0xFFFFFF), lab(scr, 240, 106, 80, "", f9, 0x8E959E));

  static const int cxs[4] = {52, 124, 196, 268};
  static const char* names[4] = {"CPU", "GPU", "ОЗУ", "ТЕМП"};
  static const uint32_t cols[4] = {0xFFA51E, 0x45C5FF, 0x2CE65C, 0xFF3B6B};
  static const uint32_t trks[4] = {0x382305, 0x0C2836, 0x093014, 0x380B18};
  const int cy = 166;
  for (int i = 0; i < 4; i++) {
    S.ring[i] = fullRing(scr, cxs[i], cy, 27, 6, trks[i], cols[i]);
    S.rowVal[i] = lab(scr, cxs[i] - 28, cy - 8, 56, "—", f16b, 0xFFFFFF, LV_TEXT_ALIGN_CENTER);
    lab(scr, cxs[i] - 28, 206, 56, names[i], f9b, 0x8E959E, LV_TEXT_ALIGN_CENTER);
  }
}

static void updClockRings(const PcStats& pc) {
  bool on = pc.online;
  float mxTemp = max(pc.cpuTemp, pc.gpuTemp);
  float v[4] = {
      on ? pc.cpu : 0,
      on && pc.hasGpu ? pc.gpuPct : 0,
      on ? pc.ramPct : 0,
      on && mxTemp > 0 ? mxTemp : 0,
  };
  for (int i = 0; i < 4; i++) {
    int iv = constrain((int)lroundf(v[i]), 0, 100);
    if (S.ring[i] && lv_arc_get_value(S.ring[i]) != iv) lv_arc_set_value(S.ring[i], iv);
  }
  if (on) setFmt(S.rowVal[0], "%.0f%%", pc.cpu);
  else setText(S.rowVal[0], "—");
  if (on && pc.hasGpu) setFmt(S.rowVal[1], "%.0f%%", pc.gpuPct);
  else setText(S.rowVal[1], "—");
  if (on) setFmt(S.rowVal[2], "%.0f%%", pc.ramPct);
  else setText(S.rowVal[2], "—");
  if (on && mxTemp > 0) setFmt(S.rowVal[3], "%.0f°", mxTemp);
  else setText(S.rowVal[3], "—");

  char d[32] = "";
  time_t t = time(nullptr);
  if (t > 1700000000) {
    struct tm tm;
    localtime_r(&t, &tm);
    static const char* wd[7] = {"вс", "пн", "вт", "ср", "чт", "пт", "сб"};
    static const char* mo[12] = {"января", "февраля", "марта", "апреля", "мая", "июня",
                                 "июля", "августа", "сентября", "октября", "ноября", "декабря"};
    snprintf(d, sizeof(d), "%s, %d %s", wd[tm.tm_wday], tm.tm_mday, mo[tm.tm_mon]);
  }
  setText(S.net, d);
}

// ------------------------------------------------------------------ 5. Терминал
static void asciiBar(char* out, float pct) {
  int n = constrain((int)lroundf(pct * 19.0f / 100.0f), 0, 19);
  for (int i = 0; i < 19; i++) out[i] = i < n ? '#' : '.';
  out[19] = '\0';
}

static void buildTerminal(lv_obj_t* scr) {
  const uint32_t FG = 0x35E85A, DIM = 0x28C048;
  lv_obj_set_style_bg_color(scr, C(0x020A04), 0);
  for (int i = 0; i < 9; i++) {
    uint32_t col = (i == 0 || i == 8) ? DIM : FG;
    S.term[i] = lab(scr, 8, 6 + i * 25, 308, "", fMono, col);
  }
  S.cursor = box(scr, 188, 206, 10, 18, FG);
}

static const char* wxWord(int c) {
  if (c == 0) return "clear";
  if (c <= 2) return "partly";
  if (c == 3) return "overcast";
  if (c == 45 || c == 48) return "fog";
  if (c >= 51 && c <= 57) return "drizzle";
  if (c >= 61 && c <= 67) return "rain";
  if (c >= 71 && c <= 77) return "snow";
  if (c >= 80 && c <= 82) return "shower";
  if (c == 85 || c == 86) return "snow";
  if (c >= 95) return "storm";
  return "--";
}

static void updTerminal(const PcStats& pc) {
  bool on = pc.online;
  char h[20] = "pc";
  if (on && pc.host[0]) {
    int j = 0;
    for (int i = 0; pc.host[i] && j < 14; i++) {
      unsigned char ch = (unsigned char)pc.host[i];
      if (ch < 128 && (isalnum(ch) || ch == '-' || ch == '_')) h[j++] = (char)tolower(ch);
    }
    h[j] = '\0';
    if (!h[0]) strcpy(h, "pc");
  }
  setFmt(S.term[0], "%s:~$ sysmon --live", h);

  char b[20];
  asciiBar(b, on ? pc.cpu : 0);
  if (on) setFmt(S.term[1], "CPU [%s] %3.0f%%", b, pc.cpu);
  else setFmt(S.term[1], "CPU [%s]  --%%", b);

  asciiBar(b, on && pc.hasGpu ? pc.gpuPct : 0);
  if (on && pc.hasGpu) setFmt(S.term[2], "GPU [%s] %3.0f%%", b, pc.gpuPct);
  else setFmt(S.term[2], "GPU [%s]  --%%", b);

  asciiBar(b, on ? pc.ramPct : 0);
  if (on) setFmt(S.term[3], "RAM [%s] %3.0f%%", b, pc.ramPct);
  else setFmt(S.term[3], "RAM [%s]  --%%", b);

  char ct[12] = "--C", gt[12] = "--C";
  if (on && pc.cpuTemp > 0) snprintf(ct, sizeof(ct), "%.0fC", pc.cpuTemp);
  if (on && pc.gpuTemp > 0) snprintf(gt, sizeof(gt), "%.0fC", pc.gpuTemp);
  setFmt(S.term[4], "TMP cpu %-5s gpu %s", ct, gt);

  if (on) {
    setFmt(S.term[5], "NET rx %-5.1f tx %.1f Mbit/s", pc.netDownMbps, pc.netUpMbps);
    if (pc.muted) setText(S.term[6], "VOL mute");
    else setFmt(S.term[6], "VOL %d", pc.volume);
  } else {
    setText(S.term[5], "NET offline");
    setText(S.term[6], "VOL --");
  }
  Weather w = monitorWeather();
  if (w.ok) setFmt(S.term[7], "WTR %+.0fC %s wind %.0fm/s", w.temp, wxWord(w.code), w.wind);
  else setText(S.term[7], "WTR n/a");

  time_t t = time(nullptr);
  char tb[32];
  if (t > 1700000000) {
    struct tm tm;
    localtime_r(&t, &tm);
    static const char* wd[7] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};
    static const char* mo[12] = {"jan", "feb", "mar", "apr", "may", "jun",
                                 "jul", "aug", "sep", "oct", "nov", "dec"};
    snprintf(tb, sizeof(tb), "%02d:%02d:%02d  %s %d %s", tm.tm_hour, tm.tm_min, tm.tm_sec,
             wd[tm.tm_wday], tm.tm_mday, mo[tm.tm_mon]);
  } else {
    strcpy(tb, "--:--:--");
  }
  setText(S.term[8], tb);
  if (S.cursor) {
    int cx = 8 + (int)strlen(tb) * 9 + 4;
    lv_obj_set_pos(S.cursor, min(cx, 304), 6 + 8 * 25);
    if ((millis() / 500) & 1) lv_obj_add_flag(S.cursor, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(S.cursor, LV_OBJ_FLAG_HIDDEN);
  }
}

// ------------------------------------------------------------------ 6. Плитки
static void buildTiles(lv_obj_t* scr) {
  lv_obj_set_style_bg_color(scr, C(0x0B0C10), 0);
  static const int xs[4] = {8, 162, 8, 162};
  static const int ys[4] = {8, 8, 98, 98};
  static const char* titles[4] = {"CPU", "GPU", "ПАМЯТЬ", "СЕТЬ"};
  static const uint32_t bgs[4] = {0x1F64F2, 0x763AF5, 0x0C7465, 0xB8530A};
  static const uint32_t trks[4] = {0x5A8DF7, 0x9C6FF8, 0x45968A, 0xCC7C43};
  for (int i = 0; i < 4; i++) {
    lv_obj_t* c = box(scr, xs[i], ys[i], 150, 84, bgs[i], 12);
    lab(c, 10, 6, 130, titles[i], f9b, 0xE6ECFF);
    S.cellVal[i] = lab(c, 9, 20, 134, "—", f36b, 0xFFFFFF);
    S.rowSub[i] = lab(c, 10, 58, 132, "", f8, 0xF0F4FF);
    S.cellBar[i] = bar(c, 10, 75, 130, 3, trks[i], 0xFFFFFF);
  }
  S.clock = lab(scr, 10, 194, 92, "--:--", f30b, 0xFFFFFF);
  S.date = lab(scr, 104, 198, 128, "", f9, 0xD0D5DD);
  S.host = lab(scr, 104, 214, 96, "", f9, 0x7A828E);
  lv_obj_t* wt = box(scr, 196, 190, 116, 44, 0x161A22, 12);
  wxPal(0xFFC83D, 0xE6ECFF, 0x5A8DF7, 0x161A22);
  wxNow(wt, 8, 5, 34, lab(wt, 46, 2, 62, "", f20b, 0xFFFFFF, LV_TEXT_ALIGN_RIGHT),
        lab(wt, 40, 26, 68, "", f8, 0xBDC4CE, LV_TEXT_ALIGN_RIGHT));
}

static void updTiles(const PcStats& pc) {
  bool on = pc.online;
  if (on) {
    setFmt(S.cellVal[0], "%.0f%%", pc.cpu);
    if (pc.cpuTemp > 0) setFmt(S.rowSub[0], "%.0f°C · %.1f ГГц", pc.cpuTemp, pc.cpuFreqGHz);
    else setFmt(S.rowSub[0], "%.1f ГГц", pc.cpuFreqGHz);
  } else {
    setText(S.cellVal[0], "—");
    setText(S.rowSub[0], "нет связи");
  }
  setBar(S.cellBar[0], on ? pc.cpu : 0);

  if (on && pc.hasGpu) {
    setFmt(S.cellVal[1], "%.0f%%", pc.gpuPct);
    if (pc.gpuTemp > 0) setFmt(S.rowSub[1], "%.0f°C · VRAM %.0f%%", pc.gpuTemp, pc.vramPct);
    else setFmt(S.rowSub[1], "VRAM %.0f%%", pc.vramPct);
  } else {
    setText(S.cellVal[1], "—");
    setText(S.rowSub[1], on ? "нет GPU" : "");
  }
  setBar(S.cellBar[1], on && pc.hasGpu ? pc.gpuPct : 0);

  if (on) {
    setFmt(S.cellVal[2], "%.1f", pc.ramUsedGB);
    setFmt(S.rowSub[2], "из %.0f ГБ", pc.ramTotalGB);
    setFmt(S.cellVal[3], "%.1f", pc.netDownMbps);
    setFmt(S.rowSub[3], "↓ Мбит/с · ↑ %.1f", pc.netUpMbps);
  } else {
    setText(S.cellVal[2], "—");
    setText(S.rowSub[2], "");
    setText(S.cellVal[3], "—");
    setText(S.rowSub[3], "");
  }
  setBar(S.cellBar[2], on ? pc.ramPct : 0);
  setBar(S.cellBar[3], on ? min(100.0f, pc.netDownMbps) : 0);
}

// ------------------------------------------------------------------ 7. Лампы
static lv_obj_t* nixieTube(lv_obj_t* scr, int x, int y, int w, int h, bool big) {
  lv_obj_t* t = box(scr, x, y, w, h, 0x160E08, big ? 14 : 9, 0x433120, 1);
  int cw = w - (big ? 16 : 10);
  box(t, (w - cw) / 2, h - (big ? 8 : 6), cw, big ? 3 : 2, 0x3A291A, 1);
  return lab(t, 0, big ? 8 : 3, w - 2, "-", big ? f64b : f36b, 0xFF8A2B, LV_TEXT_ALIGN_CENTER);
}

static void buildNixie(lv_obj_t* scr) {
  lv_obj_set_style_bg_color(scr, C(0x0E0905), 0);
  static const int cxs[4] = {34, 92, 176, 234};
  for (int i = 0; i < 4; i++) S.nixClk[i] = nixieTube(scr, cxs[i], 10, 52, 86, true);
  box(scr, 155, 38, 6, 6, 0xFF8A2B, 3);
  box(scr, 155, 64, 6, 6, 0xFF8A2B, 3);

  static const int gxs[3] = {26, 126, 226};
  static const char* gnames[3] = {"CPU, %", "GPU, %", "ОЗУ, %"};
  for (int g = 0; g < 3; g++) {
    lab(scr, gxs[g], 108, 68, gnames[g], f9b, 0xD2C5B4, LV_TEXT_ALIGN_CENTER);
    S.nixVal[g][0] = nixieTube(scr, gxs[g], 126, 32, 52, false);
    S.nixVal[g][1] = nixieTube(scr, gxs[g] + 36, 126, 32, 52, false);
  }

  lv_obj_t* bot = box(scr, 8, 190, 304, 42, 0x150E08, 8, 0x6E5233, 1);
  S.sub[0] = lab(bot, 10, 5, 86, "CPU —", f14b, 0xE6D7C3);
  S.sub[1] = lab(bot, 10, 21, 86, "GPU —", f14b, 0xE6D7C3);
  S.rx = lab(bot, 98, 5, 114, "↓ — Мбит/с", f14b, 0xE6D7C3, LV_TEXT_ALIGN_LEFT);
  S.tx = lab(bot, 98, 21, 114, "↑ — Мбит/с", f14b, 0xE6D7C3, LV_TEXT_ALIGN_LEFT);
  wxPal(0xFF8A2B, 0xC9B49A, 0xFFB46B, 0x150E08);
  wxNow(bot, 222, 4, 30, lab(bot, 252, 11, 44, "", f16b, 0xFF8A2B, LV_TEXT_ALIGN_RIGHT));
}

static void setNixPair(lv_obj_t* a, lv_obj_t* b, float v, bool ok) {
  if (!ok) {
    setText(a, "-");
    setText(b, "-");
    return;
  }
  int iv = constrain((int)lroundf(v), 0, 99);
  setFmt(a, "%d", iv / 10);
  setFmt(b, "%d", iv % 10);
}

static void updNixie(const PcStats& pc) {
  time_t t = time(nullptr);
  if (t > 1700000000) {
    struct tm tm;
    localtime_r(&t, &tm);
    setFmt(S.nixClk[0], "%d", tm.tm_hour / 10);
    setFmt(S.nixClk[1], "%d", tm.tm_hour % 10);
    setFmt(S.nixClk[2], "%d", tm.tm_min / 10);
    setFmt(S.nixClk[3], "%d", tm.tm_min % 10);
  } else {
    for (int i = 0; i < 4; i++) setText(S.nixClk[i], "-");
  }
  bool on = pc.online;
  setNixPair(S.nixVal[0][0], S.nixVal[0][1], pc.cpu, on);
  setNixPair(S.nixVal[1][0], S.nixVal[1][1], pc.gpuPct, on && pc.hasGpu);
  setNixPair(S.nixVal[2][0], S.nixVal[2][1], pc.ramPct, on);

  if (on && pc.cpuTemp > 0) setFmt(S.sub[0], "CPU %.0f°C", pc.cpuTemp);
  else setText(S.sub[0], "CPU —");
  if (on && pc.gpuTemp > 0) setFmt(S.sub[1], "GPU %.0f°C", pc.gpuTemp);
  else setText(S.sub[1], "GPU —");

  if (on) {
    setFmt(S.rx, "↓ %.1f Мбит/с", pc.netDownMbps);
    setFmt(S.tx, "↑ %.1f Мбит/с", pc.netUpMbps);
  } else {
    setText(S.rx, "↓ — Мбит/с");
    setText(S.tx, "↑ — Мбит/с");
  }
}

// ------------------------------------------------------------------ 8. Импульс
static void buildImpulse(lv_obj_t* scr) {
  const uint32_t CYAN = 0x24D8FF, MUT = 0x8AA2B8;
  lv_obj_set_style_bg_color(scr, C(0x07111D), 0);
  wxPal(0xFFC83D, 0xCFE6F5, 0x24D8FF, 0x07111D);
  wxNow(scr, 12, 3, 22, lab(scr, 38, 6, 50, "", f14b, 0xEAF7FF), lab(scr, 82, 8, 110, "", f9, MUT));
  S.clock = lab(scr, 210, 6, 98, "--:--", f14b, 0xEAF7FF, LV_TEXT_ALIGN_RIGHT);
  box(scr, 12, 28, 296, 1, 0x153047);
  S.ring[0] = arcRing(scr, 83, 116, 59, 8, 0x153047, CYAN, LV_OPA_COVER, LV_OPA_COVER);
  S.big = lab(scr, 42, 98, 82, "—", f30b, 0xEAF7FF, LV_TEXT_ALIGN_CENTER);
  S.bigSub = lab(scr, 42, 128, 82, "CPU", f9b, MUT, LV_TEXT_ALIGN_CENTER);
  lab(scr, 36, 184, 94, "НАГРУЗКА CPU", f8, MUT, LV_TEXT_ALIGN_CENTER);
  static const char* labels[3] = {"GPU", "ОЗУ", "ДИСК"};
  static const uint32_t cols[3] = {0x8B6CFF, 0x42E6B5, 0xFFB454};
  for (int i = 0; i < 3; ++i) {
    int y = 48 + i * 45;
    lab(scr, 158, y, 46, labels[i], f9b, MUT);
    S.rowVal[i] = lab(scr, 206, y - 2, 98, "—", f16b, cols[i], LV_TEXT_ALIGN_RIGHT);
    S.rowSub[i] = lab(scr, 158, y + 18, 146, "", f8, MUT);
    S.mbar[i] = bar(scr, 158, y + 34, 146, 3, 0x153047, cols[i]);
  }
  box(scr, 12, 202, 296, 1, 0x153047);
  S.net = lab(scr, 14, 211, 292, "↓ —   ↑ — Мбит/с", f11, CYAN, LV_TEXT_ALIGN_CENTER);
}

static void updImpulse(const PcStats& pc) {
  bool on = pc.online;
  int cpu = on ? constrain((int)lroundf(pc.cpu), 0, 100) : 0;
  if (S.ring[0] && lv_arc_get_value(S.ring[0]) != cpu) lv_arc_set_value(S.ring[0], cpu);
  if (on) setFmt(S.big, "%.0f%%", pc.cpu); else setText(S.big, "—");
  if (on && pc.cpuTemp > 0) setFmt(S.bigSub, "CPU · %.0f°C", pc.cpuTemp); else setText(S.bigSub, "CPU");
  if (on && pc.hasGpu) {
    setFmt(S.rowVal[0], "%.0f%%", pc.gpuPct);
    if (pc.gpuTemp > 0) setFmt(S.rowSub[0], "%.0f°C · VRAM %.0f%%", pc.gpuTemp, pc.vramPct);
    else setFmt(S.rowSub[0], "VRAM %.0f%%", pc.vramPct);
  } else { setText(S.rowVal[0], "—"); setText(S.rowSub[0], on ? "нет видеокарты" : "нет связи"); }
  if (on) {
    setFmt(S.rowVal[1], "%.0f%%", pc.ramPct);
    setFmt(S.rowSub[1], "%.1f / %.0f ГБ", pc.ramUsedGB, pc.ramTotalGB);
    setFmt(S.rowVal[2], "%.0f%%", pc.diskPct);
    setFmt(S.rowSub[2], "%.0f / %.0f ГБ", pc.diskUsedGB, pc.diskTotalGB);
  } else {
    for (int i = 1; i < 3; ++i) { setText(S.rowVal[i], "—"); setText(S.rowSub[i], "нет связи"); }
  }
  setBar(S.mbar[0], on && pc.hasGpu ? pc.gpuPct : 0);
  setBar(S.mbar[1], on ? pc.ramPct : 0);
  setBar(S.mbar[2], on ? pc.diskPct : 0);
  if (on) setFmt(S.net, "↓ %.1f   ↑ %.1f Мбит/с", pc.netDownMbps, pc.netUpMbps);
  else setText(S.net, "ПК не подключён");
}

// ------------------------------------------------------------------ 9. Шина
static void buildOrbit(lv_obj_t* scr) {
  const uint32_t MUT = 0x8295A8, TRACE = 0x24465B;
  lv_obj_set_style_bg_color(scr, C(0x081018), 0);
  wxPal(0xFFC83D, 0xCFE0EE, 0x34D5FF, 0x081018);
  wxNow(scr, 12, 3, 22, lab(scr, 38, 6, 50, "", f14b, 0xEAF5FF), lab(scr, 82, 8, 110, "", f9, MUT));
  S.clock = lab(scr, 210, 6, 98, "--:--", f14b, 0xEAF5FF, LV_TEXT_ALIGN_RIGHT);
  box(scr, 12, 28, 296, 1, 0x1C3344);

  // Дорожки уходят под модули, как на схеме системной платы.
  box(scr, 104, 68, 13, 2, TRACE); box(scr, 110, 68, 2, 26, TRACE); box(scr, 112, 92, 5, 2, TRACE);
  box(scr, 203, 68, 13, 2, TRACE); box(scr, 209, 68, 2, 32, TRACE); box(scr, 204, 98, 7, 2, TRACE);
  box(scr, 104, 180, 13, 2, TRACE); box(scr, 110, 144, 2, 38, TRACE); box(scr, 112, 144, 5, 2, TRACE);
  box(scr, 203, 180, 13, 2, TRACE); box(scr, 209, 144, 2, 38, TRACE); box(scr, 204, 144, 7, 2, TRACE);

  lv_obj_t* gpu = box(scr, 10, 42, 94, 54, 0x101C28, 7, 0x25485A, 1);
  lab(gpu, 8, 5, 76, "GPU", f8, 0x91A7B7);
  S.rowVal[0] = lab(gpu, 8, 18, 78, "—", f16b, 0x34D5FF);
  S.rowSub[0] = lab(gpu, 8, 38, 78, "", f8, 0x8295A8);

  lv_obj_t* ram = box(scr, 216, 42, 94, 54, 0x101C28, 7, 0x25485A, 1);
  lab(ram, 8, 5, 76, "ОЗУ", f8, 0x91A7B7);
  S.rowVal[1] = lab(ram, 8, 18, 78, "—", f16b, 0x45E0B2);
  S.rowSub[1] = lab(ram, 8, 38, 78, "", f8, 0x8295A8);

  lv_obj_t* disk = box(scr, 10, 154, 94, 54, 0x101C28, 7, 0x25485A, 1);
  lab(disk, 8, 5, 76, "NVME / DISK", f8, 0x91A7B7);
  S.rowVal[2] = lab(disk, 8, 18, 78, "—", f16b, 0xFFB65A);
  S.rowSub[2] = lab(disk, 8, 38, 78, "", f8, 0x8295A8);

  lv_obj_t* net = box(scr, 216, 154, 94, 54, 0x101C28, 7, 0x25485A, 1);
  lab(net, 8, 5, 76, "NETWORK", f8, 0x91A7B7);
  S.rowVal[3] = lab(net, 8, 18, 78, "—", f14b, 0xFF66AA);
  S.rowSub[3] = lab(net, 8, 38, 78, "", f8, 0x8295A8);

  lv_obj_t* core = box(scr, 116, 82, 88, 72, 0x122333, 9, 0x38BFD5, 2);
  box(core, 7, 7, 4, 4, 0x38BFD5, 1); box(core, 77, 7, 4, 4, 0x38BFD5, 1);
  box(core, 7, 61, 4, 4, 0x38BFD5, 1); box(core, 77, 61, 4, 4, 0x38BFD5, 1);
  lab(core, 12, 8, 64, "CORE / 01", f8, 0x8295A8, LV_TEXT_ALIGN_CENTER);
  S.big = lab(core, 5, 24, 78, "—", f20b, 0xF2FAFF, LV_TEXT_ALIGN_CENTER);
  S.bigSub = lab(core, 6, 51, 76, "CPU", f8, 0x38BFD5, LV_TEXT_ALIGN_CENTER);
  box(scr, 12, 218, 296, 1, 0x1C3344);
  S.net = lab(scr, 14, 222, 292, "SYSTEM BUS · ↓ —  ↑ — Мбит/с", f8, 0x38BFD5, LV_TEXT_ALIGN_CENTER);
}

static void updOrbit(const PcStats& pc) {
  bool on = pc.online;
  if (on) {
    setFmt(S.big, "%.0f%%", pc.cpu);
    if (pc.cpuTemp > 0) setFmt(S.bigSub, "CPU · %.0f°C", pc.cpuTemp); else setText(S.bigSub, "CPU");
    if (pc.hasGpu) {
      setFmt(S.rowVal[0], "%.0f%%", pc.gpuPct);
      if (pc.gpuTemp > 0) setFmt(S.rowSub[0], "%.0f°C · V %.0f%%", pc.gpuTemp, pc.vramPct);
      else setFmt(S.rowSub[0], "VRAM %.0f%%", pc.vramPct);
    } else { setText(S.rowVal[0], "—"); setText(S.rowSub[0], "нет GPU"); }
    setFmt(S.rowVal[1], "%.0f%%", pc.ramPct);
    setFmt(S.rowSub[1], "%.1f / %.0f ГБ", pc.ramUsedGB, pc.ramTotalGB);
    setFmt(S.rowVal[2], "%.0f%%", pc.diskPct);
    setFmt(S.rowSub[2], "%.0f / %.0f ГБ", pc.diskUsedGB, pc.diskTotalGB);
    setFmt(S.rowVal[3], "↓ %.1f", pc.netDownMbps);
    setFmt(S.rowSub[3], "↑ %.1f Мбит/с", pc.netUpMbps);
    setFmt(S.net, "SYSTEM BUS · ↓ %.1f  ↑ %.1f Мбит/с", pc.netDownMbps, pc.netUpMbps);
  } else {
    setText(S.big, "—"); setText(S.bigSub, "CPU");
    for (int i = 0; i < 4; ++i) { setText(S.rowVal[i], "—"); setText(S.rowSub[i], "нет связи"); }
    setText(S.net, "SYSTEM BUS · ПК не подключён");
  }
}

// ------------------------------------------------------------------ 10. HUD
static void hudCorner(lv_obj_t* scr, int x, int y, int dx, int dy, uint32_t c) {
  box(scr, dx > 0 ? x : x - 9, y, 10, 2, c);
  box(scr, x, dy > 0 ? y : y - 9, 2, 10, c);
}

static void buildHud(lv_obj_t* scr) {
  const uint32_t MUT = 0x8995A7, EDGE = 0x3B4654;
  lv_obj_set_style_bg_color(scr, C(0x0B0D12), 0);
  S.host = lab(scr, 12, 7, 100, "", f9b, MUT);
  S.net = lab(scr, 100, 7, 120, "", f9b, 0x42E6B5, LV_TEXT_ALIGN_CENTER);
  S.clock = lab(scr, 210, 6, 98, "--:--", f14b, 0xF4F7FC, LV_TEXT_ALIGN_RIGHT);
  box(scr, 12, 28, 296, 1, 0x27303D);
  static const char* names[4] = {"01 CPU", "02 GPU", "03 RAM", "04 SSD"};
  static const uint32_t colors[4] = {0xFF4E9B, 0x27D9F2, 0xB18CFF, 0xFFB454};
  for (int i = 0; i < 4; ++i) {
    int y = 36 + i * 32;
    lab(scr, 14, y + 1, 66, names[i], f9b, 0xC9D1DC);
    S.cellBar[i] = bar(scr, 84, y + 5, 160, 6, 0x222A35, colors[i]);
    for (int k = 1; k < 10; ++k) box(scr, 84 + k * 16, y + 2, 1, 12, EDGE);
    S.cellVal[i] = lab(scr, 246, y - 2, 62, "—", f16b, colors[i], LV_TEXT_ALIGN_RIGHT);
    S.rowSub[i] = lab(scr, 84, y + 15, 224, "", f8, MUT);
  }
  // нижняя панель: погода и прогноз в «прицельных» уголках
  hudCorner(scr, 8, 168, 1, 1, EDGE);
  hudCorner(scr, 310, 168, -1, 1, EDGE);
  hudCorner(scr, 8, 234, 1, -1, EDGE);
  hudCorner(scr, 310, 234, -1, -1, EDGE);
  wxPal(0xFFB454, 0xC9D1DC, 0x27D9F2, 0x0B0D12);
  wxNow(scr, 16, 178, 38, lab(scr, 58, 174, 92, "", f24, 0xF4F7FC),
        lab(scr, 58, 204, 96, "", f8, MUT));
  box(scr, 156, 176, 1, 50, 0x27303D);
  S.fcStep = 3;
  for (int i = 0; i < 3; i++) wxSlot(scr, 162 + i * 49, 174, 48, 22, f8, f9b, MUT, 0xF4F7FC);
}

static void updHud(const PcStats& pc) {
  bool on = pc.online;
  float v[4] = {pc.cpu, pc.gpuPct, pc.ramPct, pc.diskPct};
  bool valid[4] = {on, on && pc.hasGpu, on, on};
  for (int i = 0; i < 4; ++i) {
    if (valid[i]) setFmt(S.cellVal[i], "%.0f%%", v[i]); else setText(S.cellVal[i], "—");
    setBar(S.cellBar[i], valid[i] ? v[i] : 0);
  }
  if (on) {
    if (pc.cpuTemp > 0) setFmt(S.rowSub[0], "%.0f°C · %.1f ГГц", pc.cpuTemp, pc.cpuFreqGHz);
    else setFmt(S.rowSub[0], "%.1f ГГц", pc.cpuFreqGHz);
    if (pc.hasGpu && pc.gpuTemp > 0) setFmt(S.rowSub[1], "%.0f°C · VRAM %.1f / %.0f ГБ", pc.gpuTemp, pc.vramUsedGB, pc.vramTotalGB);
    else setText(S.rowSub[1], pc.hasGpu ? "" : "нет видеокарты");
    setFmt(S.rowSub[2], "%.1f / %.0f ГБ", pc.ramUsedGB, pc.ramTotalGB);
    setFmt(S.rowSub[3], "%.0f / %.0f ГБ", pc.diskUsedGB, pc.diskTotalGB);
    setFmt(S.net, "↓ %.1f  ↑ %.1f", pc.netDownMbps, pc.netUpMbps);
  } else {
    for (int i = 0; i < 4; i++) setText(S.rowSub[i], "");
    setText(S.net, "");
  }
}

// ------------------------------------------------------------------ 11. Сканлайн
static void buildScanline(lv_obj_t* scr) {
  const uint32_t MUT = 0x8297A5, CYAN = 0x42D8FF, LINE = 0x1D3440;
  lv_obj_set_style_bg_color(scr, C(0x080D12), 0);
  S.host = lab(scr, 12, 7, 100, "", f9b, MUT);
  S.net = lab(scr, 100, 7, 120, "", f9b, CYAN, LV_TEXT_ALIGN_CENTER);
  S.clock = lab(scr, 210, 6, 98, "--:--", f14b, 0xE9F6FF, LV_TEXT_ALIGN_RIGHT);
  box(scr, 12, 28, 296, 1, LINE);
  lab(scr, 14, 38, 100, "CPU · 48 С", f9b, MUT);
  S.rowVal[3] = lab(scr, 110, 33, 80, "—", f16b, 0xFF4E9B, LV_TEXT_ALIGN_RIGHT);
  lv_obj_t* frame = box(scr, 12, 56, 180, 96, 0x0B141B, 6, LINE, 1);
  S.chart = sparkline(frame, 2, 4, 176, 88, CYAN, &S.ser);
  lv_chart_set_div_line_count(S.chart, 4, 0);
  lv_obj_set_style_line_color(S.chart, C(0x16303C), LV_PART_MAIN);
  lv_obj_set_style_line_width(S.chart, 1, LV_PART_MAIN);
  lv_obj_set_style_line_dash_width(S.chart, 2, LV_PART_MAIN);
  lv_obj_set_style_line_dash_gap(S.chart, 3, LV_PART_MAIN);
  box(scr, 201, 38, 1, 114, LINE);
  lab(scr, 210, 38, 94, "GPU", f9b, MUT);
  S.rowVal[0] = lab(scr, 210, 52, 94, "—", f20b, CYAN);
  S.rowSub[0] = lab(scr, 210, 77, 98, "", f8, MUT);
  box(scr, 210, 96, 94, 1, LINE);
  lab(scr, 210, 102, 94, "ОЗУ", f9b, MUT);
  S.rowVal[1] = lab(scr, 210, 116, 94, "—", f20b, 0x42E6B5);
  S.rowSub[1] = lab(scr, 210, 141, 98, "", f8, MUT);
  box(scr, 12, 162, 296, 1, LINE);
  wxPal(0xFFC83D, 0xCFE6F5, CYAN, 0x080D12);
  S.wxLong = true;
  wxNow(scr, 12, 172, 42, lab(scr, 60, 168, 90, "", f30b, 0xE9F6FF),
        lab(scr, 60, 206, 110, "", f8, MUT));
  S.fcStep = 3;
  for (int i = 0; i < 3; i++) wxSlot(scr, 172 + i * 46, 168, 46, 24, f8, f9b, MUT, 0xE9F6FF);
}

static void updScanline(const PcStats& pc) {
  bool on = pc.online;
  if (on && pc.hasGpu) {
    setFmt(S.rowVal[0], "%.0f%%", pc.gpuPct);
    if (pc.gpuTemp > 0) setFmt(S.rowSub[0], "%.0f°C · VRAM %.0f%%", pc.gpuTemp, pc.vramPct);
    else setFmt(S.rowSub[0], "VRAM %.0f%%", pc.vramPct);
  } else { setText(S.rowVal[0], "—"); setText(S.rowSub[0], on ? "нет видеокарты" : "нет связи"); }
  if (on) {
    setFmt(S.rowVal[1], "%.0f%%", pc.ramPct);
    setFmt(S.rowSub[1], "%.1f / %.0f ГБ", pc.ramUsedGB, pc.ramTotalGB);
    if (pc.cpuTemp > 0) setFmt(S.rowVal[3], "%.0f%% %.0f°", pc.cpu, pc.cpuTemp);
    else setFmt(S.rowVal[3], "%.0f%%", pc.cpu);
    setFmt(S.net, "↓ %.1f  ↑ %.1f", pc.netDownMbps, pc.netUpMbps);
  } else {
    setText(S.rowVal[1], "—"); setText(S.rowSub[1], "");
    setText(S.rowVal[3], "—");
    setText(S.net, "");
  }
  feedChart();
}

// ------------------------------------------------------------------ заставка
static const int IDLE = 100;
struct IdlePal {
  uint32_t bg, fg, mut, acc, line;
};
static const IdlePal IDLE_PAL[SKIN_N] = {
    {0x0C0E11, 0xFFFFFF, 0xAAB1BC, 0xFF9F1C, 0x2B2F35},
    {0x07060D, 0xFFFFFF, 0xB9B3E0, 0x22E3FF, 0x1B1830},
    {0xF4F2EE, 0x16161A, 0x5C5C66, 0xC2410C, 0xD9D5CC},
    {0x1C1A17, 0xE8DCC0, 0xC9C1B0, 0x5EF2D6, 0x4A443A},
    {0x000000, 0xFFFFFF, 0x8E959E, 0xFFA51E, 0x262930},
    {0x020A04, 0x35E85A, 0x249E3D, 0x35E85A, 0x12381A},
    {0x0B0C10, 0xFFFFFF, 0x9AA2B1, 0x1F64F2, 0x232733},
    {0x0E0905, 0xE6D7C3, 0xA89682, 0xFF8A2B, 0x433120},
    {0x07111D, 0xEAF7FF, 0x8AA2B8, 0x24D8FF, 0x153047},
    {0x070B16, 0xF0F5FF, 0x8A98B8, 0x8B6CFF, 0x202B4A},
    {0x0B0D12, 0xF4F7FC, 0x8995A7, 0x42E6B5, 0x27303D},
    {0x080D12, 0xE9F6FF, 0x8297A5, 0xFF4E9B, 0x1D3440},
};

static uint32_t mix(uint32_t a, uint32_t b, uint8_t k) {
  return lv_color_to_u32(lv_color_mix(C(a), C(b), k)) & 0xFFFFFF;
}

void skinBuildIdle(lv_obj_t* scr, int baseSkin) {
  fonts();
  styles();
  skinClear();
  S.idx = IDLE;
  const IdlePal& p = IDLE_PAL[constrain(baseSkin, 0, SKIN_N - 1)];
  lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(scr, C(p.bg), 0);
  S.clock = lab(scr, 0, -2, 320, "--:--", f76b, p.fg, LV_TEXT_ALIGN_CENTER);
  S.date = lab(scr, 0, 88, 320, "", f14b, p.mut, LV_TEXT_ALIGN_CENTER);
  box(scr, 24, 112, 272, 1, p.line);
  wxPal(p.acc, mix(p.fg, p.mut, 110), p.bg == 0xF4F2EE ? 0x2F6FD6 : mix(0x4CC9F0, p.acc, 210), p.bg);
  S.wxLong = false;
  wxNow(scr, 22, 120, 46, lab(scr, 74, 120, 80, "", f30b, p.acc));
  S.wDesc = lab(scr, 160, 122, 150, "", f14b, p.fg);
  S.wSub = lab(scr, 160, 142, 150, "", f9, p.mut);
  box(scr, 24, 172, 272, 1, p.line);
  S.fcStep = 3;
  for (int i = 0; i < 4; i++) wxSlot(scr, 16 + i * 74, 176, 66, 22, f9, f9b, p.mut, p.fg);
}

static void updIdle(const WifiInfo& w) {
  settingsLock();
  String city = gSet.cityName;
  settingsUnlock();
  time_t t = time(nullptr);
  if (t > 1700000000) {
    struct tm tm;
    localtime_r(&t, &tm);
    char b[64];
    strftime(b, sizeof(b), "%H:%M", &tm);
    setText(S.clock, b);
    static const char* wd[7] = {"воскресенье", "понедельник", "вторник", "среда", "четверг", "пятница", "суббота"};
    static const char* mo[12] = {"января", "февраля", "марта", "апреля", "мая", "июня",
                                 "июля", "августа", "сентября", "октября", "ноября", "декабря"};
    if (city.length()) snprintf(b, sizeof(b), "%s, %d %s · %s", wd[tm.tm_wday], tm.tm_mday, mo[tm.tm_mon], city.c_str());
    else snprintf(b, sizeof(b), "%s, %d %s", wd[tm.tm_wday], tm.tm_mday, mo[tm.tm_mon]);
    setText(S.date, b);
  }
  updWeather();
  Weather wx = monitorWeather();
  if (wx.ok) {
    setText(S.wDesc, weatherText(wx.code));
    setFmt(S.wSub, "ощущается %+.0f° · %.0f м/с", wx.feels, wx.wind);
  } else {
    setText(S.wDesc, w.connected ? "Погода загружается" : "Погоды нет");
    setText(S.wSub, w.connected ? "" : "экран не подключён к Wi-Fi");
  }
}

// ------------------------------------------------------------------ общее
void skinBuild(lv_obj_t* scr, int idx) {
  fonts();
  styles();
  skinClear();
  S.idx = constrain(idx, 0, SKIN_N - 1);
  lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  switch (S.idx) {
    case 1: buildNeon(scr); break;
    case 2: buildMinimal(scr); break;
    case 3: buildRetro(scr); break;
    case 4: buildClockRings(scr); break;
    case 5: buildTerminal(scr); break;
    case 6: buildTiles(scr); break;
    case 7: buildNixie(scr); break;
    case 8: buildImpulse(scr); break;
    case 9: buildOrbit(scr); break;
    case 10: buildHud(scr); break;
    case 11: buildScanline(scr); break;
    default: buildAnalog(scr); break;
  }
}

void skinUpdate(const PcStats& pc, const WifiInfo& w) {
  if (S.idx < 0) return;
  if (S.idx == IDLE) {
    updIdle(w);
    return;
  }

  if (pc.online && pc.seenMs != histSeen) {
    histSeen = pc.seenMs;
    if (histN < 48) hist[histN++] = pc.cpu;
    else {
      memmove(hist, hist + 1, 47 * sizeof(float));
      hist[47] = pc.cpu;
    }
  }

  // часы и имя ПК
  time_t t = time(nullptr);
  if (t > 1700000000) {
    struct tm tm;
    localtime_r(&t, &tm);
    char b[32];
    strftime(b, sizeof(b), S.idx == 3 ? "%H:%M:%S" : "%H:%M", &tm);
    setText(S.clock, b);
    if (S.date) {
      static const char* wd[7] = {"вс", "пн", "вт", "ср", "чт", "пт", "сб"};
      static const char* mo[12] = {"января", "февраля", "марта", "апреля", "мая", "июня",
                                   "июля", "августа", "сентября", "октября", "ноября", "декабря"};
      snprintf(b, sizeof(b), "%s, %d %s", wd[tm.tm_wday], tm.tm_mday, mo[tm.tm_mon]);
      setText(S.date, b);
    }
  }
  if (S.host) setText(S.host, pc.online ? pc.host : (S.idx == 7 ? "нет связи" : "нет связи с ПК"));
  updWeather();

  switch (S.idx) {
    case 1: updNeon(pc); break;
    case 2: updMinimal(pc); break;
    case 3: updRetro(pc); break;
    case 4: updClockRings(pc); break;
    case 5: updTerminal(pc); break;
    case 6: updTiles(pc); break;
    case 7: updNixie(pc); break;
    case 8: updImpulse(pc); break;
    case 9: updOrbit(pc); break;
    case 10: updHud(pc); break;
    case 11: updScanline(pc); break;
    default: updAnalog(pc); break;
  }
}
