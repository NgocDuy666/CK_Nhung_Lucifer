#include "tasks.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "app_config.h"
#include "app_context.h"
#include "app_state.h"
#include "app_types.h"
#include "actuators.h"

static const char *TAG = "CTRL_TASK";

typedef struct {
    float         temp_c;
    float         hum_pct;
    bool          dht_valid;
    uint32_t      dht_err_streak;
    uint16_t      light_raw;
    uint8_t       light_pct;

    /* Hysteresis latch states */
    bool          temp_warn_latched;
    bool          hum_high_latched;
    bool          light_low_latched;

    /* Alarm runtime tracking */
    bool          alarm_muted;
    alarm_level_t prev_alarm_level;
    bool          prev_alarm_muted;
} ctrl_state_t;

static void update_alarm_timer(alarm_level_t level, bool muted, ctrl_state_t *st)
{
    bool state_changed = (level != st->prev_alarm_level) || (muted != st->prev_alarm_muted);
    if (!state_changed) {
        return;
    }

    if (level == ALARM_LEVEL_NONE || muted) {
        xTimerStop(g_ctx.alarm_blink_timer, 0);
    } else if (level == ALARM_LEVEL_CRITICAL) {
        /* FreeRTOS Ch. 6: Dynamic period change -> fast 250 ms critical cadence */
        xTimerChangePeriod(g_ctx.alarm_blink_timer, pdMS_TO_TICKS(ALARM_CRIT_PERIOD_MS), 0);
    } else {
        /* Warning -> slow 1000 ms cadence */
        xTimerChangePeriod(g_ctx.alarm_blink_timer, pdMS_TO_TICKS(ALARM_WARN_PERIOD_MS), 0);
    }

    st->prev_alarm_level = level;
    st->prev_alarm_muted = muted;

    /* Wake alarm_task immediately for <1 ms response on state transition */
    if (g_ctx.alarm_task_handle) {
        xTaskNotifyGive(g_ctx.alarm_task_handle);
    }
}

static void evaluate_and_apply(ctrl_state_t *st)
{
    sys_config_t cfg;
    if (!app_state_get_config(&cfg)) {
        return;
    }

    /* 1) Evaluate thresholds with hysteresis */
    bool temp_crit  = false;
    bool sensor_err = (st->dht_err_streak >= DHT_ERR_ALARM_LIMIT);

    if (st->dht_valid) {
        if (!st->temp_warn_latched && st->temp_c >= cfg.temp_warn_c) {
            st->temp_warn_latched = true;
        } else if (st->temp_warn_latched && st->temp_c <= (cfg.temp_warn_c - TEMP_HYSTERESIS_C)) {
            st->temp_warn_latched = false;
        }

        temp_crit = (st->temp_c >= cfg.temp_crit_c);

        if (!st->hum_high_latched && st->hum_pct >= cfg.hum_high_pct) {
            st->hum_high_latched = true;
        } else if (st->hum_high_latched && st->hum_pct <= (cfg.hum_high_pct - HUM_HYSTERESIS_PCT)) {
            st->hum_high_latched = false;
        }
    }

    if (!st->light_low_latched && st->light_pct < cfg.light_low_pct) {
        st->light_low_latched = true;
    } else if (st->light_low_latched && st->light_pct >= (uint8_t)(cfg.light_low_pct + LIGHT_HYSTERESIS_PCT)) {
        st->light_low_latched = false;
    }

    /* 2) Determine multi-level alarm (including Heat-Stress AND condition) */
    bool heat_stress = st->temp_warn_latched && st->hum_high_latched;
    alarm_level_t level = ALARM_LEVEL_NONE;

    if (temp_crit || heat_stress) {
        level = ALARM_LEVEL_CRITICAL;
    } else if (st->temp_warn_latched || st->hum_high_latched || sensor_err) {
        level = ALARM_LEVEL_WARNING;
    } else {
        /* Clear mute latch automatically once conditions return to normal */
        if (st->alarm_muted) {
            st->alarm_muted = false;
            xTimerStop(g_ctx.mute_timer, 0);
        }
    }

    /* 3) Synchronize FreeRTOS Event Group bits (Ch. 9) */
    EventBits_t set_bits   = 0;
    EventBits_t clear_bits = 0;

    if (st->temp_warn_latched) set_bits |= EVT_TEMP_WARN;   else clear_bits |= EVT_TEMP_WARN;
    if (temp_crit)             set_bits |= EVT_TEMP_CRIT;   else clear_bits |= EVT_TEMP_CRIT;
    if (st->hum_high_latched)  set_bits |= EVT_HUM_HIGH;    else clear_bits |= EVT_HUM_HIGH;
    if (st->light_low_latched) set_bits |= EVT_LIGHT_LOW;   else clear_bits |= EVT_LIGHT_LOW;
    if (sensor_err)            set_bits |= EVT_SENSOR_ERR;  else clear_bits |= EVT_SENSOR_ERR;
    if (st->alarm_muted)       set_bits |= EVT_ALARM_MUTED; else clear_bits |= EVT_ALARM_MUTED;
    if (cfg.mode == SYS_MODE_MANUAL) set_bits |= EVT_MODE_MANUAL; else clear_bits |= EVT_MODE_MANUAL;

    if (clear_bits) xEventGroupClearBits(g_ctx.sys_events, clear_bits);
    if (set_bits)   xEventGroupSetBits(g_ctx.sys_events, set_bits);

    /* 4) Update Software Timer cadence for alarm_task */
    update_alarm_timer(level, st->alarm_muted, st);

    /* 5) Compute Fan PWM speed (0..100%) & Lamp ON/OFF */
    uint8_t fan_pct = 0;
    bool    lamp_on = false;

    if (cfg.mode == SYS_MODE_AUTO) {
        if (level == ALARM_LEVEL_CRITICAL) {
            fan_pct = FAN_AUTO_CRIT_PCT;
        } else if (st->temp_warn_latched) {
            fan_pct = FAN_AUTO_WARN_PCT;
        } else if (st->hum_high_latched) {
            fan_pct = FAN_AUTO_MID_PCT;
        } else {
            fan_pct = 0;
        }
        lamp_on = st->light_low_latched;
    } else {
        fan_pct = cfg.manual_fan_pct;
        lamp_on = cfg.manual_lamp_on;
    }

    actuator_set_fan_pct(fan_pct);
    actuator_set_lamp(lamp_on);

    /* 6) Publish snapshot to Mailbox (xQueueOverwrite) & Notify LCD Gatekeeper */
    env_snapshot_t snap = {
        .temperature      = st->temp_c,
        .humidity         = st->hum_pct,
        .dht_valid        = st->dht_valid,
        .dht_err_count    = st->dht_err_streak,
        .light_raw        = st->light_raw,
        .light_percent    = st->light_pct,
        .fan_speed_pct    = fan_pct,
        .lamp_on          = lamp_on,
        .alarm_level      = level,
        .alarm_muted      = st->alarm_muted,
        .mode             = cfg.mode,
        .isr_press_total  = g_ctx.isr_event_count,
        .last_update_tick = xTaskGetTickCount(),
    };
    app_state_publish_snapshot(&snap);

    if (g_ctx.lcd_task_handle) {
        xTaskNotify(g_ctx.lcd_task_handle, NOTIFY_LCD_DATA, eSetBits);
    }
}

