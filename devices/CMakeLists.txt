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
        "./light/light_device.c"
        "./outlet/outlet_device.c"
    INCLUDE_DIRS "./include"
    PRIV_INCLUDE_DIRS
        "./common"
        "./dashboard"
        "./light"
        "./outlet"
    REQUIRES
        esp_driver_gpio
        esp_driver_spi
        esp_event
        esp_hap_apple_profiles
        esp_hap_core
        esp_http_client
        esp_lcd
        esp_netif
        esp_wifi
        log
        lwip
        mbedtls)

if(HOMEKIT_DEVICE_TYPE STREQUAL "light")
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        HOMEKIT_DEVICE_TYPE_LIGHT=1
        HOMEKIT_DEVICE_TYPE_OUTLET=0
        HOMEKIT_DEVICE_TYPE_DASHBOARD=0)
elseif(HOMEKIT_DEVICE_TYPE STREQUAL "dashboard")
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        HOMEKIT_DEVICE_TYPE_LIGHT=0
        HOMEKIT_DEVICE_TYPE_OUTLET=0
        HOMEKIT_DEVICE_TYPE_DASHBOARD=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        HOMEKIT_DEVICE_TYPE_LIGHT=0
        HOMEKIT_DEVICE_TYPE_OUTLET=1
        HOMEKIT_DEVICE_TYPE_DASHBOARD=0)
endif()
