#ifndef DASHBOARD_POEM_H_
#define DASHBOARD_POEM_H_

#include <stddef.h>

#include "esp_err.h"

void dashboard_poem_select_fallback(char *out, size_t out_size, size_t index);
esp_err_t dashboard_poem_fetch(char *out, size_t out_size);

#endif /* DASHBOARD_POEM_H_ */
