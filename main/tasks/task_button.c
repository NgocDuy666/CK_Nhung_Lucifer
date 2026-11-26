#include "tasks.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "app_config.h"
#include "app_context.h"
#include "app_state.h"
#include "app_types.h"
#include "buttons.h"

static const char *TAG = "BTN_TASK";

static void send_cmd(cmd_type_t type, int32_t value, bool urgent)
{
    cmd_msg_t msg = {
        .source = CMD_SRC_BUTTON,
        .type   = type,
        .value  = value,
    };
    if (urgent) {
        /* FreeRTOS Ch. 5: Priority insertion at front of queue */
        xQueueSendToFront(g_ctx.cmd_queue, &msg, 0);
    } else {
        xQueueSendToBack(g_ctx.cmd_queue, &msg, 0);
    }
}

static void handle_menu_button(void)
{
    /* Wait 20 ms to confirm genuine press (software debounce) */
    vTaskDelay(pdMS_TO_TICKS(20));
    if (!buttons_is_pressed(BTN_ID_MENU)) {
        return;
    }

    /* Measure hold time to distinguish Short Press vs Long Press (>= 1.5 s) */
    uint32_t held_ms = 20;
    while (buttons_is_pressed(BTN_ID_MENU) && held_ms < BTN_LONG_PRESS_MS) {
        vTaskDelay(pdMS_TO_TICKS(50));
        held_ms += 50;
    }

    sys_config_t cfg;
    if (!app_state_get_config(&cfg)) {
        return;
    }

    if (held_ms >= BTN_LONG_PRESS_MS) {
        if (cfg.ui_page != UI_PAGE_MAIN) {
            /* Long press inside Menu -> Save to NVS and return to Main Monitor */
            app_state_set_ui_page(UI_PAGE_MAIN);
            xTimerStop(g_ctx.ui_timeout_timer, 0);
            send_cmd(CMD_SAVE_NVS, 0, false);
            app_show_toast(">> SAVED TO NVS", "Back to Monitor");
            ESP_LOGI(TAG, "MENU hold -> saved config & returned to Main");
        } else {
            /* Long press on Main -> quick toggle AUTO <-> MANUAL */
            sys_mode_t new_mode = (cfg.mode == SYS_MODE_AUTO) ? SYS_MODE_MANUAL : SYS_MODE_AUTO;
            app_state_set_mode(new_mode);
            send_cmd(CMD_CONFIG_UPDATED, 0, false);
            app_show_toast(">> MODE CHANGED",
                           new_mode == SYS_MODE_AUTO ? "Mode: AUTO" : "Mode: MANUAL");
            ESP_LOGI(TAG, "MENU hold on Main -> switched mode to %s",
                     new_mode == SYS_MODE_AUTO ? "AUTO" : "MANUAL");
        }
        return;
    }

    /* Short press -> Cycle LCD Menu pages */
    bool wrapped = false;
    ui_page_t next_page = app_state_next_ui_page(&wrapped);

    if (wrapped) {
        xTimerStop(g_ctx.ui_timeout_timer, 0);
        if (cfg.config_dirty) {
            send_cmd(CMD_SAVE_NVS, 0, false);
            app_show_toast(">> SAVED TO NVS", "Back to Monitor");
        } else {
            xTaskNotify(g_ctx.lcd_task_handle, NOTIFY_LCD_MENU, eSetBits);
        }
    } else {
        /* Reset 10 s auto-exit timer on every menu navigation */
        xTimerReset(g_ctx.ui_timeout_timer, 0);
        xTaskNotify(g_ctx.lcd_task_handle, NOTIFY_LCD_MENU, eSetBits);
    }

    ESP_LOGI(TAG, "MENU press -> UI page %d", (int)next_page);
}

