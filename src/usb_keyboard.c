#include "usb_keyboard.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "usb_keyboard_keys.h"

static const char *TAG = "usb_keyboard";

// Queue and task configuration
#define KEYBOARD_QUEUE_DEPTH 10
#define KEYBOARD_TASK_STACK_SIZE configMINIMAL_STACK_SIZE
#define KEYBOARD_TASK_PRIORITY 6

// Queue item structure
typedef struct {
    uint8_t modifier;
    uint8_t keys[6];
    uint8_t count;
} keyboard_report_t;

// Private variables
static bool g_initialized = false;
static uint8_t g_interface_num = 0;
static QueueHandle_t g_keyboard_queue = NULL;
static TaskHandle_t g_keyboard_task_handle = NULL;

// Keyboard task function
static void keyboard_task(void *pvParameters) {
    ESP_LOGI(TAG, "Keyboard task started");

    for (;;) {
        keyboard_report_t report;
        // Wait for a keyboard report from the queue
        if (xQueueReceive(g_keyboard_queue, &report, portMAX_DELAY) == pdTRUE) {
            // Wait for USB HID to be ready before sending
            while (!tud_hid_n_ready(g_interface_num)) {
                vTaskDelay(pdMS_TO_TICKS(1));
            }

            // Send the HID keyboard report
            tud_hid_n_keyboard_report(g_interface_num, 0, report.modifier, report.keys);
            ESP_LOGD(TAG, "Sent keyboard report: modifier=0x%02X, keys=%d", report.modifier, report.count);
        }
    }
}

bool usb_keyboard_init(uint8_t interface_num) {
    if (g_initialized) {
        return true;
    }

    // Create the keyboard report queue
    g_keyboard_queue = xQueueCreate(KEYBOARD_QUEUE_DEPTH, sizeof(keyboard_report_t));
    if (g_keyboard_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create keyboard queue");
        return false;
    }

    // Create the keyboard task
    BaseType_t ret = xTaskCreate(keyboard_task, "keyboard_task", KEYBOARD_TASK_STACK_SIZE, NULL, KEYBOARD_TASK_PRIORITY, &g_keyboard_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create keyboard task");
        vQueueDelete(g_keyboard_queue);
        g_keyboard_queue = NULL;
        return false;
    }

    g_interface_num = interface_num;
    g_initialized = true;
    ESP_LOGI(TAG, "USB keyboard module initialized on interface %d", interface_num);

    return true;
}

bool usb_keyboard_send_keys(uint8_t modifier, const uint8_t *keys, uint8_t count) {
    if (!g_initialized || g_keyboard_queue == NULL) {
        return false;
    }

    // Create keyboard report structure
    keyboard_report_t report;
    report.modifier = modifier;
    report.count = (count > 6) ? 6 : count;

    // Initialize keys array to zero
    memset(report.keys, 0, sizeof(report.keys));

    // Copy keycodes to array (up to 6)
    if (keys != NULL && count > 0) {
        for (uint8_t i = 0; i < report.count; i++) {
            report.keys[i] = keys[i];
        }
    }

    // Try to enqueue the report (non-blocking)
    BaseType_t ret = xQueueSend(g_keyboard_queue, &report, 0);
    if (ret != pdTRUE) {
        ESP_LOGW(TAG, "Keyboard queue is full, dropping report");
        return false;
    }

    return true;
}
