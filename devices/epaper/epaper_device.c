#include "epaper_device.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hap.h"
#include "hap_apple_chars.h"
#include "hap_apple_servs.h"

#include "epaper_audio.h"
#include "epaper_board.h"
#include "epaper_button.h"
#include "epaper_display.h"
#include "esp_wifi.h"

static const char *TAG = "epaper_device";
static const char *SD_STATUS_PATH = "/sdcard/homekit-epaper-status.txt";
static const char *SD_LOG_PATH = "/sdcard/homekit-epaper-log.csv";

static hap_char_t *s_temperature_char;
static hap_char_t *s_humidity_char;
static hap_char_t *s_battery_level_char;
static hap_char_t *s_low_battery_char;
static QueueHandle_t s_button_queue;
static bool s_runtime_started;
static char s_status_text[32] = "BOOT READY";

static int epaper_identify(hap_acc_t *ha)
{
    (void) ha;
    ESP_LOGI(TAG, "Accessory identified");
    return HAP_SUCCESS;
}

static void epaper_set_status(const char *status)
{
    if (!status || status[0] == '\0') {
        snprintf(s_status_text, sizeof(s_status_text), "IDLE");
        return;
    }
    snprintf(s_status_text, sizeof(s_status_text), "%s", status);
}

static uint8_t epaper_wifi_score_from_rssi(int8_t rssi)
{
    if (rssi <= -100) {
        return 0;
    }
    if (rssi >= -40) {
        return 30;
    }
    return (uint8_t) (((int) rssi + 100) * 30 / 60);
}

static int epaper_add_services(hap_acc_t *accessory)
{
    hap_serv_t *temperature_service = hap_serv_temperature_sensor_create(0.0f);
    hap_serv_t *humidity_service = hap_serv_humidity_sensor_create(0.0f);
    hap_serv_t *battery_service = hap_serv_battery_service_create(100, 0, 0);

    if (!temperature_service || !humidity_service || !battery_service) {
        ESP_LOGE(TAG, "Failed to create epaper services");
        return HAP_FAIL;
    }

    if (hap_serv_add_char(temperature_service,
                hap_char_name_create("Temperature")) != HAP_SUCCESS ||
            hap_serv_add_char(humidity_service,
                hap_char_name_create("Humidity")) != HAP_SUCCESS ||
            hap_serv_add_char(battery_service,
                hap_char_name_create("Battery")) != HAP_SUCCESS) {
        ESP_LOGE(TAG, "Failed to name epaper services");
        return HAP_FAIL;
    }

    s_temperature_char = hap_serv_get_char_by_uuid(temperature_service,
            HAP_CHAR_UUID_CURRENT_TEMPERATURE);
    s_humidity_char = hap_serv_get_char_by_uuid(humidity_service,
            HAP_CHAR_UUID_CURRENT_RELATIVE_HUMIDITY);
    s_battery_level_char = hap_serv_get_char_by_uuid(battery_service,
            HAP_CHAR_UUID_BATTERY_LEVEL);
    s_low_battery_char = hap_serv_get_char_by_uuid(battery_service,
            HAP_CHAR_UUID_STATUS_LOW_BATTERY);
    if (!s_temperature_char || !s_humidity_char ||
            !s_battery_level_char || !s_low_battery_char) {
        ESP_LOGE(TAG, "Failed to resolve epaper characteristics");
        return HAP_FAIL;
    }

    hap_acc_add_serv(accessory, temperature_service);
    hap_acc_add_serv(accessory, humidity_service);
    hap_acc_add_serv(accessory, battery_service);
    return HAP_SUCCESS;
}

static void epaper_update_homekit(const epaper_dashboard_state_t *state)
{
    hap_val_t temperature_value = { 0 };
    hap_val_t humidity_value = { 0 };
    hap_val_t battery_level = { 0 };
    hap_val_t low_battery = { 0 };

    if (!state) {
        return;
    }
    if (state->reading_valid && s_temperature_char && s_humidity_char) {
        temperature_value.f = state->reading.temperature_c;
        humidity_value.f = state->reading.humidity_pct;
        hap_char_update_val(s_temperature_char, &temperature_value);
        hap_char_update_val(s_humidity_char, &humidity_value);
    }
    if (state->battery_valid && state->battery.present &&
            s_battery_level_char && s_low_battery_char) {
        battery_level.u = state->battery.level_pct;
        low_battery.u = state->battery.is_low ? 1 : 0;
        hap_char_update_val(s_battery_level_char, &battery_level);
        hap_char_update_val(s_low_battery_char, &low_battery);
    }
}

