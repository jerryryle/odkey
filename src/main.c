#include "app.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "main";

void app_main() {
    ESP_LOGI(TAG, "Starting ODKey");

    // Initialize NVS (kept in main for future config loading)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

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
