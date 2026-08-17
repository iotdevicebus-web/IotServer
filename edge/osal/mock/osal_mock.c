/**
 * @file osal_mock.c
 * @brief OSAL (OS Abstraction Layer) PC用モック実装
 */

#include "osal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#endif

/* --- OSAL Base --- */
osal_status_t osal_init(void) {
    return OSAL_OK;
}

void osal_start_scheduler(void) {
    // PC環境ではメインスレッドがそのまま継続
}

uint32_t osal_get_time_ms(void) {
#if defined(_WIN32)
    return (uint32_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));
#endif
}

void osal_task_delay_ms(uint32_t ms) {
#if defined(_WIN32)
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

/* --- Task Mock --- */
typedef struct {
    osal_task_entry_t entry_func;
    void *arg;
    char name[32];
#if defined(_WIN32)
    HANDLE handle;
    DWORD thread_id;
#else
    pthread_t thread;
#endif
} mock_task_t;

#if defined(_WIN32)
static DWORD WINAPI mock_task_wrapper(LPVOID lpParam) {
    mock_task_t *task = (mock_task_t *)lpParam;
    if (task && task->entry_func) {
        task->entry_func(task->arg);
    }
    return 0;
}
#else
static void* mock_task_wrapper(void *arg) {
    mock_task_t *task = (mock_task_t *)arg;
    if (task && task->entry_func) {
        task->entry_func(task->arg);
    }
    return NULL;
}
#endif

osal_status_t osal_task_create(
    const osal_task_config_t *config,
    osal_task_entry_t entry_func,
    void *arg,
    osal_task_handle_t *out_handle
) {
    if (!config || !entry_func || !out_handle) {
        return OSAL_ERR_INVALID_PARAM;
    }

    mock_task_t *task = (mock_task_t *)malloc(sizeof(mock_task_t));
    if (!task) {
        return OSAL_ERR_NO_MEMORY;
    }

    strncpy(task->name, config->name ? config->name : "task", sizeof(task->name) - 1);
    task->entry_func = entry_func;
    task->arg = arg;

#if defined(_WIN32)
    task->handle = CreateThread(NULL, config->stack_size, mock_task_wrapper, task, 0, &task->thread_id);
    if (!task->handle) {
        free(task);
        return OSAL_ERROR;
    }
#else
    if (pthread_create(&task->thread, NULL, mock_task_wrapper, task) != 0) {
        free(task);
        return OSAL_ERROR;
    }
#endif

    *out_handle = (osal_task_handle_t)task;
    return OSAL_OK;
}

osal_status_t osal_task_delete(osal_task_handle_t handle) {
    if (!handle) return OSAL_ERR_INVALID_PARAM;
    mock_task_t *task = (mock_task_t *)handle;
#if defined(_WIN32)
    CloseHandle(task->handle);
#endif
    free(task);
    return OSAL_OK;
}

/* --- Queue Mock --- */
typedef struct {
    uint8_t *buffer;
    size_t item_size;
    uint32_t capacity;
    uint32_t count;
    uint32_t head;
    uint32_t tail;
#if defined(_WIN32)
    CRITICAL_SECTION cs;
    CONDITION_VARIABLE not_empty;
    CONDITION_VARIABLE not_full;
#else
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
#endif
} mock_queue_t;

osal_status_t osal_queue_create(uint32_t queue_length, size_t item_size, osal_queue_handle_t *out_queue) {
    if (queue_length == 0 || item_size == 0 || !out_queue) {
        return OSAL_ERR_INVALID_PARAM;
    }

    mock_queue_t *q = (mock_queue_t *)malloc(sizeof(mock_queue_t));
    if (!q) return OSAL_ERR_NO_MEMORY;

    q->buffer = (uint8_t *)malloc(queue_length * item_size);
    if (!q->buffer) {
        free(q);
        return OSAL_ERR_NO_MEMORY;
    }

    q->item_size = item_size;
    q->capacity = queue_length;
    q->count = 0;
    q->head = 0;
    q->tail = 0;

#if defined(_WIN32)
    InitializeCriticalSection(&q->cs);
    InitializeConditionVariable(&q->not_empty);
    InitializeConditionVariable(&q->not_full);
#else
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
#endif

    *out_queue = (osal_queue_handle_t)q;
    return OSAL_OK;
}

osal_status_t osal_queue_send(osal_queue_handle_t queue, const void *item, uint32_t timeout_ms) {
    if (!queue || !item) return OSAL_ERR_INVALID_PARAM;
    mock_queue_t *q = (mock_queue_t *)queue;

#if defined(_WIN32)
    EnterCriticalSection(&q->cs);
    while (q->count == q->capacity) {
        if (timeout_ms == OSAL_NO_WAIT) {
            LeaveCriticalSection(&q->cs);
            return OSAL_ERR_RESOURCE_BUSY;
        }
        if (!SleepConditionVariableCS(&q->not_full, &q->cs, timeout_ms == OSAL_WAIT_FOREVER ? INFINITE : timeout_ms)) {
            LeaveCriticalSection(&q->cs);
            return OSAL_ERR_TIMEOUT;
        }
    }
    memcpy(q->buffer + (q->tail * q->item_size), item, q->item_size);
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    WakeConditionVariable(&q->not_empty);
    LeaveCriticalSection(&q->cs);
#else
    pthread_mutex_lock(&q->mutex);
    while (q->count == q->capacity) {
        if (timeout_ms == OSAL_NO_WAIT) {
            pthread_mutex_unlock(&q->mutex);
            return OSAL_ERR_RESOURCE_BUSY;
        }
        // POSIX timedwait 実装簡略
        pthread_cond_wait(&q->not_full, &q->mutex);
    }
    memcpy(q->buffer + (q->tail * q->item_size), item, q->item_size);
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);
#endif

    return OSAL_OK;
}

