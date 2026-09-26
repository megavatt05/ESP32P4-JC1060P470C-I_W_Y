// ============================================================================
//  main.c — тест дисплея и демо-UI: включить экран, показать сплэш,
//  отрисовать интерфейс 1024x600 и запустить анимацию
//  Плата: Guition JC1060P470C_I_W_Y (7" IPS 1024x600)
//  Чип:   ESP32-P4 rev v1.3 | Экранная IC: JD9165 (MIPI-DSI, 2 lane)
// ----------------------------------------------------------------------------
//  Тача, камеры и Wi-Fi здесь нет, LVGL не используется. Что на экране:
//    1) сплэш — приветствие и сведения о плате (3 секунды);
//    2) демо-интерфейс 1024x600: шапка, 3 панели свойств, полоса прогресса,
//       кнопки, статус-бар — всё собрано из примитивов ui.c готовыми
//       шрифтами DejaVu Sans (кириллица), затем живая анимация.
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
//  7. UI рисуется ПО НАПРЯМУЮ во фреймбуфер: примитивы ui.c (панели,
//     кнопки, текст) и ГОТОВЫЕ шрифты DejaVu Sans — файлы ui_font_*.c,
//     сгенерированные из системного TTF скриптом tools/make_font.py.
// ============================================================================

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"  // ESP_RETURN_ON_ERROR / ESP_ERROR_CHECK с логом — без него
                        // ошибка "implicit declaration of function
                        // 'ESP_RETURN_ON_ERROR'"
#include "driver/ledc.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_jd9165.h"   // компонент espressif/esp_lcd_jd9165 (см. idf_component.yml)
#include "esp_timer.h"        // esp_timer_get_time() — секунды для UI-анимации
#include "ui.h"               // примитивы UI и готовые шрифты (DejaVu Sans)
#include "ui_demo.h"          // сплэш и демо-экран 1024x600

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
// ТЕКСТ И UI вынесены в отдельные файлы (раньше здесь был шрифт 8x8):
//   ui.c / ui.h           — примитивы: заливка, рамки, линии, UTF-8 текст,
//                           панели, кнопки, полосы прогресса;
//   ui_font_sans16.c      — готовый шрифт DejaVu Sans 16 px (кириллица);
//   ui_font_sans24b.c     — готовый шрифт DejaVu Sans Bold 24 px;
//   ui_font_sans40b.c     — готовый шрифт DejaVu Sans Bold 40 px;
//   ui_demo.c / ui_demo.h — сплэш и демо-экран 1024x600 + анимация.
// ----------------------------------------------------------------------------

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
    // Тайминги — фирменные для этой матрицы (hsync/vsync porch'и).
    // Сверено с компонентом espressif/esp_lcd_jd9165 v2.0.2 (реестр, MCP):
    // порядок полей jd9165_lcd_init_cmd_t {cmd, data, data_bytes, delay_ms}
    // соответствует нашей таблице команд.
    // Альтернатива — официальный макрос JD9165_1024_600_PANEL_60HZ_DPI_CONFIG:
    // 50 МГц, H-porch 136/20/160, V-porch 12/2/20. Наши значения уже проверены
    // на этой панели в демо esp_brookesia_phone; если картинка "поедет" —
    // замените porch'и на официальный вариант.
    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = LCD_DPI_CLOCK_MHZ,
        .virtual_channel = 0,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 1,                       // один фреймбуфер — рисуем прямо в него
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

    // --- Шаг 8. Фреймбуфер: получаем адрес -----------------------------------
    void *fb = NULL;
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(panel, 1, &fb));
    uint16_t *framebuffer = (uint16_t *)fb;

    // --- Шаг 9. Сплэш: приветствие и сведения о плате (шрифты DejaVu) --------
    ui_splash_draw(framebuffer);
    ESP_LOGI(TAG, "Сплэш нарисован во фреймбуфер (PSRAM), включаю подсветку");

    // --- Шаг 10. Подсветка 100% — сплэш стал виден ---------------------------
    ESP_ERROR_CHECK(backlight_set_percent(100));

    // Пауза 3 секунды, чтобы сплэш успели прочитать
    vTaskDelay(pdMS_TO_TICKS(3000));

    // --- Шаг 11. Демо-UI 1024x600: панели, кнопки, прогресс, статус-бар ------
    ui_demo_draw(framebuffer);
    ESP_LOGI(TAG, "Демо-интерфейс отрисован; запускаю цикл анимации (10 Гц)");

    // --- Шаг 12. Цикл анимации: раз в 100 мс обновляем ТОЛЬКО динамику -------
    // Полоса прогресса проходит 0..100 % за ~10 секунд, uptime считает
    // секунды с старта, индикаторы мигают раз в секунду. Статическую часть
    // кадра не трогаем — поэтому анимация без мерцания.
    int progress = 0;
    while (true) {
        progress = (progress + 1) % 101;
        ui_demo_tick(framebuffer,
                     (int)(esp_timer_get_time() / 1000000LL),   // секунды с старта
                     progress);
        vTaskDelay(pdMS_TO_TICKS(100));   // 10 Гц — запас на отрисовку
    }
}
