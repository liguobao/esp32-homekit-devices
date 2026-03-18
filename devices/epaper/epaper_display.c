#include "epaper_display.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"

static const char *TAG = "epaper_display";

#define EPAPER_SPI_HOST SPI2_HOST
#define EPAPER_BUFFER_SIZE \
    (((CONFIG_HOMEKIT_EPAPER_WIDTH * CONFIG_HOMEKIT_EPAPER_HEIGHT) + 7) / 8)

#define FONT_WIDTH 5
#define FONT_HEIGHT 7

typedef struct {
    char code;
    uint8_t rows[FONT_HEIGHT];
} font_glyph_t;

static const font_glyph_t s_font[] = {
    { ' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00} },
    { '%', {0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13} },
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

static const uint8_t s_waveform_full[159] = {
    0x80, 0x48, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x48, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x48, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x48, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x01, 0x00, 0x08, 0x01, 0x00, 0x02,
    0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x00, 0x00, 0x00,
    0x22, 0x17, 0x41, 0x00, 0x32, 0x20,
};

static const uint8_t s_waveform_partial[159] = {
    0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x00, 0x00, 0x00,
    0x02, 0x17, 0x41, 0xB0, 0x32, 0x28,
};

static spi_device_handle_t s_spi;
static bool s_ready;
static bool s_partial_ready;
static uint8_t s_framebuffer[EPAPER_BUFFER_SIZE];
static const char *const s_icon_droplet[] = {
    "     #     ",
    "    ###    ",
    "   #####   ",
    "  #######  ",
    " ######### ",
    " ######### ",
    " ######### ",
    " ######### ",
    "  #######  ",
    "   #####   ",
    "   #####   ",
    "  #######  ",
    "  #######  ",
    "   #####   ",
    "    ###    ",
};
static const char *const s_icon_thermometer[] = {
    "    ###    ",
    "   #####   ",
    "   #####   ",
    "    ###    ",
    "    ###    ",
    "    ###    ",
    "    ###    ",
    "    ###    ",
    "    ###    ",
    "    ###    ",
    "    ###    ",
    "   #####   ",
    "  #######  ",
    " ######### ",
    " ######### ",
};
static const char *const s_icon_battery[] = {
    " #########  ",
    "#         ##",
    "# #######  #",
    "# #######  #",
    "# #######  #",
    "# #######  #",
    "#         ##",
    " #########  ",
};

static const font_glyph_t *epaper_find_glyph(char c)
{
    for (size_t i = 0; i < sizeof(s_font) / sizeof(s_font[0]); i++) {
        if (s_font[i].code == c) {
            return &s_font[i];
        }
    }
    return &s_font[0];
}

static char epaper_normalize_char(char c)
{
    if (c >= 'a' && c <= 'z') {
        return (char) (c - ('a' - 'A'));
    }
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        return c;
    }
    switch (c) {
    case ' ':
    case '%':
    case '-':
    case '.':
    case ':':
    case '/':
        return c;
    default:
        return ' ';
    }
}

static void epaper_spi_send(const uint8_t *buffer, size_t length_bits)
{
    spi_transaction_t transaction = {
        .length = length_bits,
        .tx_buffer = buffer,
    };

    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &transaction));
}

static void epaper_send_command(uint8_t command)
{
    gpio_set_level(CONFIG_HOMEKIT_EPAPER_PIN_DC, 0);
    gpio_set_level(CONFIG_HOMEKIT_EPAPER_PIN_CS, 0);
    epaper_spi_send(&command, 8);
    gpio_set_level(CONFIG_HOMEKIT_EPAPER_PIN_CS, 1);
}

static void epaper_send_data_byte(uint8_t value)
{
    gpio_set_level(CONFIG_HOMEKIT_EPAPER_PIN_DC, 1);
    gpio_set_level(CONFIG_HOMEKIT_EPAPER_PIN_CS, 0);
    epaper_spi_send(&value, 8);
    gpio_set_level(CONFIG_HOMEKIT_EPAPER_PIN_CS, 1);
}

static void epaper_send_data(const uint8_t *data, size_t len)
{
    gpio_set_level(CONFIG_HOMEKIT_EPAPER_PIN_DC, 1);
    gpio_set_level(CONFIG_HOMEKIT_EPAPER_PIN_CS, 0);
    epaper_spi_send(data, len * 8);
    gpio_set_level(CONFIG_HOMEKIT_EPAPER_PIN_CS, 1);
}

