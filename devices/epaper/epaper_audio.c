#include "epaper_audio.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "codec_board.h"
#include "codec_init.h"
#include "epaper_board.h"

static const char *TAG = "epaper_audio";

#define EPAPER_AUDIO_MUSIC_DIR "/sdcard/music"
#define EPAPER_AUDIO_FILE_PATH_LEN 160
#define EPAPER_AUDIO_FILE_LABEL_LEN 32
#define EPAPER_AUDIO_STATUS_LEN 16
#define EPAPER_AUDIO_READ_SIZE 1024
#define EPAPER_AUDIO_OUT_SIZE 4096

static esp_codec_dev_handle_t s_playback;
static esp_codec_dev_handle_t s_record;
static bool s_playback_open;
static bool s_record_open;
static esp_codec_dev_sample_info_t s_playback_sample_info;
static uint8_t *s_record_buffer;
static size_t s_record_buffer_len;
static size_t s_recorded_bytes;
static epaper_audio_state_t s_audio_state = EPAPER_AUDIO_STATE_UNINITIALIZED;
static SemaphoreHandle_t s_audio_lock;
static TaskHandle_t s_music_task;
static volatile bool s_music_stop_requested;
static bool s_decoder_registered;
static char s_music_file[EPAPER_AUDIO_FILE_LABEL_LEN] = "NO MUSIC";
static char s_music_status[EPAPER_AUDIO_STATUS_LEN] = "STOP";
static char s_selected_music_path[EPAPER_AUDIO_FILE_PATH_LEN];
static char s_selected_music_file[EPAPER_AUDIO_FILE_LABEL_LEN];

extern const uint8_t epaper_canon_pcm_start[] asm("_binary_canon_pcm_start");
extern const uint8_t epaper_canon_pcm_end[] asm("_binary_canon_pcm_end");

static SemaphoreHandle_t epaper_audio_get_lock(void)
{
    if (!s_audio_lock) {
        s_audio_lock = xSemaphoreCreateMutex();
    }
    return s_audio_lock;
}

static void epaper_audio_lock(void)
{
    SemaphoreHandle_t lock = epaper_audio_get_lock();

    if (lock) {
        xSemaphoreTake(lock, portMAX_DELAY);
    }
}

static void epaper_audio_unlock(void)
{
    if (s_audio_lock) {
        xSemaphoreGive(s_audio_lock);
    }
}

static esp_codec_dev_sample_info_t epaper_audio_default_sample_info(void)
{
    esp_codec_dev_sample_info_t sample_info = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
        .channel_mask = 0,
        .mclk_multiple = 0,
    };

    return sample_info;
}

static void epaper_audio_set_music_info_locked(const char *file_name,
        const char *status_text)
{
    if (file_name && file_name[0] != '\0') {
        snprintf(s_music_file, sizeof(s_music_file), "%s", file_name);
    }
    if (status_text && status_text[0] != '\0') {
        snprintf(s_music_status, sizeof(s_music_status), "%s", status_text);
    }
}

static void epaper_audio_set_music_info(const char *file_name,
        const char *status_text)
{
    epaper_audio_lock();
    epaper_audio_set_music_info_locked(file_name, status_text);
    epaper_audio_unlock();
}

static esp_err_t epaper_audio_alloc_buffer(void)
{
    if (s_record_buffer) {
        return ESP_OK;
    }

    s_record_buffer_len = CONFIG_HOMEKIT_EPAPER_AUDIO_BUFFER_KB * 1024U;
    s_record_buffer = heap_caps_malloc(s_record_buffer_len,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_record_buffer) {
        s_record_buffer = malloc(s_record_buffer_len);
    }
    ESP_RETURN_ON_FALSE(s_record_buffer != NULL, ESP_ERR_NO_MEM, TAG,
            "audio buffer alloc failed");
    return ESP_OK;
}

static esp_err_t epaper_audio_register_decoders(void)
{
    esp_audio_err_t ret;

    if (s_decoder_registered) {
        return ESP_OK;
    }

    ret = esp_audio_dec_register_default();
    ESP_RETURN_ON_FALSE(ret == ESP_AUDIO_ERR_OK, ESP_FAIL, TAG,
            "register default decoders failed: %d", ret);

    ret = esp_audio_simple_dec_register_default();
    ESP_RETURN_ON_FALSE(ret == ESP_AUDIO_ERR_OK, ESP_FAIL, TAG,
            "register default simple decoders failed: %d", ret);

    s_decoder_registered = true;
    return ESP_OK;
}

