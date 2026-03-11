#ifndef DASHBOARD_HTTP_H_
#define DASHBOARD_HTTP_H_

#include <stddef.h>

#include "esp_err.h"

esp_err_t dashboard_http_get_text(const char *url, int timeout_ms,
        char *out, size_t out_size);

#endif /* DASHBOARD_HTTP_H_ */
