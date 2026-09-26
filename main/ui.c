// ============================================================================
//  ui.c — реализация мини-библиотеки UI для фреймбуфера RGB565 1024x600
//  Проект: jd9165_hello_display (ESP32-P4, панель JD9165)
// ----------------------------------------------------------------------------
//  Все функции рисуют СРАЗУ во фреймбуфер, который DMA канала DPI
//  непрерывно транслирует на панель. Отдельного "вывода кадра" не нужно:
//  изменили пиксели — экран уже обновился.
//  Каждый примитив сначала отсекается по границам экрана, поэтому виджеты
//  можно смело двигать/уменьшать — за границы фреймбуфера не запишем.
// ============================================================================

#include <string.h>
#include "ui.h"

// ----------------------------------------------------------------------------
// Пиксель с защитой границ (внутренняя функция, встраивается по месту)
// ----------------------------------------------------------------------------
static inline void put_pixel(uint16_t *fb, int x, int y, uint16_t color)
{
    if (x < 0 || x >= UI_H_RES || y < 0 || y >= UI_V_RES) {
        return;
    }
    fb[y * UI_H_RES + x] = color;
}

// Отсечение прямоугольника по границам экрана. Возвращает 0, если пусто.
static int clip_rect(ui_rect_t *r)
{
    if (r->x < 0) { r->w += r->x; r->x = 0; }
    if (r->y < 0) { r->h += r->y; r->y = 0; }
    if (r->x + r->w > UI_H_RES) { r->w = UI_H_RES - r->x; }
    if (r->y + r->h > UI_V_RES) { r->h = UI_V_RES - r->y; }
    return (r->w > 0 && r->h > 0);
}

// ----------------------------------------------------------------------------
// Примитивы
// ----------------------------------------------------------------------------

// Заливка прямоугольника сплошным цветом
void ui_fill_rect(uint16_t *fb, ui_rect_t r, uint16_t color)
{
    if (!clip_rect(&r)) {
        return;
    }
    // Плотный цикл по строкам: после отсечения проверка границ не нужна
    for (int y = r.y; y < r.y + r.h; y++) {
        uint16_t *row = fb + y * UI_H_RES + r.x;
        for (int x = 0; x < r.w; x++) {
            row[x] = color;
        }
    }
}

// Рамка толщиной 1 пиксель
void ui_frame_rect(uint16_t *fb, ui_rect_t r, uint16_t color)
{
    ui_hline(fb, r.x, r.y, r.w, color);                     // верх
    ui_hline(fb, r.x, r.y + r.h - 1, r.w, color);           // низ
    ui_vline(fb, r.x, r.y, r.h, color);                     // лево
    ui_vline(fb, r.x + r.w - 1, r.y, r.h, color);           // право
}

// Горизонтальная линия
void ui_hline(uint16_t *fb, int x, int y, int len, uint16_t color)
{
    if (y < 0 || y >= UI_V_RES) {
        return;
    }
    int x1 = x + len;
    if (x < 0) { x = 0; }
    if (x1 > UI_H_RES) { x1 = UI_H_RES; }
    for (; x < x1; x++) {
        fb[y * UI_H_RES + x] = color;
    }
}

// Вертикальная линия
void ui_vline(uint16_t *fb, int x, int y, int len, uint16_t color)
{
    if (x < 0 || x >= UI_H_RES) {
        return;
    }
    int y1 = y + len;
    if (y < 0) { y = 0; }
    if (y1 > UI_V_RES) { y1 = UI_V_RES; }
    for (; y < y1; y++) {
        fb[y * UI_H_RES + x] = color;
    }
}

// ----------------------------------------------------------------------------
// Текст: UTF-8 -> Unicode -> глиф -> растр во фреймбуфер
// ----------------------------------------------------------------------------

