#include "vm_task.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "odkeyscript_vm.h"

static const char *TAG = "vm_task";

#define VM_TASK_STACK_SIZE 4096
#define VM_TASK_PRIORITY 5

// VM task state
typedef enum {
    VM_TASK_STATE_IDLE,
    VM_TASK_STATE_RUNNING
} vm_task_state_t;

// Program start request structure
typedef struct {
    const uint8_t *program;
    uint32_t program_size;
} vm_program_request_t;

// Global state
static TaskHandle_t g_vm_task_handle = NULL;
static QueueHandle_t g_program_queue = NULL;
static SemaphoreHandle_t g_state_mutex = NULL;
static EventGroupHandle_t g_halt_event_group = NULL;
static vm_task_state_t g_task_state = VM_TASK_STATE_IDLE;
static vm_hid_send_callback_t g_hid_send_callback = NULL;

// VM context (owned by VM task)
static vm_context_t g_vm_context;

// Event group bits
#define HALT_BIT (1 << 0)

// Delay callback for VM - interruptible by halt request
static void delay_callback(uint16_t ms) {
    // Wait for either the delay time or a halt signal
    EventBits_t bits = xEventGroupWaitBits(
        g_halt_event_group,
        HALT_BIT,
        pdFALSE,  // Don't clear the bit when we get it
        pdFALSE,  // Wait for any bit (not all bits)
        pdMS_TO_TICKS(ms));

    // If we got the halt bit, we were interrupted
    if (bits & HALT_BIT) {
        ESP_LOGD(TAG, "Delay interrupted by halt request");
    }
}

// Helper function to check halt request
static bool halt_requested(void) {
    EventBits_t bits = xEventGroupGetBits(g_halt_event_group);
    return (bits & HALT_BIT) != 0;
}

// Helper function to set task state
static void set_task_state(vm_task_state_t state) {
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_task_state = state;
    xSemaphoreGive(g_state_mutex);
}

// VM task function
static void vm_task_function(void *pvParameters) {
    (void)pvParameters;
    vm_program_request_t request;

    // Initialize VM context
    if (!vm_init(&g_vm_context)) {
        ESP_LOGE(TAG, "Failed to initialize VM context, task exiting");
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        if (xQueueReceive(g_program_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        xEventGroupClearBits(g_halt_event_group, HALT_BIT);
        set_task_state(VM_TASK_STATE_RUNNING);

        ESP_LOGI(TAG, "Starting program execution (%lu bytes)", (unsigned long)request.program_size);

        // Start VM
        if (vm_start(&g_vm_context, request.program, request.program_size, g_hid_send_callback, delay_callback) == VM_ERROR_NONE) {
            // Run VM step by step
            vm_error_t result = VM_ERROR_NONE;
            while (vm_running(&g_vm_context) && !halt_requested()) {
                result = vm_step(&g_vm_context);
                if (result != VM_ERROR_NONE) {
                    ESP_LOGE(TAG, "VM step failed: %s", vm_error_to_string(result));
                    break;
                }
            }

            if (halt_requested()) {
                ESP_LOGI(TAG, "Program halted by request");
            } else {
                ESP_LOGI(TAG, "Program completed successfully");
                uint32_t instructions, keys_pressed, keys_released;
                vm_get_stats(&g_vm_context, &instructions, &keys_pressed, &keys_released);
                ESP_LOGI(TAG, "VM Stats - Instructions: %lu, Keys Pressed: %lu, Keys Released: %lu", instructions, keys_pressed, keys_released);
            }
        } else {
            ESP_LOGE(TAG, "Failed to start VM");
        }

        set_task_state(VM_TASK_STATE_IDLE);
    }
}

bool vm_task_init(vm_hid_send_callback_t hid_send_callback) {
    if (g_vm_task_handle != NULL) {
        return true;  // Already initialized
    }

    if (hid_send_callback == NULL) {
        ESP_LOGE(TAG, "HID send callback cannot be NULL");
        return false;
    }

    g_hid_send_callback = hid_send_callback;

    // Create mutex for state protection
    g_state_mutex = xSemaphoreCreateMutex();
    if (g_state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return false;
    }

    // Create event group for halt signaling
    g_halt_event_group = xEventGroupCreate();
    if (g_halt_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create halt event group");
        vSemaphoreDelete(g_state_mutex);
        return false;
    }

    // Create queue for program requests
    g_program_queue = xQueueCreate(1, sizeof(vm_program_request_t));
    if (g_program_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create program queue");
        vEventGroupDelete(g_halt_event_group);
        vSemaphoreDelete(g_state_mutex);
        return false;
    }

    // Create VM task
    BaseType_t ret = xTaskCreate(vm_task_function, "vm_task", VM_TASK_STACK_SIZE, NULL, VM_TASK_PRIORITY, &g_vm_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create VM task");
        vQueueDelete(g_program_queue);
        vEventGroupDelete(g_halt_event_group);
        vSemaphoreDelete(g_state_mutex);
        return false;
    }

    ESP_LOGI(TAG, "VM task initialized successfully");
    return true;
}

bool vm_task_start_program(const uint8_t *program, uint32_t program_size) {
    if (g_vm_task_handle == NULL) {
        ESP_LOGE(TAG, "VM task not initialized");
        return false;
    }

    if (program == NULL || program_size == 0) {
        ESP_LOGE(TAG, "Invalid program parameters");
        return false;
    }

    // Check if already running
    if (vm_task_is_running()) {
        ESP_LOGW(TAG, "Program already running, ignoring start request");
        return false;
    }

    // Prepare request
    vm_program_request_t request = {
        .program = program,
        .program_size = program_size};

    // Send request to queue (non-blocking)
    BaseType_t ret = xQueueSend(g_program_queue, &request, 0);
    if (ret != pdTRUE) {
        ESP_LOGE(TAG, "Failed to queue program start request");
        return false;
    }

    ESP_LOGI(TAG, "Program start request queued");
    return true;
}

bool vm_task_is_running(void) {
    if (g_vm_task_handle == NULL) {
        return false;
    }

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    bool running = (g_task_state == VM_TASK_STATE_RUNNING);
    xSemaphoreGive(g_state_mutex);

    return running;
}

bool vm_task_halt(void) {
    if (g_vm_task_handle == NULL) {
        return true;
    }

    // Signal halt via event group
    xEventGroupSetBits(g_halt_event_group, HALT_BIT);

    // Wait for task to reach idle state
    while (vm_task_is_running()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "Program halted");
    return true;
}
