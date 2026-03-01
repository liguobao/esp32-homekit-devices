idf_component_register(
    SRCS ./app_main.c
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        app_hap_setup_payload
        app_wifi
        button
        devices
        esp_hap_core
        esp_hap_extras
        esp_hw_support
        freertos
        log)