osal_status_t osal_queue_send_from_isr(osal_queue_handle_t queue, const void *item, bool *higher_priority_task_woken) {
    if (higher_priority_task_woken) *higher_priority_task_woken = false;
    return osal_queue_send(queue, item, OSAL_NO_WAIT);
}

osal_status_t osal_queue_receive(osal_queue_handle_t queue, void *buffer, uint32_t timeout_ms) {
    if (!queue || !buffer) return OSAL_ERR_INVALID_PARAM;
    mock_queue_t *q = (mock_queue_t *)queue;

#if defined(_WIN32)
    EnterCriticalSection(&q->cs);
    while (q->count == 0) {
        if (timeout_ms == OSAL_NO_WAIT) {
            LeaveCriticalSection(&q->cs);
            return OSAL_ERR_TIMEOUT;
        }
        if (!SleepConditionVariableCS(&q->not_empty, &q->cs, timeout_ms == OSAL_WAIT_FOREVER ? INFINITE : timeout_ms)) {
            LeaveCriticalSection(&q->cs);
            return OSAL_ERR_TIMEOUT;
        }
    }
    memcpy(buffer, q->buffer + (q->head * q->item_size), q->item_size);
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    WakeConditionVariable(&q->not_full);
    LeaveCriticalSection(&q->cs);
#else
    pthread_mutex_lock(&q->mutex);
    while (q->count == 0) {
        if (timeout_ms == OSAL_NO_WAIT) {
            pthread_mutex_unlock(&q->mutex);
            return OSAL_ERR_TIMEOUT;
        }
        pthread_cond_wait(&q->not_empty, &q->mutex);
    }
    memcpy(buffer, q->buffer + (q->head * q->item_size), q->item_size);
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);
#endif

    return OSAL_OK;
}

osal_status_t osal_queue_delete(osal_queue_handle_t queue) {
    if (!queue) return OSAL_ERR_INVALID_PARAM;
    mock_queue_t *q = (mock_queue_t *)queue;
#if defined(_WIN32)
    DeleteCriticalSection(&q->cs);
#else
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
#endif
    free(q->buffer);
    free(q);
    return OSAL_OK;
}

/* --- Mutex Mock --- */
osal_status_t osal_mutex_create(osal_mutex_handle_t *out_mutex) {
    if (!out_mutex) return OSAL_ERR_INVALID_PARAM;
#if defined(_WIN32)
    CRITICAL_SECTION *cs = (CRITICAL_SECTION *)malloc(sizeof(CRITICAL_SECTION));
    InitializeCriticalSection(cs);
    *out_mutex = (osal_mutex_handle_t)cs;
#else
    pthread_mutex_t *m = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(m, NULL);
    *out_mutex = (osal_mutex_handle_t)m;
#endif
    return OSAL_OK;
}

