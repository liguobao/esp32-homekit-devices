#include "epaper_board.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/rtc_io.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "sdmmc_cmd.h"

#include "codec_init.h"

static const char *TAG = "epaper_board";

#define EPAPER_I2C_TIMEOUT_MS 1000

#define EPAPER_RTC_REG_CTRL2       0x01
#define EPAPER_RTC_REG_SECONDS     0x04
#define EPAPER_RTC_REG_ALARM_SEC   0x0B

#define EPAPER_RTC_ALARM_ENABLE_BIT   (1U << 7)
#define EPAPER_RTC_ALARM_ACTIVE_BIT   (1U << 6)

#define EPAPER_BATTERY_PRESENT_MIN_V  2.0f
#define EPAPER_BATTERY_EMPTY_V        3.0f
#define EPAPER_BATTERY_FULL_V         4.12f

#define SHTC3_CMD_READ_ID            0xEFC8
#define SHTC3_CMD_SOFT_RESET         0x805D
#define SHTC3_CMD_SLEEP              0xB098
#define SHTC3_CMD_WAKEUP             0x3517
#define SHTC3_CMD_MEAS_T_RH_POLLING  0x7866

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_rtc_dev;
static i2c_master_dev_handle_t s_shtc3_dev;
static adc_oneshot_unit_handle_t s_adc_handle;
static adc_cali_handle_t s_adc_cali_handle;
static bool s_adc_cali_ready;
static bool s_sd_mounted;
static bool s_initialized;
static epaper_wake_reason_t s_wake_reason = EPAPER_WAKE_REASON_UNKNOWN;

static int bcd_to_int(uint8_t value)
{
    return ((value >> 4) * 10) + (value & 0x0F);
}

static uint8_t int_to_bcd(int value)
{
    return (uint8_t) (((value / 10) << 4) | (value % 10));
}

static uint8_t shtc3_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (uint8_t) ((crc << 1) ^ 0x31);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static int epaper_month_from_build_date(const char *month)
{
    static const char *const months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };

    for (int i = 0; i < 12; i++) {
        if (strncmp(month, months[i], 3) == 0) {
            return i + 1;
        }
    }
    return 1;
}

static epaper_rtc_time_t epaper_build_time_default(void)
{
    epaper_rtc_time_t rtc_time = {
        .year = 2026,
        .month = 1,
        .day = 1,
        .hour = 0,
        .minute = 0,
        .second = 0,
        .weekday = 0,
    };
    char month[4] = { 0 };
    int day;
    int year;
    int hour;
    int minute;
    int second;

    if (sscanf(__DATE__, "%3s %d %d", month, &day, &year) == 3 &&
            sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second) == 3) {
        struct tm build_tm = {
            .tm_year = year - 1900,
            .tm_mon = epaper_month_from_build_date(month) - 1,
            .tm_mday = day,
            .tm_hour = hour,
            .tm_min = minute,
            .tm_sec = second,
            .tm_isdst = -1,
        };

        if (mktime(&build_tm) != (time_t) -1) {
            rtc_time.year = build_tm.tm_year + 1900;
            rtc_time.month = build_tm.tm_mon + 1;
            rtc_time.day = build_tm.tm_mday;
            rtc_time.hour = build_tm.tm_hour;
            rtc_time.minute = build_tm.tm_min;
            rtc_time.second = build_tm.tm_sec;
            rtc_time.weekday = build_tm.tm_wday;
        }
    }
    return rtc_time;
}

static void epaper_board_detect_wake_reason(void)
{
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    s_wake_reason = EPAPER_WAKE_REASON_COLD_BOOT;
    if (cause == ESP_SLEEP_WAKEUP_EXT1) {
        uint64_t wake_pins = esp_sleep_get_ext1_wakeup_status();

        if (wake_pins & (1ULL << CONFIG_HOMEKIT_EPAPER_PIN_RTC_INT)) {
            s_wake_reason = EPAPER_WAKE_REASON_RTC_ALARM;
        } else if (wake_pins & (1ULL << CONFIG_HOMEKIT_EPAPER_PIN_PWR_KEY)) {
            s_wake_reason = EPAPER_WAKE_REASON_PWR_BUTTON;
        } else if (wake_pins & (1ULL << CONFIG_HOMEKIT_EPAPER_PIN_BOOT)) {
            s_wake_reason = EPAPER_WAKE_REASON_BOOT_BUTTON;
        } else {
            s_wake_reason = EPAPER_WAKE_REASON_OTHER;
        }
    } else if (cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
        s_wake_reason = EPAPER_WAKE_REASON_OTHER;
    }
}