static esp_err_t epaper_audio_open_record_handle(void)
{
    esp_codec_dev_sample_info_t sample_info;

    if (s_record_open) {
        return ESP_OK;
    }

    sample_info = epaper_audio_default_sample_info();
    if (esp_codec_dev_open(s_record, &sample_info) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    s_record_open = true;
    esp_codec_dev_set_in_gain(s_record, 45.0f);
    return ESP_OK;
}

static esp_err_t epaper_audio_prepare_playback(
        const esp_codec_dev_sample_info_t *sample_info)
{
    esp_codec_dev_sample_info_t target_info;

    if (!sample_info) {
        return ESP_ERR_INVALID_ARG;
    }

    target_info = *sample_info;
    if (target_info.sample_rate == 0) {
        target_info.sample_rate = 16000;
    }
    if (target_info.channel == 0 || target_info.channel > 2) {
        target_info.channel = 2;
    }
    if (target_info.bits_per_sample == 0) {
        target_info.bits_per_sample = 16;
    }
    target_info.channel_mask = 0;
    target_info.mclk_multiple = 0;

    if (s_playback_open &&
            memcmp(&s_playback_sample_info, &target_info,
                sizeof(target_info)) == 0) {
        return ESP_OK;
    }

    if (s_playback_open) {
        if (esp_codec_dev_close(s_playback) != ESP_CODEC_DEV_OK) {
            return ESP_FAIL;
        }
        s_playback_open = false;
    }

    if (esp_codec_dev_open(s_playback, &target_info) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }

    s_playback_open = true;
    s_playback_sample_info = target_info;
    esp_codec_dev_set_out_vol(s_playback, 80.0f);
    return ESP_OK;
}

static char epaper_audio_sanitize_char(char c)
{
    if ((c >= '0' && c <= '9') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z')) {
        return c;
    }

    switch (c) {
    case ' ':
    case '.':
    case '-':
    case '/':
    case ':':
        return c;
    case '_':
        return '-';
    default:
        return '-';
    }
}

static void epaper_audio_sanitize_label(const char *source, char *dest,
        size_t dest_len)
{
    size_t write_index = 0;

    if (!dest || dest_len == 0) {
        return;
    }
    dest[0] = '\0';
    if (!source || source[0] == '\0') {
        snprintf(dest, dest_len, "MUSIC");
        return;
    }

    while (*source && write_index + 1 < dest_len) {
        dest[write_index++] = epaper_audio_sanitize_char(*source++);
    }
    dest[write_index] = '\0';

    if (write_index == 0) {
        snprintf(dest, dest_len, "MUSIC");
    }
}

static esp_audio_simple_dec_type_t epaper_audio_get_decoder_type(const char *path)
{
    const char *ext = strrchr(path, '.');

    if (!ext || ext[1] == '\0') {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
    }
    ext++;

    if (strcasecmp(ext, "mp3") == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    }
    if (strcasecmp(ext, "wav") == 0) {
        return ESP_AUDIO_SIMPLE_DEC_TYPE_WAV;
    }
    return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
}

static esp_err_t epaper_audio_find_music_file(char *path_out, size_t path_out_len,
        char *label_out, size_t label_out_len)
{
    DIR *directory;
    struct dirent *entry;
    struct stat stat_buf;
    bool found = false;
    char best_name[256] = { 0 };

    if (!path_out || path_out_len == 0 || !label_out || label_out_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    path_out[0] = '\0';
    label_out[0] = '\0';

    if (!epaper_board_sd_is_mounted() &&
            epaper_board_sd_mount() != ESP_OK) {
        epaper_audio_set_music_info("NO SD CARD", "STOP");
        return ESP_ERR_NOT_FOUND;
    }

    directory = opendir(EPAPER_AUDIO_MUSIC_DIR);
    if (!directory) {
        epaper_audio_set_music_info("NO MUSIC", "STOP");
        return ESP_ERR_NOT_FOUND;
    }

    while ((entry = readdir(directory)) != NULL) {
        char candidate_path[EPAPER_AUDIO_FILE_PATH_LEN];

        if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (epaper_audio_get_decoder_type(entry->d_name) ==
                ESP_AUDIO_SIMPLE_DEC_TYPE_NONE) {
            continue;
        }
        if (snprintf(candidate_path, sizeof(candidate_path), "%s/%s",
                    EPAPER_AUDIO_MUSIC_DIR, entry->d_name) >=
                (int) sizeof(candidate_path)) {
            continue;
        }
        if (stat(candidate_path, &stat_buf) != 0 ||
                !S_ISREG(stat_buf.st_mode)) {
            continue;
        }
        if (!found || strcasecmp(entry->d_name, best_name) < 0) {
            found = true;
            snprintf(best_name, sizeof(best_name), "%s", entry->d_name);
            snprintf(path_out, path_out_len, "%s", candidate_path);
        }
    }
    closedir(directory);

    if (!found) {
        epaper_audio_set_music_info("NO MUSIC", "STOP");
        return ESP_ERR_NOT_FOUND;
    }

    epaper_audio_sanitize_label(best_name, label_out, label_out_len);
    return ESP_OK;
}

static void epaper_audio_copy_selected_music(char *path, size_t path_len,
        char *file_name, size_t file_name_len)
{
    epaper_audio_lock();
    if (path && path_len > 0) {
        snprintf(path, path_len, "%s", s_selected_music_path);
    }
    if (file_name && file_name_len > 0) {
        snprintf(file_name, file_name_len, "%s", s_selected_music_file);
    }
    epaper_audio_unlock();
}

static bool epaper_audio_music_stop_requested(void)
{
    return s_music_stop_requested;
}

static void epaper_audio_music_task(void *arg)
{
    char file_path[EPAPER_AUDIO_FILE_PATH_LEN];
    char file_name[EPAPER_AUDIO_FILE_LABEL_LEN];
    FILE *file = NULL;
    uint8_t *in_buf = NULL;
    uint8_t *out_buf = NULL;
    esp_audio_simple_dec_handle_t decoder = NULL;
    esp_audio_simple_dec_type_t decoder_type;
    esp_audio_err_t audio_ret = ESP_AUDIO_ERR_OK;
    esp_err_t err = ESP_OK;
    int out_buf_size = EPAPER_AUDIO_OUT_SIZE;
    bool finished_ok = false;
    bool stopped = false;

    (void) arg;

    epaper_audio_copy_selected_music(file_path, sizeof(file_path),
            file_name, sizeof(file_name));

    epaper_audio_lock();
    s_music_task = xTaskGetCurrentTaskHandle();
    s_audio_state = EPAPER_AUDIO_STATE_PLAYING_SD;
    epaper_audio_set_music_info_locked(file_name, "LOAD");
    epaper_audio_unlock();

    do {
        esp_audio_simple_dec_cfg_t decoder_cfg = {
            .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_NONE,
            .dec_cfg = NULL,
            .cfg_size = 0,
            .use_frame_dec = false,
        };

        err = epaper_audio_register_decoders();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Decoder registration failed: %s",
                    esp_err_to_name(err));
            break;
        }

        decoder_type = epaper_audio_get_decoder_type(file_path);
        if (decoder_type == ESP_AUDIO_SIMPLE_DEC_TYPE_NONE) {
            err = ESP_ERR_NOT_SUPPORTED;
            break;
        }
        decoder_cfg.dec_type = decoder_type;

        file = fopen(file_path, "rb");
        if (!file) {
            err = ESP_ERR_NOT_FOUND;
            break;
        }

        in_buf = malloc(EPAPER_AUDIO_READ_SIZE);
        out_buf = malloc(out_buf_size);
        if (!in_buf || !out_buf) {
            err = ESP_ERR_NO_MEM;
            break;
        }

        audio_ret = esp_audio_simple_dec_open(&decoder_cfg, &decoder);
        if (audio_ret != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(TAG, "Open simple decoder failed: %d", audio_ret);
            err = ESP_FAIL;
            break;
        }

        epaper_audio_set_music_info(file_name, "PLAY");

        while (!epaper_audio_music_stop_requested()) {
            size_t read_len = fread(in_buf, 1, EPAPER_AUDIO_READ_SIZE, file);
            esp_audio_simple_dec_raw_t raw = {
                .buffer = in_buf,
                .len = (uint32_t) read_len,
                .eos = read_len < EPAPER_AUDIO_READ_SIZE,
            };

            if (read_len == 0) {
                break;
            }

            while (raw.len > 0 && !epaper_audio_music_stop_requested()) {
                esp_audio_simple_dec_out_t out_frame = {
                    .buffer = out_buf,
                    .len = (uint32_t) out_buf_size,
                };

                audio_ret = esp_audio_simple_dec_process(decoder, &raw, &out_frame);
                if (audio_ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                    uint8_t *resized = realloc(out_buf, out_frame.needed_size);

                    if (!resized) {
                        err = ESP_ERR_NO_MEM;
                        audio_ret = ESP_AUDIO_ERR_MEM_LACK;
                        break;
                    }
                    out_buf = resized;
                    out_buf_size = (int) out_frame.needed_size;
                    continue;
                }
                if (audio_ret != ESP_AUDIO_ERR_OK) {
                    ESP_LOGE(TAG, "Decode %s failed: %d", file_name, audio_ret);
                    err = ESP_FAIL;
                    break;
                }
                if (out_frame.decoded_size > 0) {
                    esp_audio_simple_dec_info_t decoder_info = { 0 };
                    esp_codec_dev_sample_info_t sample_info;

                    audio_ret = esp_audio_simple_dec_get_info(decoder, &decoder_info);
                    if (audio_ret != ESP_AUDIO_ERR_OK) {
                        err = ESP_FAIL;
                        break;
                    }

                    sample_info.bits_per_sample = decoder_info.bits_per_sample;
                    sample_info.channel = decoder_info.channel;
                    sample_info.channel_mask = 0;
                    sample_info.sample_rate = decoder_info.sample_rate;
                    sample_info.mclk_multiple = 0;

                    err = epaper_audio_prepare_playback(&sample_info);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "Prepare playback failed: %s",
                                esp_err_to_name(err));
                        break;
                    }

                    if (esp_codec_dev_write(s_playback, out_frame.buffer,
                                (int) out_frame.decoded_size) != ESP_CODEC_DEV_OK) {
                        err = ESP_FAIL;
                        break;
                    }
                }

                raw.buffer += raw.consumed;
                raw.len -= raw.consumed;
            }

            if (audio_ret != ESP_AUDIO_ERR_OK || err != ESP_OK || raw.eos) {
                break;
            }
        }

        stopped = epaper_audio_music_stop_requested();
        if (err == ESP_OK && audio_ret == ESP_AUDIO_ERR_OK) {
            finished_ok = true;
        }
    } while (0);

    if (decoder) {
        esp_audio_simple_dec_close(decoder);
    }
    free(out_buf);
    free(in_buf);
    if (file) {
        fclose(file);
    }

    epaper_audio_lock();
    s_music_task = NULL;
    s_music_stop_requested = false;
    if (s_audio_state == EPAPER_AUDIO_STATE_PLAYING_SD) {
        s_audio_state = EPAPER_AUDIO_STATE_READY;
    }
    if (stopped || finished_ok) {
        epaper_audio_set_music_info_locked(file_name, "STOP");
    } else {
        epaper_audio_set_music_info_locked(file_name, "ERR");
    }
    epaper_audio_unlock();

    vTaskDelete(NULL);
}

