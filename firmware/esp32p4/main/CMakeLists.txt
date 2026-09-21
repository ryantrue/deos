idf_component_register(
    SRCS
        "main.cpp"
        "display_controller.cpp"
        "device_preferences.cpp"
        "i2c_runtime.cpp"
        "network_controller.cpp"
        "resource_runtime.cpp"
        "shell_controller.cpp"
        "storage_controller.cpp"
        "telemetry_service.cpp"
        "touch_controller.cpp"
        "ui_runtime.cpp"
        "ui_navigation.cpp"
        "ui_settings_screen.cpp"
        "ui_home_screen.cpp"
        "ui_theme.cpp"
        "ui_widgets.cpp"
        "ui_shell.cpp"
        "update_controller.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        deos_core
        freertos
        app_update
        esp_app_format
        esp_driver_i2c
        esp_driver_ledc
        esp_driver_sdmmc
        esp_event
        esp_http_client
        esp_https_ota
        esp_http_server
        esp_hw_support
        esp_lcd
        esp_netif
        esp_system
        esp_timer
        esp_wifi
        fatfs
        heap
        mbedtls
        nvs_flash
        sdmmc
        lvgl
        esp_lcd_st7703
        esp_lcd_touch_gt911
        espressif__esp_hosted
        espressif__mdns
)

# Temporary developer convenience. Values come from GitHub Actions secrets and
# are compiled into the dev image without being committed to the repository.
if(DEFINED ENV{DEOS_DEV_WIFI_SSID} AND NOT "$ENV{DEOS_DEV_WIFI_SSID}" STREQUAL "")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        DEOS_DEV_WIFI_SSID="$ENV{DEOS_DEV_WIFI_SSID}"
        DEOS_DEV_WIFI_PASSWORD="$ENV{DEOS_DEV_WIFI_PASSWORD}")
endif()

if(DEFINED ENV{DEOS_SOURCE_SHA} AND NOT "$ENV{DEOS_SOURCE_SHA}" STREQUAL "")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        DEOS_SOURCE_SHA="$ENV{DEOS_SOURCE_SHA}")
endif()
