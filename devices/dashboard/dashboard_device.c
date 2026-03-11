#include "dashboard_device.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hap_apple_chars.h"
#include "hap_apple_servs.h"

#include "dual_panel_display.h"
#include "multi_gpio_output.h"

static const char *TAG = "dashboard_device";

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
                last_levels[i] = level;
                dashboard_button_log_state(i, gpio, level);
                dual_panel_display_set_button(i, dashboard_button_is_pressed(level));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

static void dashboard_init_buttons(void)
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

static int dashboard_identify(hap_acc_t *ha)
{
    (void) ha;
    ESP_LOGI(TAG, "Accessory identified");
    return HAP_SUCCESS;
}

static int dashboard_write(hap_write_data_t write_data[], int count,
        void *serv_priv, void *write_priv)
{
    int ret = HAP_SUCCESS;
    size_t index = (size_t) (uintptr_t) serv_priv;

    (void) write_priv;

    for (int i = 0; i < count; i++) {
        hap_write_data_t *write = &write_data[i];

        *(write->status) = HAP_STATUS_VAL_INVALID;
        if (index >= 3) {
            *(write->status) = HAP_STATUS_RES_ABSENT;
        } else if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_ON)) {
            ESP_LOGI(TAG, "Light %u -> %s",
                    (unsigned int) (index + 1),
                    write->val.b ? "on" : "off");
            if (multi_gpio_output_set(index, write->val.b) == 0) {
                hap_char_update_val(write->hc, &write->val);
                dual_panel_display_set_light(index, write->val.b);
                *(write->status) = HAP_STATUS_SUCCESS;
            }
        } else {
            *(write->status) = HAP_STATUS_RES_ABSENT;
        }

        if (*(write->status) != HAP_STATUS_SUCCESS) {
            ret = HAP_FAIL;
        }
    }
    return ret;
}

static int dashboard_add_services(hap_acc_t *accessory)
{
    for (size_t i = 0; i < 3; i++) {
        char service_name[16];
        hap_serv_t *service = hap_serv_lightbulb_create(false);

        if (!service) {
            ESP_LOGE(TAG, "Failed to create light service %u", (unsigned int) (i + 1));
            return HAP_FAIL;
        }

        snprintf(service_name, sizeof(service_name), "Light %u", (unsigned int) (i + 1));
        if (hap_serv_add_char(service, hap_char_name_create(service_name)) != HAP_SUCCESS) {
            ESP_LOGE(TAG, "Failed to add name char for light %u", (unsigned int) (i + 1));
            return HAP_FAIL;
        }
        hap_serv_set_priv(service, (void *) (uintptr_t) i);
        hap_serv_set_write_cb(service, dashboard_write);
        hap_acc_add_serv(accessory, service);
    }
    return HAP_SUCCESS;
}

static void dashboard_init_hardware(void)
{
    dual_panel_display_init();
    multi_gpio_output_init();

    for (size_t i = 0; i < 3; i++) {
        dual_panel_display_set_light(i, multi_gpio_output_get(i));
    }
    dashboard_init_buttons();
}

static void dashboard_start_runtime_services(void)
{
    dual_panel_display_start();
}

static const homekit_device_t s_dashboard_device = {
    .name_prefix = "Home Panel",
    .manufacturer = "Espressif",
    .model = "ESP32Dashboard",
    .fw_rev = "1.0.0",
    .hw_rev = "1.0",
    .protocol_version = "1.1.0",
    .cid = HAP_CID_LIGHTING,
    .identify = dashboard_identify,
    .add_services = dashboard_add_services,
    .uses_custom_display = true,
    .init_hardware = dashboard_init_hardware,
    .start_runtime_services = dashboard_start_runtime_services,
};

const homekit_device_t *dashboard_device_get(void)
{
    return &s_dashboard_device;
}
