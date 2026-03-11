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
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
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
    bool sntp_started;
    char ip_text[24];
    char location_text[24];
    char right_otp_text[16];
    char weather_text[40];
    char poem_text[LEFT_POEM_TEXT_LEN];
    char last_time_text[LEFT_TIME_TEXT_LEN];
    bool light_states[3];
    bool button_states[3];
    int right_otp_remaining;
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
    .weather_text = "Weather --",
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
static const uint16_t s_right_background = RGB565(82, 178, 122);
static const uint16_t s_right_header = RGB565(244, 124, 72);
static const uint16_t s_right_text = RGB565(252, 249, 241);
static const uint16_t s_right_status_on = RGB565(226, 244, 230);
static const uint16_t s_right_status_off = RGB565(246, 215, 196);
static const uint8_t s_st7789_ramctrl[2] = { 0x00, 0xE0 };
static const uint8_t s_st7789_positive_gamma[] = {
    0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32,
    0x44, 0x42, 0x06, 0x0E, 0x12, 0x14, 0x17,
};
static const uint8_t s_st7789_negative_gamma[] = {
    0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x31,
    0x54, 0x47, 0x0E, 0x1C, 0x17, 0x1B, 0x1E,
};

static void dashboard_update_right_otp_cache(time_t now, bool time_synced);
static void dashboard_draw_char(dashboard_panel_t *panel,
        int x, int y, char c, uint16_t color, uint8_t scale);
static void dashboard_draw_cjk16_char(dashboard_panel_t *panel,
        int x, int y, uint16_t codepoint, uint16_t color);

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

