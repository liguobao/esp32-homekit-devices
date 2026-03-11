#include "dashboard_weather.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "dashboard_http.h"

typedef struct {
    const char *name;
    const char *label;
    const char *latitude;
    const char *longitude;
} weather_city_t;

typedef struct {
    const char *alias;
    const char *label;
} weather_location_alias_t;

static const weather_city_t s_weather_cities[] = {
    { "shanghai", "上海", "31.23", "121.47" },
    { "beijing", "北京", "39.90", "116.40" },
    { "shenzhen", "深圳", "22.54", "114.06" },
    { "guangzhou", "广州", "23.13", "113.27" },
};

static const weather_location_alias_t s_location_aliases[] = {
    { "shanghai", "上海" },
    { "上海", "上海" },
    { "beijing", "北京" },
    { "北京", "北京" },
    { "shenzhen", "深圳" },
    { "深圳", "深圳" },
    { "guangzhou", "广州" },
    { "广州", "广州" },
    { "hong kong", "香港" },
    { "hong_kong", "香港" },
    { "hongkong", "香港" },
    { "香港", "香港" },
    { "kowloon", "九龙" },
    { "九龍", "九龙" },
    { "九龙", "九龙" },
    { "macau", "澳门" },
    { "macao", "澳门" },
    { "澳门", "澳门" },
    { "taipei", "台北" },
    { "台北", "台北" },
    { "hangzhou", "杭州" },
    { "杭州", "杭州" },
    { "suzhou", "苏州" },
    { "苏州", "苏州" },
    { "nanjing", "南京" },
    { "南京", "南京" },
    { "wuhan", "武汉" },
    { "武汉", "武汉" },
    { "chengdu", "成都" },
    { "成都", "成都" },
    { "chongqing", "重庆" },
    { "重庆", "重庆" },
};

static int dashboard_weather_ascii_strcasecmp(const char *left, const char *right)
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

static void dashboard_weather_copy_location_label(char *out, size_t out_size,
        const char *src)
{
    if (!out || out_size == 0) {
        return;
    }

    if (!src || src[0] == '\0') {
        out[0] = '\0';
        return;
    }

    strncpy(out, src, out_size);
    out[out_size - 1] = '\0';
}

static void dashboard_weather_sanitize_ascii(char *text)
{
    char *read_ptr = text;
    char *write_ptr = text;

    while (read_ptr && *read_ptr) {
        unsigned char ch = (unsigned char) *read_ptr++;

        if (ch == '\r' || ch == '\n') {
            break;
        }
        if (ch == 0xC2 && (unsigned char) *read_ptr == 0xB0) {
            *write_ptr++ = 'C';
            read_ptr++;
            continue;
        }
        if (ch == 0xE2 && (unsigned char) read_ptr[0] == 0x84 &&
                (unsigned char) read_ptr[1] == 0x83) {
            *write_ptr++ = 'C';
            read_ptr += 2;
            continue;
        }
        if (ch >= 32 && ch < 127) {
            *write_ptr++ = (char) ch;
        } else if (ch == '\t') {
            *write_ptr++ = ' ';
        }
    }
    *write_ptr = '\0';

    while (write_ptr > text && write_ptr[-1] == ' ') {
        *--write_ptr = '\0';
    }
}