static esp_err_t epaper_board_power_init(void)
{
    gpio_config_t gpio_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << CONFIG_HOMEKIT_EPAPER_PIN_PWR) |
                (1ULL << CONFIG_HOMEKIT_EPAPER_PIN_AUDIO_PWR) |
                (1ULL << CONFIG_HOMEKIT_EPAPER_PIN_VBAT_PWR),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };

    gpio_deep_sleep_hold_dis();
    (void) gpio_hold_dis(CONFIG_HOMEKIT_EPAPER_PIN_VBAT_PWR);
    (void) rtc_gpio_hold_dis(CONFIG_HOMEKIT_EPAPER_PIN_VBAT_PWR);

    ESP_RETURN_ON_ERROR(gpio_config(&gpio_conf), TAG, "power gpio config failed");

    gpio_set_level(CONFIG_HOMEKIT_EPAPER_PIN_VBAT_PWR, 1);
    gpio_set_level(CONFIG_HOMEKIT_EPAPER_PIN_PWR, 0);
    gpio_set_level(CONFIG_HOMEKIT_EPAPER_PIN_AUDIO_PWR, 0);
    return ESP_OK;
}

static esp_err_t epaper_board_gpio_inputs_init(void)
{
    gpio_config_t gpio_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << CONFIG_HOMEKIT_EPAPER_PIN_BOOT) |
                (1ULL << CONFIG_HOMEKIT_EPAPER_PIN_PWR_KEY) |
                (1ULL << CONFIG_HOMEKIT_EPAPER_PIN_RTC_INT),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };

    return gpio_config(&gpio_conf);
}

static esp_err_t epaper_board_i2c_init(void)
{
    esp_err_t err;
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = CONFIG_HOMEKIT_EPAPER_I2C_SCL,
        .sda_io_num = CONFIG_HOMEKIT_EPAPER_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CONFIG_HOMEKIT_EPAPER_I2C_RTC_ADDR,
        .scl_speed_hz = 300000,
    };

    err = i2c_new_master_bus(&bus_config, &s_i2c_bus);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_master_bus_add_device(s_i2c_bus, &device_config, &s_rtc_dev);
    if (err != ESP_OK) {
        return err;
    }

    device_config.device_address = CONFIG_HOMEKIT_EPAPER_I2C_SHTC3_ADDR;
    err = i2c_master_bus_add_device(s_i2c_bus, &device_config, &s_shtc3_dev);
    if (err != ESP_OK) {
        return err;
    }
    return ESP_OK;
}

static esp_err_t epaper_board_adc_init(void)
{
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    adc_oneshot_chan_cfg_t channel_config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };

    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&init_config, &s_adc_handle),
            TAG, "adc unit init failed");
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_3,
                &channel_config), TAG, "adc channel init failed");

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };

    if (adc_cali_create_scheme_curve_fitting(&cali_config, &s_adc_cali_handle) ==
            ESP_OK) {
        s_adc_cali_ready = true;
    }
#endif
    return ESP_OK;
}

static esp_err_t shtc3_write_command(uint16_t command)
{
    uint8_t buffer[2] = {
        (uint8_t) (command >> 8),
        (uint8_t) (command & 0xFF),
    };

    return i2c_master_transmit(s_shtc3_dev, buffer, sizeof(buffer),
            EPAPER_I2C_TIMEOUT_MS);
}

