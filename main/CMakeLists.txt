# ============================================================================
#  Сценарий сборки главного компонента (main) проекта ESP_Brookesia Demo
# ----------------------------------------------------------------------------
#  Здесь регистрируется исходный код приложения и задаются нестандартные
#  правила компиляции для библиотек LVGL и ESP-Brookesia.
# ============================================================================

# Регистрация компонента: единственный исходник main.cpp,
# текущая папка добавляется в пути поиска заголовочных файлов
idf_component_register(
    SRCS main.cpp
    INCLUDE_DIRS .)

# ---------------------------------------------------------------------------
# LVGL: упрощённое подключение заголовка lvgl.h и включение демо-музыки
# (макросы применяются только к самой библиотеке LVGL)
# ---------------------------------------------------------------------------
idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB}
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
        -DLV_USE_DEMO_MUSIC
)

# ---------------------------------------------------------------------------
# ESP-Brookesia: если в menuconfig включено CONFIG_ESP_BROOKESIA_MEMORY_USE_CUSTOM,
# все объекты интерфейса размещаются во внешней PSRAM через heap_caps.
# Макросы подменяют аллокатор/деаллокатор памяти фреймворка.
# ---------------------------------------------------------------------------
if(CONFIG_ESP_BROOKESIA_MEMORY_USE_CUSTOM)
    # Получаем список всех компонентов сборки...
    idf_build_get_property(build_components BUILD_COMPONENTS)
    foreach(COMPONENT ${build_components})
        # ...и ищем среди них esp-brookesia (имя зависит от способа установки)
        if(COMPONENT MATCHES "esp-brookesia" OR COMPONENT MATCHES "espressif__esp-brookesia")
            set(TARGET_COMPONENT ${COMPONENT})
            break()
        endif()
    endforeach()

    # Если фреймворк не найден — сборку прерываем с понятной ошибкой
    if(TARGET_COMPONENT STREQUAL "")
        message(FATAL_ERROR "Component 'esp-brookesia' not found.")
    else()
        idf_component_get_property(ESP_BROOKESIA_LIB ${TARGET_COMPONENT} COMPONENT_LIB)
    endif()

    # Аллокатор памяти ESP-Brookesia -> PSRAM (выравненное выделение),
    # освобождение -> стандартный free()
    target_compile_options(
        ${ESP_BROOKESIA_LIB}
        PUBLIC
            "-DESP_BROOKESIA_MEMORY_CUSTOM_MALLOC(x)=heap_caps_aligned_alloc(1, x, MALLOC_CAP_SPIRAM)"  "-DESP_BROOKESIA_MEMORY_CUSTOM_FREE=free"
    )
endif()

# ---------------------------------------------------------------------------
# LVGL: аналогичное размещение кучи LVGL во внешней PSRAM
# (включается опцией CONFIG_LV_MEM_CUSTOM, см. sdkconfig.defaults)
# ---------------------------------------------------------------------------
if(CONFIG_LV_MEM_CUSTOM)
    target_compile_options(
        ${LVGL_LIB}
        PRIVATE
            "-DLV_MEM_CUSTOM_ALLOC(x)=heap_caps_malloc(x, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)"
    )
endif()

# ---------------------------------------------------------------------------
# Образ SPIFFS: содержимое папки spiffs/ (фото, музыка, ресурсы UI)
# записывается в раздел "storage" таблицы partitions.csv и прошивается
# вместе с проектом (FLASH_IN_PROJECT)
# ---------------------------------------------------------------------------
spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)
