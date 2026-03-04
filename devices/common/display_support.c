#include "display_support.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"

#if CONFIG_HOMEKIT_ENABLE_ST7789_DISPLAY

static const char *TAG = "display_support";

#ifdef CONFIG_HOMEKIT_ST7789_BL_ACTIVE_HIGH
#define DISPLAY_CFG_BL_ACTIVE_HIGH 1
#else
#define DISPLAY_CFG_BL_ACTIVE_HIGH 0
#endif

#ifdef CONFIG_HOMEKIT_ST7789_SWAP_XY
#define DISPLAY_CFG_SWAP_XY 1
#else
#define DISPLAY_CFG_SWAP_XY 0
#endif

#ifdef CONFIG_HOMEKIT_ST7789_MIRROR_X
#define DISPLAY_CFG_MIRROR_X 1
#else
#define DISPLAY_CFG_MIRROR_X 0
#endif

#ifdef CONFIG_HOMEKIT_ST7789_MIRROR_Y
#define DISPLAY_CFG_MIRROR_Y 1
#else
#define DISPLAY_CFG_MIRROR_Y 0
#endif

#ifdef CONFIG_HOMEKIT_ST7789_INVERT_COLORS
#define DISPLAY_CFG_INVERT_COLORS 1
#else
#define DISPLAY_CFG_INVERT_COLORS 0
#endif

#ifdef CONFIG_HOMEKIT_ST7789_BGR_ORDER
#define DISPLAY_CFG_BGR_ORDER 1
#else
#define DISPLAY_CFG_BGR_ORDER 0
#endif

#define DISPLAY_SPI_HOST SPI2_HOST
#define FONT_WIDTH 5
#define FONT_HEIGHT 7
#define FOOTER_HEIGHT 44
#define SIDE_PADDING 12

#define RGB565(r, g, b) \
    (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3))

typedef struct {
    char code;
    uint8_t rows[FONT_HEIGHT];
} font_glyph_t;

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

static esp_lcd_panel_io_handle_t s_panel_io;
static esp_lcd_panel_handle_t s_panel;
static bool s_ready;
static uint16_t s_scanline[CONFIG_HOMEKIT_ST7789_H_RES];

static const uint16_t s_background = RGB565(7, 16, 31);
static const uint16_t s_header_background = RGB565(17, 82, 147);
static const uint16_t s_divider = RGB565(31, 69, 120);
static const uint16_t s_text = RGB565(245, 247, 250);
static const uint16_t s_secondary_text = RGB565(197, 215, 232);
static const uint16_t s_power_on_background = RGB565(27, 94, 32);
static const uint16_t s_power_off_background = RGB565(120, 32, 32);

static uint8_t display_header_scale(void)
{
    if (CONFIG_HOMEKIT_ST7789_H_RES < 96) {
        return 1;
    }
    if (CONFIG_HOMEKIT_ST7789_H_RES < 160) {
        return 2;
    }
    return 3;
}

static uint8_t display_body_scale(void)
{
    if (CONFIG_HOMEKIT_ST7789_H_RES < 96) {
        return 1;
    }
    return 2;
}

static int display_footer_height(void)
{
    if (display_body_scale() == 1) {
        return 28;
    }
    return FOOTER_HEIGHT;
}

static int display_side_padding(void)
{
    if (CONFIG_HOMEKIT_ST7789_H_RES < 96) {
        return 4;
    }
    return SIDE_PADDING;
}

static int display_header_height(void)
{
    const int header_scale = display_header_scale();
    const int min_header = FONT_HEIGHT * header_scale + 8;
    int max_header = CONFIG_HOMEKIT_ST7789_V_RES - display_footer_height() - 12;
    int header_height = CONFIG_HOMEKIT_ST7789_V_RES / 5;

    if (header_height < 40) {
        header_height = 40;
    }
    if (max_header < min_header) {
        max_header = min_header;
    }
    if (header_height > max_header) {
        header_height = max_header;
    }
    if (header_height < min_header) {
        header_height = min_header;
    }
    return header_height;
}

static int display_footer_top(void)
{
    int footer_top = CONFIG_HOMEKIT_ST7789_V_RES - display_footer_height();

    if (footer_top < 0) {
        footer_top = 0;
    }
    return footer_top;
}

static const font_glyph_t *display_find_glyph(char c)
{
    for (size_t i = 0; i < sizeof(s_font) / sizeof(s_font[0]); i++) {
        if (s_font[i].code == c) {
            return &s_font[i];
        }
    }
    return &s_font[0];
}

static char display_normalize_char(char c)
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

