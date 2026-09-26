// ============================================================================
//  ui_demo.c — демо-экран 1024x600, собранный из примитивов ui.h
//  Проект: jd9165_hello_display (ESP32-P4, панель JD9165)
// ----------------------------------------------------------------------------
//  КАК УСТРОЕН ЭКРАН (сетка компоновки для 1024x600):
//
//    ┌────────────────────────── шапка 1024x56 ──────────────────────────────┐
//    │ ПАНЕЛЬ УПРАВЛЕНИЯ                        ESP32-P4 · JC1060P470C      │
//    ├─────────────┬─────────────┬─────────────┤   поля 24, зазоры 24       │
//    │ Дисплей     │ Система     │ Статус      │   3 панели 309/309/310     │
//    │ (свойства)  │ (свойства)  │ (индикация) │   высота 228               │
//    ├─────────────┴─────────────┴─────────────┤                            │
//    │ Индикация: полоса прогресса + проценты  │   панель 976x116           │
//    ├─────────────────────────────────────────┤                            │
//    │ [Кнопка 1] [Кнопка 2] [Пуск] [Сброс]    │   кнопки 226x64            │
//    ├─────────────────────────────────────────┤                            │
//    │ ● Система работает          v2.0        │   статус-бар 1024x40       │
//    └─────────────────────────────────────────┴────────────────────────────┘
//
//  ДИНАМИКА: раз в тик перерисовываются только 4 маленькие области
//  (значение uptime, полоса и процент, два индикатора) — весь кадр
//  при этом остаётся нетронутым. Так делают анимацию без мерцания.
// ============================================================================

#include <stdio.h>
#include "ui.h"
#include "ui_demo.h"

// ----------------------------------------------------------------------------
// Геометрия экрана (сетка компоновки) — все размеры посчитаны для 1024x600
// ----------------------------------------------------------------------------
static const ui_rect_t R_SCREEN  = { 0,   0, 1024, 600 };  // весь экран
static const ui_rect_t R_HEADER  = { 0,   0, 1024,  56 };  // шапка
static const ui_rect_t R_PANEL_DISP = {  24,  72, 309, 228 }; // панель "Дисплей"
static const ui_rect_t R_PANEL_SYS  = { 357,  72, 309, 228 }; // панель "Система"
static const ui_rect_t R_PANEL_STAT = { 690,  72, 310, 228 }; // панель "Статус"
static const ui_rect_t R_PANEL_PROG = {  24, 316, 976, 116 }; // панель "Индикация"
static const ui_rect_t R_STATUS  = { 0, 560, 1024,  40 };  // статус-бар

// Кнопки: 4 штуки по 226x64, зазор 24
static const ui_rect_t R_BUTTONS[4] = {
    {  24, 448, 226, 64 },
    { 274, 448, 226, 64 },
    { 524, 448, 226, 64 },
    { 774, 448, 226, 64 },
};

// Динамические области (перерисовываются в ui_demo_tick)
static const ui_rect_t R_UPTIME_VAL = { 822, 199, 160, 24 }; // значение uptime
static const ui_rect_t R_BAR        = {  40, 400, 830, 20 }; // полоса прогресса
static const ui_rect_t R_PCT_BG     = { 886, 368,  98, 34 }; // фон текста процентов
static const ui_rect_t R_DOT_STATUS = {  24, 572,  16, 16 }; // индикатор в статус-баре
static const ui_rect_t R_DOT_PANEL  = { 706, 135,  20, 20 }; // индикатор в панели "Статус"

// Служебные константы компоновки
#define KV_COL        116      // ширина колонки "ключ" в парах ключ: значение
#define KV_ROW_STEP   29       // шаг строк свойств (строка 19 + отступ 10)
#define KV_Y_FIRST    132      // y первой строки свойств в панелях

