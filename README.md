# example/camera-photo-app

Эта ветка основана на полном пакете платы. **Проект камеры (фотоаппарат / live preview)** лежит здесь:

```
1-Demo/Demo_IDF/ESP-IDF/video_lcd_display/
```

Нужный драйвер OV02C10:

```
1-Demo/Demo_IDF/components/espressif__esp_cam_sensor/
```

Подробности: `1-Demo/Demo_IDF/ESP-IDF/video_lcd_display/CAMERA_PHOTO_APP.md`

## Чистый standalone-репозиторий

https://github.com/megavatt05/JC1060P470C-camera-photo-app

## Sparse checkout (только проект камеры)

```bash
git clone --filter=blob:none --sparse -b example/camera-photo-app \
  https://github.com/megavatt05/ESP32P4-JC1060P470C-I_W_Y.git
cd ESP32P4-JC1060P470C-I_W_Y
git sparse-checkout set \
  1-Demo/Demo_IDF/ESP-IDF/video_lcd_display \
  1-Demo/Demo_IDF/components/espressif__esp_cam_sensor
```

Сборка:

```bash
cd 1-Demo/Demo_IDF/ESP-IDF/video_lcd_display
idf.py set-target esp32p4
idf.py build
idf.py -p PORT flash monitor
```
