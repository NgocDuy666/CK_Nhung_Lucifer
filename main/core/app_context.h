#pragma once
/*
 * app_context.h - Global FreeRTOS synchronization handles & task handles
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"

typedef struct {
    /* Queues & Queue Sets (Ch. 5) */
    QueueHandle_t       dht_queue;
    QueueHandle_t       light_queue;
    QueueHandle_t       cmd_queue;
    QueueSetHandle_t    main_queue_set;
    QueueHandle_t       sensor_mailbox;     /* len=1, xQueueOverwrite / xQueuePeek */

    /* Button ISR Synchronization Objects (Ch. 7) */
    SemaphoreHandle_t   btn_menu_sem;       /* Binary Semaphore */
    SemaphoreHandle_t   btn_step_sem;       /* Counting Semaphore */
    QueueHandle_t       btn_isr_queue;      /* Carries btn_id_t from UP/DOWN ISR */
    QueueSetHandle_t    btn_input_set;      /* Combines btn_menu_sem + btn_step_sem */

    /* Resource Protection Mutex (Ch. 8) */
    SemaphoreHandle_t   config_mutex;       /* Protects sys_config_t & toast buffer */

    /* Event Groups (Ch. 9) */
    EventGroupHandle_t  sync_events;        /* Startup rendezvous via xEventGroupSync */
    EventGroupHandle_t  sys_events;         /* Runtime alert & mode flags */

    /* Software Timers (Ch. 6) */
    TimerHandle_t       debounce_timer;     /* One-shot 50 ms */
    TimerHandle_t       ui_timeout_timer;   /* One-shot 10 s: auto-save & return home */
    TimerHandle_t       toast_timer;        /* One-shot 2 s: clear LCD toast banner */
    TimerHandle_t       mute_timer;         /* One-shot 30 s: auto-unmute alarm */
    TimerHandle_t       alarm_blink_timer;  /* Auto-reload: dynamic 1000ms <-> 250ms */

    /* Task Handles for Direct Task Notifications (Ch. 10) */
    TaskHandle_t        button_task_handle;
    TaskHandle_t        dht_task_handle;
    TaskHandle_t        light_task_handle;
    TaskHandle_t        control_task_handle;
    TaskHandle_t        alarm_task_handle;
    TaskHandle_t        lcd_task_handle;
    TaskHandle_t        cli_task_handle;

    /* ISR diagnostic counter updated via xTimerPendFunctionCallFromISR */
    volatile uint32_t   isr_event_count;
} app_context_t;

extern app_context_t g_ctx;