static void dashboard_weather_compact_spaces(char *text)
{
    char *read_ptr = text;
    char *write_ptr = text;
    bool last_was_space = true;

    while (read_ptr && *read_ptr) {
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

static void dashboard_weather_uppercase_ascii(char *text)
{
    while (text && *text) {
        if (*text >= 'a' && *text <= 'z') {
            *text = (char) (*text - ('a' - 'A'));
        }
        text++;
    }
}

static bool dashboard_weather_json_extract_number(const char *json, const char *key,
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

static bool dashboard_weather_json_extract_string(const char *json, const char *key,
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
        out[pos++] = ch;
    }
    out[pos] = '\0';
    return pos > 0;
}

static const char *dashboard_weather_lookup_location_label(const char *src)
{
    if (!src || src[0] == '\0') {
        return NULL;
    }

    for (size_t i = 0; i < sizeof(s_location_aliases) / sizeof(s_location_aliases[0]); i++) {
        if (dashboard_weather_ascii_strcasecmp(src, s_location_aliases[i].alias) == 0) {
            return s_location_aliases[i].label;
        }
    }
    return NULL;
}

static void dashboard_weather_normalize_location_label(char *out, size_t out_size,
        const char *src, const char *fallback)
{
    const char *label = dashboard_weather_lookup_location_label(src);

    if (!out || out_size == 0) {
        return;
    }

    if (label) {
        dashboard_weather_copy_location_label(out, out_size, label);
        return;
    }
    if (src && src[0] != '\0') {
        dashboard_weather_copy_location_label(out, out_size, src);
        return;
    }
    dashboard_weather_copy_location_label(out, out_size, fallback);
}

static const weather_city_t *dashboard_weather_select_city(void)
{
    const weather_city_t *city = &s_weather_cities[0];

    for (size_t i = 0; i < sizeof(s_weather_cities) / sizeof(s_weather_cities[0]); i++) {
        if (dashboard_weather_ascii_strcasecmp(CONFIG_HOMEKIT_DASHBOARD_WEATHER_LOCATION,
                s_weather_cities[i].name) == 0) {
            city = &s_weather_cities[i];
            break;
        }
    }
    return city;
}

static void dashboard_weather_build_location_label(char *out, size_t out_size)
{
    const weather_city_t *default_city = dashboard_weather_select_city();
    const char *src = CONFIG_HOMEKIT_DASHBOARD_WEATHER_LOCATION;
    const char *fallback = src && src[0] != '\0' ? default_city->label : "LOCATING";

    dashboard_weather_normalize_location_label(out, out_size, src, fallback);
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

static const char *dashboard_weather_normalized_phrase(const char *desc)
{
    if (!desc || desc[0] == '\0') {
        return "Weather --";
    }
    if (strstr(desc, "WEATHER")) {
        return "Weather --";
    }
    if (strstr(desc, "THUNDER") || strstr(desc, "STORM")) {
        return "Thunderstorm";
    }
    if (strstr(desc, "SNOW SHOWER")) {
        return "Snow Showers";
    }
    if (strstr(desc, "SNOW")) {
        return "Snow";
    }
    if (strstr(desc, "SHOWER")) {
        return "Showers";
    }
    if (strstr(desc, "DRIZZLE")) {
        return "Drizzle";
    }
    if (strstr(desc, "RAIN")) {
        return "Rain";
    }
    if (strstr(desc, "FOG") || strstr(desc, "MIST")) {
        return "Fog";
    }
    if (strstr(desc, "PARTLY CLOUDY")) {
        return "Partly Cloudy";
    }
    if (strstr(desc, "MOSTLY CLEAR")) {
        return "Mostly Clear";
    }
    if (strstr(desc, "OVERCAST") || strstr(desc, "CLOUDY")) {
        return "Cloudy";
    }
    if (strstr(desc, "CLEAR") || strstr(desc, "SUN")) {
        return "Clear";
    }
    return "Weather";
}

static void dashboard_weather_compact_summary(char *out, size_t out_size,
        const char *weather_text)
{
    char buffer[40];
    char *end_ptr;
    const char *desc;
    float temperature_value;
    int temperature_int;

    if (!out || out_size == 0) {
        return;
    }

    if (!weather_text || weather_text[0] == '\0') {
        strncpy(out, "Weather --", out_size);
        out[out_size - 1] = '\0';
        return;
    }

    strncpy(buffer, weather_text, sizeof(buffer));
    buffer[sizeof(buffer) - 1] = '\0';
    dashboard_weather_sanitize_ascii(buffer);
    dashboard_weather_compact_spaces(buffer);
    dashboard_weather_uppercase_ascii(buffer);

    if (buffer[0] == '+') {
        memmove(buffer, buffer + 1, strlen(buffer));
    }

    temperature_value = strtof(buffer, &end_ptr);
    if (end_ptr == buffer) {
        strncpy(out, dashboard_weather_normalized_phrase(buffer), out_size);
        out[out_size - 1] = '\0';
        return;
    }

    if (*end_ptr == 'C' || *end_ptr == 'F') {
        end_ptr++;
    }
    while (*end_ptr == ' ') {
        end_ptr++;
    }
    desc = end_ptr;

    temperature_int = temperature_value >= 0.0f ?
            (int) (temperature_value + 0.5f) :
            (int) (temperature_value - 0.5f);

    if (desc[0] == '\0') {
        snprintf(out, out_size, "%dC", temperature_int);
    } else {
        snprintf(out, out_size, "%dC %s",
                temperature_int, dashboard_weather_normalized_phrase(desc));
    }
    out[out_size - 1] = '\0';
}

static void dashboard_weather_set_unavailable(char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }

    strncpy(out, "Weather --", out_size);
    out[out_size - 1] = '\0';
}

static esp_err_t dashboard_weather_fetch_open_meteo_at(const char *latitude,
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
    if (!dashboard_weather_json_extract_number(current_section, "\"temperature_2m\"",
            temp_text, sizeof(temp_text))) {
        return ESP_FAIL;
    }
    if (!dashboard_weather_json_extract_number(current_section, "\"weather_code\"",
            code_text, sizeof(code_text))) {
        return ESP_FAIL;
    }

    weather_code = (int) strtol(code_text, NULL, 10);
    snprintf(out, out_size, "%sC %s", temp_text, dashboard_weather_code_text(weather_code));
    out[out_size - 1] = '\0';
    dashboard_weather_compact_spaces(out);
    return ESP_OK;
}

static esp_err_t dashboard_weather_fetch_open_meteo(char *out, size_t out_size)
{
    const weather_city_t *city = dashboard_weather_select_city();

    return dashboard_weather_fetch_open_meteo_at(city->latitude, city->longitude, out, out_size);
}

static esp_err_t dashboard_weather_fetch_ipwhois_location(char *location_out,
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
    if (!dashboard_weather_json_extract_string(response, "\"city\"", city, sizeof(city)) ||
            !dashboard_weather_json_extract_number(response, "\"latitude\"",
                    latitude_out, latitude_out_size) ||
            !dashboard_weather_json_extract_number(response, "\"longitude\"",
                    longitude_out, longitude_out_size)) {
        return ESP_FAIL;
    }

    dashboard_weather_normalize_location_label(location_out, location_out_size, city, "LOCAL");
    return ESP_OK;
}

static esp_err_t dashboard_weather_fetch_wttr_text(const char *location,
        char *out, size_t out_size)
{
    char url[160];

    if (location && location[0] != '\0') {
        snprintf(url, sizeof(url), "https://wttr.in/%s?format=%%25t%%20%%25C", location);
    } else {
        strncpy(url, "https://wttr.in/?format=%25t%20%25C", sizeof(url));
        url[sizeof(url) - 1] = '\0';
    }

    if (dashboard_http_get_text(url, 5000, out, out_size) == ESP_OK) {
        dashboard_weather_sanitize_ascii(out);
        dashboard_weather_compact_spaces(out);
        if (out[0] != '\0') {
            return ESP_OK;
        }
    }

    if (location && location[0] != '\0') {
        snprintf(url, sizeof(url), "https://w.r2049.cn/en-wttr?location=%s", location);
    } else {
        strncpy(url, "https://w.r2049.cn/en-wttr", sizeof(url));
        url[sizeof(url) - 1] = '\0';
    }

    if (dashboard_http_get_text(url, 5000, out, out_size) == ESP_OK) {
        char *line_end = strchr(out, '\n');

        if (line_end) {
            *line_end = '\0';
        }
        dashboard_weather_sanitize_ascii(out);
        dashboard_weather_compact_spaces(out);
        if (out[0] != '\0') {
            return ESP_OK;
        }
    }

    return ESP_FAIL;
}

void dashboard_weather_get_defaults(char *location_out, size_t location_out_size,
        char *summary_out, size_t summary_out_size)
{
    dashboard_weather_build_location_label(location_out, location_out_size);
    dashboard_weather_set_unavailable(summary_out, summary_out_size);
}

esp_err_t dashboard_weather_fetch(char *location_out, size_t location_out_size,
        char *summary_out, size_t summary_out_size)
{
    char latitude[24];
    char longitude[24];
    char raw_weather[40];

    if (!location_out || location_out_size == 0 || !summary_out || summary_out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (CONFIG_HOMEKIT_DASHBOARD_WEATHER_LOCATION[0] != '\0') {
        dashboard_weather_build_location_label(location_out, location_out_size);

        if (dashboard_weather_fetch_open_meteo(raw_weather, sizeof(raw_weather)) != ESP_OK &&
                dashboard_weather_fetch_wttr_text(CONFIG_HOMEKIT_DASHBOARD_WEATHER_LOCATION,
                        raw_weather, sizeof(raw_weather)) != ESP_OK) {
            dashboard_weather_set_unavailable(raw_weather, sizeof(raw_weather));
        }
    } else {
        if (dashboard_weather_fetch_ipwhois_location(location_out, location_out_size,
                latitude, sizeof(latitude), longitude, sizeof(longitude)) != ESP_OK ||
                dashboard_weather_fetch_open_meteo_at(latitude, longitude,
                        raw_weather, sizeof(raw_weather)) != ESP_OK) {
            dashboard_weather_build_location_label(location_out, location_out_size);
            if (dashboard_weather_fetch_wttr_text(NULL, raw_weather, sizeof(raw_weather)) != ESP_OK &&
                    dashboard_weather_fetch_open_meteo(raw_weather, sizeof(raw_weather)) != ESP_OK) {
                dashboard_weather_set_unavailable(raw_weather, sizeof(raw_weather));
            }
        }
    }

    dashboard_weather_compact_summary(summary_out, summary_out_size, raw_weather);
    return ESP_OK;
}
