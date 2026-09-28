# Camera Photo App — JC1060P470C_I_W_Y

Живой preview: **OV02C10 (MIPI-CSI)** → ISP → **JD9165 1024×600 (MIPI-DSI)**.

## Полная документация

**→ [docs/CAMERA_DISPLAY.md](docs/CAMERA_DISPLAY.md)** — пины, инициализация, пайплайн кадра, режимы, сборка, известные проблемы.

## Кратко (рабочая конфигурация)

| Параметр | Значение |
|----------|----------|
| Плата | `CONFIG_BOARD_TYPE_JC1060P470=y` |
| Chip rev | **v1.3** → `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` |
| Камера | OV02C10, PID `0x5602` |
| Формат | **RAW10 1288×728 @ 30 fps** (не 1080p на v1.3) |
| Дисплей | JD9165, 1024×600 RGB565, DPI 52 MHz |
| SCCB | **SDA=GPIO7, SCL=GPIO8** |
| Backlight | **GPIO23** (LEDC) |
| LCD RST | **GPIO5** (типично) |
| LDO MIPI | channel **3**, 2.5 V |
| esp_video | **~1.2.0** + локальный `esp_cam_sensor` 1.2.1 с OV02C10 |

## Быстрый старт

```bash
cd video_lcd_display
rm -f sdkconfig   # после смены defaults
idf.py set-target esp32p4
idf.py build
idf.py -p COMx flash monitor
```

Ожидается: `width=1288 height=728`, `fps: ~30`.