// ----------------------------------------------------------------------------
// Содержимое панелей (статические пары "ключ: значение")
// ----------------------------------------------------------------------------
static void draw_panel_display(uint16_t *fb)
{
    ui_panel_draw(fb, R_PANEL_DISP, "Дисплей", &ui_font_sans24b);
    const int x = R_PANEL_DISP.x + 16;
    ui_kv_line(fb, x, KV_Y_FIRST + 0 * KV_ROW_STEP, "Панель:",     "JD9165",            &ui_font_sans16, KV_COL);
    ui_kv_line(fb, x, KV_Y_FIRST + 1 * KV_ROW_STEP, "Матрица:",    "IPS 1024×600",      &ui_font_sans16, KV_COL);
    ui_kv_line(fb, x, KV_Y_FIRST + 2 * KV_ROW_STEP, "Интерфейс:",  "MIPI-DSI, 2 lane",  &ui_font_sans16, KV_COL);
    ui_kv_line(fb, x, KV_Y_FIRST + 3 * KV_ROW_STEP, "Видеопоток:", "DPI, 52 МГц",       &ui_font_sans16, KV_COL);
    ui_kv_line(fb, x, KV_Y_FIRST + 4 * KV_ROW_STEP, "Формат:",     "RGB565, 16 бит",    &ui_font_sans16, KV_COL);
}

static void draw_panel_system(uint16_t *fb)
{
    ui_panel_draw(fb, R_PANEL_SYS, "Система", &ui_font_sans24b);
    const int x = R_PANEL_SYS.x + 16;
    ui_kv_line(fb, x, KV_Y_FIRST + 0 * KV_ROW_STEP, "Чип:",     "ESP32-P4",    &ui_font_sans16, KV_COL);
    ui_kv_line(fb, x, KV_Y_FIRST + 1 * KV_ROW_STEP, "Ревизия:", "v1.3",        &ui_font_sans16, KV_COL);
    ui_kv_line(fb, x, KV_Y_FIRST + 2 * KV_ROW_STEP, "Память:",  "PSRAM 32 МБ", &ui_font_sans16, KV_COL);
    ui_kv_line(fb, x, KV_Y_FIRST + 3 * KV_ROW_STEP, "IDF:",     "v5.5.5",      &ui_font_sans16, KV_COL);
    ui_kv_line(fb, x, KV_Y_FIRST + 4 * KV_ROW_STEP, "ОС:",      "FreeRTOS",    &ui_font_sans16, KV_COL);
}

static void draw_panel_status_static(uint16_t *fb)
{
    ui_panel_draw(fb, R_PANEL_STAT, "Статус", &ui_font_sans24b);

    // Крупная строка состояния: индикатор (точку рисует тик) + слово
    ui_draw_text(fb, R_PANEL_STAT.x + 46, 131, "РАБОТАЕТ",
                 &ui_font_sans24b, UI_COL_GREEN);

    const int x = R_PANEL_STAT.x + 16;
    ui_kv_line(fb, x, 172, "Подсветка:", "100 %", &ui_font_sans16, KV_COL);
    ui_kv_line(fb, x, 201, "Uptime:",    "",      &ui_font_sans16, KV_COL); // значение — динамическое
    ui_kv_line(fb, x, 230, "Кадр:",      "60 Гц", &ui_font_sans16, KV_COL);
}

// ----------------------------------------------------------------------------
// Статическая часть экрана
// ----------------------------------------------------------------------------
void ui_demo_draw(uint16_t *fb)
{
    // --- фон экрана ---
    ui_fill_rect(fb, R_SCREEN, UI_COL_BG);

    // --- шапка: заголовок слева, сведения справа, акцентная линия снизу ---
    ui_fill_rect(fb, R_HEADER, UI_COL_HEADER);
    ui_draw_text(fb, 24, 13, "ПАНЕЛЬ УПРАВЛЕНИЯ", &ui_font_sans24b, UI_COL_TEXT);
    ui_draw_text_centered(fb, (ui_rect_t){ 624, 0, 376, 56 },
                          "ESP32-P4 · JC1060P470C", &ui_font_sans16, UI_COL_TEXT_DIM);
    ui_fill_rect(fb, (ui_rect_t){ 0, 54, 1024, 2 }, UI_COL_ACCENT);

    // --- три панели свойств ---
    draw_panel_display(fb);
    draw_panel_system(fb);
    draw_panel_status_static(fb);

    // --- панель "Индикация": подпись, полоса прогресса, проценты ---
    ui_panel_draw(fb, R_PANEL_PROG, "Индикация", &ui_font_sans24b);
    ui_draw_text(fb, 40, 374, "Загрузка данных", &ui_font_sans16, UI_COL_TEXT_DIM);
    // Полосу и проценты рисует тик (ниже), поэтому здесь их не трогаем

    // --- ряд кнопок: демонстрация трёх стилей ---
    ui_button_draw(fb, R_BUTTONS[0], "Кнопка 1", UI_BTN_NORMAL);
    ui_button_draw(fb, R_BUTTONS[1], "Кнопка 2", UI_BTN_NORMAL);
    ui_button_draw(fb, R_BUTTONS[2], "Пуск",     UI_BTN_ACCENT);
    ui_button_draw(fb, R_BUTTONS[3], "Сброс",    UI_BTN_DANGER);

    // --- статус-бар: индикатор + текст слева, версия справа ---
    ui_fill_rect(fb, R_STATUS, UI_COL_HEADER);
    ui_hline(fb, 0, 560, 1024, UI_COL_BORDER);
    ui_draw_text(fb, 52, 570, "Система работает", &ui_font_sans16, UI_COL_TEXT);
    ui_draw_text_centered(fb, (ui_rect_t){ 512, 560, 488, 40 },
                          "jd9165_hello_display · UI без LVGL · v2.0",
                          &ui_font_sans16, UI_COL_TEXT_DIM);

    // --- начальное состояние динамики: uptime 0, прогресс 0 % ---
    ui_demo_tick(fb, 0, 0);
}

