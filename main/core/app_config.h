#pragma once
/*
 * app_config.h - Central hardware pinout, RTOS tuning, thresholds & bitmasks
 * Project: Smart Environmental Monitoring and Control System (ESP32-C3 + FreeRTOS)
 */

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

/* ======================== Hardware Pinout (ESP32-C3) ======================== */
/* Midterm pins preserved 100% */
#define LDR_ADC_UNIT            ADC_UNIT_1
#define LDR_ADC_CHANNEL         ADC_CHANNEL_3       /* GPIO3 */
#define DHT_GPIO                GPIO_NUM_4
#define I2C_SDA_GPIO            GPIO_NUM_5
#define I2C_SCL_GPIO            GPIO_NUM_6
#define LCD_I2C_ADDR            0x27
#define LCD_I2C_ADDR_ALT        0x3F
#define LCD_I2C_FREQ_HZ         100000

/* Final project added inputs (3 buttons, active-LOW w/ internal pull-up) */
#define BTN_MENU_GPIO           GPIO_NUM_2          /* Short: next menu page | Hold: save & exit (OPEN=HIGH at boot) */
#define BTN_UP_GPIO             GPIO_NUM_1          /* Value + / Fan + / Mute alarm */
#define BTN_DOWN_GPIO           GPIO_NUM_9          /* Value - / Fan - / Mute alarm (also BOOT btn) */

/* Final project added actuators */
#define FAN_PWM_GPIO            GPIO_NUM_7          /* L298N ENA or MOSFET PWM input (0..100%) */
#define LAMP_GPIO               GPIO_NUM_10         /* Lamp LED / Relay */
#define ALARM_LED_GPIO          GPIO_NUM_8          /* Red Alarm LED (or onboard LED) */
#define BUZZER_GPIO             GPIO_NUM_0          /* Bare 2-pin Buzzer: (+) -> GPIO0 (via 100R), (-) -> GND */

/*
 * Bare 2-pin Buzzer (+/-) mode:
 *   0 = Active Buzzer (default - simple GPIO HIGH/LOW, most 2-pin buzzers with '+' mark)
 *   1 = Passive Buzzer (uses LEDC 2 kHz PWM tone if bare buzzer only clicks at 0)
 */
#define BUZZER_PASSIVE_PWM      0
#define BUZZER_PWM_FREQ_HZ      2000

/*
 * PWM frequency for 24V Fan via L298N / MOSFET:
 * 500 Hz works smoothly on both 2-wire 24V BLDC fans and brushed DC motors
 * without high-pitched whine or stalling, and dims LED cleanly on Wokwi.
 */
#define FAN_PWM_FREQ_HZ         500

/* ======================== Sensor & Control Defaults ======================== */
#define LIGHT_NONE_MAX          40
#define LIGHT_WEAK_MAX          65
#define LIGHT_MEDIUM_MAX        75

#define DEFAULT_TEMP_WARN_C     30.0f
#define DEFAULT_TEMP_CRIT_DELTA 5.0f                /* Crit = Warn + 5 C */
#define DEFAULT_HUM_HIGH_PCT    80.0f
#define DEFAULT_LIGHT_LOW_PCT   40

#define TEMP_HYSTERESIS_C       1.5f
#define HUM_HYSTERESIS_PCT      5.0f
#define LIGHT_HYSTERESIS_PCT    10

#define TEMP_WARN_MIN_C         20.0f
#define TEMP_WARN_MAX_C         45.0f
#define HUM_HIGH_MIN_PCT        40.0f
#define HUM_HIGH_MAX_PCT        95.0f
#define LIGHT_LOW_MIN_PCT       10
#define LIGHT_LOW_MAX_PCT       80

#define FAN_SPEED_STEP_PCT      25                  /* 0 -> 25 -> 50 -> 75 -> 100 */
#define FAN_AUTO_MID_PCT        50                  /* High humidity / warm */
#define FAN_AUTO_WARN_PCT       75                  /* Temp warning */
#define FAN_AUTO_CRIT_PCT       100                 /* Critical / heat-stress */

