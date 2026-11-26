#pragma once
/*
 * actuators.h - L298N/MOSFET PWM Fan (LEDC), Lamp LED, Alarm LED, and Buzzer drivers
 */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t actuators_init(void);

void      actuator_set_fan_pct(uint8_t pct);   /* 0..100% PWM duty on FAN_PWM_GPIO */
uint8_t   actuator_get_fan_pct(void);

void      actuator_set_lamp(bool on);
bool      actuator_get_lamp(void);

void      actuator_set_alarm_led(bool on);
void      actuator_set_buzzer(bool on);
