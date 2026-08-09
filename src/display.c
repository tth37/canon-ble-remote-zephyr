#include "display.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "display_config.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "soc/clk_tree_defs.h"

#define SSD1306_ADDRESS_PRIMARY 0x3C
#define SSD1306_ADDRESS_SECONDARY 0x3D
#define SSD1306_CONTROL_COMMAND 0x00
#define SSD1306_CONTROL_DATA 0x40
#define DISPLAY_BUFFER_SIZE (DISPLAY_WIDTH * DISPLAY_HEIGHT / 8)
#define PANEL_COUNT 2
#define PANEL_DONE_LEFT BIT0
#define PANEL_DONE_RIGHT BIT1
#define PANEL_DONE_ALL (PANEL_DONE_LEFT | PANEL_DONE_RIGHT)

typedef struct {
    const char *name;
    int index;
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t device;
} panel_t;

typedef struct {
    char character;
    uint8_t columns[5];
} glyph_t;

static const char *TAG = "display";
static panel_t panels[PANEL_COUNT] = {
    {.name = "left", .index = 0},
    {.name = "right", .index = 1},
};
static uint8_t framebuffer[DISPLAY_BUFFER_SIZE];
static EventGroupHandle_t present_events;
static TaskHandle_t present_tasks[PANEL_COUNT];
static esp_err_t present_results[PANEL_COUNT];

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

void display_draw_centered_text_in_region(int region_x, int region_width, int y,
                                          const char *text, int scale)
{
    const int width = (int)strlen(text) * 6 * scale - scale;
    display_draw_text(region_x + (region_width - width) / 2, y, text, scale);
}

static esp_err_t send_commands(panel_t *panel, const uint8_t *commands,
                               size_t command_count)
{
    uint8_t packet[32] = {SSD1306_CONTROL_COMMAND};
    if (command_count > sizeof(packet) - 1U) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(&packet[1], commands, command_count);
    return i2c_master_transmit(panel->device, packet, command_count + 1U, 100);
}

static esp_err_t present_panel(panel_t *panel)
{
    static const uint8_t address_window[] = {0x21, 0x00, 0x7F, 0x22, 0x00, 0x07};
    ESP_RETURN_ON_ERROR(send_commands(panel, address_window, sizeof(address_window)), TAG,
                        "Could not set %s OLED address window", panel->name);

    uint8_t packet[DISPLAY_PANEL_WIDTH + 1] = {SSD1306_CONTROL_DATA};
    for (int page = 0; page < DISPLAY_HEIGHT / 8; ++page) {
        const size_t offset = (size_t)page * DISPLAY_WIDTH +
                              (size_t)panel->index * DISPLAY_PANEL_WIDTH;
        memcpy(&packet[1], &framebuffer[offset], DISPLAY_PANEL_WIDTH);
        ESP_RETURN_ON_ERROR(
            i2c_master_transmit(panel->device, packet, sizeof(packet), 100), TAG,
            "Could not write %s OLED pixels", panel->name);
    }
    return ESP_OK;
}

static void present_worker(void *argument)
{
    panel_t *panel = argument;
    const EventBits_t done_bit = panel->index == 0 ? PANEL_DONE_LEFT : PANEL_DONE_RIGHT;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        present_results[panel->index] = present_panel(panel);
        xEventGroupSetBits(present_events, done_bit);
    }
}

esp_err_t display_present(void)
{
    xEventGroupClearBits(present_events, PANEL_DONE_ALL);
    xTaskNotifyGive(present_tasks[0]);
    xTaskNotifyGive(present_tasks[1]);
    xEventGroupWaitBits(present_events, PANEL_DONE_ALL, pdTRUE, pdTRUE, portMAX_DELAY);
    ESP_RETURN_ON_ERROR(present_results[0], TAG, "Left OLED refresh failed");
    ESP_RETURN_ON_ERROR(present_results[1], TAG, "Right OLED refresh failed");
    return ESP_OK;
}

static esp_err_t initialize_ssd1306(panel_t *panel, uint8_t address)
{
    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = DISPLAY_I2C_FREQUENCY_HZ,
    };
    ESP_RETURN_ON_ERROR(
        i2c_master_bus_add_device(panel->bus, &device_config, &panel->device), TAG,
        "Could not register %s OLED at 0x%02X", panel->name, address);

    static const uint8_t init_commands[] = {
        0xAE,       0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6,
        0xAF,
    };
    return send_commands(panel, init_commands, sizeof(init_commands));
}

static bool find_display(panel_t *panel, uint8_t *display_address)
{
    static const uint8_t possible_addresses[] = {
        SSD1306_ADDRESS_PRIMARY,
        SSD1306_ADDRESS_SECONDARY,
    };
    for (size_t index = 0;
         index < sizeof(possible_addresses) / sizeof(possible_addresses[0]); ++index) {
        const uint8_t address = possible_addresses[index];
        if (i2c_master_probe(panel->bus, address, 20) != ESP_OK) {
            continue;
        }
        ESP_LOGI(TAG, "Found %s OLED at 0x%02X", panel->name, address);
        *display_address = address;
        return true;
    }
    ESP_LOGW(TAG, "No %s OLED found at 0x3C/0x3D", panel->name);
    return false;
}

static esp_err_t initialize_bus(panel_t *panel, bool low_power)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = low_power ? LP_I2C_NUM_0 : I2C_NUM_0,
        .sda_io_num = low_power ? DISPLAY_LEFT_SDA_GPIO : DISPLAY_RIGHT_SDA_GPIO,
        .scl_io_num = low_power ? DISPLAY_LEFT_SCL_GPIO : DISPLAY_RIGHT_SCL_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (low_power) {
        bus_config.lp_source_clk = LP_I2C_SCLK_DEFAULT;
    } else {
        bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    }
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &panel->bus), TAG,
                        "Could not initialize %s I2C bus", panel->name);

    uint8_t address = 0;
    while (!find_display(panel, &address)) {
        ESP_LOGI(TAG, "Waiting for %s display; retrying in 2 seconds", panel->name);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    return initialize_ssd1306(panel, address);
}

esp_err_t display_initialize(void)
{
    ESP_LOGI(TAG, "Left OLED: SDA=GPIO%d SCL=GPIO%d (LP I2C)",
             DISPLAY_LEFT_SDA_GPIO, DISPLAY_LEFT_SCL_GPIO);
    ESP_LOGI(TAG, "Right OLED: SDA=GPIO%d SCL=GPIO%d (HP I2C)",
             DISPLAY_RIGHT_SDA_GPIO, DISPLAY_RIGHT_SCL_GPIO);
    ESP_RETURN_ON_ERROR(initialize_bus(&panels[0], true), TAG,
                        "Left display initialization failed");
    ESP_RETURN_ON_ERROR(initialize_bus(&panels[1], false), TAG,
                        "Right display initialization failed");

    present_events = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(present_events != NULL, ESP_ERR_NO_MEM, TAG,
                        "Could not create display event group");
    for (int index = 0; index < PANEL_COUNT; ++index) {
        const BaseType_t created =
            xTaskCreate(present_worker, panels[index].name, 3072, &panels[index], 5,
                        &present_tasks[index]);
        ESP_RETURN_ON_FALSE(created == pdPASS, ESP_ERR_NO_MEM, TAG,
                            "Could not create %s refresh task", panels[index].name);
    }
    display_clear();
    return display_present();
}
