#include "dashboard_buttons.h"

#include <stdbool.h>
#include <stddef.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "dashboard_content.h"
#include "dual_panel_display.h"

static const char *TAG = "dashboard_buttons";

#ifdef CONFIG_HOMEKIT_DASHBOARD_BUTTON_ACTIVE_LOW
#define DASHBOARD_BUTTON_PRESSED_LEVEL 0
#else
#define DASHBOARD_BUTTON_PRESSED_LEVEL 1
#endif

static const int s_button_gpios[3] = {
    CONFIG_HOMEKIT_DASHBOARD_BUTTON1_GPIO,
    CONFIG_HOMEKIT_DASHBOARD_BUTTON2_GPIO,
    CONFIG_HOMEKIT_DASHBOARD_BUTTON3_GPIO,
};
static TaskHandle_t s_button_poll_task;

static bool dashboard_button_is_pressed(int level)
{
    return level == DASHBOARD_BUTTON_PRESSED_LEVEL;
}

static void dashboard_button_log_state(size_t index, int gpio, int level)
{
    ESP_LOGD(TAG, "Button %u GPIO%d raw=%d pressed=%s",
            (unsigned int) (index + 1), gpio, level,
            dashboard_button_is_pressed(level) ? "yes" : "no");
}

static void dashboard_button_poll_task(void *arg)
{
    int last_levels[3] = { -1, -1, -1 };

    (void) arg;

    for (size_t i = 0; i < 3; i++) {
        int gpio = s_button_gpios[i];

        if (gpio >= 0) {
            last_levels[i] = gpio_get_level((gpio_num_t) gpio);
        }
    }

    while (true) {
        for (size_t i = 0; i < 3; i++) {
            int gpio = s_button_gpios[i];

            if (gpio < 0) {
                continue;
            }

            int level = gpio_get_level((gpio_num_t) gpio);
            if (last_levels[i] != level) {
                bool is_pressed = dashboard_button_is_pressed(level);

                last_levels[i] = level;
                dashboard_button_log_state(i, gpio, level);
                if (i == 0) {
                    if (is_pressed) {
                        dashboard_content_request_poem_refresh();
                    }
                } else {
                    dual_panel_display_set_button(i, is_pressed);
                    if (i == 1 && is_pressed) {
                        dual_panel_display_request_right_refresh();
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

void dashboard_buttons_init(void)
{
    bool has_button = false;

    for (size_t i = 0; i < 3; i++) {
        int gpio = s_button_gpios[i];

        if (gpio < 0) {
            ESP_LOGW(TAG, "Skipping button %u because GPIO is not configured",
                    (unsigned int) (i + 1));
            continue;
        }

        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << gpio,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };

        if (gpio_config(&io_conf) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to configure button %u on GPIO%d",
                    (unsigned int) (i + 1), gpio);
            continue;
        }

        has_button = true;
        dashboard_button_log_state(i, gpio, gpio_get_level((gpio_num_t) gpio));
        dual_panel_display_set_button(i,
                dashboard_button_is_pressed(gpio_get_level((gpio_num_t) gpio)));
    }

    if (has_button && !s_button_poll_task) {
        BaseType_t task_ok = xTaskCreate(dashboard_button_poll_task,
                "dashboard_btn", 2048, NULL, 4, &s_button_poll_task);
        if (task_ok != pdPASS) {
            ESP_LOGW(TAG, "Failed to start dashboard button poll task");
            s_button_poll_task = NULL;
        }
    }
}
