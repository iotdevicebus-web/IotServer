/**
 * @file osal_freertos.c
 * @brief FreeRTOS 向け OSAL (OS Abstraction Layer) 実装
 */

#include "osal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "timers.h"
#include <string.h>

/* --- OSAL Base --- */
osal_status_t osal_init(void) {
    return OSAL_OK;
}

void osal_start_scheduler(void) {
    vTaskStartScheduler();
}

uint32_t osal_get_time_ms(void) {
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void osal_task_delay_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/* --- Task Implementation --- */
static UBaseType_t convert_priority(osal_priority_t prio) {
    switch (prio) {
        case OSAL_PRIORITY_IDLE:     return tskIDLE_PRIORITY;
        case OSAL_PRIORITY_LOW:      return tskIDLE_PRIORITY + 1;
        case OSAL_PRIORITY_NORMAL:   return tskIDLE_PRIORITY + 2;
        case OSAL_PRIORITY_HIGH:     return tskIDLE_PRIORITY + 3;
        case OSAL_PRIORITY_REALTIME: return configMAX_PRIORITIES - 1;
        default:                     return tskIDLE_PRIORITY + 2;
    }
}

osal_status_t osal_task_create(
    const osal_task_config_t *config,
    osal_task_entry_t entry_func,
    void *arg,
    osal_task_handle_t *out_handle
) {
    if (!config || !entry_func || !out_handle) {
        return OSAL_ERR_INVALID_PARAM;
    }

    TaskHandle_t xHandle = NULL;
    uint32_t stack_words = config->stack_size / sizeof(StackType_t);
    if (stack_words < configMINIMAL_STACK_SIZE) {
        stack_words = configMINIMAL_STACK_SIZE;
    }

    BaseType_t ret = xTaskCreate(
        (TaskFunction_t)entry_func,
        config->name ? config->name : "task",
        (configSTACK_DEPTH_TYPE)stack_words,
        arg,
        convert_priority(config->priority),
        &xHandle
    );

    if (ret != pdPASS) {
        return OSAL_ERR_NO_MEMORY;
    }

    *out_handle = (osal_task_handle_t)xHandle;
    return OSAL_OK;
}

osal_status_t osal_task_delete(osal_task_handle_t handle) {
    vTaskDelete((TaskHandle_t)handle);
    return OSAL_OK;
}

/* --- Queue Implementation --- */
osal_status_t osal_queue_create(uint32_t queue_length, size_t item_size, osal_queue_handle_t *out_queue) {
    if (queue_length == 0 || item_size == 0 || !out_queue) {
        return OSAL_ERR_INVALID_PARAM;
    }

    QueueHandle_t q = xQueueCreate(queue_length, item_size);
    if (!q) {
        return OSAL_ERR_NO_MEMORY;
    }

    *out_queue = (osal_queue_handle_t)q;
    return OSAL_OK;
}

osal_status_t osal_queue_send(osal_queue_handle_t queue, const void *item, uint32_t timeout_ms) {
    if (!queue || !item) return OSAL_ERR_INVALID_PARAM;
    TickType_t ticks = (timeout_ms == OSAL_WAIT_FOREVER) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    BaseType_t ret = xQueueSend((QueueHandle_t)queue, item, ticks);
    return (ret == pdPASS) ? OSAL_OK : OSAL_ERR_TIMEOUT;
}

osal_status_t osal_queue_send_from_isr(osal_queue_handle_t queue, const void *item, bool *higher_priority_task_woken) {
    if (!queue || !item) return OSAL_ERR_INVALID_PARAM;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    BaseType_t ret = xQueueSendFromISR((QueueHandle_t)queue, item, &xHigherPriorityTaskWoken);
    if (higher_priority_task_woken) {
        *higher_priority_task_woken = (xHigherPriorityTaskWoken == pdTRUE);
    }
    return (ret == pdPASS) ? OSAL_OK : OSAL_ERR_RESOURCE_BUSY;
}

osal_status_t osal_queue_receive(osal_queue_handle_t queue, void *buffer, uint32_t timeout_ms) {
    if (!queue || !buffer) return OSAL_ERR_INVALID_PARAM;
    TickType_t ticks = (timeout_ms == OSAL_WAIT_FOREVER) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    BaseType_t ret = xQueueReceive((QueueHandle_t)queue, buffer, ticks);
    return (ret == pdPASS) ? OSAL_OK : OSAL_ERR_TIMEOUT;
}

osal_status_t osal_queue_delete(osal_queue_handle_t queue) {
    if (!queue) return OSAL_ERR_INVALID_PARAM;
    vQueueDelete((QueueHandle_t)queue);
    return OSAL_OK;
}

/* --- Mutex & Semaphore --- */
osal_status_t osal_mutex_create(osal_mutex_handle_t *out_mutex) {
    if (!out_mutex) return OSAL_ERR_INVALID_PARAM;
    SemaphoreHandle_t m = xSemaphoreCreateMutex();
    if (!m) return OSAL_ERR_NO_MEMORY;
    *out_mutex = (osal_mutex_handle_t)m;
    return OSAL_OK;
}

osal_status_t osal_mutex_lock(osal_mutex_handle_t mutex, uint32_t timeout_ms) {
    if (!mutex) return OSAL_ERR_INVALID_PARAM;
    TickType_t ticks = (timeout_ms == OSAL_WAIT_FOREVER) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return (xSemaphoreTake((SemaphoreHandle_t)mutex, ticks) == pdTRUE) ? OSAL_OK : OSAL_ERR_TIMEOUT;
}

osal_status_t osal_mutex_unlock(osal_mutex_handle_t mutex) {
    if (!mutex) return OSAL_ERR_INVALID_PARAM;
    return (xSemaphoreGive((SemaphoreHandle_t)mutex) == pdTRUE) ? OSAL_OK : OSAL_ERROR;
}

osal_status_t osal_mutex_delete(osal_mutex_handle_t mutex) {
    if (!mutex) return OSAL_ERR_INVALID_PARAM;
    vSemaphoreDelete((SemaphoreHandle_t)mutex);
    return OSAL_OK;
}

osal_status_t osal_sem_create(uint32_t max_count, uint32_t initial_count, osal_sem_handle_t *out_sem) {
    if (!out_sem || max_count == 0) return OSAL_ERR_INVALID_PARAM;
    SemaphoreHandle_t s = xSemaphoreCreateCounting(max_count, initial_count);
    if (!s) return OSAL_ERR_NO_MEMORY;
    *out_sem = (osal_sem_handle_t)s;
    return OSAL_OK;
}

osal_status_t osal_sem_take(osal_sem_handle_t sem, uint32_t timeout_ms) {
    if (!sem) return OSAL_ERR_INVALID_PARAM;
    TickType_t ticks = (timeout_ms == OSAL_WAIT_FOREVER) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return (xSemaphoreTake((SemaphoreHandle_t)sem, ticks) == pdTRUE) ? OSAL_OK : OSAL_ERR_TIMEOUT;
}

osal_status_t osal_sem_give(osal_sem_handle_t sem) {
    if (!sem) return OSAL_ERR_INVALID_PARAM;
    return (xSemaphoreGive((SemaphoreHandle_t)sem) == pdTRUE) ? OSAL_OK : OSAL_ERROR;
}

osal_status_t osal_sem_give_from_isr(osal_sem_handle_t sem, bool *higher_priority_task_woken) {
    if (!sem) return OSAL_ERR_INVALID_PARAM;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    BaseType_t ret = xSemaphoreGiveFromISR((SemaphoreHandle_t)sem, &xHigherPriorityTaskWoken);
    if (higher_priority_task_woken) {
        *higher_priority_task_woken = (xHigherPriorityTaskWoken == pdTRUE);
    }
    return (ret == pdTRUE) ? OSAL_OK : OSAL_ERROR;
}

osal_status_t osal_sem_delete(osal_sem_handle_t sem) {
    if (!sem) return OSAL_ERR_INVALID_PARAM;
    vSemaphoreDelete((SemaphoreHandle_t)sem);
    return OSAL_OK;
}

/* --- Software Timer --- */
typedef struct {
    TimerHandle_t handle;
    osal_timer_callback_t cb;
    void *arg;
} freertos_timer_wrapper_t;

static void freertos_timer_callback_proxy(TimerHandle_t xTimer) {
    freertos_timer_wrapper_t *w = (freertos_timer_wrapper_t *)pvTimerGetTimerID(xTimer);
    if (w && w->cb) {
        w->cb(w->arg);
    }
}

osal_status_t osal_timer_create(
    const char *name,
    uint32_t period_ms,
    osal_timer_mode_t mode,
    osal_timer_callback_t callback,
    void *arg,
    osal_timer_handle_t *out_timer
) {
    if (!callback || !out_timer || period_ms == 0) return OSAL_ERR_INVALID_PARAM;

    freertos_timer_wrapper_t *w = (freertos_timer_wrapper_t *)pvPortMalloc(sizeof(freertos_timer_wrapper_t));
    if (!w) return OSAL_ERR_NO_MEMORY;

    w->cb = callback;
    w->arg = arg;

    UBaseType_t auto_reload = (mode == OSAL_TIMER_PERIODIC) ? pdTRUE : pdFALSE;
    TimerHandle_t tmr = xTimerCreate(
        name ? name : "tmr",
        pdMS_TO_TICKS(period_ms),
        auto_reload,
        (void *)w,
        freertos_timer_callback_proxy
    );

    if (!tmr) {
        vPortFree(w);
        return OSAL_ERR_NO_MEMORY;
    }

    w->handle = tmr;
    *out_timer = (osal_timer_handle_t)w;
    return OSAL_OK;
}

osal_status_t osal_timer_start(osal_timer_handle_t timer, uint32_t timeout_ms) {
    if (!timer) return OSAL_ERR_INVALID_PARAM;
    freertos_timer_wrapper_t *w = (freertos_timer_wrapper_t *)timer;
    return (xTimerStart(w->handle, pdMS_TO_TICKS(timeout_ms)) == pdPASS) ? OSAL_OK : OSAL_ERROR;
}

osal_status_t osal_timer_stop(osal_timer_handle_t timer, uint32_t timeout_ms) {
    if (!timer) return OSAL_ERR_INVALID_PARAM;
    freertos_timer_wrapper_t *w = (freertos_timer_wrapper_t *)timer;
    return (xTimerStop(w->handle, pdMS_TO_TICKS(timeout_ms)) == pdPASS) ? OSAL_OK : OSAL_ERROR;
}

osal_status_t osal_timer_change_period(osal_timer_handle_t timer, uint32_t new_period_ms, uint32_t timeout_ms) {
    if (!timer || new_period_ms == 0) return OSAL_ERR_INVALID_PARAM;
    freertos_timer_wrapper_t *w = (freertos_timer_wrapper_t *)timer;
    return (xTimerChangePeriod(w->handle, pdMS_TO_TICKS(new_period_ms), pdMS_TO_TICKS(timeout_ms)) == pdPASS) ? OSAL_OK : OSAL_ERROR;
}

osal_status_t osal_timer_delete(osal_timer_handle_t timer) {
    if (!timer) return OSAL_ERR_INVALID_PARAM;
    freertos_timer_wrapper_t *w = (freertos_timer_wrapper_t *)timer;
    xTimerDelete(w->handle, portMAX_DELAY);
    vPortFree(w);
    return OSAL_OK;
}
