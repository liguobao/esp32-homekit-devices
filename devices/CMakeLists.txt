idf_component_register(
    SRCS
        "./common/dual_panel_display.c"
        "./common/display_support.c"
        "./common/gpio_output.c"
        "./common/multi_gpio_output.c"
        "./device.c"
        "./dashboard/dashboard_buttons.c"
        "./dashboard/dashboard_content.c"
        "./dashboard/dashboard_http.c"
        "./dashboard/dashboard_poem.c"
        "./dashboard/dashboard_device.c"
        "./dashboard/dashboard_weather.c"
        "./epaper/epaper_audio.c"
        "./epaper/epaper_board.c"
        "./epaper/epaper_button.c"
        "./epaper/epaper_device.c"
        "./epaper/epaper_display.c"
        "./epaper/multi_button.c"
        "./light/light_device.c"
        "./outlet/outlet_device.c"
    INCLUDE_DIRS "./include"
    PRIV_INCLUDE_DIRS
        "./common"
        "./dashboard"
        "./epaper"
        "./light"
        "./outlet"
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_spi
        esp_event
        esp_hap_apple_profiles
        esp_hap_core
        esp_timer
        esp_adc
        esp_audio_codec
        esp_http_client
        esp_lcd
        esp_netif
        esp_wifi
        fatfs
        codec_board
        log
        lwip
        mbedtls
        sdmmc
    EMBED_FILES
        "epaper/embedded_music.mp3")

if(HOMEKIT_DEVICE_TYPE STREQUAL "light")
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        HOMEKIT_DEVICE_TYPE_LIGHT=1
        HOMEKIT_DEVICE_TYPE_OUTLET=0
        HOMEKIT_DEVICE_TYPE_DASHBOARD=0
        HOMEKIT_DEVICE_TYPE_EPAPER=0)
elseif(HOMEKIT_DEVICE_TYPE STREQUAL "dashboard")
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        HOMEKIT_DEVICE_TYPE_LIGHT=0
        HOMEKIT_DEVICE_TYPE_OUTLET=0
        HOMEKIT_DEVICE_TYPE_DASHBOARD=1
        HOMEKIT_DEVICE_TYPE_EPAPER=0)
elseif(HOMEKIT_DEVICE_TYPE STREQUAL "epaper")
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        HOMEKIT_DEVICE_TYPE_LIGHT=0
        HOMEKIT_DEVICE_TYPE_OUTLET=0
        HOMEKIT_DEVICE_TYPE_DASHBOARD=0
        HOMEKIT_DEVICE_TYPE_EPAPER=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        HOMEKIT_DEVICE_TYPE_LIGHT=0
        HOMEKIT_DEVICE_TYPE_OUTLET=1
        HOMEKIT_DEVICE_TYPE_DASHBOARD=0
        HOMEKIT_DEVICE_TYPE_EPAPER=0)
endif()
