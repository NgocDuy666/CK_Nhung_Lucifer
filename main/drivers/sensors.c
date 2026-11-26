#include "sensors.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

#include "app_config.h"

/* ======================== DHT22 (One-Wire + Critical Section) ======================== */
static portMUX_TYPE s_dht_mux = portMUX_INITIALIZER_UNLOCKED;

static int wait_while_level(int level, int timeout_us)
{
    int64_t start = esp_timer_get_time();

    while (gpio_get_level(DHT_GPIO) == level) {
        if (esp_timer_get_time() - start > timeout_us) {
            return -1;
        }
    }
    return (int)(esp_timer_get_time() - start);
}

void dht22_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << DHT_GPIO,
        .mode         = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(DHT_GPIO, 1);
}

bool dht22_read(float *temperature, float *humidity)
{
    uint8_t data[5] = {0};
    bool ok = true;

    /* Host start signal: pull low >= 1 ms, then release */
    gpio_set_level(DHT_GPIO, 0);
    esp_rom_delay_us(2000);
    gpio_set_level(DHT_GPIO, 1);
    esp_rom_delay_us(30);

    /* FreeRTOS Ch. 8 Critical Section: protect microsecond-sensitive 40-bit read */
    portENTER_CRITICAL(&s_dht_mux);
    do {
        if (wait_while_level(1, 100) < 0) { ok = false; break; }
        if (wait_while_level(0, 100) < 0) { ok = false; break; }
        if (wait_while_level(1, 100) < 0) { ok = false; break; }

        for (int i = 0; i < 40; i++) {
            if (wait_while_level(0, 100) < 0) { ok = false; break; }

            int pulse_us = wait_while_level(1, 100);   /* ~26 us = 0, ~70 us = 1 */
            if (pulse_us < 0) { ok = false; break; }

            data[i / 8] <<= 1;
            if (pulse_us > 40) {
                data[i / 8] |= 1;
            }
        }
    } while (0);
    portEXIT_CRITICAL(&s_dht_mux);

    if (!ok || ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4])) {
        return false;
    }

    *humidity = ((data[0] << 8) | data[1]) / 10.0f;

    float temp = (((data[2] & 0x7F) << 8) | data[3]) / 10.0f;
    *temperature = (data[2] & 0x80) ? -temp : temp;

    return true;
}

/* ======================== LDR (12-bit ADC Oneshot) ======================== */
static adc_oneshot_unit_handle_t s_adc = NULL;

esp_err_t ldr_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = LDR_ADC_UNIT,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) {
        return err;
    }

    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    return adc_oneshot_config_channel(s_adc, LDR_ADC_CHANNEL, &ch_cfg);
}

esp_err_t ldr_read(uint16_t *raw, uint8_t *percent)
{
    if (!s_adc || !raw || !percent) {
        return ESP_ERR_INVALID_STATE;
    }

    int value = 0;
    esp_err_t err = adc_oneshot_read(s_adc, LDR_ADC_CHANNEL, &value);
    if (err != ESP_OK) {
        return err;
    }

    if (value < 0) value = 0;
    if (value > 4095) value = 4095;

    *raw     = (uint16_t)value;
    *percent = (uint8_t)(100 - (value * 100) / 4095);  /* 100 = brightest */
    return ESP_OK;
}

const char *ldr_level_str(uint8_t percent)
{
    if (percent < LIGHT_NONE_MAX)   return "None";
    if (percent < LIGHT_WEAK_MAX)   return "Weak";
    if (percent < LIGHT_MEDIUM_MAX) return "Medium";
    return "Bright";
}
