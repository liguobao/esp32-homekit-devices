#include "dashboard_device.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "hap_apple_chars.h"
#include "hap_apple_servs.h"

#include "dual_panel_display.h"
#include "multi_gpio_output.h"

static const char *TAG = "dashboard_device";

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