static esp_err_t epaper_board_shtc3_init(void)
{
    uint8_t command[2] = {
        (uint8_t) (SHTC3_CMD_READ_ID >> 8),
        (uint8_t) (SHTC3_CMD_READ_ID & 0xFF),
    };
    uint8_t response[3];
    esp_err_t err;
    uint16_t sensor_id;

    err = shtc3_write_command(SHTC3_CMD_WAKEUP);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    err = shtc3_write_command(SHTC3_CMD_SOFT_RESET);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    err = i2c_master_transmit_receive(s_shtc3_dev, command, sizeof(command),
            response, sizeof(response), EPAPER_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    if (shtc3_crc8(response, 2) != response[2]) {
        return ESP_ERR_INVALID_CRC;
    }

    sensor_id = (uint16_t) ((response[0] << 8) | response[1]);
    ESP_LOGI(TAG, "SHTC3 ready, sensor id=0x%04X", sensor_id);
    return shtc3_write_command(SHTC3_CMD_SLEEP);
}

static esp_err_t epaper_board_rtc_write(const epaper_rtc_time_t *rtc_time)
{
    uint8_t payload[8];

    if (!rtc_time) {
        return ESP_ERR_INVALID_ARG;
    }

    payload[0] = EPAPER_RTC_REG_SECONDS;
    payload[1] = int_to_bcd(rtc_time->second) & 0x7F;
    payload[2] = int_to_bcd(rtc_time->minute) & 0x7F;
    payload[3] = int_to_bcd(rtc_time->hour) & 0x3F;
    payload[4] = int_to_bcd(rtc_time->day) & 0x3F;
    payload[5] = int_to_bcd(rtc_time->weekday) & 0x07;
    payload[6] = int_to_bcd(rtc_time->month) & 0x1F;
    payload[7] = int_to_bcd(rtc_time->year % 100);

    return i2c_master_transmit(s_rtc_dev, payload, sizeof(payload),
            EPAPER_I2C_TIMEOUT_MS);
}

static esp_err_t epaper_board_rtc_read_internal(epaper_rtc_time_t *rtc_time)
{
    uint8_t register_address = EPAPER_RTC_REG_SECONDS;
    uint8_t buffer[7];
    esp_err_t err;

    if (!rtc_time) {
        return ESP_ERR_INVALID_ARG;
    }

    err = i2c_master_transmit_receive(s_rtc_dev,
            &register_address, sizeof(register_address),
            buffer, sizeof(buffer), EPAPER_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    if (buffer[0] & 0x80) {
        return ESP_ERR_INVALID_STATE;
    }

    rtc_time->second = bcd_to_int(buffer[0] & 0x7F);
    rtc_time->minute = bcd_to_int(buffer[1] & 0x7F);
    rtc_time->hour = bcd_to_int(buffer[2] & 0x3F);
    rtc_time->day = bcd_to_int(buffer[3] & 0x3F);
    rtc_time->weekday = bcd_to_int(buffer[4] & 0x07);
    rtc_time->month = bcd_to_int(buffer[5] & 0x1F);
    rtc_time->year = 2000 + bcd_to_int(buffer[6]);
    return ESP_OK;
}

static esp_err_t epaper_board_rtc_sync_default_if_needed(void)
{
    epaper_rtc_time_t rtc_time;
    esp_err_t err = epaper_board_rtc_read_internal(&rtc_time);

    if (err == ESP_OK && rtc_time.year >= 2024) {
        return ESP_OK;
    }

    rtc_time = epaper_build_time_default();
    ESP_LOGW(TAG, "RTC invalid, seeding compile time %04d/%02d/%02d %02d:%02d:%02d",
            rtc_time.year, rtc_time.month, rtc_time.day,
            rtc_time.hour, rtc_time.minute, rtc_time.second);
    return epaper_board_rtc_write(&rtc_time);
}

static esp_err_t epaper_board_set_alarm_target(const epaper_rtc_time_t *rtc_time)
{
    uint8_t payload[5];

    if (!rtc_time) {
        return ESP_ERR_INVALID_ARG;
    }

    payload[0] = EPAPER_RTC_REG_ALARM_SEC;
    payload[1] = int_to_bcd(rtc_time->second) & 0x7F;
    payload[2] = int_to_bcd(rtc_time->minute) & 0x7F;
    payload[3] = int_to_bcd(rtc_time->hour) & 0x3F;
    payload[4] = int_to_bcd(rtc_time->day) & 0x3F;
    return i2c_master_transmit(s_rtc_dev, payload, sizeof(payload),
            EPAPER_I2C_TIMEOUT_MS);
}

static esp_err_t epaper_board_rtc_read_ctrl2(uint8_t *value)
{
    uint8_t register_address = EPAPER_RTC_REG_CTRL2;

    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(s_rtc_dev,
            &register_address, sizeof(register_address),
            value, 1, EPAPER_I2C_TIMEOUT_MS);
}

static esp_err_t epaper_board_rtc_write_ctrl2(uint8_t value)
{
    uint8_t payload[2] = {
        EPAPER_RTC_REG_CTRL2,
        value,
    };

    return i2c_master_transmit(s_rtc_dev, payload, sizeof(payload),
            EPAPER_I2C_TIMEOUT_MS);
}

epaper_wake_reason_t epaper_board_get_wake_reason(void)
{
    return s_wake_reason;
}

const char *epaper_board_wake_reason_name(epaper_wake_reason_t reason)
{
    switch (reason) {
    case EPAPER_WAKE_REASON_COLD_BOOT:
        return "WAKE COLD";
    case EPAPER_WAKE_REASON_BOOT_BUTTON:
        return "WAKE BOOT";
    case EPAPER_WAKE_REASON_PWR_BUTTON:
        return "WAKE PWR";
    case EPAPER_WAKE_REASON_RTC_ALARM:
        return "WAKE RTC";
    default:
        return "WAKE OTHER";
    }
}

esp_err_t epaper_board_init(void)
{
    esp_err_t err;

    if (s_initialized) {
        return ESP_OK;
    }

    epaper_board_detect_wake_reason();

    err = epaper_board_power_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Power init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = epaper_board_gpio_inputs_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Input gpio init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = epaper_board_i2c_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = epaper_board_shtc3_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SHTC3 init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = epaper_board_adc_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = epaper_board_rtc_sync_default_if_needed();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RTC sync skipped: %s", esp_err_to_name(err));
    }

    err = epaper_board_sd_mount();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Micro SD not mounted: %s", esp_err_to_name(err));
    }

    s_initialized = true;
    return ESP_OK;
}

