#include "tasks.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "app_config.h"
#include "app_context.h"
#include "app_state.h"
#include "app_types.h"
#include "lcd_i2c.h"

static const char *TAG = "LCD_GK";

/*
 * FreeRTOS Ch. 8.4 Gatekeeper Pattern:
 * Only lcd_gatekeeper_task touches the LCD 16x2 I2C hardware after boot.
 */
static void render_screen(const env_snapshot_t *snap, const sys_config_t *cfg)
{
    char l0[32] = {0};
    char l1[32] = {0};

    /* Highest display priority: temporary Toast confirmation banner */
    if (app_state_get_toast(l0, sizeof(l0), l1, sizeof(l1))) {
        lcd_print_line(0, l0);
        lcd_print_line(1, l1);
        return;
    }

    switch (cfg->ui_page) {
    case UI_PAGE_MAIN: {
        char mode_ch = (cfg->mode == SYS_MODE_AUTO) ? 'A' : 'M';
        if (snap->dht_valid) {
            snprintf(l0, sizeof(l0), "T:%4.1f H:%2.0f%% [%c]",
                     snap->temperature, snap->humidity, mode_ch);
        } else {
            snprintf(l0, sizeof(l0), "T:---- H:--%% [%c]", mode_ch);
        }

        if (snap->alarm_level == ALARM_LEVEL_CRITICAL && !snap->alarm_muted) {
            snprintf(l1, sizeof(l1), "!! CRITICAL !!  ");
        } else if (snap->alarm_muted && snap->alarm_level != ALARM_LEVEL_NONE) {
            snprintf(l1, sizeof(l1), "L:%2u%% F:%3u%% MUT",
                     snap->light_percent, snap->fan_speed_pct);
        } else {
            snprintf(l1, sizeof(l1), "L:%2u%% F:%3u%% P:%c",
                     snap->light_percent, snap->fan_speed_pct,
                     snap->lamp_on ? '1' : '0');
        }
        break;
    }

    case UI_PAGE_SET_MODE:
        snprintf(l0, sizeof(l0), ">Mode: %-6s",
                 cfg->mode == SYS_MODE_AUTO ? "AUTO" : "MANUAL");
        snprintf(l1, sizeof(l1), "[1/6] UP/DN:Chg");
        break;

    case UI_PAGE_SET_FAN:
        snprintf(l0, sizeof(l0), ">Fan Speed:%3u%%", cfg->manual_fan_pct);
        snprintf(l1, sizeof(l1), "[2/6] PWM:%3u%%", snap->fan_speed_pct);
        break;

    case UI_PAGE_SET_LAMP:
        snprintf(l0, sizeof(l0), ">Lamp Ctrl: %-3s", cfg->manual_lamp_on ? "ON" : "OFF");
        snprintf(l1, sizeof(l1), "[3/6] L_Now:%2u%%", snap->light_percent);
        break;

    case UI_PAGE_SET_TEMP:
        snprintf(l0, sizeof(l0), ">Temp Warn:%2.0fC", cfg->temp_warn_c);
        snprintf(l1, sizeof(l1), "[4/6] Crit:%2.0fC", cfg->temp_crit_c);
        break;

    case UI_PAGE_SET_HUM:
        snprintf(l0, sizeof(l0), ">Hum High: %2.0f%%", cfg->hum_high_pct);
        snprintf(l1, sizeof(l1), "[5/6] Now:%2.0f%%", snap->humidity);
        break;

    case UI_PAGE_SET_LIGHT:
        snprintf(l0, sizeof(l0), ">Light Low:%2u%%", cfg->light_low_pct);
        snprintf(l1, sizeof(l1), "[6/6] Hold:Save");
        break;

    default:
        snprintf(l0, sizeof(l0), "Smart Env Ctrl");
        snprintf(l1, sizeof(l1), "FreeRTOS ESP32C3");
        break;
    }

    lcd_print_line(0, l0);
    lcd_print_line(1, l1);
}

void lcd_gatekeeper_task(void *arg)
{
    (void)arg;

    lcd_print_line(0, "Smart Env Ctrl");
    lcd_print_line(1, "FreeRTOS ESP32C3");
    vTaskDelay(pdMS_TO_TICKS(1000));
    lcd_clear();

    /* FreeRTOS Ch. 9.4: Startup Rendezvous Barrier */
    xEventGroupSync(g_ctx.sync_events, SYNC_BIT_LCD, SYNC_ALL_BITS, pdMS_TO_TICKS(5000));
    ESP_LOGI(TAG, "Started (prio=%u, Gatekeeper Task)", (unsigned)uxTaskPriorityGet(NULL));

    env_snapshot_t snap = {0};
    sys_config_t   cfg  = {0};

    /* Initial draw */
    app_state_peek_snapshot(&snap);
    app_state_get_config(&cfg);
    render_screen(&snap, &cfg);

    for (;;) {
        uint32_t notify_bits = 0;

        /* FreeRTOS Ch. 10: Wait for direct Task Notification bitmask */
        xTaskNotifyWait(0, 0xFFFFFFFFUL, &notify_bits, portMAX_DELAY);

        if (notify_bits & NOTIFY_LCD_HOME) {
            app_state_set_ui_page(UI_PAGE_MAIN);
        }
        if (notify_bits & NOTIFY_LCD_TOAST_END) {
            app_state_clear_toast();
        }

        /* Read latest snapshot via Mailbox xQueuePeek & config via Mutex */
        app_state_peek_snapshot(&snap);
        if (app_state_get_config(&cfg)) {
            render_screen(&snap, &cfg);
        }
    }
}
