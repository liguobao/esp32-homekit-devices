#include "dashboard_content.h"

#include <stdbool.h>
#include <stddef.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "dashboard_poem.h"
#include "dashboard_weather.h"
#include "dual_panel_display.h"

#define DASHBOARD_POEM_REFRESH_SEC (5 * 60)

static const char *TAG = "dashboard_content";

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_content_task;
static bool s_started;
static bool s_wifi_connected;
static bool s_poem_refresh_requested;
static size_t s_poem_refresh_serial;
static TickType_t s_last_weather_fetch_tick;
static TickType_t s_last_poem_fetch_tick;

static void dashboard_content_maybe_refresh_weather(void)
{
    TickType_t now_ticks = xTaskGetTickCount();
    bool should_fetch = false;
    char location_text[24];
    char weather_text[40];

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_wifi_connected &&
            (s_last_weather_fetch_tick == 0 ||
            (now_ticks - s_last_weather_fetch_tick) >=
                    pdMS_TO_TICKS(CONFIG_HOMEKIT_DASHBOARD_WEATHER_REFRESH_SEC * 1000))) {
        should_fetch = true;
    }
    xSemaphoreGive(s_lock);

    if (!should_fetch) {
        return;
    }

    if (dashboard_weather_fetch(location_text, sizeof(location_text),
            weather_text, sizeof(weather_text)) == ESP_OK) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_last_weather_fetch_tick = now_ticks;
        xSemaphoreGive(s_lock);
        dual_panel_display_set_location_weather(location_text, weather_text);
    } else {
        ESP_LOGW(TAG, "Weather fetch failed");
    }
}

static void dashboard_content_maybe_refresh_poem(void)
{
    TickType_t now_ticks = xTaskGetTickCount();
    bool should_fetch = false;
    bool force_refresh = false;
    bool can_fetch_online = false;
    char poem_text[64];
    size_t fallback_index;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    fallback_index = s_poem_refresh_serial;
    can_fetch_online = s_wifi_connected;
    force_refresh = s_poem_refresh_requested;
    if (force_refresh) {
        should_fetch = true;
        s_poem_refresh_requested = false;
    } else if (s_last_poem_fetch_tick == 0 ||
            (now_ticks - s_last_poem_fetch_tick) >=
                    pdMS_TO_TICKS(DASHBOARD_POEM_REFRESH_SEC * 1000)) {
        should_fetch = true;
    }
    xSemaphoreGive(s_lock);

    if (!should_fetch) {
        return;
    }

    if (!can_fetch_online || dashboard_poem_fetch(poem_text, sizeof(poem_text)) != ESP_OK) {
        dashboard_poem_select_fallback(poem_text, sizeof(poem_text), fallback_index);
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_last_poem_fetch_tick = now_ticks;
    s_poem_refresh_serial++;
    xSemaphoreGive(s_lock);

    dual_panel_display_set_poem(poem_text);
}

static void dashboard_content_task(void *arg)
{
    (void) arg;

    for (;;) {
        dashboard_content_maybe_refresh_weather();
        dashboard_content_maybe_refresh_poem();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void dashboard_content_event_handler(void *arg, esp_event_base_t event_base,
        int32_t event_id, void *event_data)
{
    (void) arg;
    (void) event_data;

    if (!s_lock) {
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_wifi_connected = true;
        s_last_weather_fetch_tick = 0;
        s_last_poem_fetch_tick = 0;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
    }
    xSemaphoreGive(s_lock);
}

void dashboard_content_init(void)
{
    char location_text[24];
    char weather_text[40];
    char poem_text[64];

    if (s_lock) {
        return;
    }

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        ESP_LOGE(TAG, "Failed to create content mutex");
        return;
    }

    dashboard_weather_get_defaults(location_text, sizeof(location_text),
            weather_text, sizeof(weather_text));
    dual_panel_display_set_location_weather(location_text, weather_text);
    dashboard_poem_select_fallback(poem_text, sizeof(poem_text), 0);
    dual_panel_display_set_poem(poem_text);
}

void dashboard_content_start(void)
{
    if (!s_lock || s_started) {
        return;
    }

    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
            dashboard_content_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
            dashboard_content_event_handler, NULL));

    xTaskCreate(dashboard_content_task, "dashboard_content", 5 * 1024, NULL, 3, &s_content_task);
    s_started = true;
}

void dashboard_content_request_poem_refresh(void)
{
    if (!s_lock) {
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_poem_refresh_requested = true;
    xSemaphoreGive(s_lock);
}
