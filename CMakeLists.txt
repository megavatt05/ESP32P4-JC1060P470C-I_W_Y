# ============================================================================
#  Корневой сценарий сборки проекта ESP_Brookesia Demo
#  Плата: Guition JC1060P470C (ESP32-P4 rev v1.3) | ESP-IDF v5.4+
# ----------------------------------------------------------------------------
#  Порядок строк в этом файле менять нельзя — сборочная система ESP-IDF
#  требует строгой последовательности объявлений.
# ============================================================================

# Минимальная версия CMake, требуемая ESP-IDF
cmake_minimum_required(VERSION 3.5)

# Ограничение длины путей объектных файлов — обход ошибок компиляции
# на Windows (слишком длинные пути при глубокой вложенности сборки)
set(CMAKE_OBJECT_PATH_MAX 512)

# Подключение главной сборочной инфраструктуры ESP-IDF
# (определяет цели idf.py build/flash/monitor и систему компонентов)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Дополнительные флаги только для C++ (гасят предупреждения сторонних
# библиотек, например LVGL, при переключении значений enum)
set(CXX_COMPILE_OPTIONS
    -Wno-deprecated-enum-enum-conversion
    -Wno-write-strings
)

# Глобальные флаги предупреждений: проект собран из компонентов разных
# авторов, поэтому часть предупреждений подавлена для чистоты лога сборки
add_compile_options(
    -Wno-format
    -Wno-unused-variable
    -Wno-missing-field-initializers
    -Wno-unused-but-set-variable
    -Wno-cast-function-type
    -Wno-ignored-qualifiers
)

# То же подавление предупреждений об устаревших преобразованиях enum,
# но на уровне флагов C++ (нужно некоторым версиям GCC в составе IDF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-enum-enum-conversion")

# Имя проекта: совпадает с именем прошивки esp_brookesia_demo
project(esp_brookesia_demo VERSION 1.0.1)
