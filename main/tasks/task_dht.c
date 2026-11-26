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

static const char *TAG = "DHT_TASK";

void dht_task(void *arg)
{
    (void)arg;

    /* FreeRTOS Ch. 9.4: Startup Rendezvous Barrier */
    xEventGroupSync(g_ctx.sync_events, SYNC_BIT_DHT, SYNC_ALL_BITS, pdMS_TO_TICKS(5000));
    ESP_LOGI(TAG, "Started (prio=%u, period=%d ms)", (unsigned)uxTaskPriorityGet(NULL), DHT_PERIOD_MS);

    TickType_t last_wake = xTaskGetTickCount();
    dht_data_t d = {0};

    for (;;) {
        float t = 0.0f;
        float h = 0.0f;

        if (dht22_read(&t, &h)) {
            d.temperature = t;
            d.humidity    = h;
            d.valid       = true;
        } else {
            d.valid = false;
            ESP_LOGW(TAG, "DHT22 read failed");
        }
        d.tick = xTaskGetTickCount();

        if (xQueueSendToBack(g_ctx.dht_queue, &d, 0) != pdPASS) {
            ESP_LOGW(TAG, "dht_queue full (waiting=%u)",
                     (unsigned)uxQueueMessagesWaiting(g_ctx.dht_queue));
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(DHT_PERIOD_MS));
    }
}