static void handle_command(const cmd_msg_t *cmd, ctrl_state_t *st)
{
    switch (cmd->type) {
    case CMD_CONFIG_UPDATED:
        ESP_LOGI(TAG, "Config updated (src=%d) -> re-evaluating outputs", (int)cmd->source);
        break;

    case CMD_SET_MODE:
        app_state_set_mode((sys_mode_t)cmd->value);
        break;

    case CMD_SET_MANUAL_FAN:
        app_state_set_mode(SYS_MODE_MANUAL);
        app_state_set_manual_fan((uint8_t)cmd->value);
        break;

    case CMD_SET_MANUAL_LAMP:
        app_state_set_mode(SYS_MODE_MANUAL);
        app_state_set_manual_lamp(cmd->value != 0);
        break;

    case CMD_ALARM_MUTE:
        st->alarm_muted = true;
        xTimerReset(g_ctx.mute_timer, 0);
        ESP_LOGW(TAG, "Alarm MUTED for %d s", ALARM_MUTE_DURATION_MS / 1000);
        break;

    case CMD_ALARM_UNMUTE:
        st->alarm_muted = false;
        ESP_LOGI(TAG, "Alarm mute timer expired -> unmuted");
        break;

    case CMD_SAVE_NVS: {
        sys_config_t cfg;
        if (app_state_get_config(&cfg)) {
            /* cmd->value == 1 means save only if dirty (from UI timeout timer) */
            if (cmd->value == 0 || cfg.config_dirty) {
                if (app_state_save_nvs() == ESP_OK && cmd->source == CMD_SRC_TIMER) {
                    app_show_toast(">> AUTO SAVED", "Config in NVS");
                }
            }
        }
        break;
    }

    case CMD_RESET_DEFAULTS:
        app_state_load_defaults();
        app_state_save_nvs();
        app_show_toast(">> FACTORY RST", "Defaults loaded");
        ESP_LOGW(TAG, "Configuration reset to factory defaults");
        break;

    default:
        break;
    }
}

void control_task(void *arg)
{
    (void)arg;

    /* FreeRTOS Ch. 9.4: Startup Rendezvous Barrier */
    xEventGroupSync(g_ctx.sync_events, SYNC_BIT_CONTROL, SYNC_ALL_BITS, pdMS_TO_TICKS(5000));
    ESP_LOGI(TAG, "Started (prio=%u, QueueSet consumer)", (unsigned)uxTaskPriorityGet(NULL));

    ctrl_state_t st;
    memset(&st, 0, sizeof(st));

    /* Apply initial outputs & publish initial snapshot */
    evaluate_and_apply(&st);

    for (;;) {
        /* FreeRTOS Ch. 5: Block until DHT, Light, or Command queue has data */
        QueueSetMemberHandle_t member = xQueueSelectFromSet(g_ctx.main_queue_set, portMAX_DELAY);

        if (member == g_ctx.dht_queue) {
            dht_data_t d;
            if (xQueueReceive(g_ctx.dht_queue, &d, 0) == pdPASS) {
                if (d.valid) {
                    st.temp_c         = d.temperature;
                    st.hum_pct        = d.humidity;
                    st.dht_valid      = true;
                    st.dht_err_streak = 0;
                    ESP_LOGI(TAG, "DHT: T=%.1f C, H=%.1f %%", d.temperature, d.humidity);
                } else {
                    st.dht_err_streak++;
                    ESP_LOGW(TAG, "DHT fail streak=%lu", (unsigned long)st.dht_err_streak);
                }
            }
        } else if (member == g_ctx.light_queue) {
            light_data_t l;
            if (xQueueReceive(g_ctx.light_queue, &l, 0) == pdPASS) {
                st.light_raw = l.raw;
                st.light_pct = l.percent;
                ESP_LOGD(TAG, "LDR: raw=%u (%u%%)", l.raw, l.percent);
            }
        } else if (member == g_ctx.cmd_queue) {
            cmd_msg_t cmd;
            if (xQueueReceive(g_ctx.cmd_queue, &cmd, 0) == pdPASS) {
                handle_command(&cmd, &st);
            }
        }

        evaluate_and_apply(&st);
    }
}