esp_err_t epaper_board_shtc3_read(epaper_sensor_reading_t *reading)
{
    uint8_t response[6];
    esp_err_t err;
    uint16_t raw_temp;
    uint16_t raw_humidity;
    float temp_offset_c = CONFIG_HOMEKIT_EPAPER_TEMP_OFFSET_DECICELSIUS / 10.0f;

    if (!reading) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        ESP_RETURN_ON_ERROR(epaper_board_init(), TAG, "board init failed");
    }

    err = shtc3_write_command(SHTC3_CMD_WAKEUP);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    err = shtc3_write_command(SHTC3_CMD_MEAS_T_RH_POLLING);
    if (err != ESP_OK) {
        (void) shtc3_write_command(SHTC3_CMD_SLEEP);
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    err = i2c_master_receive(s_shtc3_dev, response, sizeof(response),
            EPAPER_I2C_TIMEOUT_MS);
    (void) shtc3_write_command(SHTC3_CMD_SLEEP);
    if (err != ESP_OK) {
        return err;
    }
    if (shtc3_crc8(response, 2) != response[2] ||
            shtc3_crc8(&response[3], 2) != response[5]) {
        return ESP_ERR_INVALID_CRC;
    }

    raw_temp = (uint16_t) ((response[0] << 8) | response[1]);
    raw_humidity = (uint16_t) ((response[3] << 8) | response[4]);

    reading->temperature_c = (175.0f * raw_temp / 65536.0f) - 45.0f + temp_offset_c;
    reading->humidity_pct = 100.0f * raw_humidity / 65536.0f;
    return ESP_OK;
}

esp_err_t epaper_board_rtc_read(epaper_rtc_time_t *rtc_time)
{
    if (!rtc_time) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        ESP_RETURN_ON_ERROR(epaper_board_init(), TAG, "board init failed");
    }
    return epaper_board_rtc_read_internal(rtc_time);
}

bool epaper_board_rtc_alarm_active(void)
{
    uint8_t ctrl2 = 0;

    if (!s_initialized && epaper_board_init() != ESP_OK) {
        return false;
    }
    if (epaper_board_rtc_read_ctrl2(&ctrl2) != ESP_OK) {
        return false;
    }
    return (ctrl2 & EPAPER_RTC_ALARM_ACTIVE_BIT) != 0;
}

esp_err_t epaper_board_rtc_alarm_reset(void)
{
    uint8_t ctrl2 = 0;

    if (!s_initialized) {
        ESP_RETURN_ON_ERROR(epaper_board_init(), TAG, "board init failed");
    }
    ESP_RETURN_ON_ERROR(epaper_board_rtc_read_ctrl2(&ctrl2), TAG,
            "rtc ctrl2 read failed");
    ctrl2 &= (uint8_t) ~EPAPER_RTC_ALARM_ACTIVE_BIT;
    return epaper_board_rtc_write_ctrl2(ctrl2);
}

