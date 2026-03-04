#include "multi_gpio_output.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "multi_gpio_output";

#ifdef CONFIG_HOMEKIT_DASHBOARD_LIGHT_ACTIVE_LOW
#define DASHBOARD_LIGHT_ACTIVE_LOW 1
#else
#define DASHBOARD_LIGHT_ACTIVE_LOW 0
#endif

static const gpio_num_t s_output_gpios[MULTI_GPIO_OUTPUT_COUNT] = {
    CONFIG_HOMEKIT_DASHBOARD_LIGHT1_GPIO,
    CONFIG_HOMEKIT_DASHBOARD_LIGHT2_GPIO,
    CONFIG_HOMEKIT_DASHBOARD_LIGHT3_GPIO,
};
static bool s_output_states[MULTI_GPIO_OUTPUT_COUNT];

static bool multi_gpio_output_is_valid(size_t index)
{
    return index < MULTI_GPIO_OUTPUT_COUNT && s_output_gpios[index] >= 0;
}

static int multi_gpio_output_apply_state(size_t index)
{
    int level;

    if (!multi_gpio_output_is_valid(index)) {
        return -1;
    }

    level = s_output_states[index] ? 1 : 0;
    if (DASHBOARD_LIGHT_ACTIVE_LOW) {
        level = !level;
    }

    ESP_ERROR_CHECK(gpio_set_level(s_output_gpios[index], level));
    ESP_LOGI(TAG, "Output %u state: %s (GPIO%d=%d)",
            (unsigned int) (index + 1),
            s_output_states[index] ? "on" : "off",
            s_output_gpios[index], level);
    return 0;
}

void multi_gpio_output_init(void)
{
    for (size_t i = 0; i < MULTI_GPIO_OUTPUT_COUNT; i++) {
        if (!multi_gpio_output_is_valid(i)) {
            ESP_LOGW(TAG, "Skipping output %u because GPIO is not configured",
                    (unsigned int) (i + 1));
            continue;
        }

        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << s_output_gpios[i],
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };

        ESP_ERROR_CHECK(gpio_config(&io_conf));
        s_output_states[i] = false;
        multi_gpio_output_apply_state(i);
    }
}

int multi_gpio_output_set(size_t index, bool value)
{
    if (!multi_gpio_output_is_valid(index)) {
        return -1;
    }
    s_output_states[index] = value;
    return multi_gpio_output_apply_state(index);
}

bool multi_gpio_output_get(size_t index)
{
    if (!multi_gpio_output_is_valid(index)) {
        return false;
    }
    return s_output_states[index];
}
