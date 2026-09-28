# Camera Photo App — JC1060P470C_I_W_Y

Живой preview с камеры MIPI-CSI (OV02C10) на дисплей MIPI-DSI 1024×600 (JD9165).
Готовый пример «фотоаппарата» / видоискателя.

## Что уже настроено в этом примере

| Параметр | Значение |
|----------|----------|
| Плата | `CONFIG_BOARD_TYPE_JC1060P470=y` |
| Камера | `CONFIG_CAMERA_OV02C10=y` |
| Формат | RAW10 1920×1080 @ 30 fps (MIPI 2-lane) + ISP → RGB565 |
| Дисплей | JD9165, 1024×600 RGB565, DPI ~52 MHz |
| SCCB (I2C камеры) | SDA=GPIO7, SCL=GPIO8 |
| Backlight | GPIO23 (LEDC) |
| LCD RST | GPIO5 (в app_lcd.c для JC1060) |
| LDO MIPI PHY | channel 3, 2.5 V |
| PSRAM | 200 MHz |

Официальная заметка в `1-Demo/Demo_IDF/ESP-IDF/README.md`:
> При использовании камеры нужно заменить managed_components/`esp_cam_sensor` на версию из `1-Demo/Demo_IDF/components/espressif__esp_cam_sensor` (v1.2.1 с поддержкой OV02C10).

## Быстрый старт

```bash
cd 1-Demo/Demo_IDF/ESP-IDF/video_lcd_display

# IDF ≥ 5.4 (рекомендуется 5.5.x)
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyACM0 flash monitor   # High-Speed USB порт
```

После старта на экране появится live-preview с камеры (PPA scale/rotate в main.c уже настроен под 1024×600).

## Менюconfig (если нужно)

```
Example Boards Types → JC1060P470
Espressif Camera Sensors Configurations → [*] OV02C10
    Default format → MIPI RAW10 1920x1080 30fps
Example Configuration →
    MIPI CSI SCCB I2C SCL Pin = 8
    MIPI CSI SCCB I2C SDA Pin = 7
```

`sdkconfig.defaults` уже содержит эти значения.

## Архитектура

1. `app_lcd_init()` — LDO ch3, DSI bus, JD9165 vendor init, DPI panel 1024×600.
2. `app_video_main()` + `app_video_open()` — esp_video + OV02C10 через V4L2-like API.
3. Callback `camera_video_frame_operation()` — PPA SRM (scale/rotate/mirror) кадр камеры → framebuffer LCD.
4. `esp_lcd_panel_draw_bitmap()` — вывод на MIPI-DSI.

## Следующие шаги (расширение «фотоаппарата»)

- Кнопка / тач (GT911 на I2C 7/8) → захват JPEG кадра (JPEG HW encoder на P4).
- Сохранение на TF-карту (`esp_video` example `image_storage/sd_card`).
- UVC webcam: `esp_video/examples/uvc`.
- HTTP MJPEG stream (после Wi-Fi через C6 / Ethernet).

Полезные официальные примеры Espressif:

- https://github.com/espressif/esp-iot-solution/tree/master/examples/camera/video_lcd_display
- https://github.com/espressif/esp-video-components/tree/master/esp_video/examples
- https://github.com/espressif/esp-bsp/tree/master/examples/display_camera_video
- docs: https://docs.espressif.com/projects/esp-video-components/en/latest/esp32p4/

## Важно для этой платы

- Chip revision v1.0/v1.3 → в menuconfig Hardware Settings → Chip revision → min Rev v1.0, без 3.x.
- Камера и дисплей оба MIPI — LDO channel 3 обязателен.
- SDIO конфликт: Wi-Fi (C6) и TF-карта делят линии; для сохранения фото предпочтительнее Ethernet или временно отключать Wi-Fi.
