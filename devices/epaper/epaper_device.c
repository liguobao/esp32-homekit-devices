#include "epaper_device.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sntp.h"
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

#define EPAPER_HOMEKIT_HUMIDITY_MIN_UPDATE_SEC 300
#define EPAPER_HOMEKIT_HUMIDITY_FORCE_DELTA_PCT 3.0f

static hap_char_t *s_temperature_char;
static hap_char_t *s_humidity_char;
static hap_char_t *s_battery_level_char;
static hap_char_t *s_low_battery_char;
static hap_char_t *s_music_char;
static QueueHandle_t s_button_queue;
static bool s_runtime_started;
static char s_status_text[32] = "BOOT READY";
static bool s_sntp_started;
static bool s_rtc_synced_from_network;
static bool s_timezone_configured;
static bool s_humidity_published;
static TickType_t s_last_humidity_publish_tick;
static float s_last_humidity_pct = -1.0f;
static epaper_dashboard_state_t s_last_display_state;
static bool s_last_display_state_valid;

typedef enum {
    EPAPER_SWITCH_SERVICE_GPIO = 0,
    EPAPER_SWITCH_SERVICE_MUSIC,
} epaper_switch_service_kind_t;

typedef struct {
    epaper_switch_service_kind_t kind;
    size_t index;
} epaper_switch_service_priv_t;

typedef struct {
    gpio_num_t gpio;
    const char *name;
    bool state;
} epaper_gpio_switch_t;

static epaper_gpio_switch_t s_gpio_switches[] = {
    { GPIO_NUM_1, "GPIO1", false },
    { GPIO_NUM_2, "GPIO2", false },
    { GPIO_NUM_3, "GPIO3", false },
};

static epaper_switch_service_priv_t s_music_switch = {
    .kind = EPAPER_SWITCH_SERVICE_MUSIC,
};
static epaper_switch_service_priv_t s_gpio_switch_service_privs[
    sizeof(s_gpio_switches) / sizeof(s_gpio_switches[0])];

static void epaper_set_status(const char *status);

static int epaper_identify(hap_acc_t *ha)
{
    (void) ha;
    ESP_LOGI(TAG, "Accessory identified");
    return HAP_SUCCESS;
}

static void epaper_update_switch_char(hap_char_t *characteristic, bool enabled)
{
    hap_val_t value = {
        .b = enabled,
    };

    if (characteristic) {
        hap_char_update_val(characteristic, &value);
    }
}

static void epaper_update_switch_char_if_needed(hap_char_t *characteristic, bool enabled)
{
    const hap_val_t *current_value;

    if (!characteristic) {
        return;
    }
    current_value = hap_char_get_val(characteristic);
    if (current_value && current_value->b == enabled) {
        return;
    }
    epaper_update_switch_char(characteristic, enabled);
}

static void epaper_sync_music_switch_char(void)
{
    epaper_update_switch_char_if_needed(s_music_char, epaper_audio_is_music_playing());
}

static float epaper_quantize_humidity_pct(float humidity_pct)
{
    if (humidity_pct < 0.0f) {
        return 0.0f;
    }
    if (humidity_pct > 100.0f) {
        return 100.0f;
    }
    return (float) ((int) (humidity_pct + 0.5f));
}

static bool epaper_should_publish_humidity(float humidity_pct)
{
    float quantized = epaper_quantize_humidity_pct(humidity_pct);
    float delta = quantized - s_last_humidity_pct;
    TickType_t now = xTaskGetTickCount();

    if (delta < 0.0f) {
        delta = -delta;
    }
    if (!s_humidity_published) {
        return true;
    }
    if (delta >= EPAPER_HOMEKIT_HUMIDITY_FORCE_DELTA_PCT) {
        return true;
    }
    if (quantized == s_last_humidity_pct) {
        return false;
    }
    return (now - s_last_humidity_publish_tick) >=
            pdMS_TO_TICKS(EPAPER_HOMEKIT_HUMIDITY_MIN_UPDATE_SEC * 1000);
}

static void epaper_mark_humidity_published(float humidity_pct)
{
    s_last_humidity_pct = epaper_quantize_humidity_pct(humidity_pct);
    s_last_humidity_publish_tick = xTaskGetTickCount();
    s_humidity_published = true;
}