#define DHT_ERR_ALARM_LIMIT     3                   /* Consecutive fails -> EVT_SENSOR_ERR */

/* ======================== RTOS Task Periods & Tuning ======================== */
#define DHT_PERIOD_MS           2000
#define LIGHT_PERIOD_MS         500

#define BTN_DEBOUNCE_MS         50
#define BTN_LONG_PRESS_MS       1200
#define UI_MENU_TIMEOUT_MS      10000
#define UI_TOAST_DURATION_MS    2000
#define ALARM_MUTE_DURATION_MS  30000
#define ALARM_WARN_PERIOD_MS    1000
#define ALARM_CRIT_PERIOD_MS    250

/* Task Priorities (higher number = higher priority) */
#define PRIO_BUTTON_TASK        6
#define PRIO_DHT_TASK           5
#define PRIO_LIGHT_TASK         5
#define PRIO_CONTROL_TASK       4
#define PRIO_ALARM_TASK         4
#define PRIO_LCD_TASK           3
#define PRIO_CLI_TASK           2

/* Task Stack Sizes (bytes in ESP-IDF FreeRTOS) */
#define STACK_BUTTON_TASK       3072
#define STACK_DHT_TASK          3072
#define STACK_LIGHT_TASK        3072
#define STACK_CONTROL_TASK      4096
#define STACK_ALARM_TASK        3072
#define STACK_LCD_TASK          4096
#define STACK_CLI_TASK          4096

/* Queue Lengths */
#define DHT_Q_LEN               5
#define LIGHT_Q_LEN             5
#define CMD_Q_LEN               10
#define MAIN_QSET_LEN           (DHT_Q_LEN + LIGHT_Q_LEN + CMD_Q_LEN)
#define BTN_STEP_MAX_COUNT      10
#define BTN_QSET_LEN            (1 + BTN_STEP_MAX_COUNT)

/* ======================== Event Group Bits ======================== */
/* 1) Startup Rendezvous Sync Group (xEventGroupSync) */
#define SYNC_BIT_DHT            (1U << 0)
#define SYNC_BIT_LIGHT          (1U << 1)
#define SYNC_BIT_CONTROL        (1U << 2)
#define SYNC_BIT_LCD            (1U << 3)
#define SYNC_ALL_BITS           (SYNC_BIT_DHT | SYNC_BIT_LIGHT | SYNC_BIT_CONTROL | SYNC_BIT_LCD)

/* 2) Runtime System Status & Alert Event Group (xEventGroupWaitBits) */
#define EVT_TEMP_WARN           (1U << 0)
#define EVT_TEMP_CRIT           (1U << 1)
#define EVT_HUM_HIGH            (1U << 2)
#define EVT_LIGHT_LOW           (1U << 3)
#define EVT_SENSOR_ERR          (1U << 4)
#define EVT_ALARM_MUTED         (1U << 5)
#define EVT_MODE_MANUAL         (1U << 6)

#define EVT_ALARM_ANY_MASK      (EVT_TEMP_WARN | EVT_TEMP_CRIT | EVT_HUM_HIGH | EVT_SENSOR_ERR)
#define EVT_HEAT_STRESS_MASK    (EVT_TEMP_WARN | EVT_HUM_HIGH)

/* ======================== LCD Task Notification Bits ======================== */
#define NOTIFY_LCD_DATA         (1U << 0)           /* New sensor/actuator snapshot */
#define NOTIFY_LCD_MENU         (1U << 1)           /* Menu page or value changed */
#define NOTIFY_LCD_TOAST        (1U << 2)           /* Show temporary toast banner */
#define NOTIFY_LCD_TOAST_END    (1U << 3)           /* Toast expired -> redraw normal page */
#define NOTIFY_LCD_HOME         (1U << 4)           /* Timeout -> return to page 0 */
