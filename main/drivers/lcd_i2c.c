#include "lcd_i2c.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include "app_config.h"

/* PCF8574 pin map: P0=RS P1=RW P2=EN P3=BL P4..P7=D4..D7 */
#define LCD_RS  0x01
#define LCD_EN  0x04
#define LCD_BL  0x08

static const char              *TAG = "LCD";
static i2c_master_bus_handle_t  s_bus = NULL;
static i2c_master_dev_handle_t  s_dev = NULL;
static uint8_t                  s_bl_mask = LCD_BL;

static void lcd_expander_write(uint8_t data)
{
    if (!s_dev) return;

    uint8_t b = data | s_bl_mask;
    esp_err_t err = i2c_master_transmit(s_dev, &b, 1, 100);
    if (err != ESP_OK) {
        static bool logged = false;
        if (!logged) {
            ESP_LOGE(TAG, "I2C write failed: %s", esp_err_to_name(err));
            logged = true;
        }
    }
}

static void lcd_write4bits(uint8_t value)
{
    lcd_expander_write(value);
    lcd_expander_write(value | LCD_EN);
    esp_rom_delay_us(1);
    lcd_expander_write(value & (uint8_t)~LCD_EN);
    esp_rom_delay_us(50);
}

static void lcd_send(uint8_t value, uint8_t mode)
{
    lcd_write4bits((value & 0xF0) | mode);
    lcd_write4bits(((uint8_t)(value << 4) & 0xF0) | mode);
}

static void lcd_cmd(uint8_t cmd)
{
    lcd_send(cmd, 0);
}

esp_err_t lcd_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port                     = I2C_NUM_0,
        .sda_io_num                   = I2C_SDA_GPIO,
        .scl_io_num                   = I2C_SCL_GPIO,
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t addr = LCD_I2C_ADDR;
    if (i2c_master_probe(s_bus, LCD_I2C_ADDR, 50) != ESP_OK) {
        if (i2c_master_probe(s_bus, LCD_I2C_ADDR_ALT, 50) != ESP_OK) {
            ESP_LOGE(TAG, "LCD not found at 0x%02X or 0x%02X", LCD_I2C_ADDR, LCD_I2C_ADDR_ALT);
            return ESP_ERR_NOT_FOUND;
        }
        addr = LCD_I2C_ADDR_ALT;
    }
    ESP_LOGI(TAG, "LCD 16x2 detected at I2C 0x%02X", addr);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = LCD_I2C_FREQ_HZ,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        return err;
    }

    /* HD44780 4-bit initialization sequence */
    vTaskDelay(pdMS_TO_TICKS(50));
    lcd_expander_write(0);
    vTaskDelay(pdMS_TO_TICKS(200));

    lcd_write4bits(0x30);
    esp_rom_delay_us(4500);
    lcd_write4bits(0x30);
    esp_rom_delay_us(4500);
    lcd_write4bits(0x30);
    esp_rom_delay_us(150);
    lcd_write4bits(0x20);           /* Enter 4-bit mode */

    lcd_cmd(0x28);                  /* 2 lines, 5x8 font */
    lcd_cmd(0x0C);                  /* Display ON, cursor OFF */
    lcd_clear();
    lcd_cmd(0x06);                  /* Entry mode: increment */

    return ESP_OK;
}

void lcd_clear(void)
{
    lcd_cmd(0x01);
    esp_rom_delay_us(2000);
}

void lcd_set_backlight(bool on)
{
    s_bl_mask = on ? LCD_BL : 0x00;
    lcd_expander_write(0);
}

void lcd_print_line(uint8_t row, const char *text)
{
    if (!text) text = "";
    int i = 0;

    lcd_cmd(0x80 | (row ? 0x40 : 0x00));

    for (; i < 16 && text[i]; i++) {
        lcd_send((uint8_t)text[i], LCD_RS);
    }
    for (; i < 16; i++) {
        lcd_send(' ', LCD_RS);
    }
}
