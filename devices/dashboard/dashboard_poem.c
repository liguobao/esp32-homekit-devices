#include "dashboard_poem.h"

#include <stdbool.h>
#include <string.h>

#include "dashboard_http.h"

static void dashboard_poem_compact_spaces(char *text)
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

void dashboard_poem_select_fallback(char *out, size_t out_size, size_t index)
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

esp_err_t dashboard_poem_fetch(char *out, size_t out_size)
{
    esp_err_t err;

    err = dashboard_http_get_text("https://v1.jinrishici.com/all.txt", 8000, out, out_size);
    if (err != ESP_OK) {
        return err;
    }

    dashboard_poem_compact_spaces(out);
    if (out[0] == '\0') {
        return ESP_FAIL;
    }
    return ESP_OK;
}