static int epaper_round_int(float value)
{
    return value >= 0.0f ? (int) (value + 0.5f) : (int) (value - 0.5f);
}

static bool epaper_rtc_minute_equal(const epaper_rtc_time_t *lhs,
        const epaper_rtc_time_t *rhs)
{
    if (!lhs || !rhs) {
        return false;
    }
    return lhs->year == rhs->year &&
            lhs->month == rhs->month &&
            lhs->day == rhs->day &&
            lhs->hour == rhs->hour &&
            lhs->minute == rhs->minute;
}

static bool epaper_sensor_block_changed(const epaper_dashboard_state_t *previous,
        const epaper_dashboard_state_t *current)
{
    if (!previous || !current) {
        return true;
    }
    if (previous->reading_valid != current->reading_valid) {
        return true;
    }
    if (!current->reading_valid) {
        return false;
    }
    return epaper_round_int(previous->reading.temperature_c) !=
            epaper_round_int(current->reading.temperature_c) ||
            epaper_round_int(previous->reading.humidity_pct) !=
            epaper_round_int(current->reading.humidity_pct);
}

static bool epaper_music_block_changed(const epaper_dashboard_state_t *previous,
        const epaper_dashboard_state_t *current)
{
    if (!previous || !current) {
        return true;
    }
    return strcmp(previous->music_file, current->music_file) != 0 ||
            strcmp(previous->music_status, current->music_status) != 0;
}

static bool epaper_power_block_changed(const epaper_dashboard_state_t *previous,
        const epaper_dashboard_state_t *current)
{
    if (!previous || !current) {
        return true;
    }
    if (previous->battery_valid != current->battery_valid) {
        return true;
    }
    if (current->battery_valid &&
            (previous->battery.present != current->battery.present ||
            previous->battery.level_pct != current->battery.level_pct)) {
        return true;
    }
    if (previous->wifi_score_valid != current->wifi_score_valid ||
            previous->wifi_score != current->wifi_score) {
        return true;
    }
    if (previous->ble_count_valid != current->ble_count_valid ||
            previous->ble_count != current->ble_count) {
        return true;
    }
    return false;
}

static bool epaper_clock_block_changed(const epaper_dashboard_state_t *previous,
        const epaper_dashboard_state_t *current)
{
    if (!previous || !current) {
        return true;
    }
    if (previous->rtc_valid != current->rtc_valid) {
        return true;
    }
    if (!current->rtc_valid) {
        return false;
    }
    return !epaper_rtc_minute_equal(&previous->rtc_time, &current->rtc_time);
}

static void epaper_apply_display_state(const epaper_dashboard_state_t *state)
{
    if (!state) {
        return;
    }
    if (!s_last_display_state_valid) {
        epaper_display_show_dashboard(state);
        s_last_display_state = *state;
        s_last_display_state_valid = true;
        return;
    }

    if (epaper_clock_block_changed(&s_last_display_state, state)) {
        epaper_display_refresh_clock(state->rtc_valid ? &state->rtc_time : NULL,
                state->rtc_valid);
    }
    if (epaper_sensor_block_changed(&s_last_display_state, state)) {
        epaper_display_refresh_sensor_block(state);
    }
    if (epaper_music_block_changed(&s_last_display_state, state)) {
        epaper_display_refresh_music_block(state);
    }
    if (epaper_power_block_changed(&s_last_display_state, state)) {
        epaper_display_refresh_power_block(state);
    }

    s_last_display_state = *state;
}

static esp_err_t epaper_gpio_switch_set(size_t index, bool enabled)
{
    esp_err_t err;

    if (index >= (sizeof(s_gpio_switches) / sizeof(s_gpio_switches[0]))) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gpio_set_level(s_gpio_switches[index].gpio, enabled ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to set gpio %d: %s",
                s_gpio_switches[index].gpio, esp_err_to_name(err));
        return err;
    }
    s_gpio_switches[index].state = enabled;
    return ESP_OK;
}