static int dashboard_measure_mixed_text(const char *text, uint8_t ascii_scale)
{
    int width = 0;
    const char *cursor = text;

    while (cursor && *cursor) {
        uint16_t codepoint;

        if (!dashboard_utf8_next_codepoint(&cursor, &codepoint)) {
            break;
        }
        if (codepoint < 0x80) {
            width += (FONT_WIDTH + 1) * ascii_scale;
        } else {
            width += CJK_GLYPH_WIDTH;
        }
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

static void dashboard_draw_mixed_text_line(dashboard_panel_t *panel,
        int y, const char *text,
        uint16_t color, uint16_t background,
        uint8_t ascii_scale, bool center)
{
    int panel_width = dashboard_panel_visible_width(panel);
    int ascii_height = FONT_HEIGHT * ascii_scale;
    int line_height = ascii_height > CJK_GLYPH_HEIGHT ? ascii_height : CJK_GLYPH_HEIGHT;
    int cursor_x = TEXT_PADDING_X;
    const char *cursor = text;

    if (center && text) {
        int text_width = dashboard_measure_mixed_text(text, ascii_scale);

        if (text_width > 0 && text_width < panel_width) {
            cursor_x = (panel_width - text_width) / 2;
        }
    }

    dashboard_panel_fill_rect(panel, 0, y - 1, panel_width, line_height + 2, background);

    while (cursor && *cursor) {
        uint16_t codepoint;
        int glyph_width;

        if (!dashboard_utf8_next_codepoint(&cursor, &codepoint)) {
            break;
        }

        glyph_width = codepoint < 0x80 ? (FONT_WIDTH + 1) * ascii_scale : CJK_GLYPH_WIDTH;
        if (cursor_x + glyph_width > panel_width - TEXT_PADDING_X) {
            break;
        }

        if (codepoint < 0x80) {
            dashboard_draw_char(panel, cursor_x,
                    y + ((line_height - ascii_height) / 2),
                    (char) codepoint, color, ascii_scale);
        } else {
            dashboard_draw_cjk16_char(panel, cursor_x,
                    y + ((line_height - CJK_GLYPH_HEIGHT) / 2),
                    codepoint, color);
        }
        cursor_x += glyph_width;
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
        s_state.left_detail_dirty = true;
        s_state.left_poem_dirty = true;
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

static void dashboard_trim_last_utf8_codepoint(char *text)
{
    size_t length;

    if (!text) {
        return;
    }

    length = strlen(text);
    while (length > 0) {
        length--;
        if ((((unsigned char) text[length]) & 0xC0) != 0x80) {
            text[length] = '\0';
            return;
        }
    }
    text[0] = '\0';
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

static void dashboard_format_left_weather_summary(char *out, size_t out_size,
        const char *weather_text, bool wifi_connected)
{
    if (!wifi_connected) {
        strncpy(out, "Offline", out_size);
        out[out_size - 1] = '\0';
    } else if (weather_text && weather_text[0] != '\0') {
        strncpy(out, weather_text, out_size);
        out[out_size - 1] = '\0';
    } else {
        strncpy(out, "Weather --", out_size);
        out[out_size - 1] = '\0';
    }
}

static void dashboard_format_left_detail(char *out, size_t out_size,
        const char *location_text, const char *weather_text, bool wifi_connected)
{
    char location[sizeof(s_state.location_text)];
    char summary[sizeof(s_state.weather_text)];
    int available_width;

    if (!out || out_size == 0) {
        return;
    }

    strncpy(location,
            (location_text && location_text[0] != '\0') ? location_text : "LOCATING",
            sizeof(location));
    location[sizeof(location) - 1] = '\0';
    dashboard_format_left_weather_summary(summary, sizeof(summary), weather_text, wifi_connected);
    available_width = dashboard_panel_visible_width(&s_left_panel) - TEXT_PADDING_X * 2;

    if (dashboard_measure_mixed_text(summary, LEFT_DETAIL_SCALE) >= available_width) {
        strncpy(out, summary, out_size);
        out[out_size - 1] = '\0';
        return;
    }

    if (location[0] != '\0') {
        snprintf(out, out_size, "%s %s", location, summary);
        out[out_size - 1] = '\0';
        while (location[0] != '\0' &&
                dashboard_measure_mixed_text(out, LEFT_DETAIL_SCALE) > available_width) {
            dashboard_trim_last_utf8_codepoint(location);
            dashboard_compact_spaces(location);
            if (location[0] != '\0') {
                snprintf(out, out_size, "%s %s", location, summary);
            } else {
                snprintf(out, out_size, "%s", summary);
            }
            out[out_size - 1] = '\0';
        }
        if (location[0] != '\0') {
            return;
        }
    }

    snprintf(out, out_size, "%s", summary);
    out[out_size - 1] = '\0';
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
    dashboard_draw_mixed_text_line(&s_left_panel, dashboard_left_detail_y(), detail_text,
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
    int divider_height = panel_height >= 90 ? 2 : 1;
    int usable_height = panel_height - divider_height * 2;
    int row1_height = usable_height / 3;
    int row2_height = usable_height / 3;
    int row3_height = usable_height - row1_height - row2_height;
    int row1_y = 0;
    int row2_y = row1_y + row1_height + divider_height;
    int row3_y = row2_y + row2_height + divider_height;
    char row1_text[24];

    if (row1_height < FONT_HEIGHT + 6) {
        row1_height = FONT_HEIGHT + 6;
    }
    if (row2_height < 1) {
        row2_height = 1;
    }
    if (row3_height < 1) {
        row3_height = 1;
    }

    dashboard_panel_fill_rect(&s_right_panel, 0, 0, panel_width, panel_height, s_right_background);
    dashboard_panel_fill_rect(&s_right_panel, 0, row1_y, panel_width, row1_height, s_right_header);
    dashboard_panel_fill_rect(&s_right_panel, 0, row2_y, panel_width, row2_height,
            button_states[1] ? s_right_status_on : s_right_status_off);
    dashboard_panel_fill_rect(&s_right_panel, 0, row3_y, panel_width, row3_height,
            button_states[2] ? s_right_status_on : s_right_status_off);

    if (otp_remaining > 0) {
        snprintf(row1_text, sizeof(row1_text), "%s %02dS", otp_text, otp_remaining);
    } else {
        snprintf(row1_text, sizeof(row1_text), "%s", otp_text);
    }
    row1_text[sizeof(row1_text) - 1] = '\0';
    dashboard_draw_text_line(&s_right_panel,
            row1_y + ((row1_height - FONT_HEIGHT * RIGHT_BODY_SCALE) / 2),
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

static void dashboard_display_task(void *arg)
{
    (void) arg;

    for (;;) {
        xSemaphoreTake(s_state.lock, portMAX_DELAY);
        dashboard_update_time_cache();
        xSemaphoreGive(s_state.lock);

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

void dual_panel_display_set_location_weather(const char *location_text, const char *weather_text)
{
    bool detail_dirty = false;

    if (!s_state.lock) {
        return;
    }

    xSemaphoreTake(s_state.lock, portMAX_DELAY);
    if (location_text && strcmp(location_text, s_state.location_text) != 0) {
        strncpy(s_state.location_text, location_text, sizeof(s_state.location_text));
        s_state.location_text[sizeof(s_state.location_text) - 1] = '\0';
        detail_dirty = true;
    }
    if (weather_text && strcmp(weather_text, s_state.weather_text) != 0) {
        strncpy(s_state.weather_text, weather_text, sizeof(s_state.weather_text));
        s_state.weather_text[sizeof(s_state.weather_text) - 1] = '\0';
        detail_dirty = true;
    }
    if (detail_dirty) {
        s_state.left_detail_dirty = true;
    }
    xSemaphoreGive(s_state.lock);
}

void dual_panel_display_set_poem(const char *poem_text)
{
    if (!s_state.lock || !poem_text) {
        return;
    }

    xSemaphoreTake(s_state.lock, portMAX_DELAY);
    if (strcmp(poem_text, s_state.poem_text) != 0) {
        strncpy(s_state.poem_text, poem_text, sizeof(s_state.poem_text));
        s_state.poem_text[sizeof(s_state.poem_text) - 1] = '\0';
        s_state.left_poem_dirty = true;
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