bool epaper_audio_is_ready(void)
{
    bool ready;

    epaper_audio_lock();
    ready = s_audio_state != EPAPER_AUDIO_STATE_UNINITIALIZED &&
            s_audio_state != EPAPER_AUDIO_STATE_ERROR;
    epaper_audio_unlock();
    return ready;
}

epaper_audio_state_t epaper_audio_get_state(void)
{
    epaper_audio_state_t state;

    epaper_audio_lock();
    state = s_audio_state;
    epaper_audio_unlock();
    return state;
}

const char *epaper_audio_state_name(epaper_audio_state_t state)
{
    switch (state) {
    case EPAPER_AUDIO_STATE_READY:
        return "AUD READY";
    case EPAPER_AUDIO_STATE_RECORDING:
        return "AUD REC";
    case EPAPER_AUDIO_STATE_PLAYING_RECORDING:
        return "AUD PLAY";
    case EPAPER_AUDIO_STATE_PLAYING_DEMO:
        return "AUD DEMO";
    case EPAPER_AUDIO_STATE_PLAYING_SD:
        return "AUD SD";
    case EPAPER_AUDIO_STATE_ERROR:
        return "AUD ERR";
    default:
        return "AUD OFF";
    }
}

size_t epaper_audio_get_recorded_bytes(void)
{
    size_t recorded_bytes;

    epaper_audio_lock();
    recorded_bytes = s_recorded_bytes;
    epaper_audio_unlock();
    return recorded_bytes;
}

