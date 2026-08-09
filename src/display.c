#include "display.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "display_config.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SSD1306_ADDRESS_PRIMARY 0x3C
#define SSD1306_ADDRESS_SECONDARY 0x3D
#define SSD1306_CONTROL_COMMAND 0x00
#define SSD1306_CONTROL_DATA 0x40
#define DISPLAY_BUFFER_SIZE (DISPLAY_WIDTH * DISPLAY_HEIGHT / 8)

typedef struct {
    char character;
    uint8_t columns[5];
} glyph_t;

static const char *TAG = "display";
static i2c_master_bus_handle_t i2c_bus;
static i2c_master_dev_handle_t display_device;
static uint8_t framebuffer[DISPLAY_BUFFER_SIZE];

static const glyph_t glyphs[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x02, 0x04, 0x08, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x26, 0x49, 0x49, 0x49, 0x32}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
    {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x03, 0x04, 0x78, 0x04, 0x03}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
};

static const uint8_t *find_glyph(char character)
{
    for (size_t i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); ++i) {
        if (glyphs[i].character == character) {
            return glyphs[i].columns;
        }
    }
    return glyphs[0].columns;
}

void display_clear(void)
{
    memset(framebuffer, 0, sizeof(framebuffer));
}

uint8_t *display_get_framebuffer(void)
{
    return framebuffer;
}

void display_draw_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) {
        return;
    }

    const size_t index = (size_t)x + ((size_t)y / 8U) * DISPLAY_WIDTH;
    const uint8_t mask = (uint8_t)(1U << (y & 7));
    if (on) {
        framebuffer[index] |= mask;
    } else {
        framebuffer[index] &= (uint8_t)~mask;
    }
}

void display_fill_rectangle(int x, int y, int width, int height)
{
    for (int py = y; py < y + height; ++py) {
        for (int px = x; px < x + width; ++px) {
            display_draw_pixel(px, py, true);
        }
    }
}

void display_draw_rectangle(int x, int y, int width, int height)
{
    for (int px = x; px < x + width; ++px) {
        display_draw_pixel(px, y, true);
        display_draw_pixel(px, y + height - 1, true);
    }
    for (int py = y; py < y + height; ++py) {
        display_draw_pixel(x, py, true);
        display_draw_pixel(x + width - 1, py, true);
    }
}

static void draw_character(int x, int y, char character, int scale)
{
    const uint8_t *columns = find_glyph(character);
    for (int column = 0; column < 5; ++column) {
        for (int row = 0; row < 7; ++row) {
            if ((columns[column] & (1U << row)) != 0) {
                display_fill_rectangle(x + column * scale, y + row * scale, scale, scale);
            }
        }
    }
}

void display_draw_text(int x, int y, const char *text, int scale)
{
    while (*text != '\0') {
        draw_character(x, y, *text++, scale);
        x += 6 * scale;
    }
}

void display_draw_centered_text(int y, const char *text, int scale)
{
    const int width = (int)strlen(text) * 6 * scale - scale;
    display_draw_text((DISPLAY_WIDTH - width) / 2, y, text, scale);
}

static esp_err_t send_commands(const uint8_t *commands, size_t command_count)
{
    uint8_t packet[32] = {SSD1306_CONTROL_COMMAND};
    if (command_count > sizeof(packet) - 1U) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(&packet[1], commands, command_count);
    return i2c_master_transmit(display_device, packet, command_count + 1U, 100);
}

esp_err_t display_present(void)
{
    static const uint8_t address_window[] = {0x21, 0x00, 0x7F, 0x22, 0x00, 0x07};
    ESP_RETURN_ON_ERROR(send_commands(address_window, sizeof(address_window)), TAG,
                        "Could not set OLED address window");

    uint8_t packet[17] = {SSD1306_CONTROL_DATA};
    for (size_t offset = 0; offset < sizeof(framebuffer); offset += 16U) {
        memcpy(&packet[1], &framebuffer[offset], 16U);
        ESP_RETURN_ON_ERROR(i2c_master_transmit(display_device, packet, sizeof(packet), 100), TAG,
                            "Could not write OLED pixels");
    }
    return ESP_OK;
}

static esp_err_t initialize_ssd1306(uint8_t address)
{
    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = DISPLAY_I2C_FREQUENCY_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(i2c_bus, &device_config, &display_device), TAG,
                        "Could not register OLED at 0x%02X", address);

    static const uint8_t init_commands[] = {
        0xAE,       0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6,
        0xAF,
    };
    return send_commands(init_commands, sizeof(init_commands));
}

static bool find_display(uint8_t *display_address)
{
    bool found_any_device = false;
    ESP_LOGI(TAG, "Scanning I2C on SDA=GPIO%d, SCL=GPIO%d", DISPLAY_SDA_GPIO,
             DISPLAY_SCL_GPIO);
    for (uint8_t address = 1; address < 0x7F; ++address) {
        if (i2c_master_probe(i2c_bus, address, 20) != ESP_OK) {
            continue;
        }
        found_any_device = true;
        ESP_LOGI(TAG, "Found I2C device at 0x%02X", address);
        if (address == SSD1306_ADDRESS_PRIMARY || address == SSD1306_ADDRESS_SECONDARY) {
            *display_address = address;
            return true;
        }
    }
    if (found_any_device) {
        ESP_LOGW(TAG, "No SSD1306 address (0x3C/0x3D) responded");
    } else {
        ESP_LOGW(TAG, "No I2C devices found; check display wiring");
    }
    return false;
}

esp_err_t display_initialize(void)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = DISPLAY_SDA_GPIO,
        .scl_io_num = DISPLAY_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &i2c_bus), TAG,
                        "Could not initialize I2C");

    uint8_t address = 0;
    while (!find_display(&address)) {
        ESP_LOGI(TAG, "Waiting for display; retrying in 2 seconds");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    ESP_LOGI(TAG, "Initializing SSD1306 128x64 at 0x%02X", address);
    return initialize_ssd1306(address);
}
