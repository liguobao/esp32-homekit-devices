#include "light_device.h"

#include <string.h>

#include "esp_log.h"
#include "hap_apple_chars.h"
#include "hap_apple_servs.h"

#include "gpio_output.h"

static const char *TAG = "light_device";
static int s_brightness = 100;
static float s_hue = 180.0f;
static float s_saturation = 100.0f;

static int light_identify(hap_acc_t *ha)
{
    (void) ha;
    ESP_LOGI(TAG, "Accessory identified");
    return HAP_SUCCESS;
}

static int light_write(hap_write_data_t write_data[], int count,
        void *serv_priv, void *write_priv)
{
    int ret = HAP_SUCCESS;

    (void) serv_priv;
    (void) write_priv;

    for (int i = 0; i < count; i++) {
        hap_write_data_t *write = &write_data[i];
        const char *uuid = hap_char_get_type_uuid(write->hc);

        *(write->status) = HAP_STATUS_VAL_INVALID;
        if (!strcmp(uuid, HAP_CHAR_UUID_ON)) {
            ESP_LOGI(TAG, "Received write for Light %s", write->val.b ? "On" : "Off");
            if (gpio_output_set_on(write->val.b) == 0) {
                *(write->status) = HAP_STATUS_SUCCESS;
            }
        } else if (!strcmp(uuid, HAP_CHAR_UUID_BRIGHTNESS)) {
            s_brightness = write->val.i;
            ESP_LOGI(TAG, "Brightness set to %d", s_brightness);
            *(write->status) = HAP_STATUS_SUCCESS;
        } else if (!strcmp(uuid, HAP_CHAR_UUID_HUE)) {
            s_hue = write->val.f;
            ESP_LOGI(TAG, "Hue set to %.1f", (double) s_hue);
            *(write->status) = HAP_STATUS_SUCCESS;
        } else if (!strcmp(uuid, HAP_CHAR_UUID_SATURATION)) {
            s_saturation = write->val.f;
            ESP_LOGI(TAG, "Saturation set to %.1f", (double) s_saturation);
            *(write->status) = HAP_STATUS_SUCCESS;
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

static int light_add_services(hap_acc_t *accessory)
{
    hap_serv_t *service = hap_serv_lightbulb_create(false);
    if (!service) {
        ESP_LOGE(TAG, "Failed to create Light service");
        return HAP_FAIL;
    }

    if (hap_serv_add_char(service, hap_char_name_create("Light")) != HAP_SUCCESS ||
            hap_serv_add_char(service, hap_char_brightness_create(s_brightness)) != HAP_SUCCESS ||
            hap_serv_add_char(service, hap_char_hue_create(s_hue)) != HAP_SUCCESS ||
            hap_serv_add_char(service, hap_char_saturation_create(s_saturation)) != HAP_SUCCESS) {
        ESP_LOGE(TAG, "Failed to add Light characteristics");
        return HAP_FAIL;
    }

    hap_serv_set_write_cb(service, light_write);
    hap_acc_add_serv(accessory, service);
    return HAP_SUCCESS;
}

static void light_init_hardware(void)
{
    gpio_output_init();
}

static const homekit_device_t s_light_device = {
    .name_prefix = "Home Light",
    .manufacturer = "Espressif",
    .model = "ESP32Light",
    .fw_rev = "1.0.0",
    .hw_rev = "1.0",
    .protocol_version = "1.1.0",
    .cid = HAP_CID_LIGHTING,
    .identify = light_identify,
    .add_services = light_add_services,
    .uses_custom_display = false,
    .init_hardware = light_init_hardware,
    .start_runtime_services = NULL,
};

const homekit_device_t *light_device_get(void)
{
    return &s_light_device;
}