// ----------------------------------------------------------------------------
// Динамика: обновляем только изменившиеся области (без перерисовки кадра)
// ----------------------------------------------------------------------------
void ui_demo_tick(uint16_t *fb, int uptime_sec, int progress_pct)
{
    char buf[32];

    // 1. Значение uptime в панели "Статус": стираем фон панели и пишем новое
    ui_fill_rect(fb, R_UPTIME_VAL, UI_COL_PANEL);
    snprintf(buf, sizeof(buf), "%d с", uptime_sec);
    ui_draw_text(fb, R_UPTIME_VAL.x, R_UPTIME_VAL.y + 2, buf,
                 &ui_font_sans16, UI_COL_TEXT);

    // 2. Полоса прогресса и процентное значение
    ui_progress_draw(fb, R_BAR, progress_pct, UI_COL_ACCENT);
    ui_fill_rect(fb, R_PCT_BG, UI_COL_PANEL);
    snprintf(buf, sizeof(buf), "%d %%", progress_pct);
    ui_draw_text(fb, R_PCT_BG.x + 4, R_PCT_BG.y + 2, buf,
                 &ui_font_sans24b, UI_COL_ACCENT);

    // 3. Индикаторы "сердцебиения": мигают раз в секунду (чёт/нечёт)
    const uint16_t dot = (uptime_sec & 1) ? UI_COL_DOT_OFF : UI_COL_GREEN;
    ui_fill_rect(fb, R_DOT_STATUS, dot);
    ui_fill_rect(fb, R_DOT_PANEL, dot);
}

// ----------------------------------------------------------------------------
// Сплэш-экран: показывается 3 секунды сразу после включения
// ----------------------------------------------------------------------------
void ui_splash_draw(uint16_t *fb)
{
    ui_fill_rect(fb, R_SCREEN, UI_COL_BG);

    ui_draw_text_centered(fb, (ui_rect_t){ 0, 130, 1024, 60 },
                          "HELLO!", &ui_font_sans40b, UI_COL_ACCENT);
    ui_draw_text_centered(fb, (ui_rect_t){ 0, 240, 1024, 40 },
                          "ESP32-P4 · JC1060P470C", &ui_font_sans24b, UI_COL_TEXT);
    ui_draw_text_centered(fb, (ui_rect_t){ 0, 300, 1024, 24 },
                          "Экран 1024×600 · панель JD9165 · MIPI-DSI 2 lane",
                          &ui_font_sans16, UI_COL_TEXT_DIM);
    ui_draw_text_centered(fb, (ui_rect_t){ 0, 330, 1024, 24 },
                          "Подсветка ШИМ GPIO23 · DPI 52 МГц · RGB565",
                          &ui_font_sans16, UI_COL_TEXT_DIM);
    ui_draw_text_centered(fb, (ui_rect_t){ 0, 520, 1024, 24 },
                          "Демо-интерфейс будет показан через 3 секунды…",
                          &ui_font_sans16, UI_COL_TEXT_DIM);
}
