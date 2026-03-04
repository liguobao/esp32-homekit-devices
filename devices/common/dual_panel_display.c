#include "dual_panel_display.h"

#include <stdbool.h>
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
#include "esp_sntp.h"
#include "esp_wifi.h"

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
#define LEFT_BODY_SCALE 1
#define RIGHT_TITLE_SCALE 2
#define RIGHT_BODY_SCALE 2
#define PANEL_CMD_DELAY 0xFE
#define PANEL_CMD_END 0x00

#define RGB565(r, g, b) \
    (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3))

typedef struct {
    char code;
    uint8_t rows[FONT_HEIGHT];
} font_glyph_t;

typedef enum {
    PANEL_CONTROLLER_ST7789 = 0,
    PANEL_CONTROLLER_NV3007,
} panel_controller_t;

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
} dashboard_panel_t;

typedef struct {
    bool initialized;
    bool started;
    bool wifi_connected;
    bool time_synced;
    bool left_dirty;
    bool right_dirty;
    bool sntp_started;
    char ip_text[24];
    char weather_text[40];
    char last_time_text[16];
    bool light_states[3];
    TickType_t last_weather_fetch_tick;
    SemaphoreHandle_t lock;
} dashboard_state_t;

static const font_glyph_t s_font[] = {
    { ' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00} },
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

static dashboard_panel_t s_left_panel;
static dashboard_panel_t s_right_panel;
static dashboard_state_t s_state = {
    .ip_text = "NO WIFI",
    .weather_text = "WEATHER --",
    .last_time_text = "--:--",
};
static uint16_t *s_line_buffer;
static size_t s_line_buffer_pixels;

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

static const uint16_t s_left_background = RGB565(9, 26, 52);
static const uint16_t s_left_header = RGB565(22, 82, 151);
static const uint16_t s_left_text = RGB565(240, 244, 248);
static const uint16_t s_left_secondary = RGB565(190, 210, 228);
static const uint16_t s_right_background = RGB565(18, 20, 24);
static const uint16_t s_right_header = RGB565(44, 78, 126);
static const uint16_t s_right_text = RGB565(245, 247, 250);
static const uint16_t s_right_on = RGB565(35, 110, 48);
static const uint16_t s_right_off = RGB565(122, 32, 40);
static const uint16_t s_right_divider = RGB565(64, 74, 88);

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

static int dashboard_panel_visible_width(const dashboard_panel_t *panel)
{
    return panel->swap_xy ? panel->height : panel->width;
}

static int dashboard_panel_visible_height(const dashboard_panel_t *panel)
{
    return panel->swap_xy ? panel->width : panel->height;
}

static esp_err_t dashboard_panel_send_sequence(esp_lcd_panel_io_handle_t io,
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
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, cmd, cursor, len),
                TAG, "panel init command 0x%02X failed", cmd);
        cursor += len;
    }
    return ESP_OK;
}

static esp_err_t dashboard_nv3007_apply_madctl(dashboard_panel_t *panel)
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
    return esp_lcd_panel_io_tx_param(panel->io_handle, 0x36, &madctl, 1);
}

static esp_err_t dashboard_panel_init_nv3007(dashboard_panel_t *panel, int reset_gpio)
{
    esp_err_t err;
    uint8_t unlock = 0xA5;
    uint8_t lock = 0x00;
    uint8_t pixel_format = 0x05;

    if (reset_gpio >= 0) {
        gpio_config_t reset_pin = {
            .pin_bit_mask = 1ULL << reset_gpio,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };

        ESP_RETURN_ON_ERROR(gpio_config(&reset_pin), TAG, "NV3007 reset gpio config failed");
        ESP_RETURN_ON_ERROR(gpio_set_level(reset_gpio, 0), TAG, "NV3007 reset low failed");
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_RETURN_ON_ERROR(gpio_set_level(reset_gpio, 1), TAG, "NV3007 reset high failed");
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(panel->io_handle, 0xFF, &unlock, 1),
            TAG, "NV3007 unlock failed");
    ESP_RETURN_ON_ERROR(dashboard_panel_send_sequence(panel->io_handle, s_nv3007_init_seq),
            TAG, "NV3007 init sequence failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(panel->io_handle, 0xFF, &lock, 1),
            TAG, "NV3007 lock failed");

    err = esp_lcd_panel_io_tx_param(panel->io_handle, 0x3A, &pixel_format, 1);
    if (err == ESP_OK) {
        err = esp_lcd_panel_io_tx_param(panel->io_handle, 0x11, NULL, 0);
    }
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(22));
        err = dashboard_nv3007_apply_madctl(panel);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_io_tx_param(panel->io_handle,
                panel->invert_colors ? 0x21 : 0x20, NULL, 0);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_io_tx_param(panel->io_handle, 0x29, NULL, 0);
    }
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return err;
}

