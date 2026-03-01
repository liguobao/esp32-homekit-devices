#include "outlet_device.h"

#include <string.h>

#include "esp_log.h"
#include "hap_apple_chars.h"
#include "hap_apple_servs.h"

#include "gpio_output.h"

static const char *TAG = "outlet_device";
static hap_char_t *s_outlet_in_use_char;

static int outlet_identify(hap_acc_t *ha)
{
    (void) ha;
    ESP_LOGI(TAG, "Accessory identified");
    return HAP_SUCCESS;
}

static int outlet_write(hap_write_data_t write_data[], int count,
        void *serv_priv, void *write_priv)
{
    int ret = HAP_SUCCESS;

    (void) serv_priv;
    (void) write_priv;

    for (int i = 0; i < count; i++) {
        hap_write_data_t *write = &write_data[i];

        *(write->status) = HAP_STATUS_VAL_INVALID;
        if (!strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_ON)) {
            hap_val_t outlet_in_use = {
                .b = write->val.b,
            };

            ESP_LOGI(TAG, "Received write for Outlet %s", write->val.b ? "On" : "Off");
            if (gpio_output_set_on(write->val.b) == 0) {
                if (s_outlet_in_use_char) {
                    hap_char_update_val(s_outlet_in_use_char, &outlet_in_use);
                }
                *(write->status) = HAP_STATUS_SUCCESS;
            }
        } else {
            *(write->status) = HAP_STATUS_RES_ABSENT;
        }

        if (*(write->status) == HAP_STATUS_SUCCESS) {
            hap_char_update_val(write->hc, &(write->val));
        } else {
            ret = HAP_FAIL;
        }
    }
    return ret;
}

static int outlet_add_services(hap_acc_t *accessory)
{
    hap_serv_t *service = hap_serv_outlet_create(false, false);
    if (!service) {
        ESP_LOGE(TAG, "Failed to create Outlet service");
        return HAP_FAIL;
    }

    if (hap_serv_add_char(service, hap_char_name_create("Outlet")) != HAP_SUCCESS) {
        ESP_LOGE(TAG, "Failed to add Outlet name characteristic");
        return HAP_FAIL;
    }

    s_outlet_in_use_char = hap_serv_get_char_by_uuid(service, HAP_CHAR_UUID_OUTLET_IN_USE);
    hap_serv_set_write_cb(service, outlet_write);
    hap_acc_add_serv(accessory, service);
    return HAP_SUCCESS;
}

static void outlet_init_hardware(void)
{
    gpio_output_init();
}

static const homekit_device_t s_outlet_device = {
    .name_prefix = "Home Outlet",
    .manufacturer = "Espressif",
    .model = "ESP32Outlet",
    .fw_rev = "1.0.0",
    .hw_rev = "1.0",
    .protocol_version = "1.1.0",
    .cid = HAP_CID_OUTLET,
    .identify = outlet_identify,
    .add_services = outlet_add_services,
    .init_hardware = outlet_init_hardware,
};

const homekit_device_t *outlet_device_get(void)
{
    return &s_outlet_device;
}
