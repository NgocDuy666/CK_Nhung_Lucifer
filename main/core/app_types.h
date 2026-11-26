#pragma once
/*
 * app_types.h - Shared data structures, enums and message contracts
 */

#include <stdbool.h>
#include <stdint.h>

/* ---------------- System Modes, UI Pages & Alarm Levels ---------------- */
typedef enum {
    SYS_MODE_AUTO = 0,
    SYS_MODE_MANUAL
} sys_mode_t;

typedef enum {
    UI_PAGE_MAIN = 0,       /* T/H/Light + Mode + Fan% + Lamp */
    UI_PAGE_SET_MODE,       /* [1/6] AUTO <-> MANUAL */
    UI_PAGE_SET_FAN,        /* [2/6] Manual Fan PWM 0..100% */
    UI_PAGE_SET_LAMP,       /* [3/6] Manual Lamp ON/OFF */
    UI_PAGE_SET_TEMP,       /* [4/6] Temp Warn threshold */
    UI_PAGE_SET_HUM,        /* [5/6] Humidity High threshold */
    UI_PAGE_SET_LIGHT,      /* [6/6] Light Low threshold */
    UI_PAGE_COUNT
} ui_page_t;

typedef enum {
    ALARM_LEVEL_NONE = 0,
    ALARM_LEVEL_WARNING,
    ALARM_LEVEL_CRITICAL
} alarm_level_t;

/* ---------------- Sensor Queue Payloads ---------------- */
typedef struct {
    float    temperature;
    float    humidity;
    bool     valid;
    uint32_t tick;
} dht_data_t;

typedef struct {
    uint16_t raw;           /* 0..4095 */
    uint8_t  percent;       /* 0..100 (100 = brightest) */
    uint32_t tick;
} light_data_t;

/* ---------------- Multi-Source Command Queue Payload (Ch. 5.1.2) ---------------- */
typedef enum {
    CMD_SRC_BUTTON = 0,
    CMD_SRC_CLI,
    CMD_SRC_TIMER
} cmd_source_t;

typedef enum {
    CMD_CONFIG_UPDATED = 0, /* Re-evaluate outputs from updated config */
    CMD_SET_MODE,           /* value = sys_mode_t */
    CMD_SET_MANUAL_FAN,     /* value = 0..100 */
    CMD_SET_MANUAL_LAMP,    /* value = 0/1 */
    CMD_ALARM_MUTE,         /* Priority command via xQueueSendToFront */
    CMD_ALARM_UNMUTE,       /* Sent by mute_timer callback */
    CMD_SAVE_NVS,           /* Persist sys_config_t to NVS */
    CMD_RESET_DEFAULTS      /* Restore default thresholds & save */
} cmd_type_t;

typedef struct {
    cmd_source_t source;
    cmd_type_t   type;
    int32_t      value;
} cmd_msg_t;

/* ---------------- Button ISR Queue Payload ---------------- */
typedef enum {
    BTN_ID_MENU = 0,
    BTN_ID_UP,
    BTN_ID_DOWN
} btn_id_t;

/* ---------------- Persistent System Config (Protected by config_mutex) ---------------- */
#define SYS_CONFIG_MAGIC    0x4C554331U     /* "LUC1" */
#define SYS_CONFIG_VERSION  1U

typedef struct {
    uint32_t   magic;
    uint32_t   version;
    sys_mode_t mode;
    float      temp_warn_c;
    float      temp_crit_c;
    float      hum_high_pct;
    uint8_t    light_low_pct;
    uint8_t    manual_fan_pct;              /* 0..100 */
    bool       manual_lamp_on;
    ui_page_t  ui_page;                     /* Runtime UI page (reset to MAIN on boot) */
    bool       config_dirty;                /* True if user modified settings since last save */
} sys_config_t;

/* ---------------- Environment Snapshot (Stored in sensor_mailbox via Overwrite/Peek) ---------------- */
typedef struct {
    float         temperature;
    float         humidity;
    bool          dht_valid;
    uint32_t      dht_err_count;
    uint16_t      light_raw;
    uint8_t       light_percent;
    uint8_t       fan_speed_pct;            /* Actual active PWM % (0..100) */
    bool          lamp_on;                  /* Actual active Lamp state */
    alarm_level_t alarm_level;
    bool          alarm_muted;
    sys_mode_t    mode;
    uint32_t      isr_press_total;
    uint32_t      last_update_tick;
} env_snapshot_t;
