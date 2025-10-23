#include "button.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "nvs.h"
#include "nvs_odkey.h"
#include "program.h"

static const char *TAG = "button";

#define BUTTON_DEFAULT_DEBOUNCE_MS 50
#define BUTTON_DEFAULT_REPEAT_DELAY_MS 225

// Forward declarations
static void program_completion_callback(void *arg);

static struct {
    uint8_t gpio_pin;
    uint32_t debounce_ms;
    uint32_t repeat_delay_ms;
    TimerHandle_t button_timer;
    bool interrupt_enabled;
} g_button_state = {0};

// ISR handler
static void IRAM_ATTR button_isr_handler(void *arg) {
    (void)arg;

    // Disable interrupt temporarily
    gpio_intr_disable(g_button_state.gpio_pin);
    g_button_state.interrupt_enabled = false;

    // Reconfigure timer period for debounce and start timer
    xTimerChangePeriodFromISR(
        g_button_state.button_timer, pdMS_TO_TICKS(g_button_state.debounce_ms), NULL);
    xTimerStartFromISR(g_button_state.button_timer, NULL);
}

// Button timer callback - handles debounce and restart delays
static void button_timer_callback(TimerHandle_t xTimer) {
    (void)xTimer;

    // Check if button is still pressed (active low with pull-up)
    int level = gpio_get_level(g_button_state.gpio_pin);

    if (level == 0) {  // Button still pressed
        ESP_LOGI(TAG, "Button pressed/held, starting flash program execution");
        // Execute flash program with completion callback for auto-restart
        if (!program_execute(PROGRAM_TYPE_FLASH, program_completion_callback, NULL)) {
            ESP_LOGW(TAG, "Failed to execute flash program");
        }
    } else {
        ESP_LOGI(TAG, "Button released");
        // Button released - re-enable interrupts for next press
        if (!g_button_state.interrupt_enabled) {
            ESP_LOGI(TAG, "Re-enabling interrupts");
            gpio_intr_enable(g_button_state.gpio_pin);
            g_button_state.interrupt_enabled = true;
        }
    }
}

// Program completion callback - checks if button is still pressed and starts restart
// timer if so
static void program_completion_callback(void *arg) {
    (void)arg;

    // Check if button is still pressed (active low with pull-up)
    int level = gpio_get_level(g_button_state.gpio_pin);

    if (level == 0) {  // Button still pressed
        ESP_LOGI(TAG,
                 "Button still pressed, starting restart timer (%lu ms)",
                 (unsigned long)g_button_state.repeat_delay_ms);
        // Reconfigure timer period for restart delay
        xTimerChangePeriod(g_button_state.button_timer,
                           pdMS_TO_TICKS(g_button_state.repeat_delay_ms),
                           0);
        xTimerStart(g_button_state.button_timer, 0);
    } else {
        ESP_LOGI(TAG, "Button released");
        // Button released - re-enable interrupts for next press
        if (!g_button_state.interrupt_enabled) {
            ESP_LOGI(TAG, "Re-enabling interrupts");
            gpio_intr_enable(g_button_state.gpio_pin);
            g_button_state.interrupt_enabled = true;
        }
    }
}

bool button_init(uint8_t gpio_pin) {
    // Read configuration from NVS with defaults
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return false;
    }

    // Read debounce delay with default
    uint32_t debounce_ms = BUTTON_DEFAULT_DEBOUNCE_MS;
    size_t required_size = sizeof(debounce_ms);
    ret = nvs_get_blob(
        nvs_handle, NVS_KEY_BUTTON_DEBOUNCE_MS, &debounce_ms, &required_size);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGD(TAG,
                 "Button debounce not found in NVS, using default %lu ms",
                 (unsigned long)debounce_ms);
    } else if (ret != ESP_OK) {
        ESP_LOGW(
            TAG, "Failed to read button debounce from NVS: %s", esp_err_to_name(ret));
    }

    // Read repeat delay with default
    uint32_t repeat_delay_ms = BUTTON_DEFAULT_REPEAT_DELAY_MS;
    required_size = sizeof(repeat_delay_ms);
    ret = nvs_get_blob(
        nvs_handle, NVS_KEY_BUTTON_REPEAT_DELAY_MS, &repeat_delay_ms, &required_size);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGD(TAG,
                 "Button repeat delay not found in NVS, using default %lu ms",
                 (unsigned long)repeat_delay_ms);
    } else if (ret != ESP_OK) {
        ESP_LOGW(TAG,
                 "Failed to read button repeat delay from NVS: %s",
                 esp_err_to_name(ret));
    }

    nvs_close(nvs_handle);

    if (debounce_ms == 0) {
        ESP_LOGE(TAG, "Debounce time must be greater than 0");
        return false;
    }

    if (repeat_delay_ms == 0) {
        ESP_LOGE(TAG, "Repeat delay must be greater than 0");
        return false;
    }

    // Store configuration
    g_button_state.gpio_pin = gpio_pin;
    g_button_state.debounce_ms = debounce_ms;
    g_button_state.repeat_delay_ms = repeat_delay_ms;
    g_button_state.interrupt_enabled = false;

    // Configure GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,  // Falling edge (button press)
    };

    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG, "Failed to configure GPIO %d: %s", gpio_pin, esp_err_to_name(ret));
        return false;
    }

    // Create unified button timer (will be reconfigured for different delays)
    g_button_state.button_timer =
        xTimerCreate("button_timer",
                     pdMS_TO_TICKS(debounce_ms),  // Initial period
                     pdFALSE,                     // One-shot timer
                     NULL,
                     button_timer_callback);

    if (g_button_state.button_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create button timer");
        return false;
    }

    // Install ISR service
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install ISR service: %s", esp_err_to_name(ret));
        xTimerDelete(g_button_state.button_timer, 0);
        return false;
    }

    // Add ISR handler
    ret = gpio_isr_handler_add(gpio_pin, button_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ISR handler: %s", esp_err_to_name(ret));
        xTimerDelete(g_button_state.button_timer, 0);
        return false;
    }

    // Enable interrupt
    gpio_intr_enable(gpio_pin);
    g_button_state.interrupt_enabled = true;

    ESP_LOGI(
        TAG,
        "Button initialized on GPIO %d with %lu ms debounce and %lu ms repeat delay",
        gpio_pin,
        (unsigned long)debounce_ms,
        (unsigned long)repeat_delay_ms);

    return true;
}
