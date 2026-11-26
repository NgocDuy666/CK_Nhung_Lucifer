#include "tasks.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "app_config.h"
#include "app_context.h"
#include "actuators.h"

static const char *TAG = "ALARM_TASK";

void alarm_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Started (prio=%u, EventGroup + TaskNotification)", (unsigned)uxTaskPriorityGet(NULL));

    bool pulse_on = false;

    for (;;) {
        /*
         * FreeRTOS Ch. 9: Block with 0% CPU until ANY alarm bit is set (OR-wait).
         */
        EventBits_t bits = xEventGroupWaitBits(
            g_ctx.sys_events,
            EVT_ALARM_ANY_MASK,
            pdFALSE,            /* Do not clear bits on exit (owned by control_task) */
            pdFALSE,            /* OR-wait: wake if any alarm bit is active */
            portMAX_DELAY);

        /* Active alarm loop driven by alarm_blink_timer notifications */
        while ((bits & EVT_ALARM_ANY_MASK) != 0) {
            if ((bits & EVT_ALARM_MUTED) != 0) {
                /* Muted: silence buzzer immediately, keep red LED solidly ON as reminder */
                actuator_set_buzzer(false);
                actuator_set_alarm_led(true);
                pulse_on = false;
            } else {
                bool is_crit = ((bits & EVT_TEMP_CRIT) != 0) ||
                               ((bits & EVT_HEAT_STRESS_MASK) == EVT_HEAT_STRESS_MASK);
                pulse_on = !pulse_on;
                actuator_set_alarm_led(pulse_on);
                actuator_set_buzzer(pulse_on);

                if (pulse_on && is_crit) {
                    ESP_LOGW(TAG, "CRITICAL ALARM ACTIVE (bits=0x%02X)", (unsigned)bits);
                }
            }

            /*
             * FreeRTOS Ch. 10: Wait for direct Task Notification from alarm_blink_timer
             * (or immediate wakeup from control_task on state/mute change).
             */
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1500));
            bits = xEventGroupGetBits(g_ctx.sys_events);
        }

        /* All hazard bits cleared -> turn off alert outputs */
        pulse_on = false;
        actuator_set_alarm_led(false);
        actuator_set_buzzer(false);
        ESP_LOGI(TAG, "Alarm cleared -> outputs OFF");
    }
}
