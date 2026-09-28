# Camera Photo App — JC1060P470C_I_W_Y

Ветка **`example/camera-photo-app`**: живой preview с камеры на дисплей  
платы **GUITION JC1060P470C_I_W / _Y** (ESP32-P4 rev **v1.3** + ESP32-C6).

| | |
|--|--|
| Камера | **OV02C10** (MIPI-CSI), PID `0x5602` |
| Режим | RAW10 **1288×728** @ ~30 fps → ISP → RGB565 |
| Дисплей | **JD9165** 1024×600, MIPI-DSI 2-lane |
| Проект | `video_lcd_display/` |

---

## Важно: драйвер камеры OV02C10

Официальный `esp_cam_sensor` из Component Registry **не содержит** OV02C10.

В проекте лежит **локальная** копия:

```
video_lcd_display/components/espressif__esp_cam_sensor/   ← версия 1.2.1 + OV02C10
```

Она подключается через `EXTRA_COMPONENT_DIRS` в `CMakeLists.txt`.

**Нельзя** ставить `esp_video` версии 2.x — он требует `esp_cam_sensor` 2.6 и ломает сборку.  
В `main/idf_component.yml` зафиксировано:

```yaml
esp_video:
  version: "~1.2.0"
```

Если после `idf.py reconfigure` появится `managed_components/espressif__esp_cam_sensor` **без** OV02C10 — удалите эту папку, чтобы использовался локальный драйвер.

---

## Требования

- **ESP-IDF ≥ 5.4**, рекомендуется **5.5.x** (проверено на 5.5.5)
- Chip revision: **P4 v1.0 / v1.3** → в menuconfig:
  - *Hardware Settings → Chip revision → Select ESP32-P4 revisions **&lt; 3.0***
  - `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`
- 32 MB PSRAM, 16 MB Flash

---

## Пины (JC1060P470C)

| Сигнал | GPIO |
|--------|------|
| Камера SCCB SDA | **7** |
| Камера SCCB SCL | **8** |
| Подсветка LCD (PWM) | **23** |
| LCD RESET | **5** (типично) |
| LDO MIPI PHY | channel **3**, 2.5 V |

---

## Сборка и запуск

```bash
git clone -b example/camera-photo-app \
  https://github.com/megavatt05/ESP32P4-JC1060P470C-I_W_Y.git
cd ESP32P4-JC1060P470C-I_W_Y/video_lcd_display

# после смены sdkconfig.defaults:
rm -f sdkconfig

idf.py set-target esp32p4
idf.py build
idf.py -p COMx flash monitor
```

Ожидаемый лог:

```
chip revision: v1.3
ov02c10: Detected Camera sensor PID=0x5602
app_video: width=1288 height=728
app_video: Video Stream Start
app_main: fps: ~30
```

На экране — live-картинка с камеры.

---

## Документация

| Файл | Содержание |
|------|------------|
| [video_lcd_display/docs/CAMERA_DISPLAY.md](video_lcd_display/docs/CAMERA_DISPLAY.md) | **Полное** описание: пины, init, пайплайн кадра, режимы, ошибки |
| [video_lcd_display/CAMERA_PHOTO_APP.md](video_lcd_display/CAMERA_PHOTO_APP.md) | Краткая шпаргалка |

---

## Почему не 1920×1080?

На ESP32-P4 **v1.3** поток RAW10 1080p@30 перегружает ISP → `ISP: fifo overflow` и Interrupt WDT.  
Режим **1288×728** стабилен (~30 fps). 1080p (в т.ч. 2-lane) можно пробовать отдельно после отладки.

---

## Структура ветки

```
.
├── README.md                 ← этот файл
└── video_lcd_display/        ← ESP-IDF проект
    ├── main/                 # main.c, app_lcd, app_video
    ├── components/
    │   └── espressif__esp_cam_sensor/   # 1.2.1 + OV02C10
    ├── sdkconfig.defaults
    └── docs/CAMERA_DISPLAY.md
```
