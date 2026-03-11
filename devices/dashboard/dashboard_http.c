#include "dashboard_http.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"

esp_err_t dashboard_http_get_text(const char *url, int timeout_ms,
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
