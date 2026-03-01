idf_component_register(
    SRCS
        "./common/gpio_output.c"
        "./device.c"
        "./light/light_device.c"
        "./outlet/outlet_device.c"
    INCLUDE_DIRS "./include"
    PRIV_INCLUDE_DIRS
        "./common"
        "./light"
        "./outlet"
    REQUIRES
        esp_driver_gpio
        esp_hap_apple_profiles
        esp_hap_core
        log)

if(HOMEKIT_DEVICE_TYPE STREQUAL "light")
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        HOMEKIT_DEVICE_TYPE_LIGHT=1
        HOMEKIT_DEVICE_TYPE_OUTLET=0)
else()
    target_compile_definitions(${COMPONENT_LIB} PUBLIC
        HOMEKIT_DEVICE_TYPE_LIGHT=0
        HOMEKIT_DEVICE_TYPE_OUTLET=1)
endif()
