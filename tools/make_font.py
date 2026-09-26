#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
make_font.py — конвертер ГОТОВОГО шрифта DejaVu Sans в C-массивы для
минимального UI (проект jd9165_hello_display, экран 1024x600).

Зачем: рисовать текст в прошивке проще всего растровыми шрифтами.
Мы НЕ рисуем шрифт вручную и НЕ зависим от FreeType на устройстве:
один раз на ПК берём готовый свободный TTF (DejaVu Sans — лицензия
Bitstream Vera + public domain, разрешает встраивание и распространение)
и превращаем начертания каждого символа в битовые массивы C.

Что генерируется (в папку main/):
  ui_font_sans16.c   — DejaVuSans.ttf 16 px    (текст панелей, статус-бар)
  ui_font_sans24b.c  — DejaVuSans-Bold 24 px   (заголовки, кнопки)
  ui_font_sans40b.c  — DejaVuSans-Bold 40 px   (крупный заголовок сплэша)

Набор символов: ASCII 0x20..0x7E, полная кириллица А-я + Ё/ё,
типографские знаки « » ° · × – — … № ● ○ — их использует демо-UI.

Запуск (нужен Pillow: pip install pillow):
  python3 tools/make_font.py

Формат данных (см. ui.h):
  ui_glyph_t { cp, w, h, advance, x0, y0, offset } — описание глифа;
  растры сложены в один массив байт, 1 бит = 1 пиксель, старший бит —
  левый пиксель строки, строки идут подряд без выравнивания.