esp_err_t epaper_audio_init(void)
{
    codec_init_cfg_t codec_cfg = {
        .in_mode = CODEC_I2S_MODE_STD,
        .out_mode = CODEC_I2S_MODE_STD,
        .in_use_tdm = false,
        .reuse_dev = false,
    };
    esp_err_t err;

    epaper_audio_get_lock();
    if (epaper_audio_is_ready()) {
        return ESP_OK;
    }

    set_codec_board_type("S3_ePaper_1_54");
    err = epaper_audio_alloc_buffer();
    if (err != ESP_OK) {
        goto fail;
    }
    if (init_codec(&codec_cfg) != 0) {
        err = ESP_FAIL;
        goto fail;
    }

    s_playback = get_playback_handle();
    s_record = get_record_handle();
    if (!s_playback || !s_record) {
        err = ESP_FAIL;
        goto fail;
    }
    {
        esp_codec_dev_sample_info_t sample_info =
            epaper_audio_default_sample_info();

        err = epaper_audio_open_record_handle();
        if (err != ESP_OK) {
            goto fail;
        }
        err = epaper_audio_prepare_playback(&sample_info);
        if (err != ESP_OK) {
            goto fail;
        }
    }

    epaper_audio_lock();
    s_audio_state = EPAPER_AUDIO_STATE_READY;
    epaper_audio_unlock();
    ESP_LOGI(TAG, "Audio ready, buffer=%u KB",
            (unsigned) CONFIG_HOMEKIT_EPAPER_AUDIO_BUFFER_KB);
    return ESP_OK;

fail:
    epaper_audio_lock();
    s_audio_state = EPAPER_AUDIO_STATE_ERROR;
    epaper_audio_unlock();
    return err;
}

