#ifndef DASHBOARD_WEATHER_H_
#define DASHBOARD_WEATHER_H_

#include <stddef.h>

#include "esp_err.h"

void dashboard_weather_get_defaults(char *location_out, size_t location_out_size,
        char *summary_out, size_t summary_out_size);
esp_err_t dashboard_weather_fetch(char *location_out, size_t location_out_size,
        char *summary_out, size_t summary_out_size);

#endif /* DASHBOARD_WEATHER_H_ */