static void display_fill_rect(int x, int y, int width, int height, uint16_t color)
{
    if (!s_ready || !s_panel || width <= 0 || height <= 0) {
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
    if (x >= CONFIG_HOMEKIT_ST7789_H_RES || y >= CONFIG_HOMEKIT_ST7789_V_RES) {
        return;
    }

    if (x + width > CONFIG_HOMEKIT_ST7789_H_RES) {
        width = CONFIG_HOMEKIT_ST7789_H_RES - x;
    }
    if (y + height > CONFIG_HOMEKIT_ST7789_V_RES) {
        height = CONFIG_HOMEKIT_ST7789_V_RES - y;
    }
    if (width <= 0 || height <= 0) {
        return;
    }

    for (int i = 0; i < width; i++) {
        s_scanline[i] = color;
    }
    for (int row = 0; row < height; row++) {
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel,
                x, y + row, x + width, y + row + 1, s_scanline);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "fill rect failed: %s", esp_err_to_name(err));
            s_ready = false;
            return;
        }
    }
}

static void display_draw_char(int x, int y, char c, uint16_t color, uint8_t scale)
{
    const font_glyph_t *glyph = display_find_glyph(display_normalize_char(c));

    for (int row = 0; row < FONT_HEIGHT; row++) {
        for (int col = 0; col < FONT_WIDTH; col++) {
            if (glyph->rows[row] & (1U << (FONT_WIDTH - col - 1))) {
                display_fill_rect(x + col * scale, y + row * scale,
                        scale, scale, color);
            }
        }
    }
}

static int display_measure_text(const char *text, uint8_t scale)
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

static void display_draw_text_line(int y, const char *text, uint16_t color,
        uint16_t background, uint8_t scale, bool center)
{
    const int side_padding = display_side_padding();
    int cursor_x = side_padding;
    int line_height = FONT_HEIGHT * scale;
    int width = display_measure_text(text, scale);

    display_fill_rect(0, y - 2, CONFIG_HOMEKIT_ST7789_H_RES, line_height + 4, background);
    if (center && width > 0 && width < CONFIG_HOMEKIT_ST7789_H_RES) {
        cursor_x = (CONFIG_HOMEKIT_ST7789_H_RES - width) / 2;
    }

    if (!text) {
        return;
    }
    while (*text) {
        if (cursor_x + FONT_WIDTH * scale > CONFIG_HOMEKIT_ST7789_H_RES - side_padding) {
            break;
        }
        display_draw_char(cursor_x, y, *text, color, scale);
        cursor_x += (FONT_WIDTH + 1) * scale;
        text++;
    }
}

static void display_draw_power_footer(bool is_on)
{
    const uint16_t background = is_on ? s_power_on_background : s_power_off_background;
    const int footer_top = display_footer_top();
    const int footer_height = display_footer_height();
    const uint8_t body_scale = display_body_scale();

    display_fill_rect(0, footer_top,
            CONFIG_HOMEKIT_ST7789_H_RES, footer_height, background);
    display_draw_text_line(footer_top + (footer_height - FONT_HEIGHT * body_scale) / 2,
            is_on ? "POWER ON" : "POWER OFF", s_text, background, body_scale, true);
}