esp_err_t epaper_board_rtc_alarm_set_after_seconds(uint32_t seconds)
{
    epaper_rtc_time_t rtc_time;
    struct tm current_tm = { 0 };
    time_t current_ts;
    time_t alarm_ts;
    struct tm alarm_tm = { 0 };
    uint8_t ctrl2 = 0;

    if (seconds == 0) {
        seconds = 1;
    }
    if (!s_initialized) {
        ESP_RETURN_ON_ERROR(epaper_board_init(), TAG, "board init failed");
    }

    ESP_RETURN_ON_ERROR(epaper_board_rtc_sync_default_if_needed(), TAG,
            "rtc sync failed");
    ESP_RETURN_ON_ERROR(epaper_board_rtc_read_internal(&rtc_time), TAG,
            "rtc read failed");

    current_tm.tm_year = rtc_time.year - 1900;
    current_tm.tm_mon = rtc_time.month - 1;
    current_tm.tm_mday = rtc_time.day;
    current_tm.tm_hour = rtc_time.hour;
    current_tm.tm_min = rtc_time.minute;
    current_tm.tm_sec = rtc_time.second;
    current_tm.tm_isdst = -1;
    current_ts = mktime(&current_tm);
    if (current_ts == (time_t) -1) {
        return ESP_ERR_INVALID_STATE;
    }

    alarm_ts = current_ts + (time_t) seconds;
    if (!localtime_r(&alarm_ts, &alarm_tm)) {
        return ESP_ERR_INVALID_STATE;
    }

    rtc_time.year = alarm_tm.tm_year + 1900;
    rtc_time.month = alarm_tm.tm_mon + 1;
    rtc_time.day = alarm_tm.tm_mday;
    rtc_time.hour = alarm_tm.tm_hour;
    rtc_time.minute = alarm_tm.tm_min;
    rtc_time.second = alarm_tm.tm_sec;
    rtc_time.weekday = alarm_tm.tm_wday;

    ESP_RETURN_ON_ERROR(epaper_board_set_alarm_target(&rtc_time), TAG,
            "rtc alarm write failed");
    ESP_RETURN_ON_ERROR(epaper_board_rtc_read_ctrl2(&ctrl2), TAG,
            "rtc ctrl2 read failed");
    ctrl2 &= (uint8_t) ~EPAPER_RTC_ALARM_ACTIVE_BIT;
    ctrl2 |= EPAPER_RTC_ALARM_ENABLE_BIT;
    return epaper_board_rtc_write_ctrl2(ctrl2);
}

esp_err_t epaper_board_battery_read(epaper_battery_reading_t *battery)
{
    int raw = 0;
    int calibrated_mv = 0;
    float voltage_v;
    float level;

    if (!battery) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized) {
        ESP_RETURN_ON_ERROR(epaper_board_init(), TAG, "board init failed");
    }

    ESP_RETURN_ON_ERROR(adc_oneshot_read(s_adc_handle, ADC_CHANNEL_3, &raw),
            TAG, "adc read failed");
    if (s_adc_cali_ready) {
        ESP_RETURN_ON_ERROR(adc_cali_raw_to_voltage(s_adc_cali_handle, raw,
                    &calibrated_mv), TAG, "adc cali failed");
    } else {
        calibrated_mv = raw;
    }

    voltage_v = 0.001f * (float) calibrated_mv * 2.0f;
    memset(battery, 0, sizeof(*battery));
    battery->voltage_v = voltage_v;
    battery->present = voltage_v >= EPAPER_BATTERY_PRESENT_MIN_V;
    if (!battery->present) {
        return ESP_OK;
    }

    if (voltage_v <= EPAPER_BATTERY_EMPTY_V) {
        battery->level_pct = 0;
    } else if (voltage_v >= EPAPER_BATTERY_FULL_V) {
        battery->level_pct = 100;
    } else {
        level = ((voltage_v - EPAPER_BATTERY_EMPTY_V) /
                (EPAPER_BATTERY_FULL_V - EPAPER_BATTERY_EMPTY_V)) * 100.0f;
        if (level < 0.0f) {
            level = 0.0f;
        } else if (level > 100.0f) {
            level = 100.0f;
        }
        battery->level_pct = (uint8_t) level;
    }
    battery->is_low = battery->level_pct <= CONFIG_HOMEKIT_EPAPER_LOW_BATTERY_LEVEL;
    return ESP_OK;
}

esp_err_t epaper_board_sd_mount(void)
{
    int ret;

    if (s_sd_mounted) {
        return ESP_OK;
    }
    ret = mount_sdcard();
    if (ret == 0 && get_sdcard_handle() != NULL) {
        s_sd_mounted = true;
        ESP_LOGI(TAG, "Micro SD mounted");
        return ESP_OK;
    }
    return ESP_FAIL;
}

