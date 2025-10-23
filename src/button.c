#include "button.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "program.h"

static const char *TAG = "button";

static struct {
    uint8_t gpio_pin;
    uint32_t debounce_ms;
    TimerHandle_t debounce_timer;
    bool interrupt_enabled;
} g_button_state = {0};

// ISR handler
static void IRAM_ATTR button_isr_handler(void *arg) {
    (void)arg;

    // Disable interrupt temporarily
    gpio_intr_disable(g_button_state.gpio_pin);
    g_button_state.interrupt_enabled = false;

    // Start debounce timer
    xTimerStartFromISR(g_button_state.debounce_timer, NULL);
}

// Debounce timer callback
static void debounce_timer_callback(TimerHandle_t xTimer) {
    (void)xTimer;

    // Check if button is still pressed (active low with pull-up)
    int level = gpio_get_level(g_button_state.gpio_pin);

    if (level == 0) {  // Button still pressed
        // Execute flash program
        if (!program_execute(PROGRAM_TYPE_FLASH)) {
            ESP_LOGW(TAG, "Failed to execute flash program");
        }
    }

    // Re-enable interrupt for next press
    gpio_intr_enable(g_button_state.gpio_pin);
    g_button_state.interrupt_enabled = true;
}

bool button_init(uint8_t gpio_pin, uint32_t debounce_ms) {
    if (debounce_ms == 0) {
        ESP_LOGE(TAG, "Debounce time must be greater than 0");
        return false;
    }

    // Store configuration
    g_button_state.gpio_pin = gpio_pin;
    g_button_state.debounce_ms = debounce_ms;
    g_button_state.interrupt_enabled = false;

    // Configure GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,  // Falling edge (button press)
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG, "Failed to configure GPIO %d: %s", gpio_pin, esp_err_to_name(ret));
        return false;
    }

    // Create debounce timer
    g_button_state.debounce_timer = xTimerCreate("button_debounce",
                                                 pdMS_TO_TICKS(debounce_ms),
                                                 pdFALSE,  // One-shot timer
                                                 NULL,
                                                 debounce_timer_callback);

    if (g_button_state.debounce_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create debounce timer");
        return false;
    }

    // Install ISR service
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install ISR service: %s", esp_err_to_name(ret));
        xTimerDelete(g_button_state.debounce_timer, 0);
        return false;
    }

    // Add ISR handler
    ret = gpio_isr_handler_add(gpio_pin, button_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ISR handler: %s", esp_err_to_name(ret));
        xTimerDelete(g_button_state.debounce_timer, 0);
        return false;
    }

    // Enable interrupt
    gpio_intr_enable(gpio_pin);
    g_button_state.interrupt_enabled = true;

    ESP_LOGI(TAG,
             "Button initialized on GPIO %d with %lu ms debounce",
             gpio_pin,
             (unsigned long)debounce_ms);

    return true;
}