esp_err_t epaper_audio_record(void)
{
    esp_err_t err;

    if (!epaper_audio_is_ready()) {
        ESP_RETURN_ON_ERROR(epaper_audio_init(), TAG, "audio init failed");
    }

    epaper_audio_lock();
    if (s_music_task) {
        epaper_audio_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    s_audio_state = EPAPER_AUDIO_STATE_RECORDING;
    epaper_audio_unlock();

    err = epaper_audio_open_record_handle();
    if (err != ESP_OK) {
        goto fail;
    }
    if (esp_codec_dev_read(s_record, s_record_buffer,
                (int) s_record_buffer_len) != ESP_CODEC_DEV_OK) {
        err = ESP_FAIL;
        goto fail;
    }

    epaper_audio_lock();
    s_recorded_bytes = s_record_buffer_len;
    s_audio_state = EPAPER_AUDIO_STATE_READY;
    epaper_audio_unlock();
    return ESP_OK;

fail:
    epaper_audio_lock();
    s_audio_state = EPAPER_AUDIO_STATE_ERROR;
    epaper_audio_unlock();
    return err;
}

esp_err_t epaper_audio_play_recorded(void)
{
    esp_err_t err;
    size_t offset = 0;
    esp_codec_dev_sample_info_t sample_info = epaper_audio_default_sample_info();

    if (!epaper_audio_is_ready()) {
        ESP_RETURN_ON_ERROR(epaper_audio_init(), TAG, "audio init failed");
    }

    epaper_audio_lock();
    if (s_music_task) {
        epaper_audio_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    if (s_recorded_bytes == 0) {
        epaper_audio_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    s_audio_state = EPAPER_AUDIO_STATE_PLAYING_RECORDING;
    epaper_audio_unlock();

    err = epaper_audio_prepare_playback(&sample_info);
    if (err != ESP_OK) {
        goto fail;
    }

    while (offset < s_recorded_bytes) {
        size_t chunk = s_recorded_bytes - offset;

        if (chunk > 1024) {
            chunk = 1024;
        }
        if (esp_codec_dev_write(s_playback, s_record_buffer + offset,
                    (int) chunk) != ESP_CODEC_DEV_OK) {
            err = ESP_FAIL;
            goto fail;
        }
        offset += chunk;
    }

    epaper_audio_lock();
    s_audio_state = EPAPER_AUDIO_STATE_READY;
    epaper_audio_unlock();
    return ESP_OK;

fail:
    epaper_audio_lock();
    s_audio_state = EPAPER_AUDIO_STATE_ERROR;
    epaper_audio_unlock();
    return err;
}

esp_err_t epaper_audio_play_demo(void)
{
    const uint8_t *data = epaper_canon_pcm_start;
    size_t total_len = (size_t) (epaper_canon_pcm_end - epaper_canon_pcm_start);
    size_t offset = 0;
    esp_err_t err;
    esp_codec_dev_sample_info_t sample_info = epaper_audio_default_sample_info();

    if (!epaper_audio_is_ready()) {
        ESP_RETURN_ON_ERROR(epaper_audio_init(), TAG, "audio init failed");
    }
    if (total_len == 0) {
        return ESP_ERR_INVALID_SIZE;
    }

    epaper_audio_lock();
    if (s_music_task) {
        epaper_audio_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    s_audio_state = EPAPER_AUDIO_STATE_PLAYING_DEMO;
    epaper_audio_unlock();

    err = epaper_audio_prepare_playback(&sample_info);
    if (err != ESP_OK) {
        goto fail;
    }

    esp_codec_dev_set_out_vol(s_playback, 90.0f);
    while (offset < total_len) {
        size_t chunk = total_len - offset;

        if (chunk > 256) {
            chunk = 256;
        }
        if (esp_codec_dev_write(s_playback, (void *) (data + offset),
                    (int) chunk) != ESP_CODEC_DEV_OK) {
            err = ESP_FAIL;
            goto fail;
        }
        offset += chunk;
    }
    esp_codec_dev_set_out_vol(s_playback, 80.0f);

    epaper_audio_lock();
    s_audio_state = EPAPER_AUDIO_STATE_READY;
    epaper_audio_unlock();
    return ESP_OK;

fail:
    epaper_audio_lock();
    s_audio_state = EPAPER_AUDIO_STATE_ERROR;
    epaper_audio_unlock();
    return err;
}

esp_err_t epaper_audio_toggle_sd_music(void)
{
    char music_path[EPAPER_AUDIO_FILE_PATH_LEN];
    char music_file[EPAPER_AUDIO_FILE_LABEL_LEN];
    esp_err_t err;

    if (!epaper_audio_is_ready()) {
        ESP_RETURN_ON_ERROR(epaper_audio_init(), TAG, "audio init failed");
    }

    epaper_audio_lock();
    if (s_music_task) {
        s_music_stop_requested = true;
        epaper_audio_set_music_info_locked(NULL, "STOP");
        epaper_audio_unlock();
        return ESP_OK;
    }
    epaper_audio_unlock();

    err = epaper_audio_find_music_file(music_path, sizeof(music_path),
            music_file, sizeof(music_file));
    if (err != ESP_OK) {
        return err;
    }

    epaper_audio_lock();
    snprintf(s_selected_music_path, sizeof(s_selected_music_path), "%s",
            music_path);
    snprintf(s_selected_music_file, sizeof(s_selected_music_file), "%s",
            music_file);
    s_music_stop_requested = false;
    epaper_audio_set_music_info_locked(music_file, "LOAD");
    epaper_audio_unlock();

    if (xTaskCreate(epaper_audio_music_task, "ep_music", 10 * 1024,
                NULL, 3, NULL) != pdPASS) {
        epaper_audio_set_music_info(music_file, "ERR");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t epaper_audio_stop_playback(void)
{
    epaper_audio_lock();
    if (!s_music_task) {
        epaper_audio_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    s_music_stop_requested = true;
    epaper_audio_set_music_info_locked(NULL, "STOP");
    epaper_audio_unlock();
    return ESP_OK;
}

bool epaper_audio_is_sd_music_playing(void)
{
    bool playing;

    epaper_audio_lock();
    playing = s_music_task != NULL &&
            s_audio_state == EPAPER_AUDIO_STATE_PLAYING_SD;
    epaper_audio_unlock();
    return playing;
}

void epaper_audio_get_music_info(char *file_name, size_t file_name_len,
        char *status_text, size_t status_text_len)
{
    epaper_audio_lock();
    if (file_name && file_name_len > 0) {
        snprintf(file_name, file_name_len, "%s", s_music_file);
    }
    if (status_text && status_text_len > 0) {
        snprintf(status_text, status_text_len, "%s", s_music_status);
    }
    epaper_audio_unlock();
}
