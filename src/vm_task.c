#include "vm_task.h"
#include "odkeyscript_vm.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

static const char *TAG = "vm_task";

// VM task state
typedef enum {
    VM_TASK_STATE_IDLE,
    VM_TASK_STATE_RUNNING,
    VM_TASK_STATE_HALTING
} vm_task_state_t;

// Program start request structure
typedef struct {
    const uint8_t* program;
    size_t program_size;
} vm_program_request_t;

// Global state
static TaskHandle_t g_vm_task_handle = NULL;
static QueueHandle_t g_program_queue = NULL;
static SemaphoreHandle_t g_state_mutex = NULL;
static EventGroupHandle_t g_halt_event_group = NULL;
static vm_task_state_t g_task_state = VM_TASK_STATE_IDLE;
static bool g_halt_requested = false;
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
        pdTRUE,  // Clear the bit when we get it
        pdFALSE, // Wait for any bit (not all bits)
        pdMS_TO_TICKS(ms)
    );
    
    // If we got the halt bit, we were interrupted
    if (bits & HALT_BIT) {
        ESP_LOGD(TAG, "Delay interrupted by halt request");
    }
}

// VM task function
static void vm_task_function(void *pvParameters) {
    (void) pvParameters;
    
    vm_program_request_t request;
    
    for (;;) {
        // Wait for program start request
        if (xQueueReceive(g_program_queue, &request, portMAX_DELAY) == pdTRUE) {
            // Check if we should ignore this request (already running)
            xSemaphoreTake(g_state_mutex, portMAX_DELAY);
            if (g_task_state != VM_TASK_STATE_IDLE) {
                xSemaphoreGive(g_state_mutex);
                ESP_LOGW(TAG, "Ignoring program start request - already running");
                continue;
            }
            g_task_state = VM_TASK_STATE_RUNNING;
            g_halt_requested = false;
            xSemaphoreGive(g_state_mutex);
            
            // Clear any pending halt signals
            xEventGroupClearBits(g_halt_event_group, HALT_BIT);
            
            ESP_LOGI(TAG, "Starting program execution (%lu bytes)", (unsigned long)request.program_size);
            
            // Initialize VM
            if (vm_init(&g_vm_context)) {
                // Start VM
                vm_error_t result = vm_start(&g_vm_context, request.program, request.program_size, 
                                           g_hid_send_callback, delay_callback);
                
                if (result == VM_ERROR_NONE) {
                    // Run VM step by step
                    while (vm_running(&g_vm_context)) {
                        // Check for halt request
                        xSemaphoreTake(g_state_mutex, 0);
                        if (g_halt_requested) {
                            g_task_state = VM_TASK_STATE_HALTING;
                            xSemaphoreGive(g_state_mutex);
                            ESP_LOGI(TAG, "Halt requested, stopping program");
                            break;
                        }
                        xSemaphoreGive(g_state_mutex);
                        
                        // Execute one VM step
                        result = vm_step(&g_vm_context);
                        if (result != VM_ERROR_NONE) {
                            ESP_LOGE(TAG, "VM step failed: %s", vm_error_to_string(result));
                            break;
                        }
                    }
                    
                    // Program completed or halted
                    if (g_task_state == VM_TASK_STATE_RUNNING) {
                        ESP_LOGI(TAG, "Program completed successfully");
                        
                        // Print statistics
                        uint32_t instructions, keys_pressed, keys_released;
                        vm_get_stats(&g_vm_context, &instructions, &keys_pressed, &keys_released);
                        ESP_LOGI(TAG, "VM Stats - Instructions: %lu, Keys Pressed: %lu, Keys Released: %lu",
                                 instructions, keys_pressed, keys_released);
                    }
                } else {
                    ESP_LOGE(TAG, "Failed to start VM: %s", vm_error_to_string(result));
                }
            } else {
                ESP_LOGE(TAG, "Failed to initialize VM");
            }
            
            // Reset state to idle
            xSemaphoreTake(g_state_mutex, portMAX_DELAY);
            g_task_state = VM_TASK_STATE_IDLE;
            g_halt_requested = false;
            xSemaphoreGive(g_state_mutex);
        }
    }
}

bool vm_task_init(vm_hid_send_callback_t hid_send_callback) {
    if (g_vm_task_handle != NULL) {
        return true; // Already initialized
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
    BaseType_t ret = xTaskCreate(vm_task_function, "vm_task", 4096, NULL, 5, &g_vm_task_handle);
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

bool vm_task_start_program(const uint8_t* program, size_t program_size) {
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
        .program_size = program_size
    };
    
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

void vm_task_halt(void) {
    if (g_vm_task_handle == NULL) {
        return;
    }
    
    // Set halt flag
    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_halt_requested = true;
    xSemaphoreGive(g_state_mutex);
    
    // Signal the event group to interrupt any ongoing delay
    xEventGroupSetBits(g_halt_event_group, HALT_BIT);
    
    // Wait for task to reach idle state
    while (vm_task_is_running()) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    ESP_LOGI(TAG, "Program halted");
}
