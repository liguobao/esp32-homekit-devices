cmake_minimum_required(VERSION 3.16)

set(HOMEKIT_PATH ${CMAKE_CURRENT_LIST_DIR}/third_party/esp-homekit-sdk)
if(DEFINED ENV{HOMEKIT_DEVICE_TYPE})
    set(HOMEKIT_DEVICE_TYPE "$ENV{HOMEKIT_DEVICE_TYPE}" CACHE STRING
        "Selected HomeKit device type" FORCE)
elseif(NOT DEFINED HOMEKIT_DEVICE_TYPE)
    set(HOMEKIT_DEVICE_TYPE "outlet" CACHE STRING "Selected HomeKit device type")
endif()
string(TOLOWER "${HOMEKIT_DEVICE_TYPE}" HOMEKIT_DEVICE_TYPE)
set(HOMEKIT_DEVICE_TYPE "${HOMEKIT_DEVICE_TYPE}" CACHE STRING
    "Selected HomeKit device type" FORCE)
set_property(CACHE HOMEKIT_DEVICE_TYPE PROPERTY STRINGS outlet light dashboard epaper)
if(NOT HOMEKIT_DEVICE_TYPE STREQUAL "outlet" AND
        NOT HOMEKIT_DEVICE_TYPE STREQUAL "light" AND
        NOT HOMEKIT_DEVICE_TYPE STREQUAL "dashboard" AND
        NOT HOMEKIT_DEVICE_TYPE STREQUAL "epaper")
    message(FATAL_ERROR
        "Unsupported HOMEKIT_DEVICE_TYPE='${HOMEKIT_DEVICE_TYPE}'. "
        "Use 'outlet', 'light', 'dashboard', or 'epaper'.")
endif()
message(STATUS "HomeKit device type: ${HOMEKIT_DEVICE_TYPE}")
set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
set(DEVICE_SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.${HOMEKIT_DEVICE_TYPE}")
if(EXISTS "${DEVICE_SDKCONFIG_DEFAULTS}")
    list(APPEND SDKCONFIG_DEFAULTS
        "${DEVICE_SDKCONFIG_DEFAULTS}")
endif()
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.local")
    list(APPEND SDKCONFIG_DEFAULTS
        "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.local")
endif()
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/devices
    ${HOMEKIT_PATH}/components
    ${HOMEKIT_PATH}/components/homekit
    ${HOMEKIT_PATH}/examples/common)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_homekit)
