#include "gpio_output.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "gpio_output";
static const gpio_num_t s_output_gpio = GPIO_NUM_2;
static bool s_is_on;

static void gpio_output_apply_state(void)
{
    const int level = s_is_on ? 0 : 1;

    /* Active-low output: low turns the load on, high turns it off. */
    ESP_ERROR_CHECK(gpio_set_level(s_output_gpio, level));
    ESP_LOGI(TAG, "Output state: %s (GPIO%d=%d)",
            s_is_on ? "on" : "off", s_output_gpio, level);
}

void gpio_output_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << s_output_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&io_conf));
    s_is_on = false;
    ESP_LOGI(TAG, "GPIO output init on GPIO%d (active-low).", s_output_gpio);
    gpio_output_apply_state();
}

int gpio_output_set_on(bool value)
{
    s_is_on = value;
    gpio_output_apply_state();
    return 0;
}

bool gpio_output_get_on(void)
{
    return s_is_on;
}
