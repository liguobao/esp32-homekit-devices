/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on ESPRESSIF SYSTEMS products only, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/* Shared HomeKit bootstrap for device-specific profiles. */

#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_mac.h>

#include <hap.h>
#include <hap_fw_upgrade.h>

#include <iot_button.h>

#include <app_wifi.h>
#include <app_hap_setup_payload.h>

#include "device.h"
#include "display_support.h"

/* Comment out the below line to disable Firmware Upgrades */
#define CONFIG_FIRMWARE_SERVICE

static const char *TAG = "homekit_app";

#define HOMEKIT_TASK_PRIORITY  1
#define HOMEKIT_TASK_STACKSIZE (6 * 1024)
#define HOMEKIT_TASK_NAME      "hap_device"

/* Reset network credentials if button is pressed for more than 3 seconds and then released */
#define RESET_NETWORK_BUTTON_TIMEOUT        3

/* Reset to factory if button is pressed and held for more than 10 seconds */
#define RESET_TO_FACTORY_BUTTON_TIMEOUT     10

/* The button "Boot" will be used as the Reset button for the example */
#if CONFIG_IDF_TARGET_ESP32C3
#define RESET_GPIO  GPIO_NUM_9
#else
#define RESET_GPIO  GPIO_NUM_0
#endif

