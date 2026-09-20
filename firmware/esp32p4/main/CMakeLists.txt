idf_component_register(
    SRCS
        "main.cpp"
        "display_controller.cpp"
        "ui_shell.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        deos_core
        esp_lcd
        esp_driver_ledc
        esp_hw_support
        esp_timer
        heap
        lvgl
        esp_lcd_st7703
)
