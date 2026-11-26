#include "app_state.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "app_config.h"
#include "app_context.h"

static const char *TAG = "STATE";
static const char *NVS_NAMESPACE = "env_cfg";
static const char *NVS_KEY_CFG   = "sys_cfg";

static sys_config_t s_cfg;
static bool         s_toast_active = false;
static char         s_toast_l0[17] = {0};
static char         s_toast_l1[17] = {0};

static void fill_defaults(sys_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->magic          = SYS_CONFIG_MAGIC;
    cfg->version        = SYS_CONFIG_VERSION;
    cfg->mode           = SYS_MODE_AUTO;
    cfg->temp_warn_c    = DEFAULT_TEMP_WARN_C;
    cfg->temp_crit_c    = DEFAULT_TEMP_WARN_C + DEFAULT_TEMP_CRIT_DELTA;
    cfg->hum_high_pct   = DEFAULT_HUM_HIGH_PCT;
    cfg->light_low_pct  = DEFAULT_LIGHT_LOW_PCT;
    cfg->manual_fan_pct = 0;
    cfg->manual_lamp_on = false;
    cfg->ui_page        = UI_PAGE_MAIN;
    cfg->config_dirty   = false;
}

static float clamp_f(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int clamp_i(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

esp_err_t app_state_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition issue (%s), erasing...", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    fill_defaults(&s_cfg);

    if (app_state_load_nvs() != ESP_OK) {
        ESP_LOGI(TAG, "No saved NVS config found, using factory defaults");
    } else {
        ESP_LOGI(TAG, "Loaded NVS config: Mode=%s T_warn=%.1f T_crit=%.1f H_high=%.0f L_low=%u%%",
                 s_cfg.mode == SYS_MODE_AUTO ? "AUTO" : "MANUAL",
                 s_cfg.temp_warn_c, s_cfg.temp_crit_c, s_cfg.hum_high_pct, s_cfg.light_low_pct);
    }

    /* Always start on Main screen after reboot */
    s_cfg.ui_page      = UI_PAGE_MAIN;
    s_cfg.config_dirty = false;

    /* Seed Mailbox with an initial snapshot */
    env_snapshot_t init_snap = {
        .mode = s_cfg.mode,
    };
    app_state_publish_snapshot(&init_snap);

    return ESP_OK;
}

void app_state_load_defaults(void)
{
    if (xSemaphoreTake(g_ctx.config_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        fill_defaults(&s_cfg);
        xSemaphoreGive(g_ctx.config_mutex);
    }
}

esp_err_t app_state_load_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    sys_config_t loaded = {0};
    size_t len = sizeof(loaded);
    err = nvs_get_blob(handle, NVS_KEY_CFG, &loaded, &len);
    nvs_close(handle);

    if (err != ESP_OK || len != sizeof(loaded) ||
        loaded.magic != SYS_CONFIG_MAGIC || loaded.version != SYS_CONFIG_VERSION) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Sanitize values */
    loaded.temp_warn_c    = clamp_f(loaded.temp_warn_c, TEMP_WARN_MIN_C, TEMP_WARN_MAX_C);
    loaded.temp_crit_c    = clamp_f(loaded.temp_crit_c, loaded.temp_warn_c + 2.0f, 60.0f);
    loaded.hum_high_pct   = clamp_f(loaded.hum_high_pct, HUM_HIGH_MIN_PCT, HUM_HIGH_MAX_PCT);
    loaded.light_low_pct  = (uint8_t)clamp_i(loaded.light_low_pct, LIGHT_LOW_MIN_PCT, LIGHT_LOW_MAX_PCT);
    loaded.manual_fan_pct = (uint8_t)clamp_i(loaded.manual_fan_pct, 0, 100);
    loaded.ui_page        = UI_PAGE_MAIN;
    loaded.config_dirty   = false;

    if (xSemaphoreTake(g_ctx.config_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        s_cfg = loaded;
        xSemaphoreGive(g_ctx.config_mutex);
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t app_state_save_nvs(void)
{
    sys_config_t snap;
    if (!app_state_get_config(&snap)) {
        return ESP_ERR_TIMEOUT;
    }
    snap.magic        = SYS_CONFIG_MAGIC;
    snap.version      = SYS_CONFIG_VERSION;
    snap.ui_page      = UI_PAGE_MAIN;
    snap.config_dirty = false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, NVS_KEY_CFG, &snap, sizeof(snap));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        if (xSemaphoreTake(g_ctx.config_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_cfg.config_dirty = false;
            xSemaphoreGive(g_ctx.config_mutex);
        }
        ESP_LOGI(TAG, "Configuration saved to NVS");
    } else {
        ESP_LOGE(TAG, "NVS save failed: %s", esp_err_to_name(err));
    }
    return err;
}

bool app_state_get_config(sys_config_t *out_cfg)
{
    if (!out_cfg) return false;
    if (xSemaphoreTake(g_ctx.config_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *out_cfg = s_cfg;
        xSemaphoreGive(g_ctx.config_mutex);
        return true;
    }
    return false;
}

bool app_state_set_config(const sys_config_t *in_cfg)
{
    if (!in_cfg) return false;
    if (xSemaphoreTake(g_ctx.config_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_cfg = *in_cfg;
        xSemaphoreGive(g_ctx.config_mutex);
        return true;
    }
    return false;
}

bool app_state_set_ui_page(ui_page_t page)
{
    if (page >= UI_PAGE_COUNT) return false;
    if (xSemaphoreTake(g_ctx.config_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_cfg.ui_page = page;
        xSemaphoreGive(g_ctx.config_mutex);
        return true;
    }
    return false;
}

ui_page_t app_state_next_ui_page(bool *wrapped_to_main)
{
    ui_page_t next = UI_PAGE_MAIN;
    if (wrapped_to_main) *wrapped_to_main = false;

    if (xSemaphoreTake(g_ctx.config_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        int candidate = (int)s_cfg.ui_page + 1;
        if (candidate >= UI_PAGE_COUNT) {
            candidate = UI_PAGE_MAIN;
            if (wrapped_to_main) *wrapped_to_main = true;
        }
        s_cfg.ui_page = (ui_page_t)candidate;
        next = s_cfg.ui_page;
        xSemaphoreGive(g_ctx.config_mutex);
    }
    return next;
}

bool app_state_adjust_current_setting(int delta, char *toast_out, size_t toast_len)
{
    if (delta == 0) return false;
    bool changed = false;

    if (xSemaphoreTake(g_ctx.config_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        switch (s_cfg.ui_page) {
        case UI_PAGE_MAIN:
            /* On main page in MANUAL mode, UP/DOWN directly steps fan speed */
            if (s_cfg.mode == SYS_MODE_MANUAL) {
                int pct = (int)s_cfg.manual_fan_pct + delta * FAN_SPEED_STEP_PCT;
                pct = clamp_i(pct, 0, 100);
                s_cfg.manual_fan_pct = (uint8_t)pct;
                changed = true;
                if (toast_out && toast_len > 0) {
                    snprintf(toast_out, toast_len, "Fan PWM: %u%%", s_cfg.manual_fan_pct);
                }
            }
            break;

        case UI_PAGE_SET_MODE:
            s_cfg.mode = (s_cfg.mode == SYS_MODE_AUTO) ? SYS_MODE_MANUAL : SYS_MODE_AUTO;
            s_cfg.config_dirty = true;
            changed = true;
            if (toast_out && toast_len > 0) {
                snprintf(toast_out, toast_len, "Mode: %s",
                         s_cfg.mode == SYS_MODE_AUTO ? "AUTO" : "MANUAL");
            }
            break;

        case UI_PAGE_SET_FAN: {
            /* Automatically switch to MANUAL if user adjusts fan speed here */
            s_cfg.mode = SYS_MODE_MANUAL;
            int pct = (int)s_cfg.manual_fan_pct + delta * FAN_SPEED_STEP_PCT;
            pct = clamp_i(pct, 0, 100);
            s_cfg.manual_fan_pct = (uint8_t)pct;
            s_cfg.config_dirty = true;
            changed = true;
            break;
        }

        case UI_PAGE_SET_LAMP:
            s_cfg.mode = SYS_MODE_MANUAL;
            s_cfg.manual_lamp_on = !s_cfg.manual_lamp_on;
            s_cfg.config_dirty = true;
            changed = true;
            break;

        case UI_PAGE_SET_TEMP: {
            float t = clamp_f(s_cfg.temp_warn_c + (float)delta * 1.0f,
                              TEMP_WARN_MIN_C, TEMP_WARN_MAX_C);
            s_cfg.temp_warn_c  = t;
            s_cfg.temp_crit_c  = t + DEFAULT_TEMP_CRIT_DELTA;
            s_cfg.config_dirty = true;
            changed = true;
            break;
        }

        case UI_PAGE_SET_HUM: {
            float h = clamp_f(s_cfg.hum_high_pct + (float)delta * 5.0f,
                              HUM_HIGH_MIN_PCT, HUM_HIGH_MAX_PCT);
            s_cfg.hum_high_pct = h;
            s_cfg.config_dirty = true;
            changed = true;
            break;
        }

        case UI_PAGE_SET_LIGHT: {
            int l = clamp_i((int)s_cfg.light_low_pct + delta * 5,
                            LIGHT_LOW_MIN_PCT, LIGHT_LOW_MAX_PCT);
            s_cfg.light_low_pct = (uint8_t)l;
            s_cfg.config_dirty  = true;
            changed = true;
            break;
        }

        default:
            break;
        }
        xSemaphoreGive(g_ctx.config_mutex);
    }
    return changed;
}

bool app_state_set_mode(sys_mode_t mode)
{
    if (xSemaphoreTake(g_ctx.config_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_cfg.mode = mode;
        s_cfg.config_dirty = true;
        xSemaphoreGive(g_ctx.config_mutex);
        return true;
    }
    return false;
}

bool app_state_set_manual_fan(uint8_t pct)
{
    if (pct > 100) pct = 100;
    if (xSemaphoreTake(g_ctx.config_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_cfg.manual_fan_pct = pct;
        s_cfg.config_dirty   = true;
        xSemaphoreGive(g_ctx.config_mutex);
        return true;
    }
    return false;
}

bool app_state_set_manual_lamp(bool on)
{
    if (xSemaphoreTake(g_ctx.config_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_cfg.manual_lamp_on = on;
        s_cfg.config_dirty   = true;
        xSemaphoreGive(g_ctx.config_mutex);
        return true;
    }
    return false;
}

bool app_state_set_thresholds(float temp_warn, float temp_crit, float hum_high, uint8_t light_low)
{
    if (xSemaphoreTake(g_ctx.config_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (temp_warn > 0.0f) {
            s_cfg.temp_warn_c = clamp_f(temp_warn, TEMP_WARN_MIN_C, TEMP_WARN_MAX_C);
            if (temp_crit > s_cfg.temp_warn_c) {
                s_cfg.temp_crit_c = clamp_f(temp_crit, s_cfg.temp_warn_c + 2.0f, 60.0f);
            } else {
                s_cfg.temp_crit_c = s_cfg.temp_warn_c + DEFAULT_TEMP_CRIT_DELTA;
            }
        }
        if (hum_high > 0.0f) {
            s_cfg.hum_high_pct = clamp_f(hum_high, HUM_HIGH_MIN_PCT, HUM_HIGH_MAX_PCT);
        }
        if (light_low > 0) {
            s_cfg.light_low_pct = (uint8_t)clamp_i(light_low, LIGHT_LOW_MIN_PCT, LIGHT_LOW_MAX_PCT);
        }
        s_cfg.config_dirty = true;
        xSemaphoreGive(g_ctx.config_mutex);
        return true;
    }
    return false;
}

/* ---------------- Mailbox Helpers (xQueueOverwrite / xQueuePeek) ---------------- */
void app_state_publish_snapshot(const env_snapshot_t *snap)
{
    if (!snap || !g_ctx.sensor_mailbox) return;
    /* Mailbox has length 1 -> xQueueOverwrite always succeeds without blocking */
    xQueueOverwrite(g_ctx.sensor_mailbox, snap);
}

bool app_state_peek_snapshot(env_snapshot_t *out_snap)
{
    if (!out_snap || !g_ctx.sensor_mailbox) return false;
    return xQueuePeek(g_ctx.sensor_mailbox, out_snap, 0) == pdPASS;
}

/* ---------------- Toast Banner Helpers ---------------- */
void app_state_set_toast(const char *line0, const char *line1)
{
    if (xSemaphoreTake(g_ctx.config_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        snprintf(s_toast_l0, sizeof(s_toast_l0), "%.16s", line0 ? line0 : "");
        snprintf(s_toast_l1, sizeof(s_toast_l1), "%.16s", line1 ? line1 : "");
        s_toast_active = true;
        xSemaphoreGive(g_ctx.config_mutex);
    }
}

bool app_state_get_toast(char *line0, size_t len0, char *line1, size_t len1)
{
    bool active = false;
    if (xSemaphoreTake(g_ctx.config_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        active = s_toast_active;
        if (active) {
            if (line0 && len0 > 0) snprintf(line0, len0, "%s", s_toast_l0);
            if (line1 && len1 > 0) snprintf(line1, len1, "%s", s_toast_l1);
        }
        xSemaphoreGive(g_ctx.config_mutex);
    }
    return active;
}

void app_state_clear_toast(void)
{
    if (xSemaphoreTake(g_ctx.config_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_toast_active = false;
        xSemaphoreGive(g_ctx.config_mutex);
    }
}
