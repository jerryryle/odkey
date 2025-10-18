#include "app.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

void app_main() {
    ESP_LOGI(TAG, "Starting ODKey");

    // TODO: Load application configuration from NVS here (future)

    if (!app_init()) {
        ESP_LOGE(TAG, "System initialization failed");
        return;
    }

    // Main loop
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
