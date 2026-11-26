#pragma once
/*
 * tasks.h - Declarations for all FreeRTOS task entry points and software timer module
 */

#include "esp_err.h"

/* Software Timers (Ch. 6) */
esp_err_t app_timers_init(void);
void      app_show_toast(const char *line0, const char *line1);

/* 7 Modular FreeRTOS Tasks (Ch. 4) */
void button_task(void *arg);            /* Priority 6 - Deferred ISR & LCD Menu */
void dht_task(void *arg);               /* Priority 5 - Periodic 2000 ms DHT22 */
void light_task(void *arg);             /* Priority 5 - Periodic 500 ms LDR */
void control_task(void *arg);           /* Priority 4 - Queue Set, Control, Mailbox, Event Group */
void alarm_task(void *arg);             /* Priority 4 - Event Group WaitBits + Timer Notify */
void lcd_gatekeeper_task(void *arg);    /* Priority 3 - Gatekeeper Task + Task Notification */
void cli_task(void *arg);               /* Priority 2 - UART/USB Console CLI */