static void epaper_append_sd_log(const epaper_dashboard_state_t *state)
{
    char line[160];
    struct stat stat_buf = { 0 };

    if (!state || !state->sd_mounted || !state->rtc_valid || !state->reading_valid) {
        return;
    }
#ifndef CONFIG_HOMEKIT_EPAPER_ENABLE_SD_LOGGING
    return;
#endif

    if (stat(SD_LOG_PATH, &stat_buf) != 0 || stat_buf.st_size == 0) {
        static const char header[] =
            "date,time,temp_c,humidity_pct,battery_pct,battery_v,wake,status\n";

        if (epaper_board_sd_append_file(SD_LOG_PATH, header, strlen(header)) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to write SD log header");
            return;
        }
    }

    snprintf(line, sizeof(line),
            "%04d-%02d-%02d,%02d:%02d:%02d,%.2f,%.2f,%u,%.2f,%s,%s\n",
            state->rtc_time.year,
            state->rtc_time.month,
            state->rtc_time.day,
            state->rtc_time.hour,
            state->rtc_time.minute,
            state->rtc_time.second,
            (double) state->reading.temperature_c,
            (double) state->reading.humidity_pct,
            state->battery_valid && state->battery.present ? state->battery.level_pct : 0,
            state->battery_valid ? (double) state->battery.voltage_v : 0.0,
            state->wake_text,
            state->status_text);
    if (epaper_board_sd_append_file(SD_LOG_PATH, line, strlen(line)) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to append SD log");
    }
}

static esp_err_t epaper_write_sd_snapshot(const epaper_dashboard_state_t *state)
{
    char snapshot[512];

    if (!state || !state->sd_mounted) {
        return ESP_ERR_NOT_FOUND;
    }

    snprintf(snapshot, sizeof(snapshot),
            "status=%s\nwake=%s\n"
            "time=%04d/%02d/%02d %02d:%02d:%02d\n"
            "temperature=%.2f C\nhumidity=%.2f %%\n"
            "battery_present=%d\nbattery_level=%u %%\nbattery_voltage=%.2f V\n"
            "audio=%s\nmusic_file=%s\nmusic_status=%s\nsd_capacity=%.2f G\n",
            state->status_text,
            state->wake_text,
            state->rtc_time.year,
            state->rtc_time.month,
            state->rtc_time.day,
            state->rtc_time.hour,
            state->rtc_time.minute,
            state->rtc_time.second,
            (double) state->reading.temperature_c,
            (double) state->reading.humidity_pct,
            state->battery.present ? 1 : 0,
            state->battery.level_pct,
            (double) state->battery.voltage_v,
            state->audio_text,
            state->music_file,
            state->music_status,
            (double) state->sd_capacity_gb);
    return epaper_board_sd_write_file(SD_STATUS_PATH, snapshot, strlen(snapshot));
}

static void epaper_collect_dashboard_state(epaper_dashboard_state_t *state)
{
    wifi_ap_record_t ap_info = { 0 };

    memset(state, 0, sizeof(*state));

    if (epaper_board_shtc3_read(&state->reading) == ESP_OK) {
        state->reading_valid = true;
    }
    if (epaper_board_rtc_read(&state->rtc_time) == ESP_OK) {
        state->rtc_valid = true;
    }
    if (epaper_board_battery_read(&state->battery) == ESP_OK) {
        state->battery_valid = true;
    }

    state->sd_mounted = epaper_board_sd_is_mounted();
    state->sd_capacity_gb = epaper_board_sd_capacity_gb();
    state->audio_ready = epaper_audio_is_ready();
    state->ble_count_valid = true;
    state->ble_count = 0;
    epaper_audio_get_music_info(state->music_file, sizeof(state->music_file),
            state->music_status, sizeof(state->music_status));
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        state->wifi_score_valid = true;
        state->wifi_score = epaper_wifi_score_from_rssi(ap_info.rssi);
    }
    snprintf(state->wake_text, sizeof(state->wake_text), "%s",
            epaper_board_wake_reason_name(epaper_board_get_wake_reason()));
    snprintf(state->audio_text, sizeof(state->audio_text), "%s",
            epaper_audio_state_name(epaper_audio_get_state()));
    snprintf(state->status_text, sizeof(state->status_text), "%s", s_status_text);
}

static void epaper_refresh_dashboard(bool append_sd_log)
{
    epaper_dashboard_state_t state;

    epaper_collect_dashboard_state(&state);
    epaper_update_homekit(&state);
    epaper_display_show_dashboard(&state);
    if (append_sd_log) {
        epaper_append_sd_log(&state);
    }
}

static void epaper_handle_boot_long(uint32_t duration_ms)
{
    if (duration_ms >= 10000) {
        epaper_set_status("FACTORY RESET");
        epaper_refresh_dashboard(false);
        vTaskDelay(pdMS_TO_TICKS(300));
        hap_reset_to_factory();
    } else if (duration_ms >= 3000) {
        epaper_set_status("WIFI RESET");
        epaper_refresh_dashboard(false);
        vTaskDelay(pdMS_TO_TICKS(300));
        hap_reset_network();
    } else {
        epaper_set_status("BOOT HOLD");
        epaper_refresh_dashboard(false);
    }
}

static void epaper_handle_pwr_sleep(void)
{
    char status[32];

    (void) epaper_audio_stop_playback();
    snprintf(status, sizeof(status), "SLEEP %dS",
            CONFIG_HOMEKIT_EPAPER_SLEEP_WAKE_INTERVAL_SEC);
    epaper_set_status(status);
    epaper_refresh_dashboard(false);
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (epaper_board_enter_deep_sleep(CONFIG_HOMEKIT_EPAPER_SLEEP_WAKE_INTERVAL_SEC) !=
            ESP_OK) {
        epaper_set_status("SLEEP ERR");
        epaper_refresh_dashboard(false);
    }
}