static int epaper_switch_write(hap_write_data_t write_data[], int count,
        void *serv_priv, void *write_priv)
{
    const epaper_switch_service_priv_t *service =
        (const epaper_switch_service_priv_t *) serv_priv;
    int ret = HAP_SUCCESS;

    (void) write_priv;

    for (int i = 0; i < count; i++) {
        hap_write_data_t *write = &write_data[i];
        esp_err_t err = ESP_OK;

        *(write->status) = HAP_STATUS_VAL_INVALID;
        if (!service ||
                strcmp(hap_char_get_type_uuid(write->hc), HAP_CHAR_UUID_ON) != 0) {
            *(write->status) = HAP_STATUS_RES_ABSENT;
            ret = HAP_FAIL;
            continue;
        }

        switch (service->kind) {
        case EPAPER_SWITCH_SERVICE_GPIO:
            err = epaper_gpio_switch_set(service->index, write->val.b);
            if (err == ESP_OK) {
                epaper_update_switch_char(write->hc, write->val.b);
                *(write->status) = HAP_STATUS_SUCCESS;
                ESP_LOGI(TAG, "%s -> %s", s_gpio_switches[service->index].name,
                        write->val.b ? "on" : "off");
            }
            break;

        case EPAPER_SWITCH_SERVICE_MUSIC:
            if (write->val.b) {
                epaper_set_status("MUSIC PLAY");
                err = epaper_audio_start_music();
                if (err == ESP_ERR_NOT_FOUND) {
                    epaper_set_status("NO MUSIC");
                } else if (err != ESP_OK) {
                    epaper_set_status("MUSIC ERR");
                }
            } else {
                epaper_set_status("MUSIC STOP");
                err = epaper_audio_stop_playback();
                if (err == ESP_ERR_INVALID_STATE) {
                    err = ESP_OK;
                } else if (err != ESP_OK) {
                    epaper_set_status("MUSIC ERR");
                }
            }
            epaper_sync_music_switch_char();
            if (err == ESP_OK) {
                *(write->status) = HAP_STATUS_SUCCESS;
            }
            break;
        }

        if (*(write->status) != HAP_STATUS_SUCCESS) {
            ret = HAP_FAIL;
        }
    }
    return ret;
}

static int epaper_add_named_switch_service(hap_acc_t *accessory,
        const char *service_name, epaper_switch_service_priv_t *service_priv)
{
    hap_serv_t *service = hap_serv_switch_create(false);
    hap_char_t *on_char;

    if (!service) {
        ESP_LOGE(TAG, "Failed to create switch service %s", service_name);
        return HAP_FAIL;
    }
    if (hap_serv_add_char(service, hap_char_name_create((char *) service_name)) !=
            HAP_SUCCESS) {
        ESP_LOGE(TAG, "Failed to add switch name %s", service_name);
        return HAP_FAIL;
    }
    hap_serv_set_priv(service, service_priv);
    hap_serv_set_write_cb(service, epaper_switch_write);
    on_char = hap_serv_get_char_by_uuid(service, HAP_CHAR_UUID_ON);
    if (!on_char) {
        ESP_LOGE(TAG, "Failed to resolve switch char %s", service_name);
        return HAP_FAIL;
    }
    if (service_priv) {
        switch (service_priv->kind) {
        case EPAPER_SWITCH_SERVICE_MUSIC:
            s_music_char = on_char;
            break;
        case EPAPER_SWITCH_SERVICE_GPIO:
            break;
        }
    }
    hap_acc_add_serv(accessory, service);
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

static bool epaper_system_time_valid(void)
{
    time_t now;
    struct tm time_info = { 0 };

    time(&now);
    if (now <= 0) {
        return false;
    }
    if (!localtime_r(&now, &time_info)) {
        return false;
    }
    return time_info.tm_year >= (2024 - 1900);
}

static void epaper_configure_timezone(void)
{
    if (s_timezone_configured) {
        return;
    }
    setenv("TZ", CONFIG_HOMEKIT_EPAPER_TIMEZONE, 1);
    tzset();
    s_timezone_configured = true;
}

static bool epaper_rtc_time_from_tm(const struct tm *time_info,
        epaper_rtc_time_t *rtc_time)
{
    if (!time_info || !rtc_time) {
        return false;
    }

    rtc_time->year = time_info->tm_year + 1900;
    rtc_time->month = time_info->tm_mon + 1;
    rtc_time->day = time_info->tm_mday;
    rtc_time->hour = time_info->tm_hour;
    rtc_time->minute = time_info->tm_min;
    rtc_time->second = time_info->tm_sec;
    rtc_time->weekday = time_info->tm_wday;
    return true;
}

static bool epaper_sync_system_time_from_rtc_if_needed(void)
{
    epaper_rtc_time_t rtc_time;
    struct tm time_info = { 0 };
    struct timeval tv = { 0 };
    time_t rtc_ts;

    epaper_configure_timezone();
    if (epaper_system_time_valid()) {
        return true;
    }
    if (epaper_board_rtc_read(&rtc_time) != ESP_OK || rtc_time.year < 2024) {
        return false;
    }

    time_info.tm_year = rtc_time.year - 1900;
    time_info.tm_mon = rtc_time.month - 1;
    time_info.tm_mday = rtc_time.day;
    time_info.tm_hour = rtc_time.hour;
    time_info.tm_min = rtc_time.minute;
    time_info.tm_sec = rtc_time.second;
    time_info.tm_isdst = -1;
    rtc_ts = mktime(&time_info);
    if (rtc_ts <= 0) {
        return false;
    }

    tv.tv_sec = rtc_ts;
    if (settimeofday(&tv, NULL) != 0) {
        ESP_LOGW(TAG, "Failed to seed system time from RTC");
        return false;
    }

    ESP_LOGI(TAG, "System time seeded from RTC: %04d/%02d/%02d %02d:%02d:%02d",
            rtc_time.year, rtc_time.month, rtc_time.day,
            rtc_time.hour, rtc_time.minute, rtc_time.second);
    return true;
}

static bool epaper_get_current_time(epaper_rtc_time_t *rtc_time)
{
    time_t now;
    struct tm time_info = { 0 };

    if (!rtc_time) {
        return false;
    }

    epaper_configure_timezone();
    if (!epaper_system_time_valid()) {
        (void) epaper_sync_system_time_from_rtc_if_needed();
    }

    time(&now);
    if (now > 0 && localtime_r(&now, &time_info) &&
            time_info.tm_year >= (2024 - 1900)) {
        return epaper_rtc_time_from_tm(&time_info, rtc_time);
    }

    return epaper_board_rtc_read(rtc_time) == ESP_OK;
}

static void epaper_start_sntp_if_needed(void)
{
    if (s_sntp_started) {
        return;
    }

    epaper_configure_timezone();
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "cn.pool.ntp.org");
    esp_sntp_setservername(2, "pool.ntp.org");
    esp_sntp_init();
    s_sntp_started = true;
    ESP_LOGI(TAG, "SNTP started for RTC sync");
}

