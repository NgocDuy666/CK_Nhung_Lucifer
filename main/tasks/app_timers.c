#include "tasks.h"

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "app_config.h"
#include "app_context.h"
#include "app_state.h"

static const char *TAG = "TIMERS";

/*
 * Timer #1 (One-shot, 50 ms): Debounce guard window.
 * Demonstrates vTimerSetTimerID() / pvTimerGetTimerID() to track debounce windows.
 */
static void debounce_timer_cb(TimerHandle_t xTimer)
{
    uintptr_t count = (uintptr_t)pvTimerGetTimerID(xTimer);
    vTimerSetTimerID(xTimer, (void *)(count + 1U));
}

/*
 * Timer #2 (One-shot, 10 s): Auto-save modified settings to NVS and return LCD to Page 0.
 * Never blocks inside timer callback (uses 0 ticks timeout).
 */
static void ui_timeout_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;

    cmd_msg_t cmd = {
        .source = CMD_SRC_TIMER,
        .type   = CMD_SAVE_NVS,
        .value  = 1, /* 1 = silent unless dirty */
    };
    xQueueSendToBack(g_ctx.cmd_queue, &cmd, 0);

    if (g_ctx.lcd_task_handle) {
        xTaskNotify(g_ctx.lcd_task_handle, NOTIFY_LCD_HOME, eSetBits);
    }
}

/*
 * Timer #3 (One-shot, 2 s): Expire temporary toast notification on LCD.
 */
static void toast_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    if (g_ctx.lcd_task_handle) {
        xTaskNotify(g_ctx.lcd_task_handle, NOTIFY_LCD_TOAST_END, eSetBits);
    }
}

/*
 * Timer #4 (One-shot, 30 s): Auto-unmute alarm if hazard persists after mute window.
 */
static void mute_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    cmd_msg_t cmd = {
        .source = CMD_SRC_TIMER,
        .type   = CMD_ALARM_UNMUTE,
        .value  = 0,
    };
    xQueueSendToBack(g_ctx.cmd_queue, &cmd, 0);
}

/*
 * Timer #5 (Auto-reload, dynamic 1000 ms <-> 250 ms via xTimerChangePeriod):
 * Drives alarm_task LED/Buzzer toggle via direct Task Notification (Ch. 10).
 */
static void alarm_blink_timer_cb(TimerHandle_t xTimer)
{
    uintptr_t ticks = (uintptr_t)pvTimerGetTimerID(xTimer);
    vTimerSetTimerID(xTimer, (void *)(ticks + 1U));

    if (g_ctx.alarm_task_handle) {
        xTaskNotifyGive(g_ctx.alarm_task_handle);
    }
}

esp_err_t app_timers_init(void)
{
    g_ctx.debounce_timer = xTimerCreate(
        "tmr_debounce", pdMS_TO_TICKS(BTN_DEBOUNCE_MS), pdFALSE, (void *)0, debounce_timer_cb);

    g_ctx.ui_timeout_timer = xTimerCreate(
        "tmr_ui_timeout", pdMS_TO_TICKS(UI_MENU_TIMEOUT_MS), pdFALSE, (void *)0, ui_timeout_timer_cb);

    g_ctx.toast_timer = xTimerCreate(
        "tmr_toast", pdMS_TO_TICKS(UI_TOAST_DURATION_MS), pdFALSE, (void *)0, toast_timer_cb);

    g_ctx.mute_timer = xTimerCreate(
        "tmr_mute", pdMS_TO_TICKS(ALARM_MUTE_DURATION_MS), pdFALSE, (void *)0, mute_timer_cb);

    g_ctx.alarm_blink_timer = xTimerCreate(
        "tmr_alarm_blink", pdMS_TO_TICKS(ALARM_WARN_PERIOD_MS), pdTRUE, (void *)0, alarm_blink_timer_cb);

    if (!g_ctx.debounce_timer || !g_ctx.ui_timeout_timer || !g_ctx.toast_timer ||
        !g_ctx.mute_timer || !g_ctx.alarm_blink_timer) {
        ESP_LOGE(TAG, "Failed to create one or more Software Timers");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "5 Software Timers created (debounce, ui_timeout, toast, mute, alarm_blink)");
    return ESP_OK;
}

void app_show_toast(const char *line0, const char *line1)
{
    app_state_set_toast(line0, line1);
    if (g_ctx.toast_timer) {
        xTimerReset(g_ctx.toast_timer, 0);
    }
    if (g_ctx.lcd_task_handle) {
        xTaskNotify(g_ctx.lcd_task_handle, NOTIFY_LCD_TOAST, eSetBits);
    }
}