// Декодер UTF-8 (поддерживает 1..4 байта; кириллица кодируется двумя)
int ui_utf8_decode(const char *s, uint32_t *cp)
{
    uint8_t c0 = (uint8_t)s[0];

    if (c0 < 0x80) {                       // 0xxxxxxx — ASCII
        *cp = c0;
        return 1;
    }
    if ((c0 & 0xE0) == 0xC0) {             // 110xxxxx 10xxxxxx — 2 байта
        if (((uint8_t)s[1] & 0xC0) != 0x80) { *cp = '?'; return 1; }
        *cp = ((uint32_t)(c0 & 0x1F) << 6) | ((uint8_t)s[1] & 0x3F);
        return 2;
    }
    if ((c0 & 0xF0) == 0xE0) {             // 1110xxxx 10xxxxxx 10xxxxxx — 3 байта
        if (((uint8_t)s[1] & 0xC0) != 0x80 || ((uint8_t)s[2] & 0xC0) != 0x80) {
            *cp = '?'; return 1;
        }
        *cp = ((uint32_t)(c0 & 0x0F) << 12) |
              ((uint32_t)((uint8_t)s[1] & 0x3F) << 6) |
              ((uint8_t)s[2] & 0x3F);
        return 3;
    }
    if ((c0 & 0xF8) == 0xF0) {             // 11110xxx ... — 4 байта (эмодзи и пр.)
        if (((uint8_t)s[1] & 0xC0) != 0x80 || ((uint8_t)s[2] & 0xC0) != 0x80 ||
            ((uint8_t)s[3] & 0xC0) != 0x80) {
            *cp = '?'; return 1;
        }
        *cp = ((uint32_t)(c0 & 0x07) << 18) |
              ((uint32_t)((uint8_t)s[1] & 0x3F) << 12) |
              ((uint32_t)((uint8_t)s[2] & 0x3F) << 6) |
              ((uint8_t)s[3] & 0x3F);
        return 4;
    }
    *cp = '?';                             // повреждённая последовательность
    return 1;
}

// Поиск глифа по кодовой точке. Таблица отсортирована, поэтому возможен
// досрочный выход (cp меньше текущего — дальше его точно нет).
static const ui_glyph_t *find_glyph(const ui_font_t *font, uint32_t cp)
{
    for (uint16_t i = 0; i < font->glyph_count; i++) {
        const ui_glyph_t *g = &font->glyphs[i];
        if (g->cp == cp) {
            return g;
        }
        if (g->cp > cp) {
            break;
        }
    }
    return NULL;
}

// Рисование одного символа; возвращает шаг пера
int ui_draw_char(uint16_t *fb, int x, int y, uint32_t cp,
                 const ui_font_t *font, uint16_t color)
{
    const ui_glyph_t *g = find_glyph(font, cp);
    if (g == NULL) {
        g = find_glyph(font, '?');         // неизвестный символ -> '?'
        if (g == NULL) {
            return font->line_height / 2;
        }
    }
    if (g->w > 0 && g->h > 0) {
        // Растр глифа: 1 бит на пиксель, строки идут подряд (старший бит — левый)
        const uint8_t *data = font->bitmap + g->offset;
        const int stride = (g->w + 7) / 8;         // байт на строку растра
        for (int row = 0; row < g->h; row++) {
            const uint8_t *line = data + row * stride;
            int py = y + g->y0 + row;
            for (int col = 0; col < g->w; col++) {
                if (line[col >> 3] & (0x80 >> (col & 7))) {
                    put_pixel(fb, x + g->x0 + col, py, color);
                }
            }
        }
    }
    return g->advance;
}

// Строка UTF-8; возвращает координату пера после последнего символа
int ui_draw_text(uint16_t *fb, int x, int y, const char *utf8,
                 const ui_font_t *font, uint16_t color)
{
    int pen = x;
    while (*utf8) {
        uint32_t cp;
        int n = ui_utf8_decode(utf8, &cp);
        utf8 += n;
        pen += ui_draw_char(fb, pen, y, cp, font, color);
    }
    return pen;
}

