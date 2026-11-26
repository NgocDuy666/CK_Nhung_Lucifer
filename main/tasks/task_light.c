#include "tasks.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "app_config.h"
#include "app_context.h"
#include "app_types.h"
#include "sensors.h"

static const char *TAG = "LIGHT_TASK";

void light_task(void *arg)
{
    (void)arg;

    /* FreeRTOS Ch. 9.4: Startup Rendezvous Barrier */
    xEventGroupSync(g_ctx.sync_events, SYNC_BIT_LIGHT, SYNC_ALL_BITS, pdMS_TO_TICKS(5000));
    ESP_LOGI(TAG, "Started (prio=%u, period=%d ms)", (unsigned)uxTaskPriorityGet(NULL), LIGHT_PERIOD_MS);

    TickType_t   last_wake = xTaskGetTickCount();
    light_data_t d = {0};

    for (;;) {
        if (ldr_read(&d.raw, &d.percent) == ESP_OK) {
            d.tick = xTaskGetTickCount();
            if (xQueueSendToBack(g_ctx.light_queue, &d, 0) != pdPASS) {
                ESP_LOGW(TAG, "light_queue full (waiting=%u)",
                         (unsigned)uxQueueMessagesWaiting(g_ctx.light_queue));
            }
        } else {
            ESP_LOGW(TAG, "LDR ADC read failed");
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(LIGHT_PERIOD_MS));
    }
}
