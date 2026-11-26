#pragma once
/*
 * app_state.h - Thread-safe shared configuration (Mutex), NVS persistence,
 *               Mailbox snapshot helpers, and LCD toast message buffer.
 */

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "app_types.h"

/* Initialize NVS, default config, Mutex, and Mailbox initial snapshot */
esp_err_t app_state_init(void);

/* Reset configuration to factory defaults (thread-safe) */
void app_state_load_defaults(void);

/* Persist / load sys_config_t in NVS Flash */
esp_err_t app_state_save_nvs(void);
esp_err_t app_state_load_nvs(void);

/* Thread-safe config read/write (protected by g_ctx.config_mutex) */
bool app_state_get_config(sys_config_t *out_cfg);
bool app_state_set_config(const sys_config_t *in_cfg);

/* Atomic field updaters for Menu / CLI (return true on success) */
bool app_state_set_ui_page(ui_page_t page);
ui_page_t app_state_next_ui_page(bool *wrapped_to_main);
bool app_state_adjust_current_setting(int delta, char *toast_out, size_t toast_len);
bool app_state_set_mode(sys_mode_t mode);
bool app_state_set_manual_fan(uint8_t pct);
bool app_state_set_manual_lamp(bool on);
bool app_state_set_thresholds(float temp_warn, float temp_crit, float hum_high, uint8_t light_low);

/* Mailbox helpers (xQueueOverwrite / xQueuePeek - Ch. 5.1.3) */
void app_state_publish_snapshot(const env_snapshot_t *snap);
bool app_state_peek_snapshot(env_snapshot_t *out_snap);

/* Toast banner buffer (protected by config_mutex) */
void app_state_set_toast(const char *line0, const char *line1);
bool app_state_get_toast(char *line0, size_t len0, char *line1, size_t len1);
void app_state_clear_toast(void);
