# Two boards share this project. The default is the Waveshare ESP32-S3-Touch-LCD-1.46B (412 x 412).
# For the 1.85B (360 x 360):   idf.py -B build185 -DSDKCONFIG=sdkconfig185 -DNAV_BOARD=1_85B build
if(NAV_BOARD STREQUAL "1_85B")
    set(BOARD_SRCS
        "board185/board_battery.c"
        "board185/board_motion.c"
        "board185/board_sound.c"
        "board_voices.c"
        "board185/I2C_Driver/I2C_Driver.c"
        "board185/LCD_Driver/Display_ST77916.c"
        "board185/Touch_Driver/Touch_CST816S.c")
    set(BOARD_DIRS
        "board185"
        "board185/I2C_Driver"
        "board185/EXIO"
        "board185/PWR_Key"
        "board185/LCD_Driver"
        "board185/Touch_Driver")
    set(BOARD_REQUIRES espressif__esp_lcd_st77916 espressif__esp_lcd_touch_cst816s espressif__esp_codec_dev)
    set(NAV_SPLASH "../../ui/splash_logo_360.c")
    set(NAV_FONTS "../../ui/fonts/font_montserrat_bold_19.c" "../../ui/fonts/font_montserrat_bold_23.c"
                  "../../ui/fonts/font_montserrat_bold_38.c" "../../ui/fonts/font_montserrat_bold_105.c"
                  "../../ui/fonts/font_montserrat_bold_70.c")
else()
    set(BOARD_SRCS
        "board_sound.c"
        "board_battery_stub.c"
        "board_motion_stub.c"
        "board_voices.c"
        "board/I2C_Driver/I2C_Driver.c"
        "board/EXIO/TCA9554PWR.c"
        "board/PWR_Key/PWR_Key.c"
        "board/LCD_Driver/Display_SPD2010.c"
        "board/LCD_Driver/esp_lcd_spd2010/esp_lcd_spd2010.c"
        "board/Touch_Driver/Touch_SPD2010.c")
    set(BOARD_DIRS
        "board"
        "board/I2C_Driver"
        "board/EXIO"
        "board/PWR_Key"
        "board/LCD_Driver"
        "board/LCD_Driver/esp_lcd_spd2010"
        "board/Touch_Driver")
    set(BOARD_REQUIRES "")
    set(NAV_SPLASH "../../ui/splash_logo.c")
    set(NAV_FONTS "../../ui/fonts/font_montserrat_bold_22.c" "../../ui/fonts/font_montserrat_bold_26.c"
                  "../../ui/fonts/font_montserrat_bold_44.c" "../../ui/fonts/font_montserrat_bold_120.c"
                  "../../ui/fonts/font_montserrat_bold_80.c")
endif()

idf_component_register(
    SRCS
        "app_main.c"
        "lvgl_port.c"
        "ble_server.c"
        "nav_nvs_store.c"
        "board_time.c"
        ${BOARD_SRCS}
        "../../ui/nav_ui.c"
        ${NAV_SPLASH}
        "../../ui/nav_trip.c"
        "../../ui/nav_route_rx.c"
        "../../ui/nav_roads_rx.c"
        "../../ui/nav_areas_rx.c"
        "../../ui/nav_trip_store.c"
        "../../ui/nav_link.c"
        "../../ui/nav_battery.c"
        ${NAV_FONTS}
        "../../sim/feed.c"
    PRIV_REQUIRES bt nvs_flash driver esp_lcd esp_timer esp_adc esp_driver_i2c esp_driver_i2s ${BOARD_REQUIRES}
    INCLUDE_DIRS
        "."
        ${BOARD_DIRS}
        "../../ui"
        "../../proto"
        "../../sim"
)

if(NAV_BOARD STREQUAL "1_85B")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE NAV_UI_SIZE=360 NAV_BOARD_1_85B=1)
endif()
