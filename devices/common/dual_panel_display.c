#include "dual_panel_display.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_rom_sys.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "mbedtls/md.h"

static const char *TAG = "dual_panel";

#ifdef CONFIG_HOMEKIT_DASHBOARD_LEFT_BL_ACTIVE_HIGH
#define DASH_CFG_LEFT_BL_ACTIVE_HIGH 1
#else
#define DASH_CFG_LEFT_BL_ACTIVE_HIGH 0
#endif

#ifdef CONFIG_HOMEKIT_DASHBOARD_LEFT_SWAP_XY
#define DASH_CFG_LEFT_SWAP_XY 1
#else
#define DASH_CFG_LEFT_SWAP_XY 0
#endif

#ifdef CONFIG_HOMEKIT_DASHBOARD_LEFT_MIRROR_X
#define DASH_CFG_LEFT_MIRROR_X 1
#else
#define DASH_CFG_LEFT_MIRROR_X 0
#endif

#ifdef CONFIG_HOMEKIT_DASHBOARD_LEFT_MIRROR_Y
#define DASH_CFG_LEFT_MIRROR_Y 1
#else
#define DASH_CFG_LEFT_MIRROR_Y 0
#endif

#ifdef CONFIG_HOMEKIT_DASHBOARD_LEFT_INVERT_COLORS
#define DASH_CFG_LEFT_INVERT_COLORS 1
#else
#define DASH_CFG_LEFT_INVERT_COLORS 0
#endif

#ifdef CONFIG_HOMEKIT_DASHBOARD_LEFT_BGR_ORDER
#define DASH_CFG_LEFT_BGR_ORDER 1
#else
#define DASH_CFG_LEFT_BGR_ORDER 0
#endif

#ifdef CONFIG_HOMEKIT_DASHBOARD_RIGHT_BL_ACTIVE_HIGH
#define DASH_CFG_RIGHT_BL_ACTIVE_HIGH 1
#else
#define DASH_CFG_RIGHT_BL_ACTIVE_HIGH 0
#endif

#ifdef CONFIG_HOMEKIT_DASHBOARD_RIGHT_SWAP_XY
#define DASH_CFG_RIGHT_SWAP_XY 1
#else
#define DASH_CFG_RIGHT_SWAP_XY 0
#endif

#ifdef CONFIG_HOMEKIT_DASHBOARD_RIGHT_MIRROR_X
#define DASH_CFG_RIGHT_MIRROR_X 1
#else
#define DASH_CFG_RIGHT_MIRROR_X 0
#endif

#ifdef CONFIG_HOMEKIT_DASHBOARD_RIGHT_MIRROR_Y
#define DASH_CFG_RIGHT_MIRROR_Y 1
#else
#define DASH_CFG_RIGHT_MIRROR_Y 0
#endif

#ifdef CONFIG_HOMEKIT_DASHBOARD_RIGHT_INVERT_COLORS
#define DASH_CFG_RIGHT_INVERT_COLORS 1
#else
#define DASH_CFG_RIGHT_INVERT_COLORS 0
#endif

#ifdef CONFIG_HOMEKIT_DASHBOARD_RIGHT_BGR_ORDER
#define DASH_CFG_RIGHT_BGR_ORDER 1
#else
#define DASH_CFG_RIGHT_BGR_ORDER 0
#endif

#define DASHBOARD_SPI_HOST SPI2_HOST
#define FONT_WIDTH 5
#define FONT_HEIGHT 7
#define LEFT_TIME_SCALE 2
#define LEFT_DETAIL_SCALE 2
#define RIGHT_TITLE_SCALE 2
#define RIGHT_BODY_SCALE 2
#define TOTP_DIGITS 6
#define TOTP_PERIOD_SEC 30
#define LEFT_TIME_TEXT_LEN 24
#define LEFT_DETAIL_TEXT_LEN 64
#define LEFT_POEM_TEXT_LEN 64
#define LEFT_POEM_PINYIN_LEN 256
#define CJK_GLYPH_WIDTH 16
#define CJK_GLYPH_HEIGHT 16
#define TEXT_PADDING_X 6
#define POEM_REFRESH_SEC (5 * 60)
#define PANEL_CMD_DELAY 0xFE
#define PANEL_CMD_END 0x00
#define ST7789_CMD_RAMCTRL 0xB0

#define RGB565(r, g, b) \
    (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3))

typedef struct {
    char code;
    uint8_t rows[FONT_HEIGHT];
} font_glyph_t;

typedef struct {
    uint16_t codepoint;
    uint8_t rows[CJK_GLYPH_HEIGHT * 2];
} cjk16_glyph_t;

typedef struct {
    uint16_t codepoint;
    uint16_t syllable_index;
} poem_pinyin_entry_t;

typedef enum {
    PANEL_CONTROLLER_ST7789 = 0,
    PANEL_CONTROLLER_NV3007,
} panel_controller_t;

#ifdef CONFIG_HOMEKIT_DASHBOARD_RIGHT_PANEL_ST7789
#define DASH_CFG_RIGHT_CONTROLLER PANEL_CONTROLLER_ST7789
#else
#define DASH_CFG_RIGHT_CONTROLLER PANEL_CONTROLLER_NV3007
#endif

typedef struct {
    panel_controller_t controller;
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel_handle;
    int width;
    int height;
    int x_gap;
    int y_gap;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    bool invert_colors;
    bool bgr_order;
    /* Independent soft-SPI pins for panel variants that do not share the hardware bus. */
    bool soft_spi;
    int soft_spi_clk;
    int soft_spi_mosi;
    int soft_spi_cs;
    int soft_spi_dc;
    int soft_spi_delay_us;
    bool soft_spi_keep_cs_low;
} dashboard_panel_t;

typedef struct {
    bool initialized;
    bool started;
    bool left_available;
    bool right_available;
    bool wifi_connected;
    bool time_synced;
    bool left_dirty;
    bool left_time_dirty;
    bool left_detail_dirty;
    bool left_poem_dirty;
    bool right_dirty;
    bool poem_refresh_requested;
    bool sntp_started;
    char ip_text[24];
    char location_text[24];
    char right_otp_text[16];
    char weather_text[40];
    char poem_text[LEFT_POEM_TEXT_LEN];
    char last_time_text[LEFT_TIME_TEXT_LEN];
    bool light_states[3];
    bool button_states[3];
    size_t poem_refresh_serial;
    int right_otp_remaining;
    TickType_t last_weather_fetch_tick;
    TickType_t last_poem_fetch_tick;
    TickType_t fallback_time_tick;
    time_t fallback_time_seed;
    SemaphoreHandle_t lock;
} dashboard_state_t;