static void epaper_wait_idle(void)
{
    while (gpio_get_level(CONFIG_HOMEKIT_EPAPER_PIN_BUSY) == 1) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

static void epaper_set_window(uint16_t x_start, uint16_t y_start,
        uint16_t x_end, uint16_t y_end)
{
    epaper_send_command(0x44);
    epaper_send_data_byte((uint8_t) ((x_start >> 3) & 0xFF));
    epaper_send_data_byte((uint8_t) ((x_end >> 3) & 0xFF));

    epaper_send_command(0x45);
    epaper_send_data_byte((uint8_t) (y_start & 0xFF));
    epaper_send_data_byte((uint8_t) ((y_start >> 8) & 0xFF));
    epaper_send_data_byte((uint8_t) (y_end & 0xFF));
    epaper_send_data_byte((uint8_t) ((y_end >> 8) & 0xFF));
}

static void epaper_set_cursor(uint16_t x_start, uint16_t y_start)
{
    epaper_send_command(0x4E);
    epaper_send_data_byte((uint8_t) (x_start & 0xFF));

    epaper_send_command(0x4F);
    epaper_send_data_byte((uint8_t) (y_start & 0xFF));
    epaper_send_data_byte((uint8_t) ((y_start >> 8) & 0xFF));
}

static void epaper_set_lut(const uint8_t *lut)
{
    epaper_send_command(0x32);
    epaper_send_data(lut, 153);
    epaper_wait_idle();

    epaper_send_command(0x3F);
    epaper_send_data_byte(lut[153]);

    epaper_send_command(0x03);
    epaper_send_data_byte(lut[154]);

    epaper_send_command(0x04);
    epaper_send_data_byte(lut[155]);
    epaper_send_data_byte(lut[156]);
    epaper_send_data_byte(lut[157]);

    epaper_send_command(0x2C);
    epaper_send_data_byte(lut[158]);
}

static void epaper_turn_on_display(void)
{
    epaper_send_command(0x22);
    epaper_send_data_byte(0xC7);
    epaper_send_command(0x20);
    epaper_wait_idle();
}

static void epaper_turn_on_display_partial(void)
{
    epaper_send_command(0x22);
    epaper_send_data_byte(0xCF);
    epaper_send_command(0x20);
    epaper_wait_idle();
}

static void epaper_panel_init_full(void)
{
    gpio_set_level(CONFIG_HOMEKIT_EPAPER_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(CONFIG_HOMEKIT_EPAPER_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(CONFIG_HOMEKIT_EPAPER_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    epaper_wait_idle();
    epaper_send_command(0x12);
    epaper_wait_idle();

    epaper_send_command(0x01);
    epaper_send_data_byte(0xC7);
    epaper_send_data_byte(0x00);
    epaper_send_data_byte(0x01);

    epaper_send_command(0x11);
    epaper_send_data_byte(0x01);

    epaper_set_window(0, CONFIG_HOMEKIT_EPAPER_WIDTH - 1,
            CONFIG_HOMEKIT_EPAPER_HEIGHT - 1, 0);

    epaper_send_command(0x3C);
    epaper_send_data_byte(0x01);

    epaper_send_command(0x18);
    epaper_send_data_byte(0x80);

    epaper_send_command(0x22);
    epaper_send_data_byte(0xB1);
    epaper_send_command(0x20);

    epaper_set_cursor(0, CONFIG_HOMEKIT_EPAPER_HEIGHT - 1);
    epaper_wait_idle();
    epaper_set_lut(s_waveform_full);
}

static void epaper_panel_init_partial(void)
{
    gpio_set_level(CONFIG_HOMEKIT_EPAPER_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(CONFIG_HOMEKIT_EPAPER_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(CONFIG_HOMEKIT_EPAPER_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    epaper_wait_idle();
    epaper_set_lut(s_waveform_partial);

    epaper_send_command(0x37);
    epaper_send_data_byte(0x00);
    epaper_send_data_byte(0x00);
    epaper_send_data_byte(0x00);
    epaper_send_data_byte(0x00);
    epaper_send_data_byte(0x00);
    epaper_send_data_byte(0x40);
    epaper_send_data_byte(0x00);
    epaper_send_data_byte(0x00);
    epaper_send_data_byte(0x00);
    epaper_send_data_byte(0x00);

    epaper_send_command(0x3C);
    epaper_send_data_byte(0x80);

    epaper_send_command(0x22);
    epaper_send_data_byte(0xC0);
    epaper_send_command(0x20);
    epaper_wait_idle();
}

static void epaper_commit_full(void)
{
    epaper_send_command(0x24);
    epaper_send_data(s_framebuffer, sizeof(s_framebuffer));
    epaper_turn_on_display();
}

static void epaper_commit_partial_base(void)
{
    epaper_send_command(0x24);
    epaper_send_data(s_framebuffer, sizeof(s_framebuffer));
    epaper_send_command(0x26);
    epaper_send_data(s_framebuffer, sizeof(s_framebuffer));
    epaper_turn_on_display();
}

static void epaper_commit_partial(void)
{
    epaper_send_command(0x24);
    epaper_send_data(s_framebuffer, sizeof(s_framebuffer));
    epaper_turn_on_display_partial();
}

static void epaper_clear(bool black)
{
    memset(s_framebuffer, black ? 0x00 : 0xFF, sizeof(s_framebuffer));
}

static void epaper_draw_pixel(int x, int y, bool black)
{
    size_t index;
    uint8_t mask;

    if (x < 0 || y < 0 ||
            x >= CONFIG_HOMEKIT_EPAPER_WIDTH ||
            y >= CONFIG_HOMEKIT_EPAPER_HEIGHT) {
        return;
    }

    index = (size_t) y * (CONFIG_HOMEKIT_EPAPER_WIDTH / 8) + (size_t) (x / 8);
    mask = (uint8_t) (1U << (7 - (x & 0x07)));
    if (black) {
        s_framebuffer[index] &= (uint8_t) ~mask;
    } else {
        s_framebuffer[index] |= mask;
    }
}

static void epaper_fill_rect(int x, int y, int width, int height, bool black)
{
    for (int row = y; row < y + height; row++) {
        for (int col = x; col < x + width; col++) {
            epaper_draw_pixel(col, row, black);
        }
    }
}

static void epaper_draw_rect_outline(int x, int y, int width, int height, bool black)
{
    if (width <= 0 || height <= 0) {
        return;
    }

    epaper_fill_rect(x, y, width, 1, black);
    epaper_fill_rect(x, y + height - 1, width, 1, black);
    epaper_fill_rect(x, y, 1, height, black);
    epaper_fill_rect(x + width - 1, y, 1, height, black);
}

static void epaper_draw_sprite(int x, int y, const char *const *rows,
        size_t row_count, bool black)
{
    if (!rows) {
        return;
    }

    for (size_t row = 0; row < row_count; row++) {
        const char *cols = rows[row];

        if (!cols) {
            continue;
        }
        for (size_t col = 0; cols[col] != '\0'; col++) {
            if (cols[col] != ' ') {
                epaper_draw_pixel(x + (int) col, y + (int) row, black);
            }
        }
    }
}

static void epaper_draw_char(int x, int y, char c, bool black, uint8_t scale)
{
    const font_glyph_t *glyph = epaper_find_glyph(epaper_normalize_char(c));

    for (int row = 0; row < FONT_HEIGHT; row++) {
        for (int col = 0; col < FONT_WIDTH; col++) {
            if (glyph->rows[row] & (1U << (FONT_WIDTH - col - 1))) {
                epaper_fill_rect(x + col * scale, y + row * scale,
                        scale, scale, black);
            }
        }
    }
}

static int epaper_measure_text(const char *text, uint8_t scale)
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

static int epaper_round_int(float value)
{
    return value >= 0.0f ? (int) (value + 0.5f) : (int) (value - 0.5f);
}

static void epaper_draw_text_at(int x, int y, const char *text, bool black,
        uint8_t scale)
{
    int cursor_x = x;

    if (!text) {
        return;
    }
    while (*text) {
        epaper_draw_char(cursor_x, y, *text, black, scale);
        cursor_x += (FONT_WIDTH + 1) * scale;
        text++;
    }
}

static void epaper_draw_text_in_rect(int x, int y, int width, int height,
        const char *text, bool black, uint8_t scale)
{
    int text_width = epaper_measure_text(text, scale);
    int text_height = FONT_HEIGHT * scale;
    int draw_x = x + (width - text_width) / 2;
    int draw_y = y + (height - text_height) / 2;

    if (draw_x < x) {
        draw_x = x;
    }
    if (draw_y < y) {
        draw_y = y;
    }
    epaper_draw_text_at(draw_x, draw_y, text, black, scale);
}

static void epaper_draw_text_line(int y, const char *text, bool black,
        uint8_t scale, bool center)
{
    int cursor_x = 8;
    int width = epaper_measure_text(text, scale);

    if (center && width > 0 && width < CONFIG_HOMEKIT_EPAPER_WIDTH) {
        cursor_x = (CONFIG_HOMEKIT_EPAPER_WIDTH - width) / 2;
    }

    epaper_draw_text_at(cursor_x, y, text, black, scale);
}

static void epaper_split_text_lines(const char *text, size_t chars_per_line,
        char *line1, size_t line1_len, char *line2, size_t line2_len)
{
    size_t text_len = text ? strlen(text) : 0;
    size_t first_len = 0;
    size_t second_len = 0;

    if (line1 && line1_len > 0) {
        line1[0] = '\0';
    }
    if (line2 && line2_len > 0) {
        line2[0] = '\0';
    }
    if (!text || text_len == 0 || chars_per_line == 0) {
        return;
    }

    first_len = text_len < chars_per_line ? text_len : chars_per_line;
    if (line1 && line1_len > 0) {
        if (first_len >= line1_len) {
            first_len = line1_len - 1;
        }
        memcpy(line1, text, first_len);
        line1[first_len] = '\0';
    }

    if (text_len > chars_per_line && line2 && line2_len > 0) {
        second_len = text_len - chars_per_line;
        if (second_len > chars_per_line) {
            second_len = chars_per_line;
        }
        if (second_len >= line2_len) {
            second_len = line2_len - 1;
        }
        memcpy(line2, text + chars_per_line, second_len);
        line2[second_len] = '\0';
    }
}

static void epaper_draw_boot_screen(void)
{
    epaper_clear(false);
    epaper_fill_rect(0, 0, CONFIG_HOMEKIT_EPAPER_WIDTH, 22, true);
    epaper_draw_text_line(4, "HOMEKIT EPAPER", false, 1, true);
    epaper_draw_text_line(34, "WAVESHARE V2", true, 2, true);
    epaper_draw_text_line(66, "AUDIO SD RTC", true, 2, true);
    epaper_draw_text_line(98, "SHTC3 BATTERY", true, 2, true);
    epaper_draw_text_line(130, "HOMEKIT READY", true, 2, true);
    epaper_draw_text_line(164, "PLEASE WAIT", true, 2, true);
}

static void epaper_draw_dashboard_screen(const epaper_dashboard_state_t *state)
{
    char hour_text[8] = "--";
    char minute_text[8] = "--";
    char value_text[24];
    char music_line1[17];
    char music_line2[17];
    const char *music_text;
    const char *music_status;

    epaper_clear(false);

    if (!state) {
        epaper_draw_text_line(84, "NO STATE", true, 3, true);
        return;
    }

    epaper_fill_rect(8, 10, 92, 88, true);
    epaper_fill_rect(102, 102, 92, 88, true);

    if (state->rtc_valid) {
        snprintf(hour_text, sizeof(hour_text), "%02d", state->rtc_time.hour);
        snprintf(minute_text, sizeof(minute_text), "%02d", state->rtc_time.minute);
    }
    epaper_draw_text_in_rect(8, 10, 92, 88, hour_text, false, 7);
    epaper_draw_text_in_rect(102, 102, 92, 88, minute_text, false, 7);

    epaper_draw_sprite(125, 12, s_icon_droplet,
            sizeof(s_icon_droplet) / sizeof(s_icon_droplet[0]), true);
    if (state->reading_valid) {
        snprintf(value_text, sizeof(value_text), "%d%%",
                epaper_round_int(state->reading.humidity_pct));
    } else {
        snprintf(value_text, sizeof(value_text), "--");
    }
    epaper_draw_text_at(146, 14, value_text, true, 2);

    epaper_draw_sprite(125, 35, s_icon_thermometer,
            sizeof(s_icon_thermometer) / sizeof(s_icon_thermometer[0]), true);
    if (state->reading_valid) {
        snprintf(value_text, sizeof(value_text), "%dC",
                epaper_round_int(state->reading.temperature_c));
    } else {
        snprintf(value_text, sizeof(value_text), "--");
    }
    epaper_draw_text_at(146, 38, value_text, true, 2);

    epaper_draw_text_at(107, 58, "BLE:", true, 1);
    if (state->ble_count_valid) {
        snprintf(value_text, sizeof(value_text), "%u", state->ble_count);
    } else {
        snprintf(value_text, sizeof(value_text), "--");
    }
    epaper_draw_text_at(141, 58, value_text, true, 1);

    epaper_draw_text_at(107, 77, "WIFI:", true, 1);
    if (state->wifi_score_valid) {
        snprintf(value_text, sizeof(value_text), "%u", state->wifi_score);
    } else {
        snprintf(value_text, sizeof(value_text), "--");
    }
    epaper_draw_text_at(143, 77, value_text, true, 1);

    epaper_draw_text_at(4, 113, "MUSIC:", true, 1);
    music_text = state->sd_mounted ? state->music_file : "NO SD CARD";
    music_status = state->sd_mounted ? state->music_status : "STOP";
    epaper_split_text_lines(music_text, 15, music_line1, sizeof(music_line1),
            music_line2, sizeof(music_line2));
    epaper_draw_text_at(4, 131, music_line1, true, 1);
    if (music_line2[0] != '\0') {
        epaper_draw_text_at(4, 145, music_line2, true, 1);
    }
    epaper_draw_text_at(4, 159, music_status, true, 1);

    epaper_draw_sprite(18, 176, s_icon_battery,
            sizeof(s_icon_battery) / sizeof(s_icon_battery[0]), true);
    if (state->battery_valid && state->battery.present) {
        snprintf(value_text, sizeof(value_text), "%u%%", state->battery.level_pct);
    } else {
        snprintf(value_text, sizeof(value_text), "--");
    }
    epaper_draw_text_at(46, 179, value_text, true, 1);

    epaper_draw_rect_outline(0, 0, CONFIG_HOMEKIT_EPAPER_WIDTH,
            CONFIG_HOMEKIT_EPAPER_HEIGHT, true);
}

esp_err_t epaper_display_init(void)
{
    esp_err_t err;
    spi_bus_config_t bus_config = {
        .mosi_io_num = CONFIG_HOMEKIT_EPAPER_PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = CONFIG_HOMEKIT_EPAPER_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = sizeof(s_framebuffer),
    };
    spi_device_interface_config_t device_config = {
        .spics_io_num = -1,
        .clock_speed_hz = CONFIG_HOMEKIT_EPAPER_SPI_CLOCK_HZ,
        .mode = 0,
        .queue_size = 1,
    };
    gpio_config_t gpio_config_data = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << CONFIG_HOMEKIT_EPAPER_PIN_RST) |
                (1ULL << CONFIG_HOMEKIT_EPAPER_PIN_DC) |
                (1ULL << CONFIG_HOMEKIT_EPAPER_PIN_CS),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };

    if (s_ready) {
        return ESP_OK;
    }

    err = gpio_config(&gpio_config_data);
    if (err != ESP_OK) {
        return err;
    }

    gpio_config_data.mode = GPIO_MODE_INPUT;
    gpio_config_data.pin_bit_mask = 1ULL << CONFIG_HOMEKIT_EPAPER_PIN_BUSY;
    err = gpio_config(&gpio_config_data);
    if (err != ESP_OK) {
        return err;
    }

    gpio_set_level(CONFIG_HOMEKIT_EPAPER_PIN_CS, 1);
    gpio_set_level(CONFIG_HOMEKIT_EPAPER_PIN_DC, 1);
    gpio_set_level(CONFIG_HOMEKIT_EPAPER_PIN_RST, 1);

    err = spi_bus_initialize(EPAPER_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        return err;
    }

    err = spi_bus_add_device(EPAPER_SPI_HOST, &device_config, &s_spi);
    if (err != ESP_OK) {
        return err;
    }

    epaper_panel_init_full();
    s_ready = true;
    ESP_LOGI(TAG, "ePaper display ready (%dx%d)",
            CONFIG_HOMEKIT_EPAPER_WIDTH, CONFIG_HOMEKIT_EPAPER_HEIGHT);
    return ESP_OK;
}

void epaper_display_show_boot(void)
{
    if (!s_ready) {
        return;
    }

    epaper_draw_boot_screen();
    epaper_commit_full();
    epaper_commit_partial_base();
    epaper_panel_init_partial();
    s_partial_ready = true;
}

void epaper_display_show_dashboard(const epaper_dashboard_state_t *state)
{
    if (!s_ready) {
        return;
    }

    epaper_draw_dashboard_screen(state);
    if (s_partial_ready) {
        epaper_commit_partial();
    } else {
        epaper_commit_full();
        epaper_commit_partial_base();
        epaper_panel_init_partial();
        s_partial_ready = true;
    }
}
