# Генератор растровых шрифтов LVGL (4 bpp) из TTF через FreeType с хинтингом.
import sys
from PIL import ImageFont, Image
from fontTools.ttLib import TTFont

def gen(ttf, px, name):
    cps = sorted(c for c in TTFont(ttf).getBestCmap() if c >= 32)
    font = ImageFont.truetype(ttf, px)
    asc, desc = font.getmetrics()
    bitmap = bytearray(); dsc = []; nib = []
    def flush():
        pass
    bits = []
    for cp in cps:
        ch = chr(cp)
        adv = int(round(font.getlength(ch) * 16))
        x0, y0, x1, y1 = font.getbbox(ch, anchor='ls')
        mask = font.getmask(ch)
        w, h = mask.size
        idx = len(bits) // 2
        if w == 0 or h == 0:
            dsc.append((idx, adv, 0, 0, 0, 0)); continue
        if len(bits) % 2: bits.append(0)      # каждый глиф с границы байта
        idx = len(bits) // 2
        for y in range(h):
            for x in range(w):
                bits.append(mask.getpixel((x, y)) >> 4)
        dsc.append((idx, adv, w, h, x0, -y0 - h))
    if len(bits) % 2: bits.append(0)
    data = bytes((bits[i] << 4) | bits[i + 1] for i in range(0, len(bits), 2))
    out = []
    out.append(f"/* {name}: {px} px, 4 bpp, {len(cps)} глифов */")
    out.append(f"static const uint8_t {name}_bitmap[] = {{")
    for i in range(0, len(data), 24):
        out.append("  " + ",".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
    out.append("};")
    out.append(f"static const lv_font_fmt_txt_glyph_dsc_t {name}_dsc[] = {{")
    out.append("  {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},")
    for (i, adv, w, h, ox, oy) in dsc:
        out.append(f"  {{.bitmap_index = {i}, .adv_w = {adv}, .box_w = {w}, .box_h = {h}, .ofs_x = {ox}, .ofs_y = {oy}}},")
    out.append("};")
    out.append(f"static const uint16_t {name}_ulist[] = {{")
    offs = [c - cps[0] for c in cps]
    for i in range(0, len(offs), 16):
        out.append("  " + ",".join(f"0x{o:04x}" for o in offs[i:i + 16]) + ",")
    out.append("};")
    out.append(f"static const lv_font_fmt_txt_cmap_t {name}_cmaps[] = {{")
    out.append(f"  {{.range_start = {cps[0]}, .range_length = {cps[-1] - cps[0] + 1}, .glyph_id_start = 1,")
    out.append(f"   .unicode_list = {name}_ulist, .glyph_id_ofs_list = NULL, .list_length = {len(cps)},")
    out.append("   .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY},")
    out.append("};")
    out.append(f"static const lv_font_fmt_txt_dsc_t {name}_fdsc = {{")
    out.append(f"  .glyph_bitmap = {name}_bitmap, .glyph_dsc = {name}_dsc, .cmaps = {name}_cmaps, .kern_dsc = NULL,")
    out.append("  .kern_scale = 0, .cmap_num = 1, .bpp = 4, .kern_classes = 0, .bitmap_format = 0,")
    out.append("};")
    out.append(f"const lv_font_t {name} = {{")
    out.append("  .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt, .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,")
    out.append(f"  .line_height = {asc + desc}, .base_line = {desc}, .subpx = LV_FONT_SUBPX_NONE,")
    out.append(f"  .underline_position = -1, .underline_thickness = 1, .dsc = &{name}_fdsc, .fallback = NULL, .user_data = NULL,")
    out.append("};\n")
    return "\n".join(out), len(data), (asc, desc)

if __name__ == "__main__":
    outdir = sys.argv[1]
    specs = [("medium", 11, "font_pm11"), ("medium", 12, "font_pm12"), ("medium", 13, "font_pm13"),
             ("semibold", 12, "font_pb12"), ("semibold", 14, "font_pb14"), ("semibold", 16, "font_pb16")]
    c = ['/* Растровые шрифты Inter для мелкого текста: отрисованы заранее с подгонкой под пиксели,',
         '   поэтому на экране 320x240 читаются чётче, чем вектор. Сгенерировано tools/mkfont.py. */',
         '#include "lvgl.h"', '']
    h = ['// Растровые шрифты для мелкого текста (см. fonts_px.c)', '#pragma once', '#include <lvgl.h>', '',
         '#ifdef __cplusplus', 'extern "C" {', '#endif']
    for w, px, name in specs:
        src, n, m = gen(f"/home/claude/fonts/{w}.ttf", px, name)
        c.append(src); h.append(f"extern const lv_font_t {name};")
        print(name, n, "bytes", m)
    h += ['#ifdef __cplusplus', '}', '#endif', '']
    open(outdir + "/fonts_px.c", "w", encoding="utf-8").write("\n".join(c))
    open(outdir + "/fonts_px.h", "w", encoding="utf-8").write("\n".join(h))