static void epaper_try_sync_rtc_from_network(void)
{
    wifi_ap_record_t ap_info = { 0 };
    epaper_rtc_time_t rtc_time;
    time_t now;
    struct tm time_info = { 0 };

    if (s_rtc_synced_from_network) {
        return;
    }
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        return;
    }

    epaper_start_sntp_if_needed();
    if (!epaper_system_time_valid()) {
        return;
    }

    time(&now);
    if (now <= 0 || !localtime_r(&now, &time_info)) {
        return;
    }

    if (!epaper_rtc_time_from_tm(&time_info, &rtc_time)) {
        return;
    }

    if (epaper_board_rtc_write_time(&rtc_time) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to write SNTP time back to RTC");
        return;
    }

    if (s_sntp_started) {
        esp_sntp_stop();
    }
    s_rtc_synced_from_network = true;
    ESP_LOGI(TAG, "RTC synced from network: %04d/%02d/%02d %02d:%02d:%02d",
            rtc_time.year, rtc_time.month, rtc_time.day,
            rtc_time.hour, rtc_time.minute, rtc_time.second);
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

    if (epaper_add_named_switch_service(accessory, "Music",
                &s_music_switch) != HAP_SUCCESS) {
        return HAP_FAIL;
    }

    for (size_t i = 0; i < (sizeof(s_gpio_switches) / sizeof(s_gpio_switches[0])); i++) {
        s_gpio_switch_service_privs[i].kind = EPAPER_SWITCH_SERVICE_GPIO;
        s_gpio_switch_service_privs[i].index = i;
        if (epaper_add_named_switch_service(accessory, s_gpio_switches[i].name,
                    &s_gpio_switch_service_privs[i]) != HAP_SUCCESS) {
            return HAP_FAIL;
        }
    }
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

    if (state->reading_valid && s_temperature_char) {
        temperature_value.f = state->reading.temperature_c;
        hap_char_update_val(s_temperature_char, &temperature_value);
    }
    if (state->reading_valid && s_humidity_char &&
            epaper_should_publish_humidity(state->reading.humidity_pct)) {
        humidity_value.f = epaper_quantize_humidity_pct(state->reading.humidity_pct);
        if (hap_char_update_val(s_humidity_char, &humidity_value) == HAP_SUCCESS) {
            epaper_mark_humidity_published(state->reading.humidity_pct);
        }
    }
    if (state->battery_valid && state->battery.present &&
            s_battery_level_char && s_low_battery_char) {
        battery_level.u = state->battery.level_pct;
        low_battery.u = state->battery.is_low ? 1 : 0;
        hap_char_update_val(s_battery_level_char, &battery_level);
        hap_char_update_val(s_low_battery_char, &low_battery);
    }
    epaper_sync_music_switch_char();
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
    if (epaper_get_current_time(&state->rtc_time)) {
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

    epaper_try_sync_rtc_from_network();
    epaper_collect_dashboard_state(&state);
    epaper_update_homekit(&state);
    epaper_apply_display_state(&state);
    if (append_sd_log) {
        epaper_append_sd_log(&state);
    }
}

