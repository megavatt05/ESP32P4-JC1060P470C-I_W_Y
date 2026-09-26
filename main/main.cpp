// ============================================================================
//  main.cpp — точка входа примера ESP_Brookesia Phone
//  Плата: Guition JC1060P470C (ESP32-P4 rev v1.3 + слейв ESP32-C6)
// ----------------------------------------------------------------------------
//  Последовательность инициализации (функция app_main):
//    1. NVS — энергонезависимое хранилище (настройки Wi-Fi, яркости и т.д.)
//    2. SPIFFS — внутренний раздел "storage" (фото, музыка, ресурсы UI)
//    3. SD-карта — опционально (для видеоплеера; без карты просто ошибка в логе)
//    4. Аудиокодек ES8311 (динамик/наушники, I2S)
//    5. Дисплей MIPI-DSI 1024x600 (JD9165) + подсветка
//    6. Рабочий стол ESP_Brookesia и установка приложений:
//       заглушка Squareline, калькулятор, плеер, настройки, 2048,
//       камера OV02C10, галерея, заметки и видеоплеер (если есть SD)
// ============================================================================

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_memory_utils.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_board_extra.h"

// Фреймворк "телефона" и набор приложений из components/apps
#include "esp_brookesia.hpp"
#include "app_examples/phone/squareline/src/phone_app_squareline.hpp"
#include "apps.h"

// Метка для строк журнала ESP_LOGI/ESP_LOGW
static const char *TAG = "main";

