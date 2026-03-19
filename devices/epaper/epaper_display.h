#ifndef EPAPER_DISPLAY_H_
#define EPAPER_DISPLAY_H_

#include <stdbool.h>

#include "esp_err.h"

#include "epaper_board.h"

typedef struct {
    bool reading_valid;
    epaper_sensor_reading_t reading;
    bool rtc_valid;
    epaper_rtc_time_t rtc_time;
    bool ble_count_valid;
    uint8_t ble_count;
    bool wifi_score_valid;
    uint8_t wifi_score;
    bool battery_valid;
    epaper_battery_reading_t battery;
    bool sd_mounted;
    float sd_capacity_gb;
    bool audio_ready;
    char music_file[32];
    char music_status[16];
    char wake_text[20];
    char audio_text[20];
    char status_text[32];
} epaper_dashboard_state_t;

esp_err_t epaper_display_init(void);
void epaper_display_show_boot(void);
void epaper_display_show_dashboard(const epaper_dashboard_state_t *state);
void epaper_display_refresh_clock(const epaper_rtc_time_t *rtc_time, bool rtc_valid);
void epaper_display_refresh_sensor_block(const epaper_dashboard_state_t *state);
void epaper_display_refresh_music_block(const epaper_dashboard_state_t *state);
void epaper_display_refresh_power_block(const epaper_dashboard_state_t *state);

#endif /* EPAPER_DISPLAY_H_ */
