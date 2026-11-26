#include "buttons.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"

#include "app_config.h"
#include "app_context.h"

static const char *TAG = "BTN_DRV";

/*
 * FreeRTOS Ch. 7.6 - Deferred work executed in the RTOS Daemon Task (Tmr Svc)
 * queued via xTimerPendFunctionCallFromISR() from hardware GPIO ISRs.
 */
static void isr_stats_pended_cb(void *pvParameter1, uint32_t ulParameter2)
{
    (void)pvParameter1;
    (void)ulParameter2;
    g_ctx.isr_event_count++;
}

/* ISR #1: BTN_MENU -> Binary Semaphore (xSemaphoreGiveFromISR) */
static void IRAM_ATTR btn_menu_isr_handler(void *arg)
{
    (void)arg;
    BaseType_t woken = pdFALSE;

    xSemaphoreGiveFromISR(g_ctx.btn_menu_sem, &woken);
    xTimerPendFunctionCallFromISR(isr_stats_pended_cb, NULL, (uint32_t)BTN_ID_MENU, &woken);

    if (woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

/* ISR #2/#3: BTN_UP & BTN_DOWN -> ISR Queue + Counting Semaphore */
static void IRAM_ATTR btn_step_isr_handler(void *arg)
{
    btn_id_t id = (btn_id_t)(uintptr_t)arg;
    BaseType_t woken = pdFALSE;

    if (xQueueSendToBackFromISR(g_ctx.btn_isr_queue, &id, &woken) == pdPASS) {
        xSemaphoreGiveFromISR(g_ctx.btn_step_sem, &woken);
    }
    xTimerPendFunctionCallFromISR(isr_stats_pended_cb, NULL, (uint32_t)id, &woken);

    if (woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

esp_err_t buttons_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BTN_MENU_GPIO) | (1ULL << BTN_UP_GPIO) | (1ULL << BTN_DOWN_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,      /* Active-LOW press */
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = gpio_isr_handler_add(BTN_MENU_GPIO, btn_menu_isr_handler, NULL);
    if (err != ESP_OK) return err;

    err = gpio_isr_handler_add(BTN_UP_GPIO, btn_step_isr_handler, (void *)(uintptr_t)BTN_ID_UP);
    if (err != ESP_OK) return err;

    err = gpio_isr_handler_add(BTN_DOWN_GPIO, btn_step_isr_handler, (void *)(uintptr_t)BTN_ID_DOWN);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "Buttons ISR ready (MENU=GPIO%d[%d], UP=GPIO%d[%d], DOWN=GPIO%d[%d])",
             BTN_MENU_GPIO, gpio_get_level(BTN_MENU_GPIO),
             BTN_UP_GPIO,   gpio_get_level(BTN_UP_GPIO),
             BTN_DOWN_GPIO, gpio_get_level(BTN_DOWN_GPIO));
    if (gpio_get_level(BTN_MENU_GPIO) == 0 ||
        gpio_get_level(BTN_UP_GPIO) == 0 ||
        gpio_get_level(BTN_DOWN_GPIO) == 0) {
        ESP_LOGW(TAG, "One or more buttons read LOW (0) at boot! Check 4-pin button orientation.");
    }
    return ESP_OK;
}

bool buttons_is_pressed(btn_id_t id)
{
    gpio_num_t pin = BTN_MENU_GPIO;
    if (id == BTN_ID_UP)        pin = BTN_UP_GPIO;
    else if (id == BTN_ID_DOWN) pin = BTN_DOWN_GPIO;

    return gpio_get_level(pin) == 0;            /* Active-LOW */
}
