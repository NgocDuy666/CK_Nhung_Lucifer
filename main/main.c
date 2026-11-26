/*
 * Smart Environmental Monitoring and Control System based on FreeRTOS
 * Target   : ESP32-C3 (ESP-IDF >= 5.2)
 * Team     : Lucifer (Final Embedded Systems Project)
 *
 * Architecture Overview:
 *   main/core/     : app_config.h, app_types.h, app_context.h, app_state.c/.h
 *   main/drivers/  : sensors.c/.h, lcd_i2c.c/.h, actuators.c/.h, buttons.c/.h
 *   main/tasks/    : app_timers.c + 7 isolated FreeRTOS task files
 *   main/main.c    : System initialization, RTOS object wiring & task launch
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "app_config.h"
#include "app_context.h"
#include "app_state.h"
#include "app_types.h"
#include "sensors.h"
#include "lcd_i2c.h"
#include "actuators.h"
#include "buttons.h"
#include "tasks.h"

static const char *TAG = "APP_MAIN";

app_context_t g_ctx;

static void init_rtos_objects(void)
{
    memset(&g_ctx, 0, sizeof(g_ctx));

    /* 1) Resource Mutex (Ch. 8) & Sensor Mailbox len=1 (Ch. 5) */
    g_ctx.config_mutex   = xSemaphoreCreateMutex();
    g_ctx.sensor_mailbox = xQueueCreate(1, sizeof(env_snapshot_t));
    configASSERT(g_ctx.config_mutex && g_ctx.sensor_mailbox);

    /* 2) Sensor & Command Queues + Main Queue Set (Ch. 5) */
    g_ctx.dht_queue      = xQueueCreate(DHT_Q_LEN, sizeof(dht_data_t));
    g_ctx.light_queue    = xQueueCreate(LIGHT_Q_LEN, sizeof(light_data_t));
    g_ctx.cmd_queue      = xQueueCreate(CMD_Q_LEN, sizeof(cmd_msg_t));
    g_ctx.main_queue_set = xQueueCreateSet(MAIN_QSET_LEN);
    configASSERT(g_ctx.dht_queue && g_ctx.light_queue && g_ctx.cmd_queue && g_ctx.main_queue_set);

    /* All member queues must be empty before adding to QueueSet */
    configASSERT(xQueueAddToSet(g_ctx.dht_queue, g_ctx.main_queue_set) == pdPASS);
    configASSERT(xQueueAddToSet(g_ctx.light_queue, g_ctx.main_queue_set) == pdPASS);
    configASSERT(xQueueAddToSet(g_ctx.cmd_queue, g_ctx.main_queue_set) == pdPASS);

    /* 3) Button ISR Binary Sem + Counting Sem + ISR Queue + Queue Set (Ch. 7) */
    g_ctx.btn_menu_sem  = xSemaphoreCreateBinary();
    g_ctx.btn_step_sem  = xSemaphoreCreateCounting(BTN_STEP_MAX_COUNT, 0);
    g_ctx.btn_isr_queue = xQueueCreate(BTN_STEP_MAX_COUNT, sizeof(btn_id_t));
    g_ctx.btn_input_set = xQueueCreateSet(BTN_QSET_LEN);
    configASSERT(g_ctx.btn_menu_sem && g_ctx.btn_step_sem &&
                 g_ctx.btn_isr_queue && g_ctx.btn_input_set);

    configASSERT(xQueueAddToSet(g_ctx.btn_menu_sem, g_ctx.btn_input_set) == pdPASS);
    configASSERT(xQueueAddToSet(g_ctx.btn_step_sem, g_ctx.btn_input_set) == pdPASS);

    /* 4) Event Groups: Startup Sync Barrier + System Alert Flags (Ch. 9) */
    g_ctx.sync_events = xEventGroupCreate();
    g_ctx.sys_events  = xEventGroupCreate();
    configASSERT(g_ctx.sync_events && g_ctx.sys_events);

    /* 5) Software Timers (Ch. 6) */
    ESP_ERROR_CHECK(app_timers_init());
}

static void init_hardware(void)
{
    dht22_init();
    ESP_ERROR_CHECK(ldr_init());
    ESP_ERROR_CHECK(actuators_init());

    esp_err_t lcd_err = lcd_init();
    if (lcd_err != ESP_OK) {
        ESP_LOGW(TAG, "LCD init failed (%s) - continuing without LCD display",
                 esp_err_to_name(lcd_err));
    }

    /* Install GPIO ISRs only after all semaphores/queues are created */
    ESP_ERROR_CHECK(buttons_init());
}

static void start_tasks(void)
{
    BaseType_t ok = pdPASS;

    /* Create consumers (LCD Gatekeeper & Alarm) first so Task Handles are ready */
    ok &= xTaskCreate(lcd_gatekeeper_task, "lcd_gk_task",
                      STACK_LCD_TASK, NULL, PRIO_LCD_TASK, &g_ctx.lcd_task_handle);

    ok &= xTaskCreate(alarm_task, "alarm_task",
                      STACK_ALARM_TASK, NULL, PRIO_ALARM_TASK, &g_ctx.alarm_task_handle);

    ok &= xTaskCreate(control_task, "control_task",
                      STACK_CONTROL_TASK, NULL, PRIO_CONTROL_TASK, &g_ctx.control_task_handle);

    ok &= xTaskCreate(dht_task, "dht_task",
                      STACK_DHT_TASK, NULL, PRIO_DHT_TASK, &g_ctx.dht_task_handle);

    ok &= xTaskCreate(light_task, "light_task",
                      STACK_LIGHT_TASK, NULL, PRIO_LIGHT_TASK, &g_ctx.light_task_handle);

    ok &= xTaskCreate(button_task, "button_task",
                      STACK_BUTTON_TASK, NULL, PRIO_BUTTON_TASK, &g_ctx.button_task_handle);

    ok &= xTaskCreate(cli_task, "cli_task",
                      STACK_CLI_TASK, NULL, PRIO_CLI_TASK, &g_ctx.cli_task_handle);

    configASSERT(ok == pdPASS);
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================================");
    ESP_LOGI(TAG, " Smart Environmental Monitoring & Control (ESP32-C3)");
    ESP_LOGI(TAG, " FreeRTOS Final Project - Team Lucifer");
    ESP_LOGI(TAG, "========================================================");

    init_rtos_objects();
    ESP_ERROR_CHECK(app_state_init());
    init_hardware();
    start_tasks();

    ESP_LOGI(TAG, "All 7 FreeRTOS tasks launched. Type 'help' in Serial Monitor.");
}