static const font_glyph_t s_font[] = {
    { ' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00} },
    { '+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00} },
    { '-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00} },
    { '.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C} },
    { ':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00} },
    { '/', {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00} },
    { '0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E} },
    { '1', {0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x1F} },
    { '2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F} },
    { '3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E} },
    { '4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02} },
    { '5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E} },
    { '6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E} },
    { '7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08} },
    { '8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E} },
    { '9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C} },
    { 'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11} },
    { 'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E} },
    { 'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E} },
    { 'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E} },
    { 'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F} },
    { 'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10} },
    { 'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E} },
    { 'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11} },
    { 'I', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F} },
    { 'J', {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C} },
    { 'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11} },
    { 'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F} },
    { 'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11} },
    { 'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11} },
    { 'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E} },
    { 'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10} },
    { 'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D} },
    { 'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11} },
    { 'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E} },
    { 'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04} },
    { 'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E} },
    { 'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04} },
    { 'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A} },
    { 'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11} },
    { 'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04} },
    { 'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F} },
};

#include "dashboard_cjk16_font.inc"

#include "dashboard_poem_pinyin_table.inc"

static dashboard_panel_t s_left_panel;
static dashboard_panel_t s_right_panel;
static dashboard_state_t s_state = {
    .ip_text = "NO WIFI",
    .location_text = "LOCATING",
    .right_otp_text = "NO KEY",
    .weather_text = "WEATHER --",
    .poem_text = "SHI CI LOADING",
    .last_time_text = "----.--.-- --:--:--",
    .right_otp_remaining = TOTP_PERIOD_SEC,
};
static uint16_t *s_line_buffer;
static size_t s_line_buffer_pixels;
static char s_left_rendered_time[LEFT_TIME_TEXT_LEN];

static const uint8_t s_nv3007_init_seq[] = {
    0x9A, 1, 0x08,
    0xA5, 1, 0x21,
    0xA8, 1, 0x03,
    0x86, 1, 0x91,
    0x87, 1, 0x48,
    0x88, 1, 0x11,
    0x89, 1, 0x27,
    0x8B, 1, 0x80,
    0x8D, 1, 0x11,
    0x8E, 1, 0xFF,
    0x8F, 1, 0xFF,
    0x6A, 1, 0xC0,
    0x4D, 1, 0x21,
    0x50, 1, 0xA9,
    0x35, 1, 0x26,
    0x34, 1, 0x5F,
    0x44, 2, 0x00, 0x0E,
    0x36, 1, 0x02,
    0x90, 2, 0x0A, 0x0A,
    0x91, 2, 0x0A, 0x0A,
    0x92, 1, 0x2A,
    0x93, 1, 0x18,
    0x94, 1, 0x48,
    0x95, 1, 0x02,
    0xA0, 2, 0x00, 0x00,
    0xA1, 2, 0x09, 0x08,
    0xA2, 2, 0x13, 0x24,
    0xA3, 2, 0x1A, 0x18,
    0xA4, 2, 0x1D, 0x31,
    0xA5, 2, 0x23, 0x26,
    0xA6, 2, 0x21, 0x2F,
    0xA7, 2, 0x25, 0x1F,
    0xA8, 2, 0x0B, 0xB0,
    0xA9, 2, 0x17, 0x18,
    0xAA, 2, 0x25, 0x23,
    0xAB, 2, 0x22, 0x46,
    0xAC, 2, 0x18, 0x1A,
    0xAD, 2, 0x14, 0x13,
    0xAE, 2, 0x09, 0x00,
    0xAF, 2, 0x00, 0x00,
    0xC0, 2, 0x00, 0x00,
    0xC1, 2, 0x09, 0x08,
    0xC2, 2, 0x13, 0x24,
    0xC3, 2, 0x1A, 0x18,
    0xC4, 2, 0x1D, 0x31,
    0xC5, 2, 0x23, 0x26,
    0xC6, 2, 0x21, 0x2F,
    0xC7, 2, 0x25, 0x1F,
    0xC8, 2, 0x0B, 0xB0,
    0xC9, 2, 0x17, 0x18,
    0xCA, 2, 0x25, 0x23,
    0xCB, 2, 0x22, 0x46,
    0xCC, 2, 0x18, 0x1A,
    0xCD, 2, 0x14, 0x13,
    0xCE, 2, 0x09, 0x00,
    0xCF, 2, 0x00, 0x00,
    0xD0, 2, 0x00, 0x00,
    0xD1, 2, 0x09, 0x08,
    0xD2, 2, 0x13, 0x24,
    0xD3, 2, 0x1A, 0x18,
    0xD4, 2, 0x1D, 0x31,
    0xD5, 2, 0x23, 0x26,
    0xD6, 2, 0x21, 0x2F,
    0xD7, 2, 0x25, 0x1F,
    0xD8, 2, 0x0B, 0xB0,
    0xD9, 2, 0x17, 0x18,
    0xDA, 2, 0x25, 0x23,
    0xDB, 2, 0x22, 0x46,
    0xDC, 2, 0x18, 0x1A,
    0xDD, 2, 0x14, 0x13,
    0xDE, 2, 0x09, 0x00,
    0xDF, 2, 0x00, 0x00,
    0xB1, 16,
        0x00, 0x00, 0x02, 0x42, 0x06, 0x00, 0x08, 0x10,
        0x24, 0x02, 0x51, 0x50, 0x01, 0x24, 0x42, 0x3D,
    0xB4, 4, 0x11, 0x00, 0x00, 0x00,
    0xBB, 1, 0x11,
    0xBC, 1, 0x11,
    0xBD, 1, 0x11,
    0xBE, 1, 0x11,
    0xBF, 1, 0x11,
    0xC8, 1, 0xFF,
    0xC9, 1, 0xFF,
    0xCA, 1, 0xFF,
    0xCB, 1, 0xFF,
    0xCC, 1, 0xFF,
    0xBC, 1, 0x00,
    0xBD, 1, 0x00,
    0xBE, 1, 0x00,
    0xBF, 1, 0x00,
    0xC0, 1, 0x00,
    0x46, 1, 0x10,
    PANEL_CMD_DELAY, 1, 10,
    PANEL_CMD_END, 0
};

static const uint16_t s_left_background = RGB565(82, 178, 122);
static const uint16_t s_left_header = RGB565(244, 124, 72);
static const uint16_t s_left_text = RGB565(252, 249, 241);
static const uint16_t s_left_detail_text = RGB565(18, 48, 46);
static const uint16_t s_left_secondary = RGB565(226, 244, 230);
static const uint16_t s_left_poem_background = RGB565(109, 104, 212);
static const uint16_t s_left_poem_text = RGB565(255, 238, 186);
static const uint16_t s_right_background = RGB565(18, 20, 24);
static const uint16_t s_right_header = RGB565(44, 78, 126);
static const uint16_t s_right_text = RGB565(245, 247, 250);
static const uint16_t s_right_on = RGB565(35, 110, 48);
static const uint16_t s_right_off = RGB565(122, 32, 40);
static const uint8_t s_st7789_ramctrl[2] = { 0x00, 0xE0 };
static const uint8_t s_st7789_positive_gamma[] = {
    0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32,
    0x44, 0x42, 0x06, 0x0E, 0x12, 0x14, 0x17,
};
static const uint8_t s_st7789_negative_gamma[] = {
    0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x31,
    0x54, 0x47, 0x0E, 0x1C, 0x17, 0x1B, 0x1E,
};

static int dashboard_ascii_strcasecmp(const char *left, const char *right);
static void dashboard_update_right_otp_cache(time_t now, bool time_synced);

static char dashboard_normalize_char(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char) (c - ('a' - 'A'));
    }
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        return c;
    }
    switch (c) {
    case ' ':
    case '+':
    case '-':
    case '.':
    case ':':
    case '/':
        return c;
    default:
        return ' ';
    }
}

static const font_glyph_t *dashboard_find_glyph(char c)
{
    for (size_t i = 0; i < sizeof(s_font) / sizeof(s_font[0]); i++) {
        if (s_font[i].code == c) {
            return &s_font[i];
        }
    }
    return &s_font[0];
}

static const cjk16_glyph_t *dashboard_find_cjk16_glyph(uint16_t codepoint)
{
    size_t left = 0;
    size_t right = sizeof(s_cjk16_font) / sizeof(s_cjk16_font[0]);

    while (left < right) {
        size_t mid = left + (right - left) / 2;

        if (s_cjk16_font[mid].codepoint == codepoint) {
            return &s_cjk16_font[mid];
        }
        if (s_cjk16_font[mid].codepoint < codepoint) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    return NULL;
}

static bool dashboard_utf8_next_codepoint(const char **text, uint16_t *codepoint)
{
    const unsigned char *ptr;

    if (!text || !*text || !codepoint || **text == '\0') {
        return false;
    }

    ptr = (const unsigned char *) *text;
    if (ptr[0] < 0x80) {
        *codepoint = ptr[0];
        *text += 1;
        return true;
    }
    if ((ptr[0] & 0xE0) == 0xC0 && ptr[1] != '\0') {
        *codepoint = (uint16_t) (((ptr[0] & 0x1F) << 6) | (ptr[1] & 0x3F));
        *text += 2;
        return true;
    }
    if ((ptr[0] & 0xF0) == 0xE0 && ptr[1] != '\0' && ptr[2] != '\0') {
        *codepoint = (uint16_t) (((ptr[0] & 0x0F) << 12) |
                ((ptr[1] & 0x3F) << 6) |
                (ptr[2] & 0x3F));
        *text += 3;
        return true;
    }

    *codepoint = '?';
    *text += 1;
    return true;
}

static int dashboard_panel_visible_width(const dashboard_panel_t *panel)
{
    return panel->swap_xy ? panel->height : panel->width;
}

static int dashboard_panel_visible_height(const dashboard_panel_t *panel)
{
    return panel->swap_xy ? panel->width : panel->height;
}

/* ---- Soft-SPI (bit-bang) helpers ----------------------------------------- */

static void dashboard_soft_spi_write_byte(const dashboard_panel_t *panel, uint8_t byte)
{
    for (int bit = 7; bit >= 0; bit--) {
        gpio_set_level(panel->soft_spi_mosi, (byte >> bit) & 1);
        gpio_set_level(panel->soft_spi_clk, 1);
        if (panel->soft_spi_delay_us > 0) {
            esp_rom_delay_us((uint32_t) panel->soft_spi_delay_us);
        }
        gpio_set_level(panel->soft_spi_clk, 0);
        if (panel->soft_spi_delay_us > 0) {
            esp_rom_delay_us((uint32_t) panel->soft_spi_delay_us);
        }
    }
}

/* Unified tx_param: routes to soft-SPI or hardware SPI panel IO */
static esp_err_t dashboard_panel_tx_param(dashboard_panel_t *panel,
        uint8_t cmd, const void *data, size_t len)
{
    if (panel->soft_spi) {
        if (panel->soft_spi_cs >= 0) {
            gpio_set_level(panel->soft_spi_cs, 0);
        }
        gpio_set_level(panel->soft_spi_dc, 0);   /* command phase */
        dashboard_soft_spi_write_byte(panel, cmd);
        if (data && len > 0) {
            gpio_set_level(panel->soft_spi_dc, 1);   /* data phase */
            const uint8_t *p = (const uint8_t *) data;
            for (size_t i = 0; i < len; i++) {
                dashboard_soft_spi_write_byte(panel, p[i]);
            }
        }
        if (!panel->soft_spi_keep_cs_low && panel->soft_spi_cs >= 0) {
            gpio_set_level(panel->soft_spi_cs, 1);
        }
        return ESP_OK;
    }
    return esp_lcd_panel_io_tx_param(panel->io_handle, (int) cmd, data, len);
}

/* Unified tx_color: sends cmd + big-endian RGB565 pixel data */
static esp_err_t dashboard_panel_tx_color(dashboard_panel_t *panel,
        uint8_t cmd, const void *color_data, size_t len)
{
    if (panel->soft_spi) {
        if (panel->soft_spi_cs >= 0) {
            gpio_set_level(panel->soft_spi_cs, 0);
        }
        gpio_set_level(panel->soft_spi_dc, 0);   /* command phase */
        dashboard_soft_spi_write_byte(panel, cmd);
        if (color_data && len > 0) {
            gpio_set_level(panel->soft_spi_dc, 1);   /* data phase */
            /* RGB565 is stored little-endian in memory; transmit big-endian to panel */
            const uint16_t *pixels = (const uint16_t *) color_data;
            size_t num_pixels = len / sizeof(uint16_t);
            for (size_t i = 0; i < num_pixels; i++) {
                dashboard_soft_spi_write_byte(panel, (uint8_t) (pixels[i] >> 8));
                dashboard_soft_spi_write_byte(panel, (uint8_t) (pixels[i] & 0xFF));
            }
        }
        if (!panel->soft_spi_keep_cs_low && panel->soft_spi_cs >= 0) {
            gpio_set_level(panel->soft_spi_cs, 1);
        }
        return ESP_OK;
    }
    return esp_lcd_panel_io_tx_color(panel->io_handle, (int) cmd, color_data, len);
}

/* --------------------------------------------------------------------------- */

static esp_err_t dashboard_panel_send_sequence(dashboard_panel_t *panel,
        const uint8_t *sequence)
{
    const uint8_t *cursor = sequence;

    while (cursor[0] != PANEL_CMD_END || cursor[1] != 0) {
        uint8_t cmd = cursor[0];
        uint8_t len = cursor[1];

        cursor += 2;
        if (cmd == PANEL_CMD_DELAY) {
            vTaskDelay(pdMS_TO_TICKS(len ? cursor[0] : 0));
            cursor += len;
            continue;
        }
        ESP_RETURN_ON_ERROR(dashboard_panel_tx_param(panel, cmd, cursor, len),
                TAG, "panel init command 0x%02X failed", cmd);
        cursor += len;
    }
    return ESP_OK;
}

static esp_err_t dashboard_st77xx_apply_madctl(dashboard_panel_t *panel)
{
    uint8_t madctl = panel->bgr_order ? 0x08 : 0x00;

    if (panel->mirror_x) {
        madctl |= 0x40;
    }
    if (panel->mirror_y) {
        madctl |= 0x80;
    }
    if (panel->swap_xy) {
        madctl |= 0x20;
    }
    return dashboard_panel_tx_param(panel, 0x36, &madctl, 1);
}

static esp_err_t dashboard_panel_reset_gpio(int reset_gpio, const char *panel_name)
{
    gpio_config_t reset_pin;

    if (reset_gpio < 0) {
        return ESP_OK;
    }

    memset(&reset_pin, 0, sizeof(reset_pin));
    reset_pin.pin_bit_mask = 1ULL << reset_gpio;
    reset_pin.mode = GPIO_MODE_OUTPUT;
    reset_pin.pull_up_en = GPIO_PULLUP_DISABLE;
    reset_pin.pull_down_en = GPIO_PULLDOWN_DISABLE;
    reset_pin.intr_type = GPIO_INTR_DISABLE;

    ESP_RETURN_ON_ERROR(gpio_config(&reset_pin), TAG, "%s reset gpio config failed",
            panel_name);
    ESP_RETURN_ON_ERROR(gpio_set_level(reset_gpio, 0), TAG, "%s reset low failed",
            panel_name);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(gpio_set_level(reset_gpio, 1), TAG, "%s reset high failed",
            panel_name);
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

static esp_err_t dashboard_panel_init_nv3007(dashboard_panel_t *panel, int reset_gpio)
{
    esp_err_t err;
    uint8_t unlock = 0xA5;
    uint8_t lock = 0x00;
    uint8_t pixel_format = 0x05;

    ESP_RETURN_ON_ERROR(dashboard_panel_reset_gpio(reset_gpio, "NV3007"), TAG,
            "NV3007 reset failed");

    ESP_RETURN_ON_ERROR(dashboard_panel_tx_param(panel, 0xFF, &unlock, 1),
            TAG, "NV3007 unlock failed");
    ESP_RETURN_ON_ERROR(dashboard_panel_send_sequence(panel, s_nv3007_init_seq),
            TAG, "NV3007 init sequence failed");
    ESP_RETURN_ON_ERROR(dashboard_panel_tx_param(panel, 0xFF, &lock, 1),
            TAG, "NV3007 lock failed");

    err = dashboard_panel_tx_param(panel, 0x3A, &pixel_format, 1);
    if (err == ESP_OK) {
        err = dashboard_panel_tx_param(panel, 0x11, NULL, 0);
    }
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(22));
        err = dashboard_st77xx_apply_madctl(panel);
    }
    if (err == ESP_OK) {
        uint8_t inv_cmd = panel->invert_colors ? 0x21 : 0x20;
        err = dashboard_panel_tx_param(panel, inv_cmd, NULL, 0);
    }
    if (err == ESP_OK) {
        err = dashboard_panel_tx_param(panel, 0x29, NULL, 0);
    }
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return err;
}

static esp_err_t dashboard_panel_init_st7789(dashboard_panel_t *panel, int reset_gpio)
{
    esp_err_t err;
    uint8_t pixel_format = 0x55;
    uint8_t display_function[2] = { 0x0A, 0x82 };
    uint8_t porch[5] = { 0x0C, 0x0C, 0x00, 0x33, 0x33 };
    uint8_t gate = 0x35;
    uint8_t vcom = 0x28;
    uint8_t lcm = 0x0C;
    uint8_t vdv_vrh_en[2] = { 0x01, 0xFF };
    uint8_t vrhs = 0x10;
    uint8_t vdv = 0x20;
    uint8_t frctrl2 = 0x0F;
    uint8_t pwctrl1[2] = { 0xA4, 0xA1 };
    uint8_t inv_cmd = panel->invert_colors ? 0x21 : 0x20;

    ESP_RETURN_ON_ERROR(dashboard_panel_reset_gpio(reset_gpio, "ST7789"), TAG,
            "ST7789 reset failed");

    err = dashboard_panel_tx_param(panel, 0x11, NULL, 0);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(120));
        err = dashboard_panel_tx_param(panel, 0x13, NULL, 0);
    }
    if (err == ESP_OK) {
        err = dashboard_st77xx_apply_madctl(panel);
    }
    if (err == ESP_OK) {
        err = dashboard_panel_tx_param(panel, 0xB6, display_function,
                sizeof(display_function));
    }
    if (err == ESP_OK) {
        err = dashboard_panel_tx_param(panel, ST7789_CMD_RAMCTRL,
                s_st7789_ramctrl, sizeof(s_st7789_ramctrl));
    }
    if (err == ESP_OK) {
        err = dashboard_panel_tx_param(panel, 0x3A, &pixel_format, 1);
    }
    if (err == ESP_OK) {
        err = dashboard_panel_tx_param(panel, 0xB2, porch, sizeof(porch));
    }
    if (err == ESP_OK) {
        err = dashboard_panel_tx_param(panel, 0xB7, &gate, 1);
    }
    if (err == ESP_OK) {
        err = dashboard_panel_tx_param(panel, 0xBB, &vcom, 1);
    }
    if (err == ESP_OK) {
        err = dashboard_panel_tx_param(panel, 0xC0, &lcm, 1);
    }
    if (err == ESP_OK) {
        err = dashboard_panel_tx_param(panel, 0xC2, vdv_vrh_en,
                sizeof(vdv_vrh_en));
    }
    if (err == ESP_OK) {
        err = dashboard_panel_tx_param(panel, 0xC3, &vrhs, 1);
    }
    if (err == ESP_OK) {
        err = dashboard_panel_tx_param(panel, 0xC4, &vdv, 1);
    }
    if (err == ESP_OK) {
        err = dashboard_panel_tx_param(panel, 0xC6, &frctrl2, 1);
    }
    if (err == ESP_OK) {
        err = dashboard_panel_tx_param(panel, 0xD0, pwctrl1,
                sizeof(pwctrl1));
    }
    if (err == ESP_OK) {
        err = dashboard_panel_tx_param(panel, 0xE0, s_st7789_positive_gamma,
                sizeof(s_st7789_positive_gamma));
    }
    if (err == ESP_OK) {
        err = dashboard_panel_tx_param(panel, 0xE1, s_st7789_negative_gamma,
                sizeof(s_st7789_negative_gamma));
    }
    if (err == ESP_OK) {
        err = dashboard_panel_tx_param(panel, inv_cmd, NULL, 0);
    }
    if (err == ESP_OK) {
        err = dashboard_panel_tx_param(panel, 0x29, NULL, 0);
    }
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    return err;
}

static esp_err_t dashboard_panel_create(dashboard_panel_t *panel,
        panel_controller_t controller,
        int width, int height,
        int x_gap, int y_gap,
        bool swap_xy, bool mirror_x, bool mirror_y,
        bool invert_colors, bool bgr_order,
        int cs_gpio, int dc_gpio, int reset_gpio, int backlight_gpio,
        bool backlight_active_high,
        int own_sclk_gpio, int own_mosi_gpio)
{
    esp_err_t err;

    memset(panel, 0, sizeof(*panel));
    panel->controller = controller;
    panel->width = width;
    panel->height = height;
    panel->x_gap = x_gap;
    panel->y_gap = y_gap;
    panel->swap_xy = swap_xy;
    panel->mirror_x = mirror_x;
    panel->mirror_y = mirror_y;
    panel->invert_colors = invert_colors;
    panel->bgr_order = bgr_order;

    if (own_sclk_gpio >= 0 && own_mosi_gpio >= 0) {
        /* Independent soft-SPI: configure GPIOs directly, skip hardware SPI bus */
        gpio_config_t spi_pins = {
            .pin_bit_mask = (1ULL << own_sclk_gpio) | (1ULL << own_mosi_gpio) |
                            (1ULL << cs_gpio) | (1ULL << dc_gpio),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&spi_pins), TAG, "Soft-SPI GPIO config failed");
        gpio_set_level(own_sclk_gpio, 0);
        gpio_set_level(own_mosi_gpio, 0);
        gpio_set_level(cs_gpio, 1);   /* CS idle high */
        gpio_set_level(dc_gpio, 1);
        panel->soft_spi = true;
        panel->soft_spi_clk = own_sclk_gpio;
        panel->soft_spi_mosi = own_mosi_gpio;
        panel->soft_spi_cs = cs_gpio;
        panel->soft_spi_dc = dc_gpio;
        panel->soft_spi_delay_us = (cs_gpio == CONFIG_HOMEKIT_DASHBOARD_RIGHT_CS) ? 2 : 0;
        panel->soft_spi_keep_cs_low = false;
    } else {
        esp_lcd_panel_io_spi_config_t io_config = {
            .cs_gpio_num = cs_gpio,
            .dc_gpio_num = dc_gpio,
            .spi_mode = 0,
            .pclk_hz = CONFIG_HOMEKIT_DASHBOARD_SPI_CLOCK_HZ,
            .trans_queue_depth = 8,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
        };
        err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) DASHBOARD_SPI_HOST,
                &io_config, &panel->io_handle);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (controller == PANEL_CONTROLLER_ST7789) {
        err = dashboard_panel_init_st7789(panel, reset_gpio);
    } else {
        err = dashboard_panel_init_nv3007(panel, reset_gpio);
    }
    if (err != ESP_OK) {
        if (panel->panel_handle) {
            esp_lcd_panel_del(panel->panel_handle);
            panel->panel_handle = NULL;
        }
        if (panel->io_handle) {
            esp_lcd_panel_io_del(panel->io_handle);
            panel->io_handle = NULL;
        }
        return err;
    }

    if (backlight_gpio >= 0) {
        gpio_config_t backlight_pin = {
            .pin_bit_mask = 1ULL << backlight_gpio,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };

        ESP_RETURN_ON_ERROR(gpio_config(&backlight_pin), TAG, "Backlight config failed");
        ESP_RETURN_ON_ERROR(gpio_set_level(backlight_gpio, backlight_active_high ? 1 : 0),
                TAG, "Backlight enable failed");
    }
    return ESP_OK;
}

static esp_err_t dashboard_panel_draw_bitmap(dashboard_panel_t *panel,
        int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    if (panel->controller == PANEL_CONTROLLER_ST7789 &&
            panel->panel_handle && !panel->soft_spi) {
        return esp_lcd_panel_draw_bitmap(panel->panel_handle,
                x_start, y_start, x_end, y_end, color_data);
    }

    x_start += panel->x_gap;
    y_start += panel->y_gap;
    x_end += panel->x_gap;
    y_end += panel->y_gap;

    uint8_t col_data[4] = {
        (uint8_t) ((x_start >> 8) & 0xFF),
        (uint8_t) (x_start & 0xFF),
        (uint8_t) (((x_end - 1) >> 8) & 0xFF),
        (uint8_t) ((x_end - 1) & 0xFF),
    };
    uint8_t row_data[4] = {
        (uint8_t) ((y_start >> 8) & 0xFF),
        (uint8_t) (y_start & 0xFF),
        (uint8_t) (((y_end - 1) >> 8) & 0xFF),
        (uint8_t) ((y_end - 1) & 0xFF),
    };
    size_t len = (size_t) (x_end - x_start) * (size_t) (y_end - y_start) * sizeof(uint16_t);
    esp_err_t err = dashboard_panel_tx_param(panel, 0x2A, col_data, sizeof(col_data));

    if (err == ESP_OK) {
        err = dashboard_panel_tx_param(panel, 0x2B, row_data, sizeof(row_data));
    }
    /* 将 0x2C 命令与像素数据合并在同一个 SPI 事务中发出（硬件 SPI 路径），
     * 软件 SPI 路径内部已按命令+数据合并发送。 */
    if (err == ESP_OK) {
        err = dashboard_panel_tx_color(panel, 0x2C, color_data, len);
    }
    return err;
}

static void dashboard_panel_fill_solid_immediate(dashboard_panel_t *panel, uint16_t color)
{
    int panel_width = dashboard_panel_visible_width(panel);
    int panel_height = dashboard_panel_visible_height(panel);

    if (!panel || !s_line_buffer || panel_width <= 0 || panel_height <= 0) {
        return;
    }
    if ((size_t) panel_width > s_line_buffer_pixels) {
        panel_width = (int) s_line_buffer_pixels;
    }

    for (int x = 0; x < panel_width; x++) {
        s_line_buffer[x] = color;
    }
    for (int y = 0; y < panel_height; y++) {
        if (dashboard_panel_draw_bitmap(panel, 0, y, panel_width, y + 1, s_line_buffer) != ESP_OK) {
            ESP_LOGW(TAG, "Immediate panel fill failed");
            return;
        }
    }
}

static void dashboard_panel_run_boot_pattern(dashboard_panel_t *panel, const char *panel_name)
{
    static const uint16_t colors[] = {
        RGB565(0xFF, 0x30, 0x30),
        RGB565(0x30, 0xFF, 0x30),
        RGB565(0x30, 0x60, 0xFF),
        RGB565(0x08, 0x10, 0x18),
    };

    if (!panel) {
        return;
    }

    ESP_LOGI(TAG, "%s panel boot pattern %dx%d",
            panel_name,
            dashboard_panel_visible_width(panel),
            dashboard_panel_visible_height(panel));
    for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
        dashboard_panel_fill_solid_immediate(panel, colors[i]);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
}

static void dashboard_panel_fill_rect(dashboard_panel_t *panel,
        int x, int y, int width, int height, uint16_t color)
{
    int panel_width = dashboard_panel_visible_width(panel);
    int panel_height = dashboard_panel_visible_height(panel);

    if (!s_state.initialized || !s_line_buffer || width <= 0 || height <= 0) {
        return;
    }
    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (x >= panel_width || y >= panel_height) {
        return;
    }
    if (x + width > panel_width) {
        width = panel_width - x;
    }
    if (y + height > panel_height) {
        height = panel_height - y;
    }
    if (width <= 0 || height <= 0) {
        return;
    }
    if ((size_t) width > s_line_buffer_pixels) {
        width = (int) s_line_buffer_pixels;
    }

    for (int i = 0; i < width; i++) {
        s_line_buffer[i] = color;
    }
    for (int row = 0; row < height; row++) {
        if (dashboard_panel_draw_bitmap(panel, x, y + row, x + width, y + row + 1,
                s_line_buffer) != ESP_OK) {
            ESP_LOGW(TAG, "Panel fill failed");
            return;
        }
    }
}

static int dashboard_measure_text(const char *text, uint8_t scale)
{
    int width = 0;

    if (!text) {
        return 0;
    }
    while (*text) {
        width += (FONT_WIDTH + 1) * scale;
        text++;
    }
    return width;
}

static int dashboard_text_origin_x(int panel_width, const char *text,
        uint8_t scale, bool center)
{
    int cursor_x = TEXT_PADDING_X;
    int text_width;

    if (!center || !text) {
        return cursor_x;
    }

    text_width = dashboard_measure_text(text, scale);
    if (text_width > 0 && text_width < panel_width) {
        cursor_x = (panel_width - text_width) / 2;
    }
    return cursor_x;
}

static void dashboard_draw_char(dashboard_panel_t *panel,
        int x, int y, char c, uint16_t color, uint8_t scale)
{
    const font_glyph_t *glyph = dashboard_find_glyph(dashboard_normalize_char(c));

    for (int row = 0; row < FONT_HEIGHT; row++) {
        for (int col = 0; col < FONT_WIDTH; col++) {
            if (glyph->rows[row] & (1U << (FONT_WIDTH - col - 1))) {
                dashboard_panel_fill_rect(panel,
                        x + col * scale, y + row * scale,
                        scale, scale, color);
            }
        }
    }
}

static void dashboard_draw_cjk16_char(dashboard_panel_t *panel,
        int x, int y, uint16_t codepoint, uint16_t color)
{
    const cjk16_glyph_t *glyph = dashboard_find_cjk16_glyph(codepoint);

    if (!glyph) {
        dashboard_draw_char(panel, x + 5, y + 4, '?', color, 1);
        return;
    }

    for (int row = 0; row < CJK_GLYPH_HEIGHT; row++) {
        uint16_t bits = (uint16_t) ((glyph->rows[row * 2] << 8) | glyph->rows[row * 2 + 1]);

        for (int col = 0; col < CJK_GLYPH_WIDTH; col++) {
            if (bits & (0x8000U >> col)) {
                dashboard_panel_fill_rect(panel, x + col, y + row, 1, 1, color);
            }
        }
    }
}

static int dashboard_poem_codepoint_width(uint16_t codepoint)
{
    if (codepoint < 0x80) {
        return FONT_WIDTH + 1;
    }
    if (dashboard_find_cjk16_glyph(codepoint)) {
        return CJK_GLYPH_WIDTH;
    }
    return CJK_GLYPH_WIDTH;
}

static int dashboard_measure_poem_text(const char *text)
{
    int width = 0;
    const char *cursor = text;

    while (cursor && *cursor) {
        uint16_t codepoint;

        if (!dashboard_utf8_next_codepoint(&cursor, &codepoint)) {
            break;
        }
        width += dashboard_poem_codepoint_width(codepoint);
    }
    return width;
}

static bool dashboard_poem_text_supported(const char *text)
{
    const char *cursor = text;

    while (cursor && *cursor) {
        uint16_t codepoint;

        if (!dashboard_utf8_next_codepoint(&cursor, &codepoint)) {
            break;
        }
        if (codepoint < 0x80) {
            continue;
        }
        if (!dashboard_find_cjk16_glyph(codepoint)) {
            return false;
        }
    }
    return true;
}

static void dashboard_draw_poem_line(dashboard_panel_t *panel,
        int y, const char *text, uint16_t color, uint16_t background)
{
    int panel_width = dashboard_panel_visible_width(panel);
    int panel_height = dashboard_panel_visible_height(panel);
    int strip_height = CJK_GLYPH_HEIGHT + 4;
    int cursor_x = TEXT_PADDING_X;
    int baseline_y = y;
    const char *cursor = text;

    if (baseline_y + strip_height > panel_height) {
        strip_height = panel_height - baseline_y;
    }
    if (strip_height < 1) {
        strip_height = 1;
    }

    dashboard_panel_fill_rect(panel, 0, baseline_y - 2, panel_width, strip_height, background);

    while (cursor && *cursor) {
        uint16_t codepoint;
        int glyph_width;

        if (!dashboard_utf8_next_codepoint(&cursor, &codepoint)) {
            break;
        }

        glyph_width = dashboard_poem_codepoint_width(codepoint);
        if (cursor_x + glyph_width > panel_width - TEXT_PADDING_X) {
            break;
        }

        if (codepoint < 0x80) {
            dashboard_draw_char(panel, cursor_x, baseline_y + 4,
                    (char) codepoint, color, 1);
        } else {
            dashboard_draw_cjk16_char(panel, cursor_x, baseline_y, codepoint, color);
        }
        cursor_x += glyph_width;
    }
}

static void dashboard_draw_text_line(dashboard_panel_t *panel,
        int y, const char *text,
        uint16_t color, uint16_t background,
        uint8_t scale, bool center)
{
    int panel_width = dashboard_panel_visible_width(panel);
    int cursor_x = dashboard_text_origin_x(panel_width, text, scale, center);
    int line_height = FONT_HEIGHT * scale;

    dashboard_panel_fill_rect(panel, 0, y - 1, panel_width, line_height + 2, background);

    if (!text) {
        return;
    }
    while (*text) {
        if (cursor_x + FONT_WIDTH * scale > panel_width - TEXT_PADDING_X) {
            break;
        }
        dashboard_draw_char(panel, cursor_x, y, *text, color, scale);
        cursor_x += (FONT_WIDTH + 1) * scale;
        text++;
    }
}

static void dashboard_draw_text_segment(dashboard_panel_t *panel,
        int y, const char *full_text,
        int start_col, int clear_cols, const char *segment_text,
        uint16_t color, uint16_t background,
        uint8_t scale, bool center)
{
    int panel_width = dashboard_panel_visible_width(panel);
    int cell_width = (FONT_WIDTH + 1) * scale;
    int line_height = FONT_HEIGHT * scale;
    int base_x = dashboard_text_origin_x(panel_width, full_text, scale, center);
    int x = base_x + start_col * cell_width;
    int clear_width = clear_cols * cell_width;
    int cursor_x = x;

    if (x >= panel_width || clear_width <= 0) {
        return;
    }
    if (x < 0) {
        clear_width += x;
        x = 0;
        cursor_x = x;
    }
    if (x + clear_width > panel_width) {
        clear_width = panel_width - x;
    }
    if (clear_width <= 0) {
        return;
    }

    dashboard_panel_fill_rect(panel, x, y - 1, clear_width, line_height + 2, background);
    if (!segment_text) {
        return;
    }

    while (*segment_text) {
        if (cursor_x + FONT_WIDTH * scale > x + clear_width) {
            break;
        }
        dashboard_draw_char(panel, cursor_x, y, *segment_text, color, scale);
        cursor_x += cell_width;
        segment_text++;
    }
}

static void dashboard_refresh_text_cells(dashboard_panel_t *panel,
        int y, const char *old_text, const char *new_text,
        uint16_t color, uint16_t background,
        uint8_t scale, bool center)
{
    size_t old_len = old_text ? strlen(old_text) : 0;
    size_t new_len = new_text ? strlen(new_text) : 0;
    size_t common = old_len < new_len ? old_len : new_len;

    if (old_len != new_len) {
        dashboard_draw_text_line(panel, y, new_text, color, background, scale, center);
        return;
    }

    for (size_t i = 0; i < common; i++) {
        if (old_text[i] != new_text[i]) {
            char segment[2] = { new_text[i], '\0' };

            dashboard_draw_text_segment(panel, y, new_text,
                    (int) i, 1, segment,
                    color, background, scale, center);
        }
    }
}

static int dashboard_build_month_to_int(const char *month)
{
    static const char *const s_months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };

    for (int i = 0; i < 12; i++) {
        if (strncmp(month, s_months[i], 3) == 0) {
            return i;
        }
    }
    return 0;
}

static time_t dashboard_build_time_seed(void)
{
    struct tm build_tm = {0};
    char month[4];
    int day;
    int year;
    int hour;
    int minute;
    int second;

    if (sscanf(__DATE__, "%3s %d %d", month, &day, &year) != 3) {
        return 0;
    }
    if (sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second) != 3) {
        return 0;
    }

    build_tm.tm_year = year - 1900;
    build_tm.tm_mon = dashboard_build_month_to_int(month);
    build_tm.tm_mday = day;
    build_tm.tm_hour = hour;
    build_tm.tm_min = minute;
    build_tm.tm_sec = second;
    build_tm.tm_isdst = -1;
    return mktime(&build_tm);
}

static void dashboard_update_time_cache(void)
{
    time_t now;
    time_t display_now;
    struct tm time_info;
    bool real_time_valid;
    bool time_was_synced;
    char time_text[LEFT_TIME_TEXT_LEN];

    time(&now);
    localtime_r(&now, &time_info);
    real_time_valid = time_info.tm_year >= (2024 - 1900);
    time_was_synced = s_state.time_synced;

    if (real_time_valid) {
        s_state.time_synced = true;
        display_now = now;
    } else if (s_state.fallback_time_seed > 0) {
        TickType_t elapsed_ticks = xTaskGetTickCount() - s_state.fallback_time_tick;

        s_state.time_synced = false;
        display_now = s_state.fallback_time_seed + (time_t) (elapsed_ticks / configTICK_RATE_HZ);
        localtime_r(&display_now, &time_info);
    } else {
        s_state.time_synced = false;
        if (strcmp(s_state.last_time_text, "----.--.-- --:--:--") != 0) {
            strcpy(s_state.last_time_text, "----.--.-- --:--:--");
            s_state.left_time_dirty = true;
        }
        dashboard_update_right_otp_cache(0, false);
        return;
    }

    {
        int year = time_info.tm_year + 1900;
        int month = time_info.tm_mon + 1;
        int day = time_info.tm_mday;
        int hour = time_info.tm_hour;
        int minute = time_info.tm_min;
        int second = time_info.tm_sec;

        if (year < 0 || year > 9999) {
            year = 0;
        }
        if (month < 0 || month > 99) {
            month = 0;
        }
        if (day < 0 || day > 99) {
            day = 0;
        }
        if (hour < 0 || hour > 99) {
            hour = 0;
        }
        if (minute < 0 || minute > 99) {
            minute = 0;
        }
        if (second < 0 || second > 99) {
            second = 0;
        }

        snprintf(time_text, sizeof(time_text), "%04d-%02d-%02d %02d:%02d:%02d",
                year, month, day, hour, minute, second);
    }
    if (strcmp(time_text, s_state.last_time_text) != 0) {
        strcpy(s_state.last_time_text, time_text);
        s_state.left_time_dirty = true;
    }
    dashboard_update_right_otp_cache(display_now, s_state.time_synced);
    if (!time_was_synced && s_state.time_synced) {
        s_state.last_weather_fetch_tick = 0;
        s_state.last_poem_fetch_tick = 0;
        s_state.left_detail_dirty = true;
        s_state.left_poem_dirty = true;
    }
}

static void dashboard_sanitize_ascii(char *text)
{
    char *read_ptr = text;
    char *write_ptr = text;

    while (*read_ptr) {
        unsigned char ch = (unsigned char) *read_ptr;

        if (ch == '\r' || ch == '\n') {
            read_ptr++;
            continue;
        }
        if (ch >= 32 && ch <= 126) {
            *write_ptr++ = (char) ch;
            read_ptr++;
        } else {
            if (write_ptr == text || write_ptr[-1] != ' ') {
                *write_ptr++ = ' ';
            }
            if ((ch & 0xE0) == 0xC0) {
                read_ptr += 2;
            } else if ((ch & 0xF0) == 0xE0) {
                read_ptr += 3;
            } else if ((ch & 0xF8) == 0xF0) {
                read_ptr += 4;
            } else {
                read_ptr++;
            }
        }
    }
    *write_ptr = '\0';

    while (write_ptr > text && write_ptr[-1] == ' ') {
        *--write_ptr = '\0';
    }
}

static void dashboard_compact_spaces(char *text)
{
    char *read_ptr = text;
    char *write_ptr = text;
    bool last_was_space = true;

    while (*read_ptr) {
        char ch = *read_ptr++;

        if (ch == ' ') {
            if (last_was_space) {
                continue;
            }
            last_was_space = true;
        } else {
            last_was_space = false;
        }
        *write_ptr++ = ch;
    }

    if (write_ptr > text && write_ptr[-1] == ' ') {
        write_ptr--;
    }
    *write_ptr = '\0';
}

static size_t dashboard_left_line_char_limit(uint8_t scale)
{
    int panel_width = dashboard_panel_visible_width(&s_left_panel);
    int cell_width = (FONT_WIDTH + 1) * scale;

    if (cell_width <= 0 || panel_width <= 12) {
        return 1;
    }
    return (size_t) ((panel_width - 12) / cell_width);
}

static void dashboard_fit_text_for_line(char *text, size_t max_chars)
{
    static const char *delimiters = " ,.;:!?-/";
    size_t length;
    size_t cut = 0;

    if (!text || max_chars == 0) {
        if (text) {
            text[0] = '\0';
        }
        return;
    }

    dashboard_compact_spaces(text);
    length = strlen(text);
    if (length <= max_chars) {
        return;
    }

    for (size_t i = max_chars; i > 0; i--) {
        if (strchr(delimiters, text[i - 1])) {
            cut = i;
            break;
        }
    }
    if (cut == 0) {
        cut = max_chars;
    }
    text[cut] = '\0';
    dashboard_compact_spaces(text);
}

typedef struct {
    const char *name;
    const char *label;
    const char *latitude;
    const char *longitude;
} weather_city_t;

static const weather_city_t s_weather_cities[] = {
    { "shanghai", "SHANGHAI", "31.23", "121.47" },
    { "beijing", "BEIJING", "39.90", "116.40" },
    { "shenzhen", "SHENZHEN", "22.54", "114.06" },
    { "guangzhou", "GUANGZHOU", "23.13", "113.27" },
};

static const weather_city_t *dashboard_select_weather_city(void)
{
    const weather_city_t *city = &s_weather_cities[0];

    for (size_t i = 0; i < sizeof(s_weather_cities) / sizeof(s_weather_cities[0]); i++) {
        if (dashboard_ascii_strcasecmp(CONFIG_HOMEKIT_DASHBOARD_WEATHER_LOCATION,
                s_weather_cities[i].name) == 0) {
            city = &s_weather_cities[i];
            break;
        }
    }
    return city;
}

static void dashboard_normalize_location_label(char *out, size_t out_size,
        const char *src, const char *fallback)
{
    size_t pos = 0;
    bool last_was_space = true;

    if (!out || out_size == 0) {
        return;
    }

    if (!src || src[0] == '\0') {
        strncpy(out, fallback, out_size);
        out[out_size - 1] = '\0';
        return;
    }

    while (*src && pos + 1 < out_size) {
        char ch = *src++;

        if (ch >= 'a' && ch <= 'z') {
            ch = (char) (ch - ('a' - 'A'));
        } else if (ch == '_' || ch == ',') {
            ch = ' ';
        }

        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            out[pos++] = ch;
            last_was_space = false;
        } else if (!last_was_space && ch == ' ') {
            out[pos++] = ' ';
            last_was_space = true;
        }
    }

    if (pos > 0 && out[pos - 1] == ' ') {
        pos--;
    }
    if (pos == 0) {
        strncpy(out, fallback, out_size);
        out[out_size - 1] = '\0';
        return;
    }
    out[pos] = '\0';
}

static void dashboard_build_location_label(char *out, size_t out_size)
{
    const weather_city_t *default_city = dashboard_select_weather_city();
    const char *src = CONFIG_HOMEKIT_DASHBOARD_WEATHER_LOCATION;
    const char *fallback = src && src[0] != '\0' ? default_city->label : "LOCATING";

    dashboard_normalize_location_label(out, out_size, src, fallback);
}

static int dashboard_ascii_strcasecmp(const char *left, const char *right)
{
    while (*left && *right) {
        char a = *left++;
        char b = *right++;

        if (a >= 'a' && a <= 'z') {
            a = (char) (a - ('a' - 'A'));
        }
        if (b >= 'a' && b <= 'z') {
            b = (char) (b - ('a' - 'A'));
        }
        if (a != b) {
            return (int) ((unsigned char) a - (unsigned char) b);
        }
    }
    return (int) ((unsigned char) *left - (unsigned char) *right);
}

static int dashboard_base32_value(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a';
    }
    if (ch >= '2' && ch <= '7') {
        return 26 + (ch - '2');
    }
    return -1;
}

static bool dashboard_totp_decode_secret(uint8_t *out, size_t out_size, size_t *out_len)
{
    static bool s_loaded;
    static bool s_valid;
    static uint8_t s_secret[64];
    static size_t s_secret_len;
    uint32_t buffer = 0;
    int bits_left = 0;

    if (!out || out_size == 0 || !out_len) {
        return false;
    }

    if (!s_loaded) {
        const char *cursor = CONFIG_HOMEKIT_DASHBOARD_TOTP_SECRET;

        s_loaded = true;
        s_valid = false;
        s_secret_len = 0;
        if (cursor && cursor[0] != '\0') {
            while (*cursor) {
                int value;
                char ch = *cursor++;

                if (ch == ' ' || ch == '-' || ch == '=') {
                    continue;
                }

                value = dashboard_base32_value(ch);
                if (value < 0) {
                    s_secret_len = 0;
                    break;
                }

                buffer = (buffer << 5) | (uint32_t) value;
                bits_left += 5;
                while (bits_left >= 8) {
                    if (s_secret_len >= sizeof(s_secret)) {
                        s_secret_len = 0;
                        bits_left = 0;
                        break;
                    }
                    bits_left -= 8;
                    s_secret[s_secret_len++] = (uint8_t) ((buffer >> bits_left) & 0xFFU);
                }
                if (s_secret_len == 0 && bits_left == 0 && *cursor != '\0') {
                    break;
                }
            }
            s_valid = s_secret_len > 0;
        }
    }

    if (!s_valid || s_secret_len > out_size) {
        return false;
    }

    memcpy(out, s_secret, s_secret_len);
    *out_len = s_secret_len;
    return true;
}

static bool dashboard_totp_generate(time_t now, char *out, size_t out_size, int *remaining_out)
{
    uint8_t secret[64];
    size_t secret_len = 0;
    unsigned char hmac[20];
    unsigned char counter[8];
    const mbedtls_md_info_t *md_info;
    uint64_t timestep;
    uint32_t binary;
    uint32_t otp;
    size_t offset;

    if (!out || out_size == 0 || !remaining_out) {
        return false;
    }
    if (!dashboard_totp_decode_secret(secret, sizeof(secret), &secret_len)) {
        return false;
    }
    if (now <= 0) {
        return false;
    }

    *remaining_out = TOTP_PERIOD_SEC - (int) (now % TOTP_PERIOD_SEC);
    if (*remaining_out <= 0 || *remaining_out > TOTP_PERIOD_SEC) {
        *remaining_out = TOTP_PERIOD_SEC;
    }

    timestep = (uint64_t) now / TOTP_PERIOD_SEC;
    for (int i = 7; i >= 0; i--) {
        counter[i] = (unsigned char) (timestep & 0xFFU);
        timestep >>= 8;
    }

    md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (!md_info ||
            mbedtls_md_hmac(md_info, secret, secret_len, counter, sizeof(counter), hmac) != 0) {
        return false;
    }

    offset = hmac[19] & 0x0FU;
    binary = ((uint32_t) (hmac[offset] & 0x7F) << 24) |
            ((uint32_t) hmac[offset + 1] << 16) |
            ((uint32_t) hmac[offset + 2] << 8) |
            (uint32_t) hmac[offset + 3];
    otp = binary % 1000000U;

    snprintf(out, out_size, "%06" PRIu32, otp);
    out[out_size - 1] = '\0';
    return true;
}

static void dashboard_update_right_otp_cache(time_t now, bool time_synced)
{
    char next_text[sizeof(s_state.right_otp_text)];
    int next_remaining = TOTP_PERIOD_SEC;

    if (!time_synced) {
        strncpy(next_text, "NO TIME", sizeof(next_text));
        next_text[sizeof(next_text) - 1] = '\0';
        next_remaining = 0;
    } else if (!dashboard_totp_generate(now, next_text, sizeof(next_text), &next_remaining)) {
        strncpy(next_text, "NO KEY", sizeof(next_text));
        next_text[sizeof(next_text) - 1] = '\0';
        next_remaining = 0;
    }

    if (strcmp(next_text, s_state.right_otp_text) != 0 ||
            next_remaining != s_state.right_otp_remaining) {
        strcpy(s_state.right_otp_text, next_text);
        s_state.right_otp_remaining = next_remaining;
        s_state.right_dirty = true;
    }
}

static bool dashboard_json_extract_number(const char *json, const char *key,
        char *out, size_t out_size)
{
    const char *cursor;
    size_t pos = 0;
    bool saw_digit = false;

    if (!json || !key || !out || out_size == 0) {
        return false;
    }

    cursor = strstr(json, key);
    if (!cursor) {
        return false;
    }

    cursor = strchr(cursor, ':');
    if (!cursor) {
        return false;
    }
    cursor++;

    while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }
    if (*cursor == '-' || *cursor == '+') {
        if (pos + 1 >= out_size) {
            return false;
        }
        out[pos++] = *cursor++;
    }
    while ((*cursor >= '0' && *cursor <= '9') || *cursor == '.') {
        if (pos + 1 >= out_size) {
            break;
        }
        out[pos++] = *cursor++;
        saw_digit = true;
    }

    out[pos] = '\0';
    return saw_digit;
}

static bool dashboard_json_extract_string(const char *json, const char *key,
        char *out, size_t out_size)
{
    const char *cursor;
    size_t pos = 0;

    if (!json || !key || !out || out_size == 0) {
        return false;
    }

    cursor = strstr(json, key);
    if (!cursor) {
        return false;
    }

    cursor = strchr(cursor, ':');
    if (!cursor) {
        return false;
    }
    cursor++;

    while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }
    if (*cursor != '"') {
        return false;
    }
    cursor++;

    while (*cursor && *cursor != '"' && pos + 1 < out_size) {
        char ch = *cursor++;

        if (ch == '\\' && *cursor) {
            ch = *cursor++;
        }
        if ((unsigned char) ch < 32) {
            continue;
        }
        out[pos++] = ch;
    }

    out[pos] = '\0';
    return pos > 0;
}

static bool dashboard_codepoint_renderable(uint16_t codepoint)
{
    if (codepoint < 0x80) {
        return codepoint >= 32 && codepoint <= 126;
    }
    return dashboard_find_cjk16_glyph(codepoint) != NULL;
}

static const char *dashboard_poem_pinyin_lookup(uint16_t codepoint)
{
    size_t left = 0;
    size_t right = sizeof(s_poem_pinyin_entries) / sizeof(s_poem_pinyin_entries[0]);

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        uint16_t mid_codepoint = s_poem_pinyin_entries[mid].codepoint;

        if (mid_codepoint == codepoint) {
            return s_poem_pinyin_syllables[s_poem_pinyin_entries[mid].syllable_index];
        }
        if (mid_codepoint < codepoint) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    return NULL;
}

static bool dashboard_poem_codepoint_is_separator(uint16_t codepoint)
{
    switch (codepoint) {
    case ' ':
    case ',':
    case '.':
    case ';':
    case ':':
    case '!':
    case '?':
    case '-':
    case '/':
    case 0x3000: /* ideographic space */
    case 0x3001: /* 、 */
    case 0x3002: /* 。 */
    case 0x300A: /* 《 */
    case 0x300B: /* 》 */
    case 0xFF01: /* ！ */
    case 0xFF08: /* （ */
    case 0xFF09: /* ） */
    case 0xFF0C: /* ， */
    case 0xFF1A: /* ： */
    case 0xFF1B: /* ； */
    case 0xFF1F: /* ？ */
    case 0x2014: /* — */
    case 0x2018: /* ‘ */
    case 0x2019: /* ’ */
    case 0x201C: /* “ */
    case 0x201D: /* ” */
        return true;
    default:
        return false;
    }
}

static void dashboard_append_poem_ascii(char *out, size_t out_size,
        size_t *pos, bool *last_was_space, const char *text)
{
    while (text && *text && *pos + 1 < out_size) {
        char ch = *text++;

        if (ch == ' ') {
            if (*last_was_space) {
                continue;
            }
            *last_was_space = true;
        } else {
            *last_was_space = false;
        }
        out[(*pos)++] = ch;
    }
    out[*pos] = '\0';
}

static bool dashboard_build_poem_pinyin(char *out, size_t out_size, const char *raw_text)
{
    const char *cursor = raw_text;
    size_t pos = 0;
    bool last_was_space = true;
    bool wrote_content = false;

    if (!out || out_size == 0) {
        return false;
    }

    out[0] = '\0';
    while (cursor && *cursor && *cursor != '\r' && *cursor != '\n') {
        uint16_t codepoint;

        if (!dashboard_utf8_next_codepoint(&cursor, &codepoint)) {
            break;
        }

        if (codepoint < 0x80) {
            char ascii_text[2] = {0};

            if ((codepoint >= '0' && codepoint <= '9') ||
                    (codepoint >= 'A' && codepoint <= 'Z') ||
                    (codepoint >= 'a' && codepoint <= 'z')) {
                ascii_text[0] = (char) codepoint;
                dashboard_append_poem_ascii(out, out_size, &pos, &last_was_space, ascii_text);
                wrote_content = true;
            } else if (dashboard_poem_codepoint_is_separator(codepoint)) {
                dashboard_append_poem_ascii(out, out_size, &pos, &last_was_space, " ");
            }
            continue;
        }

        if (dashboard_poem_codepoint_is_separator(codepoint)) {
            dashboard_append_poem_ascii(out, out_size, &pos, &last_was_space, " ");
            continue;
        }

        {
            const char *pinyin = dashboard_poem_pinyin_lookup(codepoint);

            if (pinyin && pinyin[0] != '\0') {
                dashboard_append_poem_ascii(out, out_size, &pos, &last_was_space, pinyin);
                dashboard_append_poem_ascii(out, out_size, &pos, &last_was_space, " ");
            } else {
                dashboard_append_poem_ascii(out, out_size, &pos, &last_was_space, "? ");
            }
            wrote_content = true;
        }
    }

    dashboard_compact_spaces(out);
    return wrote_content && out[0] != '\0';
}

static bool dashboard_prepare_poem_pinyin_text(char *out, size_t out_size,
        const char *raw_text, int available_width)
{
    char pinyin_text[LEFT_POEM_PINYIN_LEN];
    size_t ascii_limit;

    if (!out || out_size == 0 || available_width <= 0) {
        return false;
    }
    if (!dashboard_build_poem_pinyin(pinyin_text, sizeof(pinyin_text), raw_text)) {
        return false;
    }

    ascii_limit = (size_t) (available_width / (FONT_WIDTH + 1));
    if (ascii_limit == 0) {
        return false;
    }

    dashboard_fit_text_for_line(pinyin_text, ascii_limit);
    if (pinyin_text[0] == '\0') {
        return false;
    }

    strncpy(out, pinyin_text, out_size);
    out[out_size - 1] = '\0';
    return true;
}

static const char *dashboard_weather_code_text(int code)
{
    if (code == 0) {
        return "CLEAR";
    }
    if (code == 1) {
        return "MOSTLY CLEAR";
    }
    if (code == 2) {
        return "PARTLY CLOUDY";
    }
    if (code == 3) {
        return "CLOUDY";
    }
    if (code == 45 || code == 48) {
        return "FOG";
    }
    if (code >= 51 && code <= 57) {
        return "DRIZZLE";
    }
    if (code >= 61 && code <= 67) {
        return "RAIN";
    }
    if (code >= 71 && code <= 77) {
        return "SNOW";
    }
    if (code >= 80 && code <= 82) {
        return "SHOWERS";
    }
    if (code >= 85 && code <= 86) {
        return "SNOW SHOWERS";
    }
    if (code >= 95) {
        return "THUNDER";
    }
    return "WEATHER";
}

static void dashboard_extract_supported_poem_fragment(char *out, size_t out_size,
        const char *raw_text, int available_width)
{
    char best_text[LEFT_POEM_TEXT_LEN];
    char current_text[LEFT_POEM_TEXT_LEN];
    size_t best_len = 0;
    size_t current_len = 0;
    int current_width = 0;
    const char *cursor = raw_text;

    if (!out || out_size == 0) {
        return;
    }

    best_text[0] = '\0';
    current_text[0] = '\0';

    while (cursor && *cursor && *cursor != '\r' && *cursor != '\n') {
        const char *char_start = cursor;
        uint16_t codepoint;
        size_t utf8_len;
        int glyph_width;

        if (!dashboard_utf8_next_codepoint(&cursor, &codepoint)) {
            break;
        }
        utf8_len = (size_t) (cursor - char_start);

        if (!dashboard_codepoint_renderable(codepoint)) {
            if (current_len > best_len) {
                memcpy(best_text, current_text, current_len + 1);
                best_len = current_len;
            }
            current_len = 0;
            current_width = 0;
            current_text[0] = '\0';
            continue;
        }

        glyph_width = dashboard_poem_codepoint_width(codepoint);
        if (current_width + glyph_width > available_width ||
                current_len + utf8_len >= sizeof(current_text)) {
            if (current_len > best_len) {
                memcpy(best_text, current_text, current_len + 1);
                best_len = current_len;
            }
            current_len = 0;
            current_width = 0;
            current_text[0] = '\0';
            if (glyph_width > available_width || utf8_len >= sizeof(current_text)) {
                continue;
            }
        }

        memcpy(&current_text[current_len], char_start, utf8_len);
        current_len += utf8_len;
        current_text[current_len] = '\0';
        current_width += glyph_width;
    }

    if (current_len > best_len) {
        memcpy(best_text, current_text, current_len + 1);
        best_len = current_len;
    }

    if (best_len > 0) {
        strncpy(out, best_text, out_size);
        out[out_size - 1] = '\0';
    } else {
        out[0] = '\0';
    }
}

static esp_err_t dashboard_http_get_text(const char *url, int timeout_ms,
        char *out, size_t out_size)
{
    esp_http_client_config_t http_config = {
        .timeout_ms = timeout_ms,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .url = url,
    };
    esp_http_client_handle_t client;
    esp_err_t err;
    int read_len;

    if (!out || out_size == 0 || !url || url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    client = esp_http_client_init(&http_config);
    if (!client) {
        return ESP_FAIL;
    }

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    (void) esp_http_client_fetch_headers(client);
    read_len = esp_http_client_read_response(client, out, (int) out_size - 1);
    if (read_len < 0) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    out[read_len] = '\0';

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_OK;
}

static esp_err_t dashboard_fetch_weather_open_meteo_at(const char *latitude,
        const char *longitude, char *out, size_t out_size)
{
    const char *current_section;
    char url[192];
    char response[384];
    char temp_text[16];
    char code_text[16];
    int weather_code;

    snprintf(url, sizeof(url),
            "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
            "&current=temperature_2m,weather_code&timezone=Asia%%2FShanghai",
            latitude, longitude);

    if (dashboard_http_get_text(url, 6000, response, sizeof(response)) != ESP_OK) {
        return ESP_FAIL;
    }
    current_section = strstr(response, "\"current\":");
    if (!current_section) {
        return ESP_FAIL;
    }
    if (!dashboard_json_extract_number(current_section, "\"temperature_2m\"",
            temp_text, sizeof(temp_text))) {
        return ESP_FAIL;
    }
    if (!dashboard_json_extract_number(current_section, "\"weather_code\"",
            code_text, sizeof(code_text))) {
        return ESP_FAIL;
    }

    weather_code = (int) strtol(code_text, NULL, 10);
    snprintf(out, out_size, "%sC %s", temp_text, dashboard_weather_code_text(weather_code));
    out[out_size - 1] = '\0';
    dashboard_compact_spaces(out);
    return ESP_OK;
}

static esp_err_t dashboard_fetch_weather_open_meteo(char *out, size_t out_size)
{
    const weather_city_t *city = dashboard_select_weather_city();

    return dashboard_fetch_weather_open_meteo_at(city->latitude, city->longitude, out, out_size);
}

static esp_err_t dashboard_fetch_weather_location_ipwhois(char *location_out,
        size_t location_out_size, char *latitude_out, size_t latitude_out_size,
        char *longitude_out, size_t longitude_out_size)
{
    char response[1024];
    char city[32];

    if (!location_out || location_out_size == 0 ||
            !latitude_out || latitude_out_size == 0 ||
            !longitude_out || longitude_out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (dashboard_http_get_text("https://ipwho.is/", 5000, response, sizeof(response)) != ESP_OK) {
        return ESP_FAIL;
    }
    if (!strstr(response, "\"success\": true") && !strstr(response, "\"success\":true")) {
        return ESP_FAIL;
    }
    if (!dashboard_json_extract_string(response, "\"city\"", city, sizeof(city)) ||
            !dashboard_json_extract_number(response, "\"latitude\"",
                    latitude_out, latitude_out_size) ||
            !dashboard_json_extract_number(response, "\"longitude\"",
                    longitude_out, longitude_out_size)) {
        return ESP_FAIL;
    }

    dashboard_normalize_location_label(location_out, location_out_size, city, "LOCAL");
    return ESP_OK;
}

static esp_err_t dashboard_fetch_weather_wttr_text(const char *location,
        char *out, size_t out_size)
{
    char url[160];

    if (location && location[0] != '\0') {
        snprintf(url, sizeof(url),
                "https://wttr.in/%s?format=%%25t%%20%%25C",
                location);
    } else {
        strncpy(url, "https://wttr.in/?format=%25t%20%25C", sizeof(url));
        url[sizeof(url) - 1] = '\0';
    }

    if (dashboard_http_get_text(url, 5000, out, out_size) == ESP_OK) {
        dashboard_sanitize_ascii(out);
        dashboard_compact_spaces(out);
        if (out[0] != '\0') {
            return ESP_OK;
        }
    }

    if (location && location[0] != '\0') {
        snprintf(url, sizeof(url),
                "https://w.r2049.cn/en-wttr?location=%s",
                location);
    } else {
        strncpy(url, "https://w.r2049.cn/en-wttr", sizeof(url));
        url[sizeof(url) - 1] = '\0';
    }

    if (dashboard_http_get_text(url, 5000, out, out_size) == ESP_OK) {
        char *line_end = strchr(out, '\n');

        if (line_end) {
            *line_end = '\0';
        }
        dashboard_sanitize_ascii(out);
        dashboard_compact_spaces(out);
        if (out[0] != '\0') {
            return ESP_OK;
        }
    }

    return ESP_FAIL;
}

static esp_err_t dashboard_fetch_weather(char *location_out, size_t location_out_size,
        char *weather_out, size_t weather_out_size)
{
    char latitude[24];
    char longitude[24];

    if (!location_out || location_out_size == 0 || !weather_out || weather_out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (CONFIG_HOMEKIT_DASHBOARD_WEATHER_LOCATION[0] != '\0') {
        dashboard_build_location_label(location_out, location_out_size);

        if (dashboard_fetch_weather_open_meteo(weather_out, weather_out_size) == ESP_OK) {
            return ESP_OK;
        }
        if (dashboard_fetch_weather_wttr_text(CONFIG_HOMEKIT_DASHBOARD_WEATHER_LOCATION,
                weather_out, weather_out_size) == ESP_OK) {
            return ESP_OK;
        }
    } else {
        if (dashboard_fetch_weather_location_ipwhois(location_out, location_out_size,
                latitude, sizeof(latitude), longitude, sizeof(longitude)) == ESP_OK &&
                dashboard_fetch_weather_open_meteo_at(latitude, longitude,
                        weather_out, weather_out_size) == ESP_OK) {
            return ESP_OK;
        }

        dashboard_build_location_label(location_out, location_out_size);
        if (dashboard_fetch_weather_wttr_text(NULL, weather_out, weather_out_size) == ESP_OK) {
            return ESP_OK;
        }
        if (dashboard_fetch_weather_open_meteo(weather_out, weather_out_size) == ESP_OK) {
            return ESP_OK;
        }
    }

    strncpy(weather_out, "WEATHER --", weather_out_size);
    weather_out[weather_out_size - 1] = '\0';
    return ESP_OK;
}

static void dashboard_select_fallback_poem(char *out, size_t out_size, size_t index)
{
    static const char *const s_poem_fallbacks[] = {
        "春眠不觉晓",
        "明月松间照",
        "山气日夕佳",
        "夜半钟声到",
        "江清月近人",
    };

    if (!out || out_size == 0) {
        return;
    }

    strncpy(out,
            s_poem_fallbacks[index % (sizeof(s_poem_fallbacks) / sizeof(s_poem_fallbacks[0]))],
            out_size);
    out[out_size - 1] = '\0';
}

static void dashboard_prepare_poem_text(char *out, size_t out_size,
        const char *raw_text, size_t fallback_index)
{
    int available_width = dashboard_panel_visible_width(&s_left_panel) - 12;
    char raw_line[LEFT_POEM_TEXT_LEN];
    size_t pos = 0;
    const char *cursor = raw_text;

    if (!out || out_size == 0) {
        return;
    }

    raw_line[0] = '\0';
    if (cursor) {
        while (*cursor && *cursor != '\r' && *cursor != '\n' &&
                pos + 1 < sizeof(raw_line)) {
            raw_line[pos++] = *cursor++;
        }
        raw_line[pos] = '\0';
    }

    if (raw_line[0] != '\0' &&
            dashboard_poem_text_supported(raw_line) &&
            dashboard_measure_poem_text(raw_line) <= available_width) {
        strncpy(out, raw_line, out_size);
        out[out_size - 1] = '\0';
        return;
    }

    if (raw_line[0] != '\0' &&
            dashboard_prepare_poem_pinyin_text(out, out_size, raw_line, available_width)) {
        return;
    }

    dashboard_extract_supported_poem_fragment(out, out_size, raw_line, available_width);
    if (out[0] == '\0') {
        dashboard_select_fallback_poem(out, out_size, fallback_index);
    }
}

static esp_err_t dashboard_fetch_poem(char *out, size_t out_size, size_t fallback_index)
{
    char raw_text[96];
    esp_err_t err;

    err = dashboard_http_get_text("https://v1.jinrishici.com/all.txt", 8000,
            raw_text, sizeof(raw_text));
    if (err != ESP_OK) {
        dashboard_select_fallback_poem(out, out_size, fallback_index);
        return ESP_OK;
    }

    dashboard_prepare_poem_text(out, out_size, raw_text, fallback_index);
    return ESP_OK;
}

static int dashboard_left_header_height(void)
{
    return FONT_HEIGHT * LEFT_TIME_SCALE + 10;
}

static int dashboard_left_time_y(void)
{
    return 4;
}

static int dashboard_left_detail_y(void)
{
    return dashboard_left_header_height() + 4;
}

static int dashboard_left_poem_band_y(void)
{
    int panel_height = dashboard_panel_visible_height(&s_left_panel);
    int band_height = CJK_GLYPH_HEIGHT + 6;

    return panel_height - band_height;
}

static int dashboard_left_poem_y(void)
{
    return dashboard_left_poem_band_y() + 3;
}

static void dashboard_format_left_detail(char *out, size_t out_size,
        const char *location_text, const char *weather_text, bool wifi_connected)
{
    char summary[sizeof(s_state.weather_text)];
    size_t line_limit = dashboard_left_line_char_limit(LEFT_DETAIL_SCALE);
    const char *location = (location_text && location_text[0] != '\0') ? location_text : "LOCATING";

    if (!wifi_connected) {
        strncpy(summary, "OFFLINE", sizeof(summary));
        summary[sizeof(summary) - 1] = '\0';
    } else if (weather_text && weather_text[0] != '\0') {
        strncpy(summary, weather_text, sizeof(summary));
        summary[sizeof(summary) - 1] = '\0';
    } else {
        strncpy(summary, "WEATHER --", sizeof(summary));
        summary[sizeof(summary) - 1] = '\0';
    }

    snprintf(out, out_size, "%s %s", location, summary);
    out[out_size - 1] = '\0';
    dashboard_fit_text_for_line(out, line_limit);
}

static void dashboard_redraw_left_frame(void)
{
    int panel_width = dashboard_panel_visible_width(&s_left_panel);
    int panel_height = dashboard_panel_visible_height(&s_left_panel);
    int poem_band_y = dashboard_left_poem_band_y();

    dashboard_panel_fill_rect(&s_left_panel, 0, 0, panel_width, panel_height, s_left_background);
    dashboard_panel_fill_rect(&s_left_panel, 0, 0, panel_width,
            dashboard_left_header_height(), s_left_header);
    dashboard_panel_fill_rect(&s_left_panel, 0, poem_band_y, panel_width,
            panel_height - poem_band_y, s_left_poem_background);
}

static void dashboard_redraw_left_time_full(const char *time_text)
{
    dashboard_draw_text_line(&s_left_panel, dashboard_left_time_y(), time_text,
            s_left_text, s_left_header, LEFT_TIME_SCALE, false);
    strcpy(s_left_rendered_time, time_text);
}

static void dashboard_refresh_left_time(const char *time_text)
{
    dashboard_refresh_text_cells(&s_left_panel, dashboard_left_time_y(),
            s_left_rendered_time, time_text,
            s_left_text, s_left_header, LEFT_TIME_SCALE, false);
    strcpy(s_left_rendered_time, time_text);
}

static void dashboard_redraw_left_detail(const char *location_text,
        const char *weather_text, bool wifi_connected)
{
    char detail_text[LEFT_DETAIL_TEXT_LEN];

    dashboard_format_left_detail(detail_text, sizeof(detail_text),
            location_text, weather_text, wifi_connected);
    dashboard_draw_text_line(&s_left_panel, dashboard_left_detail_y(), detail_text,
            wifi_connected ? s_left_detail_text : s_left_secondary,
            s_left_background, LEFT_DETAIL_SCALE, false);
}

static void dashboard_redraw_left_poem(const char *poem_text)
{
    char visible_text[LEFT_POEM_TEXT_LEN];

    dashboard_prepare_poem_text(visible_text, sizeof(visible_text), poem_text, 0);
    dashboard_draw_poem_line(&s_left_panel, dashboard_left_poem_y(), visible_text,
            s_left_poem_text, s_left_poem_background);
}

static void dashboard_redraw_left(const char *time_text, const char *location_text,
        const char *weather_text, const char *poem_text, bool wifi_connected)
{
    dashboard_redraw_left_frame();
    dashboard_redraw_left_time_full(time_text);
    dashboard_redraw_left_detail(location_text, weather_text, wifi_connected);
    dashboard_redraw_left_poem(poem_text);
}

static void dashboard_redraw_right(const char *otp_text, int otp_remaining,
        const bool button_states[3])
{
    int panel_width = dashboard_panel_visible_width(&s_right_panel);
    int panel_height = dashboard_panel_visible_height(&s_right_panel);
    int row_gap = panel_width > panel_height ? 4 : 6;
    int row_height = (panel_height - row_gap * 4) / 3;
    int row1_y = row_gap;
    int row2_y = row1_y + row_height + row_gap;
    int row3_y = row2_y + row_height + row_gap;
    char row1_text[24];

    if (row_height < FONT_HEIGHT + 6) {
        row_height = FONT_HEIGHT + 6;
    }

    dashboard_panel_fill_rect(&s_right_panel, 0, 0, panel_width, panel_height, s_right_background);
    dashboard_panel_fill_rect(&s_right_panel, 6, row1_y, panel_width - 12, row_height, s_right_header);
    dashboard_panel_fill_rect(&s_right_panel, 6, row2_y, panel_width - 12, row_height,
            button_states[1] ? s_right_on : s_right_off);
    dashboard_panel_fill_rect(&s_right_panel, 6, row3_y, panel_width - 12, row_height,
            button_states[2] ? s_right_on : s_right_off);

    if (otp_remaining > 0) {
        snprintf(row1_text, sizeof(row1_text), "%s %02dS", otp_text, otp_remaining);
    } else {
        snprintf(row1_text, sizeof(row1_text), "%s", otp_text);
    }
    row1_text[sizeof(row1_text) - 1] = '\0';
    dashboard_draw_text_line(&s_right_panel,
            row1_y + ((row_height - FONT_HEIGHT * RIGHT_BODY_SCALE) / 2),
            row1_text, s_right_text, s_right_header,
            panel_width > 120 ? RIGHT_BODY_SCALE : 1, true);
}

static void dashboard_render_if_needed(void)
{
    char time_text[sizeof(s_state.last_time_text)];
    char location_text[sizeof(s_state.location_text)];
    char weather_text[sizeof(s_state.weather_text)];
    char poem_text[sizeof(s_state.poem_text)];
    char otp_text[sizeof(s_state.right_otp_text)];
    bool left_dirty;
    bool left_time_dirty;
    bool left_detail_dirty;
    bool left_poem_dirty;
    bool right_dirty;
    bool wifi_connected;
    bool buttons[3];
    int otp_remaining;

    xSemaphoreTake(s_state.lock, portMAX_DELAY);
    left_dirty = s_state.left_dirty;
    left_time_dirty = s_state.left_time_dirty;
    left_detail_dirty = s_state.left_detail_dirty;
    left_poem_dirty = s_state.left_poem_dirty;
    right_dirty = s_state.right_dirty;
    strcpy(time_text, s_state.last_time_text);
    strcpy(location_text, s_state.location_text);
    strcpy(weather_text, s_state.weather_text);
    strcpy(poem_text, s_state.poem_text);
    strcpy(otp_text, s_state.right_otp_text);
    wifi_connected = s_state.wifi_connected;
    memcpy(buttons, s_state.button_states, sizeof(buttons));
    otp_remaining = s_state.right_otp_remaining;
    s_state.left_dirty = false;
    s_state.left_time_dirty = false;
    s_state.left_detail_dirty = false;
    s_state.left_poem_dirty = false;
    s_state.right_dirty = false;
    xSemaphoreGive(s_state.lock);

    if (left_dirty && s_state.left_available) {
        dashboard_redraw_left(time_text, location_text, weather_text, poem_text, wifi_connected);
    } else if (s_state.left_available) {
        if (left_time_dirty) {
            dashboard_refresh_left_time(time_text);
        }
        if (left_detail_dirty) {
            dashboard_redraw_left_detail(location_text, weather_text, wifi_connected);
        }
        if (left_poem_dirty) {
            dashboard_redraw_left_poem(poem_text);
        }
    }
    if (right_dirty && s_state.right_available) {
        dashboard_redraw_right(otp_text, otp_remaining, buttons);
    }
}

static void dashboard_maybe_refresh_weather(void)
{
    TickType_t now_ticks = xTaskGetTickCount();
    bool should_fetch = false;
    char location_text[sizeof(s_state.location_text)];
    char weather_text[sizeof(s_state.weather_text)];

    xSemaphoreTake(s_state.lock, portMAX_DELAY);
    if (s_state.wifi_connected &&
            s_state.time_synced &&
            (s_state.last_weather_fetch_tick == 0 ||
            (now_ticks - s_state.last_weather_fetch_tick) >=
                    pdMS_TO_TICKS(CONFIG_HOMEKIT_DASHBOARD_WEATHER_REFRESH_SEC * 1000))) {
        should_fetch = true;
    }
    xSemaphoreGive(s_state.lock);

    if (!should_fetch) {
        return;
    }

    if (dashboard_fetch_weather(location_text, sizeof(location_text),
            weather_text, sizeof(weather_text)) == ESP_OK) {
        xSemaphoreTake(s_state.lock, portMAX_DELAY);
        s_state.last_weather_fetch_tick = now_ticks;
        if (strcmp(location_text, s_state.location_text) != 0) {
            strcpy(s_state.location_text, location_text);
            s_state.left_detail_dirty = true;
        }
        if (strcmp(weather_text, s_state.weather_text) != 0) {
            strcpy(s_state.weather_text, weather_text);
            s_state.left_detail_dirty = true;
        }
        xSemaphoreGive(s_state.lock);
    } else {
        ESP_LOGW(TAG, "Weather fetch failed");
    }
}

static void dashboard_maybe_refresh_poem(void)
{
    TickType_t now_ticks = xTaskGetTickCount();
    bool should_fetch = false;
    bool force_refresh = false;
    bool can_fetch_online = false;
    bool refresh_ok = false;
    char poem_text[sizeof(s_state.poem_text)];
    size_t fallback_index = 0;

    xSemaphoreTake(s_state.lock, portMAX_DELAY);
    fallback_index = s_state.poem_refresh_serial;
    can_fetch_online = s_state.wifi_connected && s_state.time_synced;
    force_refresh = s_state.poem_refresh_requested;
    if (force_refresh) {
        should_fetch = true;
        s_state.poem_refresh_requested = false;
    } else if (can_fetch_online &&
            (s_state.last_poem_fetch_tick == 0 ||
            (now_ticks - s_state.last_poem_fetch_tick) >=
                    pdMS_TO_TICKS(POEM_REFRESH_SEC * 1000))) {
        should_fetch = true;
    }
    xSemaphoreGive(s_state.lock);

    if (!should_fetch) {
        return;
    }

    if (can_fetch_online) {
        refresh_ok = dashboard_fetch_poem(poem_text, sizeof(poem_text), fallback_index) == ESP_OK;
    }
    if (!refresh_ok) {
        dashboard_select_fallback_poem(poem_text, sizeof(poem_text), fallback_index);
        refresh_ok = true;
    }

    if (refresh_ok) {
        xSemaphoreTake(s_state.lock, portMAX_DELAY);
        s_state.last_poem_fetch_tick = now_ticks;
        s_state.poem_refresh_serial++;
        if (strcmp(poem_text, s_state.poem_text) != 0 || force_refresh) {
            strcpy(s_state.poem_text, poem_text);
            s_state.left_poem_dirty = true;
        }
        xSemaphoreGive(s_state.lock);
    }
}

static void dashboard_display_task(void *arg)
{
    (void) arg;

    for (;;) {
        xSemaphoreTake(s_state.lock, portMAX_DELAY);
        dashboard_update_time_cache();
        xSemaphoreGive(s_state.lock);

        dashboard_maybe_refresh_weather();
        dashboard_maybe_refresh_poem();
        dashboard_render_if_needed();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void dashboard_event_handler(void *arg, esp_event_base_t event_base,
        int32_t event_id, void *event_data)
{
    (void) arg;

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;

        xSemaphoreTake(s_state.lock, portMAX_DELAY);
        snprintf(s_state.ip_text, sizeof(s_state.ip_text), IPSTR, IP2STR(&event->ip_info.ip));
        s_state.wifi_connected = true;
        s_state.right_dirty = true;
        s_state.left_detail_dirty = true;
        s_state.last_weather_fetch_tick = 0;
        s_state.last_poem_fetch_tick = 0;
        xSemaphoreGive(s_state.lock);

        if (!s_state.sntp_started) {
            esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "ntp.aliyun.com");
            esp_sntp_setservername(1, "cn.pool.ntp.org");
            esp_sntp_setservername(2, "pool.ntp.org");
            esp_sntp_init();
            s_state.sntp_started = true;
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xSemaphoreTake(s_state.lock, portMAX_DELAY);
        strcpy(s_state.ip_text, "NO WIFI");
        s_state.wifi_connected = false;
        s_state.right_dirty = true;
        s_state.left_detail_dirty = true;
        xSemaphoreGive(s_state.lock);
    }
}

void dual_panel_display_init(void)
{
    spi_bus_config_t bus_config;
    size_t max_width;
    esp_err_t err;
    bool left_uses_soft_spi;
    bool right_uses_soft_spi;
    bool need_shared_spi;

    if (s_state.initialized) {
        return;
    }

    s_state.lock = xSemaphoreCreateMutex();
    if (!s_state.lock) {
        ESP_LOGE(TAG, "Failed to create display mutex");
        return;
    }

    setenv("TZ", CONFIG_HOMEKIT_DASHBOARD_TIMEZONE, 1);
    tzset();
    s_state.fallback_time_seed = dashboard_build_time_seed();
    s_state.fallback_time_tick = xTaskGetTickCount();
    dashboard_build_location_label(s_state.location_text, sizeof(s_state.location_text));

    max_width = CONFIG_HOMEKIT_DASHBOARD_LEFT_H_RES;
    if ((size_t) CONFIG_HOMEKIT_DASHBOARD_LEFT_V_RES > max_width) {
        max_width = CONFIG_HOMEKIT_DASHBOARD_LEFT_V_RES;
    }
    if ((size_t) CONFIG_HOMEKIT_DASHBOARD_RIGHT_H_RES > max_width) {
        max_width = CONFIG_HOMEKIT_DASHBOARD_RIGHT_H_RES;
    }
    if ((size_t) CONFIG_HOMEKIT_DASHBOARD_RIGHT_V_RES > max_width) {
        max_width = CONFIG_HOMEKIT_DASHBOARD_RIGHT_V_RES;
    }

    s_line_buffer = heap_caps_malloc(max_width * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!s_line_buffer) {
        ESP_LOGE(TAG, "Failed to allocate display line buffer");
        return;
    }
    s_line_buffer_pixels = max_width;

    left_uses_soft_spi = (CONFIG_HOMEKIT_DASHBOARD_LEFT_SCLK >= 0 &&
            CONFIG_HOMEKIT_DASHBOARD_LEFT_MOSI >= 0);
    right_uses_soft_spi = (CONFIG_HOMEKIT_DASHBOARD_RIGHT_SCLK >= 0 &&
            CONFIG_HOMEKIT_DASHBOARD_RIGHT_MOSI >= 0);
    need_shared_spi = !left_uses_soft_spi ||
            ((CONFIG_HOMEKIT_DASHBOARD_RIGHT_CS >= 0 &&
              CONFIG_HOMEKIT_DASHBOARD_RIGHT_DC >= 0) && !right_uses_soft_spi);

    if (need_shared_spi) {
        memset(&bus_config, 0, sizeof(bus_config));
        bus_config.sclk_io_num = CONFIG_HOMEKIT_DASHBOARD_SPI_SCLK;
        bus_config.mosi_io_num = CONFIG_HOMEKIT_DASHBOARD_SPI_MOSI;
        bus_config.miso_io_num = -1;
        bus_config.quadwp_io_num = -1;
        bus_config.quadhd_io_num = -1;
        bus_config.max_transfer_sz = (int) (max_width * sizeof(uint16_t));

        err = spi_bus_initialize(DASHBOARD_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
            return;
        }
    }

    err = dashboard_panel_create(&s_left_panel, PANEL_CONTROLLER_ST7789,
            CONFIG_HOMEKIT_DASHBOARD_LEFT_H_RES,
            CONFIG_HOMEKIT_DASHBOARD_LEFT_V_RES,
            CONFIG_HOMEKIT_DASHBOARD_LEFT_X_OFFSET,
            CONFIG_HOMEKIT_DASHBOARD_LEFT_Y_OFFSET,
            DASH_CFG_LEFT_SWAP_XY,
            DASH_CFG_LEFT_MIRROR_X,
            DASH_CFG_LEFT_MIRROR_Y,
            DASH_CFG_LEFT_INVERT_COLORS,
            DASH_CFG_LEFT_BGR_ORDER,
            CONFIG_HOMEKIT_DASHBOARD_LEFT_CS,
            CONFIG_HOMEKIT_DASHBOARD_LEFT_DC,
            CONFIG_HOMEKIT_DASHBOARD_LEFT_RST,
            CONFIG_HOMEKIT_DASHBOARD_LEFT_BL,
            DASH_CFG_LEFT_BL_ACTIVE_HIGH,
            CONFIG_HOMEKIT_DASHBOARD_LEFT_SCLK,
            CONFIG_HOMEKIT_DASHBOARD_LEFT_MOSI);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Left panel init failed: %s", esp_err_to_name(err));
        return;
    }
    s_state.left_available = true;

    if (CONFIG_HOMEKIT_DASHBOARD_RIGHT_CS >= 0 &&
            CONFIG_HOMEKIT_DASHBOARD_RIGHT_DC >= 0) {
        err = dashboard_panel_create(&s_right_panel, DASH_CFG_RIGHT_CONTROLLER,
                CONFIG_HOMEKIT_DASHBOARD_RIGHT_H_RES,
                CONFIG_HOMEKIT_DASHBOARD_RIGHT_V_RES,
                CONFIG_HOMEKIT_DASHBOARD_RIGHT_X_OFFSET,
                CONFIG_HOMEKIT_DASHBOARD_RIGHT_Y_OFFSET,
                DASH_CFG_RIGHT_SWAP_XY,
                DASH_CFG_RIGHT_MIRROR_X,
                DASH_CFG_RIGHT_MIRROR_Y,
                DASH_CFG_RIGHT_INVERT_COLORS,
                DASH_CFG_RIGHT_BGR_ORDER,
                CONFIG_HOMEKIT_DASHBOARD_RIGHT_CS,
                CONFIG_HOMEKIT_DASHBOARD_RIGHT_DC,
                CONFIG_HOMEKIT_DASHBOARD_RIGHT_RST,
                CONFIG_HOMEKIT_DASHBOARD_RIGHT_BL,
                DASH_CFG_RIGHT_BL_ACTIVE_HIGH,
                CONFIG_HOMEKIT_DASHBOARD_RIGHT_SCLK,
                CONFIG_HOMEKIT_DASHBOARD_RIGHT_MOSI);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Right panel init failed: %s", esp_err_to_name(err));
        } else {
            s_state.right_available = true;
            dashboard_panel_run_boot_pattern(&s_right_panel, "Right");
        }
    } else {
        ESP_LOGW(TAG, "Right panel disabled by configuration");
    }

    s_state.initialized = true;
    s_state.left_dirty = true;
    s_state.right_dirty = true;
    dashboard_render_if_needed();
}

void dual_panel_display_start(void)
{
    if (!s_state.initialized || s_state.started) {
        return;
    }

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
            dashboard_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
            dashboard_event_handler, NULL));

    xTaskCreate(dashboard_display_task, "dual_panel_ui", 6 * 1024, NULL, 2, NULL);
    s_state.started = true;
}

void dual_panel_display_set_light(size_t index, bool is_on)
{
    if (!s_state.lock || index >= 3) {
        return;
    }

    xSemaphoreTake(s_state.lock, portMAX_DELAY);
    if (s_state.light_states[index] != is_on) {
        s_state.light_states[index] = is_on;
    }
    xSemaphoreGive(s_state.lock);
}

void dual_panel_display_set_button(size_t index, bool is_pressed)
{
    if (!s_state.lock || index >= 3) {
        return;
    }

    xSemaphoreTake(s_state.lock, portMAX_DELAY);
    if (s_state.button_states[index] != is_pressed) {
        s_state.button_states[index] = is_pressed;
        s_state.right_dirty = true;
    }
    xSemaphoreGive(s_state.lock);
}

void dual_panel_display_request_right_refresh(void)
{
    if (!s_state.lock) {
        return;
    }

    xSemaphoreTake(s_state.lock, portMAX_DELAY);
    s_state.right_dirty = true;
    xSemaphoreGive(s_state.lock);
}

void dual_panel_display_request_poem_refresh(void)
{
    if (!s_state.lock) {
        return;
    }

    xSemaphoreTake(s_state.lock, portMAX_DELAY);
    s_state.poem_refresh_requested = true;
    xSemaphoreGive(s_state.lock);
}
