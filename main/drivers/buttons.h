#pragma once
/*
 * buttons.h - 3-Button GPIO Interrupt (ISR) driver using FreeRTOS Ch. 7 patterns:
 *   - BTN_MENU (GPIO0): Binary Semaphore (xSemaphoreGiveFromISR)
 *   - BTN_UP   (GPIO1): Counting Semaphore + ISR Queue (xQueueSendToBackFromISR)
 *   - BTN_DOWN (GPIO9): Counting Semaphore + ISR Queue (xQueueSendToBackFromISR)
 *   - Deferred work to RTOS Daemon Task (xTimerPendFunctionCallFromISR)
 */

#include <stdbool.h>
#include "esp_err.h"
#include "app_types.h"

esp_err_t buttons_init(void);
bool      buttons_is_pressed(btn_id_t id);