bool epaper_board_sd_is_mounted(void)
{
    sdmmc_card_t *card = (sdmmc_card_t *) get_sdcard_handle();

    if (!s_sd_mounted || !card) {
        return false;
    }
    return sdmmc_get_status(card) == ESP_OK;
}

float epaper_board_sd_capacity_gb(void)
{
    sdmmc_card_t *card = (sdmmc_card_t *) get_sdcard_handle();

    if (!epaper_board_sd_is_mounted() || !card) {
        return 0.0f;
    }
    return (float) card->csd.capacity / 2048.0f / 1024.0f;
}

static esp_err_t epaper_board_sd_write_internal(const char *path, const void *data,
        size_t len, const char *mode)
{
    sdmmc_card_t *card = (sdmmc_card_t *) get_sdcard_handle();
    FILE *file;
    size_t written;

    if (!path || (!data && len > 0) || !mode) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!epaper_board_sd_is_mounted() || !card) {
        return ESP_ERR_NOT_FOUND;
    }

    file = fopen(path, mode);
    if (!file) {
        return ESP_FAIL;
    }
    written = fwrite(data, 1, len, file);
    fclose(file);
    return written == len ? ESP_OK : ESP_FAIL;
}

esp_err_t epaper_board_sd_write_file(const char *path, const void *data, size_t len)
{
    return epaper_board_sd_write_internal(path, data, len, "wb");
}

esp_err_t epaper_board_sd_append_file(const char *path, const void *data, size_t len)
{
    return epaper_board_sd_write_internal(path, data, len, "ab");
}

esp_err_t epaper_board_sd_read_file(const char *path, void *buffer, size_t buffer_len,
        size_t *out_len)
{
    sdmmc_card_t *card = (sdmmc_card_t *) get_sdcard_handle();
    FILE *file;
    size_t read_len;

    if (!path || !buffer || buffer_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (out_len) {
        *out_len = 0;
    }
    if (!epaper_board_sd_is_mounted() || !card) {
        return ESP_ERR_NOT_FOUND;
    }

    file = fopen(path, "rb");
    if (!file) {
        return ESP_ERR_NOT_FOUND;
    }
    read_len = fread(buffer, 1, buffer_len, file);
    fclose(file);
    if (out_len) {
        *out_len = read_len;
    }
    return ESP_OK;
}

esp_err_t epaper_board_enter_deep_sleep(uint32_t wake_after_seconds)
{
    uint64_t wake_mask;

    if (!s_initialized) {
        ESP_RETURN_ON_ERROR(epaper_board_init(), TAG, "board init failed");
    }
    if (wake_after_seconds > 0) {
        ESP_RETURN_ON_ERROR(epaper_board_rtc_alarm_set_after_seconds(wake_after_seconds),
                TAG, "rtc alarm set failed");
    }

    gpio_set_level(CONFIG_HOMEKIT_EPAPER_PIN_AUDIO_PWR, 1);
    gpio_set_level(CONFIG_HOMEKIT_EPAPER_PIN_PWR, 1);
    gpio_set_level(CONFIG_HOMEKIT_EPAPER_PIN_VBAT_PWR, 1);

    wake_mask = (1ULL << CONFIG_HOMEKIT_EPAPER_PIN_BOOT) |
            (1ULL << CONFIG_HOMEKIT_EPAPER_PIN_PWR_KEY) |
            (1ULL << CONFIG_HOMEKIT_EPAPER_PIN_RTC_INT);

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    ESP_RETURN_ON_ERROR(esp_sleep_enable_ext1_wakeup_io(wake_mask,
                ESP_EXT1_WAKEUP_ANY_LOW), TAG, "ext1 wake config failed");

    rtc_gpio_pulldown_dis(CONFIG_HOMEKIT_EPAPER_PIN_RTC_INT);
    rtc_gpio_pullup_en(CONFIG_HOMEKIT_EPAPER_PIN_RTC_INT);
    gpio_deep_sleep_hold_en();
    rtc_gpio_hold_en(CONFIG_HOMEKIT_EPAPER_PIN_VBAT_PWR);

    ESP_LOGI(TAG, "Entering deep sleep, rtc wake in %lu sec",
            (unsigned long) wake_after_seconds);
    esp_deep_sleep_start();
    return ESP_OK;
}
