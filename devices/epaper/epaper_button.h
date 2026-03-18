#ifndef EPAPER_BUTTON_H_
#define EPAPER_BUTTON_H_

#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum {
    EPAPER_BUTTON_EVENT_BOOT_SINGLE = 0,
    EPAPER_BUTTON_EVENT_BOOT_DOUBLE,
    EPAPER_BUTTON_EVENT_BOOT_LONG_RELEASE,
    EPAPER_BUTTON_EVENT_PWR_SINGLE,
    EPAPER_BUTTON_EVENT_PWR_DOUBLE,
    EPAPER_BUTTON_EVENT_PWR_LONG_RELEASE,
} epaper_button_event_type_t;

typedef struct {
    epaper_button_event_type_t type;
    uint32_t press_duration_ms;
} epaper_button_event_t;

esp_err_t epaper_button_init(QueueHandle_t queue);

#endif /* EPAPER_BUTTON_H_ */
