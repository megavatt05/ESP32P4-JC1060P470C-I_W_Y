// ============================================================================
//  main.c — минимальный тест дисплея: включить экран и вывести текст
//  Плата: Guition JC1060P470C_I_W_Y (7" IPS 1024x600)
//  Чип:   ESP32-P4 rev v1.3 | Экранная IC: JD9165 (MIPI-DSI, 2 lane)
// ----------------------------------------------------------------------------
//  Больше ничего приложение не делает: без LVGL, без тача, без камеры,
//  без Wi-Fi. На экране — 4 строки текста на чёрном фоне.
//
//  КАК РАБОТАЕТ ВЫВОД НА ЭТОТ ДИСПЛЕЙ (изучено по BSP и демо вендора):
//
//  ESP32-P4 ──MIPI-DSI(2 lane, 750 Мбит/с)──> JD9165 ──> матрица 1024x600
//
//  1. Включается питание DSI-PHY (внутренний LDO, канал 3, 2500 мВ).
//  2. Создаётся MIPI-DSI шина (esp_lcd_new_dsi_bus).
//  3. Через DBI-канал шины (командный интерфейс) панель получают команды.
//  4. Через DPI-канал шины (видеопоток) идёт сама картинка: пиксели
//     берутся DMA из фреймбуфера в PSRAM и передаются на JD9165
//     с частотой 52 МГц в формате RGB565.
//  5. Драйвер esp_lcd_jd9165 (реестр компонентов Espressif) отправляет
//     панельному контроллеру vendor-команды инициализации — без них
//     JD9165 не знает, как раскрасить именно эту матрицу.
//  6. Подсветка — ШИМ (LEDC) на GPIO23; сброс панели — GPIO0.
//  7. Текст рисуется ПО НАПРЯМУЮ в фреймбуфер шрифтом 8x8
//     (public domain, Daniel Hepper) с масштабированием.
// ============================================================================

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/ledc.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_jd9165.h"   // компонент espressif/esp_lcd_jd9165 (см. idf_component.yml)

// Метка журнала
static const char *TAG = "hello_display";

// ----------------------------------------------------------------------------
// Параметры дисплея (сняты с платы Guition JC1060P470C_I_W_Y,
// совпадают с BSP espressif__esp32_p4_function_ev_board в конфигурации 1024x600)
// ----------------------------------------------------------------------------
#define LCD_H_RES               1024    // ширина, пикселей
#define LCD_V_RES               600     // высота, пикселей
#define LCD_BIT_PER_PIXEL       16      // RGB565 = 2 байта на пиксель
#define LCD_MIPI_DSI_LANE_NUM   2       // число data-линий MIPI-DSI
#define LCD_LANE_BIT_RATE_MBPS 750     // скорость линии, Мбит/с
#define LCD_DPI_CLOCK_MHZ       52      // тактовая частота видеопотока DPI
#define LCD_BACKLIGHT_GPIO      23      // ШИМ подсветки
#define LCD_BACKLIGHT_LEDC_CH   1       // канал LEDC для подсветки
#define LCD_RST_GPIO            0       // GPIO сброса панели
#define DSI_PHY_LDO_CHANNEL     3       // канал LDO, питающий DSI-PHY
#define DSI_PHY_LDO_VOLTAGE_MV  2500    // его напряжение, мВ