void display_support_init(void)
{
    if (s_ready) {
        return;
    }
    if (CONFIG_HOMEKIT_ST7789_PIN_SCLK < 0 ||
            CONFIG_HOMEKIT_ST7789_PIN_MOSI < 0 ||
            CONFIG_HOMEKIT_ST7789_PIN_CS < 0 ||
            CONFIG_HOMEKIT_ST7789_PIN_DC < 0) {
        ESP_LOGW(TAG, "ST7789 enabled but SPI pins are incomplete");
        return;
    }

    spi_bus_config_t bus_config = {
        .sclk_io_num = CONFIG_HOMEKIT_ST7789_PIN_SCLK,
        .mosi_io_num = CONFIG_HOMEKIT_ST7789_PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CONFIG_HOMEKIT_ST7789_H_RES * sizeof(uint16_t),
    };
    esp_err_t err = spi_bus_initialize(DISPLAY_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return;
    }

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = CONFIG_HOMEKIT_ST7789_PIN_CS,
        .dc_gpio_num = CONFIG_HOMEKIT_ST7789_PIN_DC,
        .spi_mode = 0,
        .pclk_hz = CONFIG_HOMEKIT_ST7789_SPI_CLOCK_HZ,
        .trans_queue_depth = 8,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) DISPLAY_SPI_HOST,
            &io_config, &s_panel_io);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_lcd_new_panel_io_spi failed: %s", esp_err_to_name(err));
        spi_bus_free(DISPLAY_SPI_HOST);
        return;
    }

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = CONFIG_HOMEKIT_ST7789_PIN_RST,
        .rgb_ele_order = DISPLAY_CFG_BGR_ORDER ?
                LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_st7789(s_panel_io, &panel_config, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_lcd_new_panel_st7789 failed: %s", esp_err_to_name(err));
        esp_lcd_panel_io_del(s_panel_io);
        s_panel_io = NULL;
        spi_bus_free(DISPLAY_SPI_HOST);
        return;
    }

    err = esp_lcd_panel_reset(s_panel);
    if (err == ESP_OK) {
        err = esp_lcd_panel_init(s_panel);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_set_gap(s_panel,
                CONFIG_HOMEKIT_ST7789_X_OFFSET, CONFIG_HOMEKIT_ST7789_Y_OFFSET);
    }
    if (err == ESP_OK && DISPLAY_CFG_SWAP_XY) {
        err = esp_lcd_panel_swap_xy(s_panel, true);
    }
    if (err == ESP_OK && (DISPLAY_CFG_MIRROR_X || DISPLAY_CFG_MIRROR_Y)) {
        err = esp_lcd_panel_mirror(s_panel,
                DISPLAY_CFG_MIRROR_X, DISPLAY_CFG_MIRROR_Y);
    }
    if (err == ESP_OK && DISPLAY_CFG_INVERT_COLORS) {
        err = esp_lcd_panel_invert_color(s_panel, true);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_disp_on_off(s_panel, true);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ST7789 init failed: %s", esp_err_to_name(err));
        esp_lcd_panel_del(s_panel);
        s_panel = NULL;
        esp_lcd_panel_io_del(s_panel_io);
        s_panel_io = NULL;
        spi_bus_free(DISPLAY_SPI_HOST);
        return;
    }

    if (CONFIG_HOMEKIT_ST7789_PIN_BL >= 0) {
        gpio_config_t backlight_gpio = {
            .pin_bit_mask = 1ULL << CONFIG_HOMEKIT_ST7789_PIN_BL,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        err = gpio_config(&backlight_gpio);
        if (err == ESP_OK) {
            err = gpio_set_level(CONFIG_HOMEKIT_ST7789_PIN_BL,
                    DISPLAY_CFG_BL_ACTIVE_HIGH ? 1 : 0);
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Backlight init failed: %s", esp_err_to_name(err));
        }
    }

    s_ready = true;
    ESP_LOGI(TAG, "ST7789 display ready (%dx%d)",
            CONFIG_HOMEKIT_ST7789_H_RES, CONFIG_HOMEKIT_ST7789_V_RES);
}

void display_support_show_boot(const char *accessory_name,
        const char *model, const char *setup_code)
{
    const uint8_t header_scale = display_header_scale();
    const uint8_t body_scale = display_body_scale();
    const int header_height = display_header_height();
    const int footer_top = display_footer_top();
    const int body_line_height = FONT_HEIGHT * body_scale;
    int body_gap = (footer_top - header_height - body_line_height * 3) / 4;
    int accessory_top;
    int model_top;
    int setup_top;
    int header_top;

    if (!s_ready) {
        return;
    }
    if (body_gap < 2) {
        body_gap = 2;
    }

    header_top = (header_height - FONT_HEIGHT * header_scale) / 2;
    if (header_top < 4) {
        header_top = 4;
    }
    accessory_top = header_height + body_gap;
    model_top = accessory_top + body_line_height + body_gap;
    setup_top = model_top + body_line_height + body_gap;

    display_fill_rect(0, 0, CONFIG_HOMEKIT_ST7789_H_RES,
            CONFIG_HOMEKIT_ST7789_V_RES, s_background);
    display_fill_rect(0, 0, CONFIG_HOMEKIT_ST7789_H_RES, header_height, s_header_background);
    display_fill_rect(0, header_height + 1, CONFIG_HOMEKIT_ST7789_H_RES, 2, s_divider);

    display_draw_text_line(header_top, "HOMEKIT", s_text, s_header_background, header_scale, true);
    display_draw_text_line(accessory_top, accessory_name, s_text, s_background, body_scale, false);
    display_draw_text_line(model_top, model, s_secondary_text, s_background, body_scale, false);
    display_draw_text_line(setup_top, setup_code, s_secondary_text, s_background, body_scale, false);
    display_draw_power_footer(false);
}

void display_support_show_power(bool is_on)
{
    if (!s_ready) {
        return;
    }
    display_draw_power_footer(is_on);
}

#else

void display_support_init(void)
{
}

void display_support_show_boot(const char *accessory_name,
        const char *model, const char *setup_code)
{
    (void) accessory_name;
    (void) model;
    (void) setup_code;
}

void display_support_show_power(bool is_on)
{
    (void) is_on;
}

#endif
