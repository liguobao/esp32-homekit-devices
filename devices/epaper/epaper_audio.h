#ifndef EPAPER_AUDIO_H_
#define EPAPER_AUDIO_H_

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

typedef enum {
    EPAPER_AUDIO_STATE_UNINITIALIZED = 0,
    EPAPER_AUDIO_STATE_READY,
    EPAPER_AUDIO_STATE_RECORDING,
    EPAPER_AUDIO_STATE_PLAYING_RECORDING,
    EPAPER_AUDIO_STATE_PLAYING_DEMO,
    EPAPER_AUDIO_STATE_PLAYING_SD,
    EPAPER_AUDIO_STATE_ERROR,
} epaper_audio_state_t;

esp_err_t epaper_audio_init(void);
bool epaper_audio_is_ready(void);
epaper_audio_state_t epaper_audio_get_state(void);
const char *epaper_audio_state_name(epaper_audio_state_t state);
size_t epaper_audio_get_recorded_bytes(void);
esp_err_t epaper_audio_record(void);
esp_err_t epaper_audio_play_recorded(void);
esp_err_t epaper_audio_play_demo(void);
esp_err_t epaper_audio_start_music(void);
esp_err_t epaper_audio_toggle_music(void);
esp_err_t epaper_audio_stop_playback(void);
bool epaper_audio_is_music_playing(void);
void epaper_audio_get_music_info(char *file_name, size_t file_name_len,
        char *status_text, size_t status_text_len);

#endif /* EPAPER_AUDIO_H_ */
