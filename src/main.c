#include "app.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

void app_main() {
    ESP_LOGI(TAG, "Starting ODKey");

    if (!app_init()) {
        ESP_LOGE(TAG, "System initialization failed");
        return;
    }

    // Application is now event-driven - the main task can exit now
    ESP_LOGI(TAG, "ODKey initialized successfully, main task exiting. Godspeed!");
}
