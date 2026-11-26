#pragma once
/*
 * lcd_i2c.h - LCD 16x2 via PCF8574 I2C backpack (owned exclusively by lcd_gatekeeper_task)
 */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t lcd_init(void);
void      lcd_clear(void);
void      lcd_set_backlight(bool on);
void      lcd_print_line(uint8_t row, const char *text);   /* Pads/truncates to 16 chars */
