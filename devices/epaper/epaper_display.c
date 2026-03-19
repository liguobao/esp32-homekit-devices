#include "epaper_display.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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
#define CJK_GLYPH_WIDTH 16
#define CJK_GLYPH_HEIGHT 16

#define EPAPER_CARD_WIDTH 88
#define EPAPER_CARD_HEIGHT 88
#define EPAPER_CARD_LEFT_X 8
#define EPAPER_CARD_RIGHT_X 104
#define EPAPER_CARD_TOP_Y 8
#define EPAPER_CARD_BOTTOM_Y 104

#define EPAPER_CLOCK_BLOCK_X EPAPER_CARD_LEFT_X
#define EPAPER_CLOCK_BLOCK_Y EPAPER_CARD_TOP_Y
#define EPAPER_CLOCK_BLOCK_WIDTH EPAPER_CARD_WIDTH
#define EPAPER_CLOCK_BLOCK_HEIGHT EPAPER_CARD_HEIGHT
#define EPAPER_CLOCK_TIME_BOX_X (EPAPER_CLOCK_BLOCK_X + 6)
#define EPAPER_CLOCK_TIME_BOX_Y (EPAPER_CLOCK_BLOCK_Y + 46)
#define EPAPER_CLOCK_TIME_BOX_WIDTH (EPAPER_CLOCK_BLOCK_WIDTH - 12)
#define EPAPER_CLOCK_TIME_BOX_HEIGHT 18
#define EPAPER_CLOCK_SECOND_BOX_X (EPAPER_CLOCK_BLOCK_X + 26)
#define EPAPER_CLOCK_SECOND_BOX_Y (EPAPER_CLOCK_BLOCK_Y + 68)
#define EPAPER_CLOCK_SECOND_BOX_WIDTH 36
#define EPAPER_CLOCK_SECOND_BOX_HEIGHT 16

typedef struct {
    char code;
    uint8_t rows[FONT_HEIGHT];
} font_glyph_t;

typedef struct {
    uint16_t codepoint;
    uint8_t rows[CJK_GLYPH_HEIGHT * 2];
} cjk16_glyph_t;

typedef struct {
    int year;
    int month;
    int day;
    bool is_leap_month;
} epaper_lunar_date_t;

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

#include "../common/dashboard_cjk16_font.inc"

static const uint32_t s_lunar_data[] = {
    0x04BD8, 0x04AE0, 0x0A570, 0x054D5, 0x0D260, 0x0D950, 0x16554, 0x056A0,
    0x09AD0, 0x055D2, 0x04AE0, 0x0A5B6, 0x0A4D0, 0x0D250, 0x1D255, 0x0B540,
    0x0D6A0, 0x0ADA2, 0x095B0, 0x14977, 0x04970, 0x0A4B0, 0x0B4B5, 0x06A50,
    0x06D40, 0x1AB54, 0x02B60, 0x09570, 0x052F2, 0x04970, 0x06566, 0x0D4A0,
    0x0EA50, 0x06E95, 0x05AD0, 0x02B60, 0x186E3, 0x092E0, 0x1C8D7, 0x0C950,
    0x0D4A0, 0x1D8A6, 0x0B550, 0x056A0, 0x1A5B4, 0x025D0, 0x092D0, 0x0D2B2,
    0x0A950, 0x0B557, 0x06CA0, 0x0B550, 0x15355, 0x04DA0, 0x0A5D0, 0x14573,
    0x052D0, 0x0A9A8, 0x0E950, 0x06AA0, 0x0AEA6, 0x0AB50, 0x04B60, 0x0AAE4,
    0x0A570, 0x05260, 0x0F263, 0x0D950, 0x05B57, 0x056A0, 0x096D0, 0x04DD5,
    0x04AD0, 0x0A4D0, 0x0D4D4, 0x0D250, 0x0D558, 0x0B540, 0x0B5A0, 0x195A6,
    0x095B0, 0x049B0, 0x0A974, 0x0A4B0, 0x0B27A, 0x06A50, 0x06D40, 0x0AF46,
    0x0AB60, 0x09570, 0x04AF5, 0x04970, 0x064B0, 0x074A3, 0x0EA50, 0x06B58,
    0x05AC0, 0x0AB60, 0x096D5, 0x092E0, 0x0C960, 0x0D954, 0x0D4A0, 0x0DA50,
    0x07552, 0x056A0, 0x0ABB7, 0x025D0, 0x092D0, 0x0CAB5, 0x0A950, 0x0B4A0,
    0x0BAA4, 0x0AD50, 0x055D9, 0x04BA0, 0x0A5B0, 0x15176, 0x052B0, 0x0A930,
    0x07954, 0x06AA0, 0x0AD50, 0x05B52, 0x04B60, 0x0A6E6, 0x0A4E0, 0x0D260,
    0x0EA65, 0x0D530, 0x05AA0, 0x076A3, 0x096D0, 0x04BD7, 0x04AD0, 0x0A4D0,
    0x1D0B6, 0x0D250, 0x0D520, 0x0DD45, 0x0B5A0, 0x056D0, 0x055B2, 0x049B0,
    0x0A577, 0x0A4B0, 0x0AA50, 0x1B255, 0x06D20, 0x0ADA0, 0x14B63
};