static void epaper_handle_button_event(const epaper_button_event_t *event)
{
    epaper_dashboard_state_t state;
    esp_err_t err;

    if (!event) {
        return;
    }

    switch (event->type) {
    case EPAPER_BUTTON_EVENT_BOOT_SINGLE:
        epaper_set_status("REC AUDIO");
        epaper_refresh_dashboard(false);
        err = epaper_audio_record();
        epaper_set_status(err == ESP_OK ? "REC OK" : "REC ERR");
        epaper_refresh_dashboard(false);
        break;

    case EPAPER_BUTTON_EVENT_BOOT_DOUBLE:
        epaper_set_status("PLAY RECORD");
        epaper_refresh_dashboard(false);
        err = epaper_audio_play_recorded();
        epaper_set_status(err == ESP_OK ? "PLAY OK" : "PLAY ERR");
        epaper_refresh_dashboard(false);
        break;

    case EPAPER_BUTTON_EVENT_BOOT_LONG_RELEASE:
        epaper_handle_boot_long(event->press_duration_ms);
        break;

    case EPAPER_BUTTON_EVENT_PWR_SINGLE:
    {
        bool was_playing = epaper_audio_is_sd_music_playing();

        epaper_set_status(was_playing ? "MUSIC STOP" : "MUSIC PLAY");
        epaper_refresh_dashboard(false);
        err = epaper_audio_toggle_sd_music();
        if (err == ESP_OK) {
            epaper_set_status(was_playing ? "MUSIC STOP" : "MUSIC PLAY");
        } else if (err == ESP_ERR_NOT_FOUND) {
            epaper_set_status("NO MUSIC");
        } else {
            epaper_set_status("MUSIC ERR");
        }
        epaper_refresh_dashboard(false);
        break;
    }

    case EPAPER_BUTTON_EVENT_PWR_DOUBLE:
        epaper_collect_dashboard_state(&state);
        err = epaper_write_sd_snapshot(&state);
        epaper_set_status(err == ESP_OK ? "SD SNAP OK" : "SD SNAP ERR");
        epaper_refresh_dashboard(false);
        break;

    case EPAPER_BUTTON_EVENT_PWR_LONG_RELEASE:
        epaper_handle_pwr_sleep();
        break;
    }
}

static void epaper_runtime_task(void *arg)
{
    epaper_button_event_t event;
    TickType_t wait_ticks = pdMS_TO_TICKS(CONFIG_HOMEKIT_EPAPER_REFRESH_INTERVAL_SEC * 1000);

    (void) arg;

    epaper_refresh_dashboard(true);
    while (true) {
        if (xQueueReceive(s_button_queue, &event, wait_ticks) == pdTRUE) {
            epaper_handle_button_event(&event);
        } else {
            epaper_refresh_dashboard(true);
        }
    }
}

static void epaper_init_hardware(void)
{
    esp_err_t err;

    err = epaper_board_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Board init failed: %s", esp_err_to_name(err));
        return;
    }

    if (epaper_board_get_wake_reason() == EPAPER_WAKE_REASON_RTC_ALARM &&
            epaper_board_rtc_alarm_active()) {
        (void) epaper_board_rtc_alarm_reset();
        epaper_set_status("RTC WAKE");
    } else {
        epaper_set_status("BOOT READY");
    }

    err = epaper_display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed: %s", esp_err_to_name(err));
        return;
    }
    epaper_display_show_boot();

    if (!s_button_queue) {
        s_button_queue = xQueueCreate(8, sizeof(epaper_button_event_t));
    }
    if (!s_button_queue) {
        ESP_LOGE(TAG, "Failed to create button queue");
        return;
    }

    err = epaper_button_init(s_button_queue);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Button init failed: %s", esp_err_to_name(err));
        return;
    }

    err = epaper_audio_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Audio init failed: %s", esp_err_to_name(err));
        epaper_set_status("AUD INIT ERR");
    }
}

static void epaper_start_runtime_services(void)
{
    if (s_runtime_started) {
        return;
    }

    xTaskCreate(epaper_runtime_task, "epaper_runtime", 8 * 1024,
            NULL, 2, NULL);
    s_runtime_started = true;
}

static const homekit_device_t s_epaper_device = {
    .name_prefix = "Home ePaper",
    .manufacturer = "Waveshare",
    .model = "ESP32-S3-ePaper-1.54 V2",
    .fw_rev = "1.1.0",
    .hw_rev = "2.0",
    .protocol_version = "1.1.0",
    .cid = HAP_CID_SENSOR,
    .identify = epaper_identify,
    .add_services = epaper_add_services,
    .uses_custom_display = true,
    .uses_custom_buttons = true,
    .init_hardware = epaper_init_hardware,
    .start_runtime_services = epaper_start_runtime_services,
};

const homekit_device_t *epaper_device_get(void)
{
    return &s_epaper_device;
}
