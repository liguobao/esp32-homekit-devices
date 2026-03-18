#ifndef EPAPER_BOARD_H_
#define EPAPER_BOARD_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    float temperature_c;
    float humidity_pct;
} epaper_sensor_reading_t;

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int weekday;
} epaper_rtc_time_t;

typedef struct {
    float voltage_v;
    uint8_t level_pct;
    bool present;
    bool is_low;
} epaper_battery_reading_t;

typedef enum {
    EPAPER_WAKE_REASON_UNKNOWN = 0,
    EPAPER_WAKE_REASON_COLD_BOOT,
    EPAPER_WAKE_REASON_BOOT_BUTTON,
    EPAPER_WAKE_REASON_PWR_BUTTON,
    EPAPER_WAKE_REASON_RTC_ALARM,
    EPAPER_WAKE_REASON_OTHER,
} epaper_wake_reason_t;

esp_err_t epaper_board_init(void);
esp_err_t epaper_board_shtc3_read(epaper_sensor_reading_t *reading);
esp_err_t epaper_board_rtc_read(epaper_rtc_time_t *rtc_time);
esp_err_t epaper_board_rtc_alarm_set_after_seconds(uint32_t seconds);
esp_err_t epaper_board_rtc_alarm_reset(void);
bool epaper_board_rtc_alarm_active(void);
epaper_wake_reason_t epaper_board_get_wake_reason(void);
const char *epaper_board_wake_reason_name(epaper_wake_reason_t reason);
esp_err_t epaper_board_battery_read(epaper_battery_reading_t *battery);
esp_err_t epaper_board_sd_mount(void);
bool epaper_board_sd_is_mounted(void);
float epaper_board_sd_capacity_gb(void);
esp_err_t epaper_board_sd_write_file(const char *path, const void *data, size_t len);
esp_err_t epaper_board_sd_append_file(const char *path, const void *data, size_t len);
esp_err_t epaper_board_sd_read_file(const char *path, void *buffer, size_t buffer_len,
        size_t *out_len);
esp_err_t epaper_board_enter_deep_sleep(uint32_t wake_after_seconds);

#endif /* EPAPER_BOARD_H_ */