static void handle_step_button(btn_id_t id)
{
    vTaskDelay(pdMS_TO_TICKS(25));
    if (!buttons_is_pressed(id)) {
        return;
    }

    sys_config_t cfg;
    if (!app_state_get_config(&cfg)) {
        return;
    }

    /* If on Main screen and an unmuted alarm is sounding -> Urgent Mute! */
    EventBits_t ev = xEventGroupGetBits(g_ctx.sys_events);
    if (cfg.ui_page == UI_PAGE_MAIN &&
        (ev & EVT_ALARM_ANY_MASK) != 0 &&
        (ev & EVT_ALARM_MUTED) == 0) {
        send_cmd(CMD_ALARM_MUTE, 0, true); /* xQueueSendToFront */
        app_show_toast(">> ALARM MUTED", "Auto-unmute 30s");
        ESP_LOGI(TAG, "Button %d -> Urgent ALARM MUTE", (int)id);
        return;
    }

    int  delta = (id == BTN_ID_UP) ? +1 : -1;
    char toast_line[17] = {0};

    if (app_state_adjust_current_setting(delta, toast_line, sizeof(toast_line))) {
        send_cmd(CMD_CONFIG_UPDATED, 0, false);

        if (cfg.ui_page != UI_PAGE_MAIN) {
            xTimerReset(g_ctx.ui_timeout_timer, 0);
            xTaskNotify(g_ctx.lcd_task_handle, NOTIFY_LCD_MENU, eSetBits);
        } else {
            /* Direct fan speed adjustment on Main screen in MANUAL mode */
            xTaskNotify(g_ctx.lcd_task_handle, NOTIFY_LCD_DATA, eSetBits);
        }

        ESP_LOGI(TAG, "Step %s on page %d applied", delta > 0 ? "UP" : "DOWN", (int)cfg.ui_page);
    } else if (cfg.ui_page == UI_PAGE_MAIN && cfg.mode == SYS_MODE_AUTO) {
        /* Helpful hint if user presses UP/DOWN on Main while in AUTO mode */
        app_show_toast("Mode is AUTO", "Hold MENU:Manual");
    }
}

void button_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Started (prio=%u, Deferred ISR Handler)", (unsigned)uxTaskPriorityGet(NULL));

    TickType_t last_menu_tick = 0;
    TickType_t last_up_tick   = 0;
    TickType_t last_down_tick = 0;
    const TickType_t menu_guard_ticks = pdMS_TO_TICKS(180);
    const TickType_t step_guard_ticks = pdMS_TO_TICKS(150);

    for (;;) {
        /* FreeRTOS Ch. 5 & 7: Wait on QueueSet combining Binary Sem + Counting Sem */
        QueueSetMemberHandle_t member = xQueueSelectFromSet(g_ctx.btn_input_set, portMAX_DELAY);
        TickType_t now = xTaskGetTickCount();

        if (member == g_ctx.btn_menu_sem) {
            xSemaphoreTake(g_ctx.btn_menu_sem, 0);

            if ((now - last_menu_tick) >= menu_guard_ticks && !xTimerIsTimerActive(g_ctx.debounce_timer)) {
                xTimerReset(g_ctx.debounce_timer, 0);
                handle_menu_button();
                /* Update timestamp after hold completes & drain any release bounce */
                last_menu_tick = xTaskGetTickCount();
                xTimerReset(g_ctx.debounce_timer, 0);
            }
        } else if (member == g_ctx.btn_step_sem) {
            xSemaphoreTake(g_ctx.btn_step_sem, 0);

            btn_id_t id = BTN_ID_UP;
            if (xQueueReceive(g_ctx.btn_isr_queue, &id, 0) != pdPASS) {
                continue;
            }

            TickType_t *last_tick = (id == BTN_ID_UP) ? &last_up_tick : &last_down_tick;
            if ((now - *last_tick) >= step_guard_ticks) {
                xTimerReset(g_ctx.debounce_timer, 0);
                handle_step_button(id);
                *last_tick = xTaskGetTickCount();
            }
        }
    }
}
