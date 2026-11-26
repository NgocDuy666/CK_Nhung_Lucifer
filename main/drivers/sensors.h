#pragma once
/*
 * sensors.h - DHT22 (one-wire w/ Critical Section) and LDR (12-bit ADC) drivers
 */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

void        dht22_init(void);
bool        dht22_read(float *temperature, float *humidity);

esp_err_t   ldr_init(void);
esp_err_t   ldr_read(uint16_t *raw, uint8_t *percent);
const char *ldr_level_str(uint8_t percent);