static void fill_accessory_identity(const homekit_device_t *device,
        char *name, size_t name_size,
        char *serial_num, size_t serial_size)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(name, name_size, "%s-%02X%02X", device->name_prefix, mac[4], mac[5]);
    snprintf(serial_num, serial_size, "%02X%02X%02X%02X%02X%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
/**
 * @brief The network reset button callback handler.
 * Useful for testing the Wi-Fi re-configuration feature of WAC2
 */
static void reset_network_handler(void* arg)
{
    hap_reset_network();
}
/**
 * @brief The factory reset button callback handler.
 */
static void reset_to_factory_handler(void* arg)
{
    hap_reset_to_factory();
}

/**
 * The Reset button  GPIO initialisation function.
 * Same button will be used for resetting Wi-Fi network as well as for reset to factory based on
 * the time for which the button is pressed.
 */
static void reset_key_init(uint32_t key_gpio_pin)
{
    button_handle_t handle = iot_button_create(key_gpio_pin, BUTTON_ACTIVE_LOW);
    iot_button_add_on_release_cb(handle, RESET_NETWORK_BUTTON_TIMEOUT, reset_network_handler, NULL);
    iot_button_add_on_press_cb(handle, RESET_TO_FACTORY_BUTTON_TIMEOUT, reset_to_factory_handler, NULL);
}

static bool should_init_reset_key(const homekit_device_t *device)
{
    if (device && device->uses_custom_buttons) {
        ESP_LOGI(TAG, "Skipping shared reset key because the device manages its own buttons");
        return false;
    }
#if CONFIG_IDF_TARGET_ESP32C3
    if (device && device->uses_custom_display && RESET_GPIO == GPIO_NUM_9) {
        ESP_LOGW(TAG, "Skipping reset key on GPIO9 because it conflicts with the dashboard display wiring");
        return false;
    }
#else
    (void) device;
#endif
    return true;
}

/* The main thread for handling the selected device profile. */
static void device_thread_entry(void *arg)
{
    const homekit_device_t *device = device_get_active();
    hap_acc_t *accessory = NULL;
    static char accessory_name[32];
    static char serial_num[20];
#ifdef CONFIG_EXAMPLE_SETUP_CODE
    const char *setup_code = CONFIG_EXAMPLE_SETUP_CODE;
#else
    const char *setup_code = "PAIR IN APP";
#endif

    if (!device) {
        ESP_LOGE(TAG, "No device profile selected");
        vTaskDelete(NULL);
        return;
    }

    /* Initialize the HAP core */
    hap_init(HAP_TRANSPORT_WIFI);
    fill_accessory_identity(device, accessory_name, sizeof(accessory_name),
            serial_num, sizeof(serial_num));
    ESP_LOGI(TAG, "Starting accessory %s", accessory_name);

    /* Initialise the mandatory parameters for Accessory which will be added as
     * the mandatory services internally
     */
    hap_acc_cfg_t cfg = {
        .name = accessory_name,
        .manufacturer = device->manufacturer,
        .model = device->model,
        .serial_num = serial_num,
        .fw_rev = device->fw_rev,
        .hw_rev = device->hw_rev,
        .pv = device->protocol_version,
        .identify_routine = device->identify,
        .cid = device->cid,
    };

    /* Create accessory object */
    accessory = hap_acc_create(&cfg);
    if (!accessory) {
        ESP_LOGE(TAG, "Failed to create accessory");
        goto app_err;
    }

    /* Add a dummy Product Data */
    uint8_t product_data[] = {'E','S','P','3','2','H','A','P'};
    hap_acc_add_product_data(accessory, product_data, sizeof(product_data));

    /* Add Wi-Fi Transport service required for HAP Spec R16 */
    hap_acc_add_wifi_transport_service(accessory, 0);

    if (device->add_services(accessory) != HAP_SUCCESS) {
        ESP_LOGE(TAG, "Failed to add services for %s", device->model);
        goto app_err;
    }

#ifdef CONFIG_FIRMWARE_SERVICE
    /*  Required for server verification during OTA, PEM format as string  */
    static char server_cert[] = {};
    hap_fw_upgrade_config_t ota_config = {
        .server_cert_pem = server_cert,
    };
    /* Create and add the Firmware Upgrade Service, if enabled.
     * Please refer the FW Upgrade documentation under components/homekit/extras/include/hap_fw_upgrade.h
     * and the top level README for more information.
     */
    hap_serv_t *service = hap_serv_fw_upgrade_create(&ota_config);
    if (!service) {
        ESP_LOGE(TAG, "Failed to create Firmware Upgrade Service");
        goto app_err;
    }
    hap_acc_add_serv(accessory, service);
#endif

    /* Add the Accessory to the HomeKit Database */
    hap_add_accessory(accessory);

    /* Initialize the optional shared status display before hardware-specific I/O. */
    if (!device->uses_custom_display) {
        display_support_init();
        display_support_show_boot(accessory_name, device->model, setup_code);
    }

    /* Initialize the selected device hardware. */
    device->init_hardware();

    /* Register a common button for reset Wi-Fi network and reset to factory.
     */
    if (should_init_reset_key(device)) {
        reset_key_init(RESET_GPIO);
    }

    /* TODO: Do the actual hardware initialization here */

    /* For production accessories, the setup code shouldn't be programmed on to
     * the device. Instead, the setup info, derived from the setup code must
     * be used. Use the factory_nvs_gen utility to generate this data and then
     * flash it into the factory NVS partition.
     *
     * By default, the setup ID and setup info will be read from the factory_nvs
     * Flash partition and so, is not required to set here explicitly.
     *
     * However, for testing purpose, this can be overridden by using hap_set_setup_code()
     * and hap_set_setup_id() APIs, as has been done here.
     */
#ifdef CONFIG_EXAMPLE_USE_HARDCODED_SETUP_CODE
    /* Unique Setup code of the format xxx-xx-xxx. Default: 111-22-333 */
    hap_set_setup_code(CONFIG_EXAMPLE_SETUP_CODE);
    /* Unique four character Setup Id. Default: ES32 */
    hap_set_setup_id(CONFIG_EXAMPLE_SETUP_ID);
#ifdef CONFIG_APP_WIFI_USE_WAC_PROVISIONING
    app_hap_setup_payload(CONFIG_EXAMPLE_SETUP_CODE, CONFIG_EXAMPLE_SETUP_ID, true, cfg.cid);
#else
    app_hap_setup_payload(CONFIG_EXAMPLE_SETUP_CODE, CONFIG_EXAMPLE_SETUP_ID, false, cfg.cid);
#endif
#endif
    ESP_LOGI(TAG, "HomeKit setup code: %s", setup_code);

    /* Enable Hardware MFi authentication (applicable only for MFi variant of SDK) */
    hap_enable_mfi_auth(HAP_MFI_AUTH_HW);

    /* Initialize Wi-Fi */
    app_wifi_init();
    if (device->start_runtime_services) {
        device->start_runtime_services();
    }

    /* After all the initializations are done, start the HAP core */
    hap_start();
    /* Start Wi-Fi */
    if (app_wifi_start(portMAX_DELAY) != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi connection stopped after repeated failures");
    }

    /* The task ends here. The read/write callbacks will be invoked by the HAP Framework */
    vTaskDelete(NULL);

app_err:
    if (accessory) {
        hap_acc_delete(accessory);
    }
    vTaskDelete(NULL);
}

void app_main()
{
    xTaskCreate(device_thread_entry, HOMEKIT_TASK_NAME, HOMEKIT_TASK_STACKSIZE,
            NULL, HOMEKIT_TASK_PRIORITY, NULL);
}