static const char *const s_lunar_month_names[] = {
    "", "正", "二", "三", "四", "五", "六", "七", "八", "九", "十", "冬", "腊",
};

static const char *const s_lunar_day_names[] = {
    "",
    "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
    "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十",
    "二十一", "二十二", "二十三", "二十四", "二十五", "二十六", "二十七", "二十八", "二十九", "三十",
};

static const char *const s_weekday_names[] = {
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
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
static uint8_t s_previous_framebuffer[EPAPER_BUFFER_SIZE];
static const font_glyph_t *epaper_find_glyph(char c)
{
    for (size_t i = 0; i < sizeof(s_font) / sizeof(s_font[0]); i++) {
        if (s_font[i].code == c) {
            return &s_font[i];
        }
    }
    return &s_font[0];
}

static const cjk16_glyph_t *epaper_find_cjk16_glyph(uint16_t codepoint)
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

static bool epaper_utf8_next_codepoint(const char **text, uint16_t *codepoint)
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

static void epaper_prepare_full_frame_transfer(void)
{
    epaper_set_window(0, CONFIG_HOMEKIT_EPAPER_HEIGHT - 1,
            CONFIG_HOMEKIT_EPAPER_WIDTH - 1, 0);
    epaper_set_cursor(0, CONFIG_HOMEKIT_EPAPER_HEIGHT - 1);
}

static void epaper_send_full_frame(uint8_t command, const uint8_t *buffer)
{
    epaper_prepare_full_frame_transfer();
    epaper_send_command(command);
    epaper_send_data(buffer, sizeof(s_framebuffer));
}

static void epaper_send_region_frame(uint8_t command, const uint8_t *buffer,
        int aligned_x, int y, int byte_width, int height,
        int ram_y_start, int ram_y_end)
{
    epaper_set_window((uint16_t) aligned_x, (uint16_t) ram_y_start,
            (uint16_t) (aligned_x + byte_width * 8 - 1), (uint16_t) ram_y_end);
    epaper_set_cursor((uint16_t) (aligned_x / 8), (uint16_t) ram_y_start);

    epaper_send_command(command);
    for (int row = 0; row < height; row++) {
        size_t index = (size_t) (y + row) * (CONFIG_HOMEKIT_EPAPER_WIDTH / 8) +
                (size_t) (aligned_x / 8);

        epaper_send_data(&buffer[index], (size_t) byte_width);
    }
}

static void epaper_copy_region_to_previous(int aligned_x, int y,
        int byte_width, int height)
{
    for (int row = 0; row < height; row++) {
        size_t index = (size_t) (y + row) * (CONFIG_HOMEKIT_EPAPER_WIDTH / 8) +
                (size_t) (aligned_x / 8);

        memcpy(&s_previous_framebuffer[index], &s_framebuffer[index],
                (size_t) byte_width);
    }
}

static void epaper_commit_full(void)
{
    epaper_send_full_frame(0x24, s_framebuffer);
    epaper_send_full_frame(0x26, s_framebuffer);
    epaper_turn_on_display();
    memcpy(s_previous_framebuffer, s_framebuffer, sizeof(s_framebuffer));
}

static void epaper_commit_partial_base(void)
{
    epaper_send_full_frame(0x24, s_framebuffer);
    epaper_send_full_frame(0x26, s_framebuffer);
    epaper_turn_on_display();
    memcpy(s_previous_framebuffer, s_framebuffer, sizeof(s_framebuffer));
}

static void epaper_commit_partial_frame(void)
{
    epaper_send_full_frame(0x26, s_previous_framebuffer);
    epaper_send_full_frame(0x24, s_framebuffer);
    epaper_turn_on_display_partial();
    memcpy(s_previous_framebuffer, s_framebuffer, sizeof(s_framebuffer));
}

static void epaper_commit_partial_region(int x, int y, int width, int height)
{
    int aligned_x;
    int aligned_x_end;
    int byte_width;
    int ram_y_start;
    int ram_y_end;

    if (width <= 0 || height <= 0) {
        return;
    }

    aligned_x = x & ~0x07;
    aligned_x_end = (x + width - 1) | 0x07;

    if (aligned_x < 0) {
        aligned_x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (aligned_x_end >= CONFIG_HOMEKIT_EPAPER_WIDTH) {
        aligned_x_end = CONFIG_HOMEKIT_EPAPER_WIDTH - 1;
    }
    if (y + height > CONFIG_HOMEKIT_EPAPER_HEIGHT) {
        height = CONFIG_HOMEKIT_EPAPER_HEIGHT - y;
    }
    if (aligned_x > aligned_x_end || height <= 0) {
        return;
    }

    byte_width = ((aligned_x_end - aligned_x) + 1) / 8;
    ram_y_start = CONFIG_HOMEKIT_EPAPER_HEIGHT - 1 - y;
    ram_y_end = CONFIG_HOMEKIT_EPAPER_HEIGHT - (y + height);

    epaper_send_region_frame(0x26, s_previous_framebuffer,
            aligned_x, y, byte_width, height, ram_y_start, ram_y_end);
    epaper_send_region_frame(0x24, s_framebuffer,
            aligned_x, y, byte_width, height, ram_y_start, ram_y_end);
    epaper_turn_on_display_partial();
    epaper_copy_region_to_previous(aligned_x, y, byte_width, height);
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

static void epaper_draw_cjk16_char(int x, int y, uint16_t codepoint, bool black)
{
    const cjk16_glyph_t *glyph = epaper_find_cjk16_glyph(codepoint);

    if (!glyph) {
        epaper_draw_char(x + 5, y + 4, '?', black, 1);
        return;
    }

    for (int row = 0; row < CJK_GLYPH_HEIGHT; row++) {
        uint16_t bits = (uint16_t) ((glyph->rows[row * 2] << 8) |
                glyph->rows[row * 2 + 1]);

        for (int col = 0; col < CJK_GLYPH_WIDTH; col++) {
            if (bits & (0x8000U >> col)) {
                epaper_draw_pixel(x + col, y + row, black);
            }
        }
    }
}

static void epaper_draw_mixed_text_at(int x, int y, const char *text, bool black,
        uint8_t ascii_scale)
{
    const char *cursor = text;
    int cursor_x = x;

    while (cursor && *cursor) {
        uint16_t codepoint;

        if (!epaper_utf8_next_codepoint(&cursor, &codepoint)) {
            break;
        }

        if (codepoint < 0x80) {
            epaper_draw_char(cursor_x,
                    y + ((CJK_GLYPH_HEIGHT - (FONT_HEIGHT * ascii_scale)) / 2),
                    (char) codepoint, black, ascii_scale);
            cursor_x += (FONT_WIDTH + 1) * ascii_scale;
        } else {
            epaper_draw_cjk16_char(cursor_x, y, codepoint, black);
            cursor_x += CJK_GLYPH_WIDTH;
        }
    }
}

static int epaper_lunar_leap_month(int year)
{
    if (year < 1900 || year >= 1900 + (int) (sizeof(s_lunar_data) / sizeof(s_lunar_data[0]))) {
        return 0;
    }
    return (int) (s_lunar_data[year - 1900] & 0x0F);
}

static int epaper_lunar_leap_days(int year)
{
    int leap_month = epaper_lunar_leap_month(year);

    if (leap_month == 0) {
        return 0;
    }
    return (s_lunar_data[year - 1900] & 0x10000U) ? 30 : 29;
}

static int epaper_lunar_month_days(int year, int month)
{
    if (month < 1 || month > 12 || year < 1900 ||
            year >= 1900 + (int) (sizeof(s_lunar_data) / sizeof(s_lunar_data[0]))) {
        return 29;
    }
    return (s_lunar_data[year - 1900] & (0x10000U >> month)) ? 30 : 29;
}

static int epaper_lunar_year_days(int year)
{
    int total = 348;
    uint32_t info;

    if (year < 1900 || year >= 1900 + (int) (sizeof(s_lunar_data) / sizeof(s_lunar_data[0]))) {
        return 0;
    }

    info = s_lunar_data[year - 1900];
    for (uint32_t bit = 0x8000U; bit > 0x08U; bit >>= 1) {
        if (info & bit) {
            total++;
        }
    }
    return total + epaper_lunar_leap_days(year);
}

static bool epaper_gregorian_to_lunar(const epaper_rtc_time_t *rtc_time,
        epaper_lunar_date_t *lunar_date)
{
    const int max_supported_year =
            1900 + (int) (sizeof(s_lunar_data) / sizeof(s_lunar_data[0])) - 1;
    struct tm base_tm = { 0 };
    struct tm current_tm = { 0 };
    time_t base_time;
    time_t current_time;
    int offset_days;
    int lunar_year;
    int leap_month;
    int lunar_month;
    int days_in_month = 0;
    bool is_leap_month = false;

    if (!rtc_time || !lunar_date || rtc_time->year < 1900 || rtc_time->year > max_supported_year ||
            rtc_time->month < 1 || rtc_time->month > 12 ||
            rtc_time->day < 1 || rtc_time->day > 31) {
        return false;
    }

    base_tm.tm_year = 1900 - 1900;
    base_tm.tm_mon = 0;
    base_tm.tm_mday = 31;
    base_tm.tm_hour = 12;

    current_tm.tm_year = rtc_time->year - 1900;
    current_tm.tm_mon = rtc_time->month - 1;
    current_tm.tm_mday = rtc_time->day;
    current_tm.tm_hour = 12;

    base_time = mktime(&base_tm);
    current_time = mktime(&current_tm);
    if (base_time == (time_t) -1 || current_time == (time_t) -1 || current_time < base_time) {
        return false;
    }

    offset_days = (int) ((current_time - base_time) / 86400);
    for (lunar_year = 1900; lunar_year <= max_supported_year && offset_days > 0; lunar_year++) {
        int year_days = epaper_lunar_year_days(lunar_year);

        offset_days -= year_days;
    }
    if (offset_days < 0) {
        offset_days += epaper_lunar_year_days(--lunar_year);
    }

    leap_month = epaper_lunar_leap_month(lunar_year);
    for (lunar_month = 1; lunar_month <= 12 && offset_days > 0; lunar_month++) {
        if (leap_month > 0 && lunar_month == (leap_month + 1) && !is_leap_month) {
            lunar_month--;
            is_leap_month = true;
            days_in_month = epaper_lunar_leap_days(lunar_year);
        } else {
            days_in_month = epaper_lunar_month_days(lunar_year, lunar_month);
        }

        offset_days -= days_in_month;

        if (is_leap_month && lunar_month == leap_month) {
            is_leap_month = false;
        }
    }

    if (offset_days == 0 && leap_month > 0 && lunar_month == leap_month + 1) {
        if (is_leap_month) {
            is_leap_month = false;
        } else {
            is_leap_month = true;
            lunar_month--;
        }
    }
    if (offset_days < 0) {
        offset_days += days_in_month;
        lunar_month--;
    }

    lunar_date->year = lunar_year;
    lunar_date->month = lunar_month;
    lunar_date->day = offset_days + 1;
    lunar_date->is_leap_month = is_leap_month;
    return true;
}

static void epaper_format_calendar_text(bool rtc_valid, const epaper_rtc_time_t *rtc_time,
        char *date_text, size_t date_text_len,
        char *lunar_text, size_t lunar_text_len)
{
    epaper_lunar_date_t lunar_date;

    if (date_text && date_text_len > 0) {
        date_text[0] = '\0';
    }
    if (lunar_text && lunar_text_len > 0) {
        lunar_text[0] = '\0';
    }

    if (!rtc_valid || !rtc_time) {
        if (date_text && date_text_len > 0) {
            snprintf(date_text, date_text_len, "---- -- --");
        }
        if (lunar_text && lunar_text_len > 0) {
            snprintf(lunar_text, lunar_text_len, "--");
        }
        return;
    }

    if (date_text && date_text_len > 0) {
        snprintf(date_text, date_text_len, "%04d-%02d-%02d",
                rtc_time->year, rtc_time->month, rtc_time->day);
    }

    if (lunar_text && lunar_text_len > 0 && epaper_gregorian_to_lunar(rtc_time, &lunar_date) &&
            lunar_date.month >= 1 && lunar_date.month <= 12 &&
            lunar_date.day >= 1 && lunar_date.day <= 30) {
        snprintf(lunar_text, lunar_text_len, "%s%s月%s",
                lunar_date.is_leap_month ? "闰" : "",
                s_lunar_month_names[lunar_date.month],
                s_lunar_day_names[lunar_date.day]);
    } else if (lunar_text && lunar_text_len > 0) {
        snprintf(lunar_text, lunar_text_len, "--");
    }
}

static void epaper_draw_datetime_block(bool rtc_valid, const epaper_rtc_time_t *rtc_time)
{
    char date_text[40];
    char lunar_text[40];
    char hm_text[6] = "--:--";
    const char *weekday_text = "---";

    epaper_fill_rect(EPAPER_CLOCK_BLOCK_X, EPAPER_CLOCK_BLOCK_Y,
            EPAPER_CLOCK_BLOCK_WIDTH, EPAPER_CLOCK_BLOCK_HEIGHT, false);
    epaper_draw_rect_outline(EPAPER_CLOCK_BLOCK_X, EPAPER_CLOCK_BLOCK_Y,
            EPAPER_CLOCK_BLOCK_WIDTH, EPAPER_CLOCK_BLOCK_HEIGHT, true);

    epaper_format_calendar_text(rtc_valid, rtc_time,
            date_text, sizeof(date_text),
            lunar_text, sizeof(lunar_text));

    epaper_draw_text_in_rect(EPAPER_CLOCK_BLOCK_X, EPAPER_CLOCK_BLOCK_Y + 10,
            EPAPER_CLOCK_BLOCK_WIDTH, 10, date_text, true, 1);
    epaper_draw_mixed_text_at(EPAPER_CLOCK_BLOCK_X + 12, EPAPER_CLOCK_BLOCK_Y + 28,
            lunar_text, true, 1);

    if (rtc_valid && rtc_time) {
        snprintf(hm_text, sizeof(hm_text), "%02d:%02d",
                rtc_time->hour, rtc_time->minute);
        if (rtc_time->weekday >= 0 && rtc_time->weekday < 7) {
            weekday_text = s_weekday_names[rtc_time->weekday];
        }
    }
    epaper_draw_text_in_rect(EPAPER_CLOCK_TIME_BOX_X, EPAPER_CLOCK_TIME_BOX_Y,
            EPAPER_CLOCK_TIME_BOX_WIDTH, EPAPER_CLOCK_TIME_BOX_HEIGHT,
            hm_text, true, 2);
    epaper_draw_text_in_rect(EPAPER_CLOCK_SECOND_BOX_X, EPAPER_CLOCK_SECOND_BOX_Y,
            EPAPER_CLOCK_SECOND_BOX_WIDTH, EPAPER_CLOCK_SECOND_BOX_HEIGHT,
            weekday_text, true, 2);
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

static void epaper_draw_sensor_block(const epaper_dashboard_state_t *state)
{
    char value_text[24];

    epaper_fill_rect(EPAPER_CARD_RIGHT_X, EPAPER_CARD_TOP_Y,
            EPAPER_CARD_WIDTH, EPAPER_CARD_HEIGHT, false);
    epaper_draw_rect_outline(EPAPER_CARD_RIGHT_X, EPAPER_CARD_TOP_Y,
            EPAPER_CARD_WIDTH, EPAPER_CARD_HEIGHT, true);
    epaper_draw_text_in_rect(EPAPER_CARD_RIGHT_X, EPAPER_CARD_TOP_Y + 8,
            EPAPER_CARD_WIDTH, 8, "TEMP", true, 1);
    if (state && state->reading_valid) {
        snprintf(value_text, sizeof(value_text), "%dC",
                epaper_round_int(state->reading.temperature_c));
    } else {
        snprintf(value_text, sizeof(value_text), "--");
    }
    epaper_draw_text_in_rect(EPAPER_CARD_RIGHT_X, EPAPER_CARD_TOP_Y + 22,
            EPAPER_CARD_WIDTH, 18, value_text, true, 2);

    epaper_draw_text_in_rect(EPAPER_CARD_RIGHT_X, EPAPER_CARD_TOP_Y + 50,
            EPAPER_CARD_WIDTH, 8, "HUM", true, 1);
    if (state && state->reading_valid) {
        snprintf(value_text, sizeof(value_text), "%d%%",
                epaper_round_int(state->reading.humidity_pct));
    } else {
        snprintf(value_text, sizeof(value_text), "--");
    }
    epaper_draw_text_in_rect(EPAPER_CARD_RIGHT_X, EPAPER_CARD_TOP_Y + 64,
            EPAPER_CARD_WIDTH, 16, value_text, true, 2);
}

static const char *epaper_music_action_text(const epaper_dashboard_state_t *state)
{
    if (!state) {
        return "PLAY";
    }
    if (strcmp(state->music_status, "PLAY") == 0) {
        return "STOP";
    }
    if (strcmp(state->music_status, "STOP") == 0) {
        return "PLAY";
    }
    return state->music_status[0] != '\0' ? state->music_status : "PLAY";
}

static void epaper_draw_music_block(const epaper_dashboard_state_t *state)
{
    char music_line1[17];
    char music_line2[17];
    const char *music_text;
    const char *music_action;

    epaper_fill_rect(EPAPER_CARD_LEFT_X, EPAPER_CARD_BOTTOM_Y,
            EPAPER_CARD_WIDTH, EPAPER_CARD_HEIGHT, false);
    epaper_draw_rect_outline(EPAPER_CARD_LEFT_X, EPAPER_CARD_BOTTOM_Y,
            EPAPER_CARD_WIDTH, EPAPER_CARD_HEIGHT, true);
    epaper_draw_text_in_rect(EPAPER_CARD_LEFT_X, EPAPER_CARD_BOTTOM_Y + 8,
            EPAPER_CARD_WIDTH, 8, "MUSIC", true, 1);
    music_text = (state && state->music_file[0] != '\0') ? state->music_file : "NO MUSIC";
    music_action = epaper_music_action_text(state);
    epaper_split_text_lines(music_text, 13, music_line1, sizeof(music_line1),
            music_line2, sizeof(music_line2));
    epaper_draw_text_at(EPAPER_CARD_LEFT_X + 5, EPAPER_CARD_BOTTOM_Y + 24,
            music_line1, true, 1);
    if (music_line2[0] != '\0') {
        epaper_draw_text_at(EPAPER_CARD_LEFT_X + 5, EPAPER_CARD_BOTTOM_Y + 38,
                music_line2, true, 1);
    }
    epaper_draw_text_in_rect(EPAPER_CARD_LEFT_X, EPAPER_CARD_BOTTOM_Y + 60,
            EPAPER_CARD_WIDTH, 8, music_action, true, 1);
}

static void epaper_draw_power_block(const epaper_dashboard_state_t *state)
{
    char value_text[24];

    epaper_fill_rect(EPAPER_CARD_RIGHT_X, EPAPER_CARD_BOTTOM_Y,
            EPAPER_CARD_WIDTH, EPAPER_CARD_HEIGHT, false);
    epaper_draw_rect_outline(EPAPER_CARD_RIGHT_X, EPAPER_CARD_BOTTOM_Y,
            EPAPER_CARD_WIDTH, EPAPER_CARD_HEIGHT, true);
    epaper_draw_text_in_rect(EPAPER_CARD_RIGHT_X, EPAPER_CARD_BOTTOM_Y + 8,
            EPAPER_CARD_WIDTH, 8, "POWER", true, 1);
    if (state && state->battery_valid && state->battery.present) {
        snprintf(value_text, sizeof(value_text), "%u%%", state->battery.level_pct);
    } else {
        snprintf(value_text, sizeof(value_text), "--");
    }
    epaper_draw_text_in_rect(EPAPER_CARD_RIGHT_X, EPAPER_CARD_BOTTOM_Y + 20,
            EPAPER_CARD_WIDTH, 18, value_text, true, 2);

    if (state && state->wifi_score_valid) {
        snprintf(value_text, sizeof(value_text), "WIFI:%u", state->wifi_score);
    } else {
        snprintf(value_text, sizeof(value_text), "WIFI:--");
    }
    epaper_draw_text_at(EPAPER_CARD_RIGHT_X + 8, EPAPER_CARD_BOTTOM_Y + 52,
            value_text, true, 1);

    if (state && state->ble_count_valid) {
        snprintf(value_text, sizeof(value_text), "BLE:%u", state->ble_count);
    } else {
        snprintf(value_text, sizeof(value_text), "BLE:--");
    }
    epaper_draw_text_at(EPAPER_CARD_RIGHT_X + 8, EPAPER_CARD_BOTTOM_Y + 64,
            value_text, true, 1);
}

static void epaper_draw_dashboard_screen(const epaper_dashboard_state_t *state)
{
    epaper_clear(false);

    if (!state) {
        epaper_draw_text_line(84, "NO STATE", true, 3, true);
        return;
    }

    epaper_draw_datetime_block(state->rtc_valid, &state->rtc_time);
    epaper_draw_sensor_block(state);
    epaper_draw_music_block(state);
    epaper_draw_power_block(state);
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
    s_partial_ready = false;
}

void epaper_display_show_dashboard(const epaper_dashboard_state_t *state)
{
    if (!s_ready) {
        return;
    }

    epaper_draw_dashboard_screen(state);
    if (s_partial_ready) {
        epaper_commit_partial_frame();
    } else {
        epaper_commit_partial_base();
        epaper_panel_init_partial();
        s_partial_ready = true;
    }
}

void epaper_display_refresh_clock(const epaper_rtc_time_t *rtc_time, bool rtc_valid)
{
    if (!s_ready || !s_partial_ready) {
        return;
    }

    epaper_draw_datetime_block(rtc_valid, rtc_time);
    epaper_commit_partial_region(EPAPER_CLOCK_BLOCK_X, EPAPER_CLOCK_BLOCK_Y,
            EPAPER_CLOCK_BLOCK_WIDTH, EPAPER_CLOCK_BLOCK_HEIGHT);
}

void epaper_display_refresh_sensor_block(const epaper_dashboard_state_t *state)
{
    if (!s_ready || !s_partial_ready) {
        return;
    }

    epaper_draw_sensor_block(state);
    epaper_commit_partial_region(EPAPER_CARD_RIGHT_X, EPAPER_CARD_TOP_Y,
            EPAPER_CARD_WIDTH, EPAPER_CARD_HEIGHT);
}

void epaper_display_refresh_music_block(const epaper_dashboard_state_t *state)
{
    if (!s_ready || !s_partial_ready) {
        return;
    }

    epaper_draw_music_block(state);
    epaper_commit_partial_region(EPAPER_CARD_LEFT_X, EPAPER_CARD_BOTTOM_Y,
            EPAPER_CARD_WIDTH, EPAPER_CARD_HEIGHT);
}

void epaper_display_refresh_power_block(const epaper_dashboard_state_t *state)
{
    if (!s_ready || !s_partial_ready) {
        return;
    }

    epaper_draw_power_block(state);
    epaper_commit_partial_region(EPAPER_CARD_RIGHT_X, EPAPER_CARD_BOTTOM_Y,
            EPAPER_CARD_WIDTH, EPAPER_CARD_HEIGHT);
}