osal_status_t osal_mutex_lock(osal_mutex_handle_t mutex, uint32_t timeout_ms) {
    (void)timeout_ms;
    if (!mutex) return OSAL_ERR_INVALID_PARAM;
#if defined(_WIN32)
    EnterCriticalSection((CRITICAL_SECTION *)mutex);
#else
    pthread_mutex_lock((pthread_mutex_t *)mutex);
#endif
    return OSAL_OK;
}

osal_status_t osal_mutex_unlock(osal_mutex_handle_t mutex) {
    if (!mutex) return OSAL_ERR_INVALID_PARAM;
#if defined(_WIN32)
    LeaveCriticalSection((CRITICAL_SECTION *)mutex);
#else
    pthread_mutex_unlock((pthread_mutex_t *)mutex);
#endif
    return OSAL_OK;
}

osal_status_t osal_mutex_delete(osal_mutex_handle_t mutex) {
    if (!mutex) return OSAL_ERR_INVALID_PARAM;
#if defined(_WIN32)
    DeleteCriticalSection((CRITICAL_SECTION *)mutex);
#else
    pthread_mutex_destroy((pthread_mutex_t *)mutex);
#endif
    free(mutex);
    return OSAL_OK;
}

/* --- Timer Mock --- */
typedef struct {
    char name[32];
    uint32_t period_ms;
    osal_timer_mode_t mode;
    osal_timer_callback_t callback;
    void *arg;
    bool is_running;
    bool should_stop;
#if defined(_WIN32)
    HANDLE thread;
#else
    pthread_t thread;
#endif
} mock_timer_t;

#if defined(_WIN32)
static DWORD WINAPI mock_timer_worker(LPVOID lpParam) {
    mock_timer_t *timer = (mock_timer_t *)lpParam;
    while (!timer->should_stop && timer->is_running) {
        Sleep(timer->period_ms);
        if (timer->should_stop || !timer->is_running) break;
        if (timer->callback) {
            timer->callback(timer->arg);
        }
        if (timer->mode == OSAL_TIMER_ONE_SHOT) {
            timer->is_running = false;
            break;
        }
    }
    return 0;
}
#endif

osal_status_t osal_timer_create(
    const char *name,
    uint32_t period_ms,
    osal_timer_mode_t mode,
    osal_timer_callback_t callback,
    void *arg,
    osal_timer_handle_t *out_timer
) {
    if (!callback || !out_timer || period_ms == 0) return OSAL_ERR_INVALID_PARAM;
    mock_timer_t *t = (mock_timer_t *)malloc(sizeof(mock_timer_t));
    if (!t) return OSAL_ERR_NO_MEMORY;

    strncpy(t->name, name ? name : "timer", sizeof(t->name) - 1);
    t->period_ms = period_ms;
    t->mode = mode;
    t->callback = callback;
    t->arg = arg;
    t->is_running = false;
    t->should_stop = false;

    *out_timer = (osal_timer_handle_t)t;
    return OSAL_OK;
}

osal_status_t osal_timer_start(osal_timer_handle_t timer, uint32_t timeout_ms) {
    (void)timeout_ms;
    if (!timer) return OSAL_ERR_INVALID_PARAM;
    mock_timer_t *t = (mock_timer_t *)timer;
    t->is_running = true;
    t->should_stop = false;
#if defined(_WIN32)
    t->thread = CreateThread(NULL, 0, mock_timer_worker, t, 0, NULL);
#endif
    return OSAL_OK;
}

osal_status_t osal_timer_stop(osal_timer_handle_t timer, uint32_t timeout_ms) {
    (void)timeout_ms;
    if (!timer) return OSAL_ERR_INVALID_PARAM;
    mock_timer_t *t = (mock_timer_t *)timer;
    t->should_stop = true;
    t->is_running = false;
    return OSAL_OK;
}

osal_status_t osal_timer_change_period(osal_timer_handle_t timer, uint32_t new_period_ms, uint32_t timeout_ms) {
    (void)timeout_ms;
    if (!timer || new_period_ms == 0) return OSAL_ERR_INVALID_PARAM;
    mock_timer_t *t = (mock_timer_t *)timer;
    t->period_ms = new_period_ms;
    return OSAL_OK;
}

osal_status_t osal_timer_delete(osal_timer_handle_t timer) {
    if (!timer) return OSAL_ERR_INVALID_PARAM;
    mock_timer_t *t = (mock_timer_t *)timer;
    t->should_stop = true;
    t->is_running = false;
    free(t);
    return OSAL_OK;
}
