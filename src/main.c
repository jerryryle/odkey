#include "app.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "log_buffer.h"

static const char *TAG = "main";

void app_main() {
    // Initialize log buffer first to capture all logs from startup
    if (!log_buffer_init()) {
        ESP_LOGE(TAG, "Failed to initialize log buffer");
        return;
    }

    ESP_LOGI(TAG, "Starting ODKey");

    if (!app_init()) {
        ESP_LOGE(TAG, "System initialization failed");
        return;
    }

    // Application is now event-driven - the main task can exit now
    ESP_LOGI(TAG, "ODKey initialized successfully, main task exiting. Godspeed!");
}