// Ширина строки в пикселях (сумма шагов пера всех символов)
int ui_text_width(const char *utf8, const ui_font_t *font)
{
    int w = 0;
    while (*utf8) {
        uint32_t cp;
        int n = ui_utf8_decode(utf8, &cp);
        utf8 += n;
        const ui_glyph_t *g = find_glyph(font, cp);
        if (g == NULL) {
            g = find_glyph(font, '?');
        }
        w += g ? g->advance : (font->line_height / 2);
    }
    return w;
}

// Центрирование строки внутри прямоугольника
void ui_draw_text_centered(uint16_t *fb, ui_rect_t r, const char *utf8,
                           const ui_font_t *font, uint16_t color)
{
    int w = ui_text_width(utf8, font);
    int x = r.x + (r.w - w) / 2;
    int y = r.y + (r.h - font->line_height) / 2;
    ui_draw_text(fb, x, y, utf8, font, color);
}

// Пара "ключ: значение": подпись приглушённым цветом, значение — основным.
// Значение рисуется в фиксированной колонке (x + value_col) — строки
// выравниваются в столбик, как в таблице свойств.
void ui_kv_line(uint16_t *fb, int x, int y, const char *key, const char *value,
                const ui_font_t *font, int value_col)
{
    ui_draw_text(fb, x, y, key, font, UI_COL_TEXT_DIM);
    ui_draw_text(fb, x + value_col, y, value, font, UI_COL_TEXT);
}

// ----------------------------------------------------------------------------
// Виджеты
// ----------------------------------------------------------------------------

// Панель: заливка + рамка + заголовок + линия-разделитель под заголовком
void ui_panel_draw(uint16_t *fb, ui_rect_t r, const char *title,
                   const ui_font_t *title_font)
{
    ui_fill_rect(fb, r, UI_COL_PANEL);
    ui_frame_rect(fb, r, UI_COL_BORDER);
    if (title != NULL && title_font != NULL) {
        ui_draw_text(fb, r.x + 16, r.y + 12, title, title_font, UI_COL_ACCENT);
        // Разделитель под заголовком: 12 (отступ) + высота строки + 8 (отступ)
        int divider_y = r.y + 12 + title_font->line_height + 8;
        ui_hline(fb, r.x + 1, divider_y, r.w - 2, UI_COL_BORDER);
    }
}

// Кнопка: три стиля на выбор (см. ui_btn_style_t в ui.h)
void ui_button_draw(uint16_t *fb, ui_rect_t r, const char *label,
                    ui_btn_style_t style)
{
    uint16_t fill = UI_COL_PANEL_HI;
    uint16_t border = UI_COL_BORDER;
    uint16_t text = UI_COL_TEXT;

    switch (style) {
    case UI_BTN_ACCENT:                     // главная кнопка: залита акцентом
        fill = UI_COL_ACCENT;
        border = UI_COL_ACCENT;
        text = UI_COL_TEXT_DARK;
        break;
    case UI_BTN_DANGER:                     // "опасная": красный контур
        fill = UI_COL_PANEL_HI;
        border = UI_COL_RED;
        text = UI_COL_RED;
        break;
    case UI_BTN_NORMAL:                     // обычная: тёмная с рамкой
    default:
        break;
    }

    ui_fill_rect(fb, r, fill);
    ui_frame_rect(fb, r, border);
    ui_draw_text_centered(fb, r, label, &ui_font_sans24b, text);
}

// Полоса прогресса: тёмная дорожка + рамка + заполнение по проценту
void ui_progress_draw(uint16_t *fb, ui_rect_t r, int percent, uint16_t fill_color)
{
    if (percent < 0)   { percent = 0; }
    if (percent > 100) { percent = 100; }

    ui_fill_rect(fb, r, UI_COL_TRACK);
    ui_frame_rect(fb, r, UI_COL_BORDER);

    ui_rect_t inner = {
        .x = r.x + 2,
        .y = r.y + 2,
        .w = ((r.w - 4) * percent) / 100,
        .h = r.h - 4,
    };
    if (inner.w > 0) {
        ui_fill_rect(fb, inner, fill_color);
    }
}
