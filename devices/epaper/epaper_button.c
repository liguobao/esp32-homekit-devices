#include "epaper_button.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "epaper_board.h"
#include "multi_button.h"

static const char *TAG = "epaper_button";

enum {
    EPAPER_BUTTON_ID_BOOT = 1,
    EPAPER_BUTTON_ID_PWR = 2,
};

static QueueHandle_t s_event_queue;
static esp_timer_handle_t s_tick_timer;
static Button s_boot_button;
static Button s_pwr_button;
static int64_t s_boot_press_start_us;
static int64_t s_pwr_press_start_us;
static bool s_boot_long_pending;
static bool s_pwr_long_pending;
static bool s_initialized;

static void epaper_button_send_event(epaper_button_event_type_t type,
        uint32_t press_duration_ms)
{
    epaper_button_event_t event = {
        .type = type,
        .press_duration_ms = press_duration_ms,
    };

    if (!s_event_queue) {
        return;
    }
    if (xQueueSend(s_event_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Button event queue full, dropping %d", (int) type);
    }
}

static uint32_t epaper_button_elapsed_ms(int64_t start_us)
{
    int64_t now_us = esp_timer_get_time();

    if (start_us <= 0 || now_us <= start_us) {
        return 0;
    }
    return (uint32_t) ((now_us - start_us) / 1000);
}

static uint8_t epaper_button_read_gpio(uint8_t button_id)
{
    switch (button_id) {
    case EPAPER_BUTTON_ID_BOOT:
        return gpio_get_level(CONFIG_HOMEKIT_EPAPER_PIN_BOOT);
    case EPAPER_BUTTON_ID_PWR:
        return gpio_get_level(CONFIG_HOMEKIT_EPAPER_PIN_PWR_KEY);
    default:
        return 1;
    }
}

static void epaper_button_tick(void *arg)
{
    (void) arg;
    button_ticks();
}

static void epaper_boot_down(Button *button)
{
    (void) button;
    s_boot_press_start_us = esp_timer_get_time();
    s_boot_long_pending = false;
}

static void epaper_boot_single(Button *button)
{
    (void) button;
    epaper_button_send_event(EPAPER_BUTTON_EVENT_BOOT_SINGLE, 0);
}

static void epaper_boot_double(Button *button)
{
    (void) button;
    epaper_button_send_event(EPAPER_BUTTON_EVENT_BOOT_DOUBLE, 0);
}

static void epaper_boot_long(Button *button)
{
    (void) button;
    s_boot_long_pending = true;
}

static void epaper_boot_up(Button *button)
{
    (void) button;
    if (s_boot_long_pending) {
        epaper_button_send_event(EPAPER_BUTTON_EVENT_BOOT_LONG_RELEASE,
                epaper_button_elapsed_ms(s_boot_press_start_us));
        s_boot_long_pending = false;
    }
}

static void epaper_pwr_down(Button *button)
{
    (void) button;
    s_pwr_press_start_us = esp_timer_get_time();
    s_pwr_long_pending = false;
}

static void epaper_pwr_single(Button *button)
{
    (void) button;
    epaper_button_send_event(EPAPER_BUTTON_EVENT_PWR_SINGLE, 0);
}

static void epaper_pwr_double(Button *button)
{
    (void) button;
    epaper_button_send_event(EPAPER_BUTTON_EVENT_PWR_DOUBLE, 0);
}

static void epaper_pwr_long(Button *button)
{
    (void) button;
    s_pwr_long_pending = true;
}

static void epaper_pwr_up(Button *button)
{
    (void) button;
    if (s_pwr_long_pending) {
        epaper_button_send_event(EPAPER_BUTTON_EVENT_PWR_LONG_RELEASE,
                epaper_button_elapsed_ms(s_pwr_press_start_us));
        s_pwr_long_pending = false;
    }
}

esp_err_t epaper_button_init(QueueHandle_t queue)
{
    esp_timer_create_args_t timer_args = {
        .callback = epaper_button_tick,
        .name = "epaper_btn",
    };

    ESP_RETURN_ON_FALSE(queue != NULL, ESP_ERR_INVALID_ARG, TAG,
            "button queue required");
    if (s_initialized) {
        s_event_queue = queue;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(epaper_board_init(), TAG, "board init failed");

    s_event_queue = queue;

    button_init(&s_boot_button, epaper_button_read_gpio, 0, EPAPER_BUTTON_ID_BOOT);
    button_attach(&s_boot_button, BTN_PRESS_DOWN, epaper_boot_down);
    button_attach(&s_boot_button, BTN_SINGLE_CLICK, epaper_boot_single);
    button_attach(&s_boot_button, BTN_DOUBLE_CLICK, epaper_boot_double);
    button_attach(&s_boot_button, BTN_LONG_PRESS_START, epaper_boot_long);
    button_attach(&s_boot_button, BTN_PRESS_UP, epaper_boot_up);
    button_start(&s_boot_button);

    button_init(&s_pwr_button, epaper_button_read_gpio, 0, EPAPER_BUTTON_ID_PWR);
    button_attach(&s_pwr_button, BTN_PRESS_DOWN, epaper_pwr_down);
    button_attach(&s_pwr_button, BTN_SINGLE_CLICK, epaper_pwr_single);
    button_attach(&s_pwr_button, BTN_DOUBLE_CLICK, epaper_pwr_double);
    button_attach(&s_pwr_button, BTN_LONG_PRESS_START, epaper_pwr_long);
    button_attach(&s_pwr_button, BTN_PRESS_UP, epaper_pwr_up);
    button_start(&s_pwr_button);

    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_tick_timer), TAG,
            "button timer create failed");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_tick_timer, 1000 * TICKS_INTERVAL),
            TAG, "button timer start failed");

    s_initialized = true;
    ESP_LOGI(TAG, "Button runtime ready");
    return ESP_OK;
}