static void epaper_refresh_clock_display(void)
{
    epaper_rtc_time_t rtc_time;
    bool rtc_valid = epaper_get_current_time(&rtc_time);

    epaper_display_refresh_clock(rtc_valid ? &rtc_time : NULL, rtc_valid);
    if (s_last_display_state_valid) {
        s_last_display_state.rtc_valid = rtc_valid;
        if (rtc_valid) {
            s_last_display_state.rtc_time = rtc_time;
        } else {
            memset(&s_last_display_state.rtc_time, 0,
                    sizeof(s_last_display_state.rtc_time));
        }
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
        bool was_playing = epaper_audio_is_music_playing();

        err = epaper_audio_toggle_music();
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
    TickType_t full_refresh_ticks =
            pdMS_TO_TICKS(CONFIG_HOMEKIT_EPAPER_REFRESH_INTERVAL_SEC * 1000);
    TickType_t poll_ticks = pdMS_TO_TICKS(200);
    TickType_t last_full_refresh_tick;
    epaper_rtc_time_t rtc_time;

    (void) arg;

    epaper_refresh_dashboard(true);
    last_full_refresh_tick = xTaskGetTickCount();
    while (true) {
        if (xQueueReceive(s_button_queue, &event, poll_ticks) == pdTRUE) {
            epaper_handle_button_event(&event);
            last_full_refresh_tick = xTaskGetTickCount();
        } else {
            TickType_t now = xTaskGetTickCount();

            if ((now - last_full_refresh_tick) >= full_refresh_ticks) {
                epaper_refresh_dashboard(true);
                last_full_refresh_tick = xTaskGetTickCount();
            } else {
                if (epaper_get_current_time(&rtc_time)) {
                    if (!s_last_display_state_valid ||
                            !s_last_display_state.rtc_valid ||
                            !epaper_rtc_minute_equal(&s_last_display_state.rtc_time,
                                &rtc_time)) {
                        epaper_display_refresh_clock(&rtc_time, true);
                        if (s_last_display_state_valid) {
                            s_last_display_state.rtc_valid = true;
                            s_last_display_state.rtc_time = rtc_time;
                        }
                    }
                } else if (s_last_display_state_valid && s_last_display_state.rtc_valid) {
                    epaper_refresh_clock_display();
                }
            }
        }
    }
}

static void epaper_init_hardware(void)
{
    esp_err_t err;
    gpio_config_t gpio_output_config = {
        .pin_bit_mask = 0,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

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
    s_last_display_state_valid = false;
    (void) epaper_sync_system_time_from_rtc_if_needed();

    for (size_t i = 0; i < (sizeof(s_gpio_switches) / sizeof(s_gpio_switches[0])); i++) {
        gpio_output_config.pin_bit_mask |= 1ULL << s_gpio_switches[i].gpio;
    }
    err = gpio_config(&gpio_output_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO switch init failed: %s", esp_err_to_name(err));
        return;
    }
    for (size_t i = 0; i < (sizeof(s_gpio_switches) / sizeof(s_gpio_switches[0])); i++) {
        s_gpio_switches[i].state = false;
        (void) gpio_set_level(s_gpio_switches[i].gpio, 0);
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
    .fixed_name = "epaper",
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