"""

import os
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("Нужен Pillow: pip install pillow")

# ----------------------------------------------------------------------------
# Пути: скрипт лежит в tools/, результат — в main/
# ----------------------------------------------------------------------------
HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, "..", "main")

DEJAVU_REGULAR = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
DEJAVU_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

# ----------------------------------------------------------------------------
# Набор символов (кодовые точки Unicode), отсортирован — таблица глифов
# получится отсортированной, поиск сможет выходить досрочно
# ----------------------------------------------------------------------------
CHARSET = sorted(
    list(range(0x20, 0x7F))                    # ASCII: пробел .. тильда
    + [0x0401, 0x0451]                         # Ё и ё
    + list(range(0x0410, 0x0450))              # А..я (полная кириллица)
    + [0x00AB, 0x00BB, 0x00B0, 0x00B7, 0x00D7] # « » ° · ×
    + [0x2013, 0x2014, 0x2026, 0x2116]         # – — … №
    + [0x25CF, 0x25CB]                         # ● ○ (индикаторы статуса)
)

# ----------------------------------------------------------------------------
# Шрифты проекта: имя переменной в C, файл, размер в пикселях, назначение
# ----------------------------------------------------------------------------
FONTS = [
    ("ui_font_sans16",  DEJAVU_REGULAR, 16, "обычный текст панелей и статус-бар"),
    ("ui_font_sans24b", DEJAVU_BOLD,    24, "заголовки панелей, кнопки"),
    ("ui_font_sans40b", DEJAVU_BOLD,    40, "крупный заголовок сплэш-экрана"),
]

# Кэш текущего размера (нужен растеризатору для ширины буфера)
_px_size = 16


def rasterize_glyph(font, cp, line_height):
    """Растеризует один символ. Возвращает (w, h, advance, x0, y0, bytes)."""
    ch = chr(cp)

    # Рисуем символ в чистое монохромное изображение высотой в строку.
    # Точка (0,0) — левый край и верх строки (якорь "la" в Pillow):
    # так y0 у всех глифов отсчитывается от одной и той же линии.
    img = Image.new("1", (_px_size * 3, line_height), 0)
    draw = ImageDraw.Draw(img)
    draw.text((0, 0), ch, font=font, fill=1)

    bbox = img.getbbox()                       # рамка "чернил" символа
    advance = round(font.getlength(ch))        # шаг пера этого символа

    if bbox is None:                           # невидимый глиф (например пробел)
        return 0, 0, advance, 0, 0, b""

    x0, y0, x1, y1 = bbox
    gw, gh = x1 - x0, y1 - y0
    crop = img.crop(bbox)
    px = crop.load()

    stride = (gw + 7) // 8                     # байт на строку растра
    data = bytearray()
    for row in range(gh):
        for b in range(stride):
            byte = 0
            for bit in range(8):
                col = b * 8 + bit
                if col < gw and px[col, row]:
                    byte |= 0x80 >> bit        # старший бит — левый пиксель
            data.append(byte)

    if not (-128 <= x0 <= 127 and -128 <= y0 <= 127):
        sys.exit(f"Глиф U+{cp:04X}: смещения x0={x0} y0={y0} не влезают в int8")
    return gw, gh, advance, x0, y0, bytes(data)


def generate_font(var_name, ttf_path, px_size, purpose):
    """Генерирует один файл main/<var_name>.c и возвращает статистику."""
    global _px_size
    _px_size = px_size

    font = ImageFont.truetype(ttf_path, px_size)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent

    glyphs = []                                # (cp, w, h, advance, x0, y0, offset)
    bitmap = bytearray()

    for cp in CHARSET:
        gw, gh, adv, gx, gy, data = rasterize_glyph(font, cp, line_height)
        if len(bitmap) + len(data) > 65535:
            sys.exit("Массив растра превысил 64 КБ — увеличьте тип offset в ui.h")
        glyphs.append((cp, gw, gh, adv, gx, gy, len(bitmap)))
        bitmap.extend(data)

    # ----- формируем C-файл --------------------------------------------------
    src_name = os.path.basename(ttf_path)
    lines = []
    a = lines.append
    a("// ----------------------------------------------------------------------------")
    a(f"//  {var_name}.c — готовый шрифт DejaVu Sans ({src_name}, {px_size} px)")
    a(f"//  Назначение: {purpose}")
    a("//  Автогенерировано скриптом tools/make_font.py — НЕ редактировать руками.")
    a("//  Перегенерация:  python3 tools/make_font.py")
    a("//  Лицензия шрифта: DejaVu (Bitstream Vera License + public domain),")
    a("//  свободно допускает встраивание в прошивку и распространение.")
    a("// ----------------------------------------------------------------------------")
    a("")
    a('#include "ui.h"')
    a("")
    a(f"// Растры всех {len(glyphs)} глифов: 1 бит на пиксель, старший бит — левый")
    a(f"static const uint8_t {var_name}_bitmap[] = {{")
    for i in range(0, len(bitmap), 16):
        chunk = ", ".join(f"0x{b:02X}" for b in bitmap[i:i + 16])
        a(f"    {chunk},")
    a("};")
    a("")
    a("// Таблица глифов: {кодовая точка, w, h, шаг пера, x0, y0, смещение растра}")
    a(f"static const ui_glyph_t {var_name}_glyphs[] = {{")
    for cp, gw, gh, adv, gx, gy, off in glyphs:
        ch = chr(cp)
        label = f"'{ch}'" if cp >= 0x20 else ""
        a(f"    {{ 0x{cp:04X}, {gw:3d}, {gh:3d}, {adv:3d}, {gx:3d}, {gy:3d}, 0x{off:04X} }},"
          f"   // U+{cp:04X} {label}")
    a("};")
    a("")
    a(f"const ui_font_t {var_name} = {{")
    a(f"    .line_height = {line_height},")
    a(f"    .ascent      = {ascent},")
    a(f"    .glyph_count = {len(glyphs)},")
    a(f"    .glyphs      = {var_name}_glyphs,")
    a(f"    .bitmap      = {var_name}_bitmap,")
    a("};")
    a("")

    out_path = os.path.normpath(os.path.join(OUT_DIR, var_name + ".c"))
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

    return {
        "name": var_name,
        "glyphs": len(glyphs),
        "bitmap_kb": len(bitmap) / 1024.0,
        "line_height": line_height,
        "ascent": ascent,
        "path": out_path,
    }


def main():
    print("Конвертация готового шрифта DejaVu Sans в C-массивы")
    print(f"Символов в наборе: {len(CHARSET)} (ASCII + кириллица + типографика)\n")
    for var_name, ttf, px, purpose in FONTS:
        st = generate_font(var_name, ttf, px, purpose)
        print(f"  {st['name']}.c: {st['glyphs']} глифов, "
              f"растр {st['bitmap_kb']:.1f} КБ, строка {st['line_height']} px "
              f"({st['ascent']}+{st['line_height'] - st['ascent']})")
        print(f"      -> {st['path']}")
    print("\nГотово. Файлы лежат в main/ и уже включены в сборку (CMakeLists.txt).")


if __name__ == "__main__":
    main()