// ----------------------------------------------------------------------------
// Vendor-команды инициализации панели JD9165 (последовательность Guition
// для матрицы 1024x600). Формат: {команда, данные, длина, пауза мс}.
// Скопировано из BSP вендора; заканчивается 0x11 (выход из сна, пауза 120 мс)
// и 0x29 (включить изображение, пауза 20 мс).
// ----------------------------------------------------------------------------
static const jd9165_lcd_init_cmd_t jd9165_gui_init_cmds[] = {
    {0x30, (uint8_t[]){0x00}, 1, 0},
    {0xF7, (uint8_t[]){0x49, 0x61, 0x02, 0x00}, 4, 0},
    {0x30, (uint8_t[]){0x01}, 1, 0},
    {0x04, (uint8_t[]){0x0C}, 1, 0},
    {0x05, (uint8_t[]){0x00}, 1, 0},
    {0x06, (uint8_t[]){0x00}, 1, 0},
    {0x0B, (uint8_t[]){0x11}, 1, 0},
    {0x17, (uint8_t[]){0x00}, 1, 0},
    {0x20, (uint8_t[]){0x04}, 1, 0},
    {0x1F, (uint8_t[]){0x05}, 1, 0},
    {0x23, (uint8_t[]){0x00}, 1, 0},
    {0x25, (uint8_t[]){0x19}, 1, 0},
    {0x28, (uint8_t[]){0x18}, 1, 0},
    {0x29, (uint8_t[]){0x04}, 1, 0},
    {0x2A, (uint8_t[]){0x01}, 1, 0},
    {0x2B, (uint8_t[]){0x04}, 1, 0},
    {0x2C, (uint8_t[]){0x01}, 1, 0},
    {0x30, (uint8_t[]){0x02}, 1, 0},
    {0x01, (uint8_t[]){0x22}, 1, 0},
    {0x03, (uint8_t[]){0x12}, 1, 0},
    {0x04, (uint8_t[]){0x00}, 1, 0},
    {0x05, (uint8_t[]){0x64}, 1, 0},
    {0x0A, (uint8_t[]){0x08}, 1, 0},
    // Гамма-коррекция положительной полярности (0x0B..0x12)
    {0x0B, (uint8_t[]){0x0A, 0x1A, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x06, 0x08, 0x1F, 0x1D}, 11, 0},
    {0x0C, (uint8_t[]){0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}, 11, 0},
    {0x0D, (uint8_t[]){0x16, 0x1B, 0x0B, 0x0D, 0x0D, 0x11, 0x10, 0x07, 0x09, 0x1E, 0x1C}, 11, 0},
    {0x0E, (uint8_t[]){0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}, 11, 0},
    {0x0F, (uint8_t[]){0x16, 0x1B, 0x0D, 0x0B, 0x0D, 0x11, 0x10, 0x1C, 0x1E, 0x09, 0x07}, 11, 0},
    {0x10, (uint8_t[]){0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}, 11, 0},
    {0x11, (uint8_t[]){0x0A, 0x1A, 0x0D, 0x0B, 0x0D, 0x11, 0x10, 0x1D, 0x1F, 0x08, 0x06}, 11, 0},
    {0x12, (uint8_t[]){0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D, 0x0D}, 11, 0},
    {0x14, (uint8_t[]){0x00, 0x00, 0x11, 0x11}, 4, 0},
    {0x18, (uint8_t[]){0x99}, 1, 0},
    {0x30, (uint8_t[]){0x06}, 1, 0},
    // VCOM AM/IC параметры
    {0x12, (uint8_t[]){0x36, 0x2C, 0x2E, 0x3C, 0x38, 0x35, 0x35, 0x32, 0x2E, 0x1D, 0x2B, 0x21, 0x16, 0x29}, 14, 0},
    {0x13, (uint8_t[]){0x36, 0x2C, 0x2E, 0x3C, 0x38, 0x35, 0x35, 0x32, 0x2E, 0x1D, 0x2B, 0x21, 0x16, 0x29}, 14, 0},
    {0x30, (uint8_t[]){0x0A}, 1, 0},
    {0x02, (uint8_t[]){0x4F}, 1, 0},
    {0x0B, (uint8_t[]){0x40}, 1, 0},
    {0x12, (uint8_t[]){0x3E}, 1, 0},
    {0x13, (uint8_t[]){0x78}, 1, 0},
    {0x30, (uint8_t[]){0x0D}, 1, 0},
    {0x0D, (uint8_t[]){0x04}, 1, 0},
    {0x10, (uint8_t[]){0x0C}, 1, 0},
    {0x11, (uint8_t[]){0x0C}, 1, 0},
    {0x12, (uint8_t[]){0x0C}, 1, 0},
    {0x13, (uint8_t[]){0x0C}, 1, 0},
    {0x30, (uint8_t[]){0x00}, 1, 0},
    // Формат пикселей 16 бит (0x55 = RGB565)
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    // Выход из сна и включение изображения
    {0x11, (uint8_t[]){0x00}, 1, 120},
    {0x29, (uint8_t[]){0x00}, 1, 20},
};