static esp_err_t dashboard_panel_init_st7789(dashboard_panel_t *panel, int reset_gpio)
{
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = reset_gpio,
        .rgb_ele_order = panel->bgr_order ?
                LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = 16,
    };
    esp_err_t err = esp_lcd_new_panel_st7789(panel->io_handle, &panel_config, &panel->panel_handle);

    if (err == ESP_OK) {
        err = esp_lcd_panel_reset(panel->panel_handle);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_init(panel->panel_handle);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_set_gap(panel->panel_handle, panel->x_gap, panel->y_gap);
    }
    if (err == ESP_OK && panel->swap_xy) {
        err = esp_lcd_panel_swap_xy(panel->panel_handle, true);
    }
    if (err == ESP_OK && (panel->mirror_x || panel->mirror_y)) {
        err = esp_lcd_panel_mirror(panel->panel_handle, panel->mirror_x, panel->mirror_y);
    }
    if (err == ESP_OK && panel->invert_colors) {
        err = esp_lcd_panel_invert_color(panel->panel_handle, true);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_disp_on_off(panel->panel_handle, true);
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
        bool backlight_active_high)
{
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = cs_gpio,
        .dc_gpio_num = dc_gpio,
        .spi_mode = 0,
        .pclk_hz = CONFIG_HOMEKIT_DASHBOARD_SPI_CLOCK_HZ,
        .trans_queue_depth = 8,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
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

    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) DASHBOARD_SPI_HOST,
            &io_config, &panel->io_handle);
    if (err != ESP_OK) {
        return err;
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
    if (panel->controller == PANEL_CONTROLLER_ST7789) {
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
    esp_err_t err = esp_lcd_panel_io_tx_param(panel->io_handle, 0x2A, col_data, sizeof(col_data));

    if (err == ESP_OK) {
        err = esp_lcd_panel_io_tx_param(panel->io_handle, 0x2B, row_data, sizeof(row_data));
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_io_tx_param(panel->io_handle, 0x2C, NULL, 0);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_io_tx_color(panel->io_handle, -1, color_data, len);
    }
    return err;
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

static void dashboard_draw_text_line(dashboard_panel_t *panel,
        int y, const char *text,
        uint16_t color, uint16_t background,
        uint8_t scale, bool center)
{
    int panel_width = dashboard_panel_visible_width(panel);
    int cursor_x = 6;
    int text_width = dashboard_measure_text(text, scale);
    int line_height = FONT_HEIGHT * scale;

    dashboard_panel_fill_rect(panel, 0, y - 1, panel_width, line_height + 2, background);
    if (center && text_width > 0 && text_width < panel_width) {
        cursor_x = (panel_width - text_width) / 2;
    }

    if (!text) {
        return;
    }
    while (*text) {
        if (cursor_x + FONT_WIDTH * scale > panel_width - 4) {
            break;
        }
        dashboard_draw_char(panel, cursor_x, y, *text, color, scale);
        cursor_x += (FONT_WIDTH + 1) * scale;
        text++;
    }
}

static void dashboard_update_time_cache(void)
{
    time_t now;
    struct tm time_info;

    time(&now);
    localtime_r(&now, &time_info);

    if (time_info.tm_year >= (2024 - 1900)) {
        char time_text[sizeof(s_state.last_time_text)];

        s_state.time_synced = true;
        snprintf(time_text, sizeof(time_text), "%02d:%02d",
                time_info.tm_hour, time_info.tm_min);
        if (strcmp(time_text, s_state.last_time_text) != 0) {
            strcpy(s_state.last_time_text, time_text);
            s_state.left_dirty = true;
        }
    } else if (strcmp(s_state.last_time_text, "--:--") != 0) {
        s_state.time_synced = false;
        strcpy(s_state.last_time_text, "--:--");
        s_state.left_dirty = true;
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

static esp_err_t dashboard_fetch_weather(char *out, size_t out_size)
{
    esp_http_client_config_t http_config = {
        .timeout_ms = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client;
    esp_err_t err;
    char url[128];
    int read_len;

    if (CONFIG_HOMEKIT_DASHBOARD_WEATHER_LOCATION[0] != '\0') {
        snprintf(url, sizeof(url),
                "https://wttr.in/%s?format=%%25t%%20%%25C",
                CONFIG_HOMEKIT_DASHBOARD_WEATHER_LOCATION);
    } else {
        strncpy(url, "https://wttr.in/?format=%25t%20%25C", sizeof(url));
        url[sizeof(url) - 1] = '\0';
    }
    http_config.url = url;

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

    dashboard_sanitize_ascii(out);
    return ESP_OK;
}

static void dashboard_redraw_left(const char *time_text, const char *weather_text,
        bool wifi_connected)
{
    int panel_width = dashboard_panel_visible_width(&s_left_panel);
    int panel_height = dashboard_panel_visible_height(&s_left_panel);
    int header_height = FONT_HEIGHT * LEFT_TIME_SCALE + 18;
    int weather_y = header_height + 18;
    int status_y = weather_y + FONT_HEIGHT * LEFT_BODY_SCALE + 18;

    dashboard_panel_fill_rect(&s_left_panel, 0, 0, panel_width, panel_height, s_left_background);
    dashboard_panel_fill_rect(&s_left_panel, 0, 0, panel_width, header_height, s_left_header);
    dashboard_draw_text_line(&s_left_panel, 8, time_text,
            s_left_text, s_left_header, LEFT_TIME_SCALE, true);
    dashboard_draw_text_line(&s_left_panel, weather_y, weather_text,
            s_left_text, s_left_background, LEFT_BODY_SCALE, false);
    dashboard_draw_text_line(&s_left_panel, status_y,
            wifi_connected ? "ONLINE" : "OFFLINE",
            s_left_secondary, s_left_background, LEFT_BODY_SCALE, false);
}

static void dashboard_redraw_right(const char *ip_text, const bool light_states[3])
{
    int panel_width = dashboard_panel_visible_width(&s_right_panel);
    int panel_height = dashboard_panel_visible_height(&s_right_panel);
    int header_height = FONT_HEIGHT * RIGHT_TITLE_SCALE + 16;
    int ip_y = header_height + 14;
    int row_height = 42;
    int row_y = ip_y + FONT_HEIGHT * RIGHT_BODY_SCALE + 18;

    dashboard_panel_fill_rect(&s_right_panel, 0, 0, panel_width, panel_height, s_right_background);
    dashboard_panel_fill_rect(&s_right_panel, 0, 0, panel_width, header_height, s_right_header);
    dashboard_draw_text_line(&s_right_panel, 6, "STATUS",
            s_right_text, s_right_header, RIGHT_TITLE_SCALE, true);
    dashboard_draw_text_line(&s_right_panel, ip_y, ip_text,
            s_right_text, s_right_background, 1, false);

    for (size_t i = 0; i < 3; i++) {
        char row_text[16];
        uint16_t row_color = light_states[i] ? s_right_on : s_right_off;

        dashboard_panel_fill_rect(&s_right_panel, 6, row_y + (int) i * row_height,
                panel_width - 12, row_height - 6, row_color);
        snprintf(row_text, sizeof(row_text), "L%u %s",
                (unsigned int) (i + 1), light_states[i] ? "ON" : "OFF");
        dashboard_draw_text_line(&s_right_panel,
                row_y + 10 + (int) i * row_height,
                row_text, s_right_text, row_color, RIGHT_BODY_SCALE, true);
        if (i < 2) {
            dashboard_panel_fill_rect(&s_right_panel, 10,
                    row_y + (int) (i + 1) * row_height - 3,
                    panel_width - 20, 2, s_right_divider);
        }
    }
}

static void dashboard_render_if_needed(void)
{
    char time_text[sizeof(s_state.last_time_text)];
    char weather_text[sizeof(s_state.weather_text)];
    char ip_text[sizeof(s_state.ip_text)];
    bool left_dirty;
    bool right_dirty;
    bool wifi_connected;
    bool lights[3];

    xSemaphoreTake(s_state.lock, portMAX_DELAY);
    left_dirty = s_state.left_dirty;
    right_dirty = s_state.right_dirty;
    strcpy(time_text, s_state.last_time_text);
    strcpy(weather_text, s_state.weather_text);
    strcpy(ip_text, s_state.ip_text);
    wifi_connected = s_state.wifi_connected;
    memcpy(lights, s_state.light_states, sizeof(lights));
    s_state.left_dirty = false;
    s_state.right_dirty = false;
    xSemaphoreGive(s_state.lock);

    if (left_dirty) {
        dashboard_redraw_left(time_text, weather_text, wifi_connected);
    }
    if (right_dirty) {
        dashboard_redraw_right(ip_text, lights);
    }
}

static void dashboard_maybe_refresh_weather(void)
{
    TickType_t now_ticks = xTaskGetTickCount();
    bool should_fetch = false;
    char weather_text[sizeof(s_state.weather_text)];

    xSemaphoreTake(s_state.lock, portMAX_DELAY);
    if (s_state.wifi_connected &&
            (s_state.last_weather_fetch_tick == 0 ||
            (now_ticks - s_state.last_weather_fetch_tick) >=
                    pdMS_TO_TICKS(CONFIG_HOMEKIT_DASHBOARD_WEATHER_REFRESH_SEC * 1000))) {
        s_state.last_weather_fetch_tick = now_ticks;
        should_fetch = true;
    }
    xSemaphoreGive(s_state.lock);

    if (!should_fetch) {
        return;
    }

    if (dashboard_fetch_weather(weather_text, sizeof(weather_text)) == ESP_OK) {
        xSemaphoreTake(s_state.lock, portMAX_DELAY);
        if (strcmp(weather_text, s_state.weather_text) != 0) {
            strcpy(s_state.weather_text, weather_text);
            s_state.left_dirty = true;
        }
        xSemaphoreGive(s_state.lock);
    } else {
        ESP_LOGW(TAG, "Weather fetch failed");
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
        dashboard_render_if_needed();
        vTaskDelay(pdMS_TO_TICKS(1000));
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
        s_state.left_dirty = true;
        s_state.last_weather_fetch_tick = 0;
        xSemaphoreGive(s_state.lock);

        if (!s_state.sntp_started) {
            setenv("TZ", CONFIG_HOMEKIT_DASHBOARD_TIMEZONE, 1);
            tzset();
            esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "pool.ntp.org");
            esp_sntp_init();
            s_state.sntp_started = true;
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xSemaphoreTake(s_state.lock, portMAX_DELAY);
        strcpy(s_state.ip_text, "NO WIFI");
        s_state.wifi_connected = false;
        s_state.right_dirty = true;
        s_state.left_dirty = true;
        xSemaphoreGive(s_state.lock);
    }
}

void dual_panel_display_init(void)
{
    spi_bus_config_t bus_config;
    size_t max_width;
    esp_err_t err;

    if (s_state.initialized) {
        return;
    }

    s_state.lock = xSemaphoreCreateMutex();
    if (!s_state.lock) {
        ESP_LOGE(TAG, "Failed to create display mutex");
        return;
    }

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
            DASH_CFG_LEFT_BL_ACTIVE_HIGH);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Left panel init failed: %s", esp_err_to_name(err));
        return;
    }

    err = dashboard_panel_create(&s_right_panel, PANEL_CONTROLLER_NV3007,
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
            DASH_CFG_RIGHT_BL_ACTIVE_HIGH);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Right panel init failed: %s", esp_err_to_name(err));
        return;
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
        s_state.right_dirty = true;
    }
    xSemaphoreGive(s_state.lock);
}