extern "C" void app_main(void)
{
    // --- Шаг 1: NVS. Если хранилище повреждено или сменило версию — стираем и заново
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // --- Шаг 2: SPIFFS (раздел storage, контент из папки spiffs/)
    ESP_ERROR_CHECK(bsp_spiffs_mount());
    ESP_LOGI(TAG, "SPIFFS mount successfully");

// #if CONFIG_EXAMPLE_ENABLE_SD_CARD
    // --- Шаг 3: SD-карта. ВНИМАНИЕ: вендорский код пытается монтировать карту
    // всегда (проверка CONFIG_EXAMPLE_ENABLE_SD_CARD закомментирована).
    // Ошибка в логе "sdmmc_init_ocr ... 0x107" = карты нет в слоте — это не сбой.
    esp_err_t ret = bsp_sdcard_mount();
    if(ret == ESP_OK)
        ESP_LOGI(TAG, "SD card mount successfully");
// #endif

    // --- Шаг 4: аудиокодек ES8311 (динамик/наушники, шина I2S)
    ESP_ERROR_CHECK(bsp_extra_codec_init());

    // --- Шаг 5: дисплей. Буферы кадров размещаем во внешней PSRAM
    // (buff_spiram = true), размер — ширина экрана x 1280 строк
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * 1280,
        .double_buffer = false,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = false,
        }
    };
    // --- Шаг 6: запуск дисплея и подсветки, затем создание рабочего стола
    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    // Все операции с UI выполняем под блокировкой LVGL-порта
    bsp_display_lock(0);

    // Создание объекта "телефона" и подключение тёмной темы 1024x600
    ESP_Brookesia_Phone *phone = new ESP_Brookesia_Phone();
    assert(phone != nullptr && "Failed to create phone");

    ESP_Brookesia_PhoneStylesheet_t *phone_stylesheet = new ESP_Brookesia_PhoneStylesheet_t ESP_BROOKESIA_PHONE_1024_600_DARK_STYLESHEET();
    ESP_BROOKESIA_CHECK_NULL_EXIT(phone_stylesheet, "Create phone stylesheet failed");
    ESP_BROOKESIA_CHECK_FALSE_EXIT(phone->addStylesheet(*phone_stylesheet), "Add phone stylesheet failed");
    ESP_BROOKESIA_CHECK_FALSE_EXIT(phone->activateStylesheet(*phone_stylesheet), "Activate phone stylesheet failed");

    assert(phone->begin() && "Failed to begin phone");

    // --- Установка приложений на рабочий стол ---
    // Заглушка Squareline (демо-виджет)
    PhoneAppSquareline *smart_gadget = new PhoneAppSquareline();
    assert(smart_gadget != nullptr && "Failed to create phone app squareline");
    assert((phone->installApp(smart_gadget) >= 0) && "Failed to install phone app squareline");

    // Калькулятор
    Calculator *calculator = new Calculator();
    assert(calculator != nullptr && "Failed to create calculator");
    assert((phone->installApp(calculator) >= 0) && "Failed to begin calculator");

    MusicPlayer *music_player = new MusicPlayer();
    assert(music_player != nullptr && "Failed to create music_player");
    assert((phone->installApp(music_player) >= 0) && "Failed to begin music_player");

    AppSettings *app_settings = new AppSettings();
    assert(app_settings != nullptr && "Failed to create app_settings");
    assert((phone->installApp(app_settings) >= 0) && "Failed to begin app_settings");

    Game2048 *game_2048 = new Game2048();
    assert(game_2048 != nullptr && "Failed to create game_2048");
    assert((phone->installApp(game_2048) >= 0) && "Failed to begin game_2048");

    // Камера OV02C10 (разрешение 1288x728); если конвейер не поднялся —
    // приложение удаляется с рабочего стола, чтобы не мешать интерфейсу
    Camera *camera = new Camera(1288, 728);
    assert(camera != nullptr && "Failed to create camera");
    assert((phone->installApp(camera) >= 0) && "Failed to begin camera");
    if(camera->get_camera_ctlr_handle() < 0)
    {
        assert((phone->uninstallApp(camera) >= 0) && "Failed to begin camera");
    
    }
        

    AppImageDisplay *image = new AppImageDisplay();
    assert(image != nullptr && "Failed to create image");
    assert((phone->installApp(image) >= 0) && "Failed to begin image");

    NewApp *new_app = new NewApp(480, 800);
    assert(new_app != nullptr && "Failed to create new_app");
    assert((phone->installApp(new_app) >= 0) && "Failed to begin new_app");
    
    // Сводка по памяти после инициализации: внутренняя SRAM и внешняя PSRAM
    uint16_t free_sram_size_kb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
    uint16_t total_sram_size_kb = heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024;
    uint16_t free_psram_size_kb = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
    uint16_t total_psram_size_kb = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024;
    ESP_LOGI(TAG, "Free sram size: %d KB, total sram size: %d KB, "
                         "free psram size: %d KB, total psram size: %d KB",
                         free_sram_size_kb, total_sram_size_kb, free_psram_size_kb, total_psram_size_kb);

// #if CONFIG_EXAMPLE_ENABLE_SD_CARD
    // Видеоплеер ставится на рабочий стол ТОЛЬКО если SD-карта смонтировалась.
    // На карте должно лежать видео в формате MJPEG (см. README)
    if(ret == ESP_OK)
    {
        ESP_LOGW(TAG, "Using Video Player example requires inserting the SD card in advance and saving an MJPEG format video on the SD card");
        AppVideoPlayer *app_video_player = new AppVideoPlayer();
        assert(app_video_player != nullptr && "Failed to create app_video_player");
        assert((phone->installApp(app_video_player) >= 0) && "Failed to begin app_video_player");
    }

    free_sram_size_kb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
    total_sram_size_kb = heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024;
    free_psram_size_kb = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
    total_psram_size_kb = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024;
    ESP_LOGI(TAG, "Free sram size: %d KB, total sram size: %d KB, "
                         "free psram size: %d KB, total psram size: %d KB",
                         free_sram_size_kb, total_sram_size_kb, free_psram_size_kb, total_psram_size_kb);

    
// #endif
    // Инициализация завершена — отпускаем блокировку UI, рабочий стол живёт
    ESP_LOGI(TAG,"setup done");
    bsp_display_unlock();
}