// ----------------------------------------------------------------------------
// Шрифт font8x8_basic (public domain, автор Daniel Hepper,
// https://github.com/dhepper/font8x8). Каждый глиф — 8 байт, по одному байту
// на строку пикселей; бит 0 — крайний ЛЕВЫЙ пиксель строки.
// Покрывает ASCII 0x20..0x7F (пробел..tilde). Русские буквы не включены,
// чтобы не раздувать минимальный проект.
// ----------------------------------------------------------------------------
static const uint8_t font8x8_basic[96][8] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0020 (пробел)
    { 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00},   // U+0021 (!)
    { 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0022 (")
    { 0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00},   // U+0023 (#)
    { 0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00},   // U+0024 ($)
    { 0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00},   // U+0025 (%)
    { 0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00},   // U+0026 (&)
    { 0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0027 (')
    { 0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00},   // U+0028 (()
    { 0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00},   // U+0029 ())
    { 0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},   // U+002A (*)
    { 0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00},   // U+002B (+)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06},   // U+002C (,)
    { 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00},   // U+002D (-)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00},   // U+002E (.)
    { 0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00},   // U+002F (/)
    { 0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00},   // U+0030 (0)
    { 0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00},   // U+0031 (1)
    { 0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00},   // U+0032 (2)
    { 0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00},   // U+0033 (3)
    { 0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00},   // U+0034 (4)
    { 0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00},   // U+0035 (5)
    { 0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00},   // U+0036 (6)
    { 0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00},   // U+0037 (7)
    { 0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00},   // U+0038 (8)
    { 0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00},   // U+0039 (9)
    { 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00},   // U+003A (:)
    { 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06},   // U+003B (;)
    { 0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00},   // U+003C (<)
    { 0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00},   // U+003D (=)
    { 0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00},   // U+003E (>)
    { 0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00},   // U+003F (?)
    { 0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00},   // U+0040 (@)
    { 0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00},   // U+0041 (A)
    { 0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00},   // U+0042 (B)
    { 0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00},   // U+0043 (C)
    { 0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00},   // U+0044 (D)
    { 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00},   // U+0045 (E)
    { 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00},   // U+0046 (F)
    { 0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00},   // U+0047 (G)
    { 0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00},   // U+0048 (H)
    { 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+0049 (I)
    { 0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00},   // U+004A (J)
    { 0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00},   // U+004B (K)
    { 0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00},   // U+004C (L)
    { 0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00},   // U+004D (M)
    { 0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00},   // U+004E (N)
    { 0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00},   // U+004F (O)
    { 0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00},   // U+0050 (P)
    { 0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00},   // U+0051 (Q)
    { 0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00},   // U+0052 (R)
    { 0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00},   // U+0053 (S)
    { 0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+0054 (T)
    { 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00},   // U+0055 (U)
    { 0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},   // U+0056 (V)
    { 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},   // U+0057 (W)
    { 0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00},   // U+0058 (X)
    { 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00},   // U+0059 (Y)
    { 0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00},   // U+005A (Z)
    { 0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00},   // U+005B ([)
    { 0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00},   // U+005C (\)
    { 0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00},   // U+005D (])
    { 0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00},   // U+005E (^)
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},   // U+005F (_)
    { 0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+0060 (`)
    { 0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00},   // U+0061 (a)
    { 0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00},   // U+0062 (b)
    { 0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00},   // U+0063 (c)
    { 0x38, 0x30, 0x30, 0x3E, 0x33, 0x33, 0x6E, 0x00},   // U+0064 (d)
    { 0x00, 0x00, 0x1E, 0x33, 0x3F, 0x03, 0x1E, 0x00},   // U+0065 (e)
    { 0x1C, 0x36, 0x06, 0x0F, 0x06, 0x06, 0x0F, 0x00},   // U+0066 (f)
    { 0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F},   // U+0067 (g)
    { 0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00},   // U+0068 (h)
    { 0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+0069 (i)
    { 0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E},   // U+006A (j)
    { 0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00},   // U+006B (k)
    { 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},   // U+006C (l)
    { 0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00},   // U+006D (m)
    { 0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00},   // U+006E (n)
    { 0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00},   // U+006F (o)
    { 0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F},   // U+0070 (p)
    { 0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78},   // U+0071 (q)
    { 0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00},   // U+0072 (r)
    { 0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00},   // U+0073 (s)
    { 0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00},   // U+0074 (t)
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00},   // U+0075 (u)
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},   // U+0076 (v)
    { 0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00},   // U+0077 (w)
    { 0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00},   // U+0078 (x)
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F},   // U+0079 (y)
    { 0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00},   // U+007A (z)
    { 0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00},   // U+007B ({)
    { 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00},   // U+007C (|)
    { 0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00},   // U+007D (})
    { 0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // U+007E (~)
};

// ----------------------------------------------------------------------------
// Рисование текста прямо во фреймбуфер RGB565
// ----------------------------------------------------------------------------

// Цвета RGB565 (5 красных | 6 зелёных | 5 синих)
#define COLOR_BLACK  0x0000
#define COLOR_WHITE  0xFFFF
#define COLOR_CYAN   0x07FF
#define COLOR_YELLOW 0xFFE0
#define COLOR_GREEN  0x07E0

// Пиксель в фреймбуфере с защитой от выхода за границы экрана
static inline void put_pixel(uint16_t *fb, int x, int y, uint16_t color)
{
    if (x < 0 || x >= LCD_H_RES || y < 0 || y >= LCD_V_RES) {
        return;
    }
    fb[y * LCD_H_RES + x] = color;
}

// Один символ шрифтом 8x8 с масштабом scale (пиксель шрифта -> scale x scale)
static void draw_char(uint16_t *fb, int x, int y, char ch, uint16_t color, int scale)
{
    if (ch < 0x20 || ch > 0x7E) {
        ch = '?';   // непечатаемые символы показываем знаком вопроса
    }
    const uint8_t *glyph = font8x8_basic[ch - 0x20];
    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (1 << col)) {   // бит 0 — левый пиксель
                // закрашиваем квадрат scale x scale
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        put_pixel(fb, x + col * scale + sx, y + row * scale + sy, color);
                    }
                }
            }
        }
    }
}

// Строка текста, начиная с точки (x, y)
static void draw_text(uint16_t *fb, int x, int y, const char *text, uint16_t color, int scale)
{
    while (*text) {
        draw_char(fb, x, y, *text++, color, scale);
        x += 8 * scale;   // ширина символа вместе с промежутком
    }
}

// Ширина строки в пикселях (для центрирования)
static int text_width(const char *text, int scale)
{
    return (int)strlen(text) * 8 * scale;
}

// Строка по центру экрана
static void draw_text_centered(uint16_t *fb, int y, const char *text, uint16_t color, int scale)
{
    int x = (LCD_H_RES - text_width(text, scale)) / 2;
    draw_text(fb, x, y, text, color, scale);
}

// ----------------------------------------------------------------------------
// Подсветка: ШИМ (LEDC) на GPIO23 — так же, как в BSP платы
// ----------------------------------------------------------------------------
static esp_err_t backlight_init(void)
{
    // Таймер LEDC: 20 кГц, разрядность 10 бит (чтобы не слышать писк)
    const ledc_timer_config_t lcd_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = 1,
        .freq_hz = 20000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&lcd_timer), TAG, "LEDC timer init failed");

    // Канал LEDC: скважность 0 (выключено, включим после отрисовки)
    const ledc_channel_config_t lcd_channel = {
        .gpio_num = LCD_BACKLIGHT_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LCD_BACKLIGHT_LEDC_CH,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = 1,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&lcd_channel), TAG, "LEDC channel init failed");
    return ESP_OK;
}

// Установка яркости подсветки в процентах (0..100)
static esp_err_t backlight_set_percent(int percent)
{
    if (percent < 0 || percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t duty = (1023 * percent) / 100;   // 10 бит = максимум 1023
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_BACKLIGHT_LEDC_CH, duty), TAG, "LEDC set duty failed");
    ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_BACKLIGHT_LEDC_CH), TAG, "LEDC update duty failed");
    return ESP_OK;
}

// ----------------------------------------------------------------------------
// ГЛАВНАЯ ФУНКЦИЯ: вся инициализация дисплея и вывод текста
// ----------------------------------------------------------------------------
void app_main(void)
{
    ESP_LOGI(TAG, "jd9165_hello_display — минимальный тест дисплея 1024x600");

    // --- Шаг 1. Питание DSI-PHY: внутренний LDO, канал 3, 2500 мВ ------------
    esp_ldo_channel_handle_t phy_ldo = NULL;
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = DSI_PHY_LDO_CHANNEL,
        .voltage_mv = DSI_PHY_LDO_VOLTAGE_MV,
        .flags.adjustable = true,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &phy_ldo));
    ESP_LOGI(TAG, "MIPI DSI PHY питание включено (LDO канал %d, %d мВ)", DSI_PHY_LDO_CHANNEL, DSI_PHY_LDO_VOLTAGE_MV);

    // --- Шаг 2. Подсветка: инициализируем с выключенной яркостью -------------
    ESP_ERROR_CHECK(backlight_init());

    // --- Шаг 3. MIPI-DSI шина: 2 data-lane по 750 Мбит/с ---------------------
    esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
    const esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id = 0,
        .num_data_lanes = LCD_MIPI_DSI_LANE_NUM,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = LCD_LANE_BIT_RATE_MBPS,
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus));
    ESP_LOGI(TAG, "MIPI-DSI шина создана (%d lane, %d Мбит/с)", LCD_MIPI_DSI_LANE_NUM, LCD_LANE_BIT_RATE_MBPS);

    // --- Шаг 4. Командный канал DBI (8 бит команда / 8 бит параметр) ---------
    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_dbi_io_config_t dbi_cfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &io_handle));

    // --- Шаг 5. Конфигурация DPI (видеопоток): 52 МГц, RGB565, 1024x600 ------
    // Тайминги — фирменные для этой матрицы (hsync/vsync porch'и)
    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = LCD_DPI_CLOCK_MHZ,
        .virtual_channel = 0,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 1,                       // один фреймбуфер — текст статичный
        .video_timing = {
            .h_size = LCD_H_RES,
            .v_size = LCD_V_RES,
            .hsync_back_porch = 160,
            .hsync_pulse_width = 24,
            .hsync_front_porch = 160,
            .vsync_back_porch = 21,
            .vsync_pulse_width = 2,
            .vsync_front_porch = 12,
        },
        .flags.use_dma2d = true,            // ускорение заливок через DMA2D
    };

    // --- Шаг 6. Панель JD9165: vendor-команды + привязка к шине --------------
    jd9165_vendor_config_t vendor_cfg = {
        .init_cmds = jd9165_gui_init_cmds,
        .init_cmds_size = sizeof(jd9165_gui_init_cmds) / sizeof(jd9165_lcd_init_cmd_t),
        .mipi_config = {
            .dsi_bus = dsi_bus,
            .dpi_config = &dpi_cfg,
        },
    };
    esp_lcd_panel_dev_config_t panel_dev_cfg = {
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .rgb_ele_order = ESP_LCD_COLOR_SPACE_RGB,
        .reset_gpio_num = LCD_RST_GPIO,
        .vendor_config = &vendor_cfg,
    };
    esp_lcd_panel_handle_t panel = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9165(io_handle, &panel_dev_cfg, &panel));

    // --- Шаг 7. Сброс, инициализация (vendor-команды, экран включится) -------
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_LOGI(TAG, "Панель JD9165 инициализирована (%d vendor-команд)", (int)(sizeof(jd9165_gui_init_cmds) / sizeof(jd9165_lcd_init_cmd_t)));

    // --- Шаг 8. Фреймбуфер: получаем адрес и рисуем текст --------------------
    void *fb = NULL;
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(panel, 1, &fb));
    uint16_t *framebuffer = (uint16_t *)fb;

    // Заливка чёрным (2 байта на пиксель)
    memset(framebuffer, 0, LCD_H_RES * LCD_V_RES * (LCD_BIT_PER_PIXEL / 8));

    // Четыре строки по центру: приветствие крупно + сведения о железе
    draw_text_centered(framebuffer, 170, "HELLO!", COLOR_WHITE, 10);         // 80 пт
    draw_text_centered(framebuffer, 300, "ESP32-P4 rev v1.3", COLOR_CYAN, 5);
    draw_text_centered(framebuffer, 370, "JC1060P470C 1024x600", COLOR_YELLOW, 5);
    draw_text_centered(framebuffer, 440, "JD9165 MIPI-DSI 2-lane", COLOR_GREEN, 5);

    ESP_LOGI(TAG, "Текст нарисован во фреймбуфер (PSRAM), включаю подсветку");

    // --- Шаг 9. Включаем подсветку на 100% -----------------------------------
    ESP_ERROR_CHECK(backlight_set_percent(100));

    ESP_LOGI(TAG, "Готово: на экране текст. Дальше приложение ничего не делает.");
    // app_main завершается — дисплей продолжает показывать картинку:
    // DMA DPI-канала сам бесконечно передаёт фреймбуфер на панель.
}
