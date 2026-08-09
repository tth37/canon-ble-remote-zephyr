#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "ble_display_service.h"
#include "display.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define DISPLAY_EVENT_QUEUE_LENGTH 8
#define TEXT_COLUMNS 21
#define TEXT_LINES 6

static const char *TAG = "codex_display";
static QueueHandle_t display_event_queue;

static void render_status(const char *line_one, const char *line_two)
{
    display_clear();
    display_draw_rectangle(0, 0, 128, 64);
    display_draw_centered_text(8, "BLE DISPLAY", 1);
    display_fill_rectangle(7, 20, 114, 1);
    display_draw_centered_text(29, line_one, 1);
    if (line_two != NULL) {
        display_draw_centered_text(44, line_two, 1);
    }
    ESP_ERROR_CHECK(display_present());
}

static char display_character(char character)
{
    const unsigned char value = (unsigned char)character;
    if (character == '\n' || character == '\r') {
        return character;
    }
    if (isalnum(value)) {
        return (char)toupper(value);
    }
    return character == '-' ? '-' : ' ';
}

static void render_text_message(const char *text)
{
    display_clear();
    display_draw_text(1, 0, "PC MESSAGE", 1);
    display_fill_rectangle(0, 9, 128, 1);

    int column = 0;
    int line = 0;
    for (size_t index = 0; text[index] != '\0' && line < TEXT_LINES; ++index) {
        const char character = display_character(text[index]);
        if (character == '\n' || character == '\r') {
            if (column > 0) {
                ++line;
                column = 0;
            }
            continue;
        }
        if (column >= TEXT_COLUMNS) {
            ++line;
            column = 0;
            if (line >= TEXT_LINES) {
                break;
            }
        }
        char single_character[2] = {character, '\0'};
        display_draw_text(column * 6, 12 + line * 8, single_character, 1);
        ++column;
    }
    ESP_ERROR_CHECK(display_present());
}

static void bluetooth_event_callback(const ble_display_event_t *event, void *context)
{
    QueueHandle_t queue = (QueueHandle_t)context;
    if (xQueueSend(queue, event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Display event queue full; dropping event %d", event->type);
    }
}

static void initialize_nvs(void)
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        error = nvs_flash_init();
    }
    ESP_ERROR_CHECK(error);
}

void app_main(void)
{
    ESP_ERROR_CHECK(display_initialize());
    render_status("STARTING BLE", NULL);
    initialize_nvs();

    display_event_queue = xQueueCreate(DISPLAY_EVENT_QUEUE_LENGTH, sizeof(ble_display_event_t));
    if (display_event_queue == NULL) {
        ESP_LOGE(TAG, "Could not create display event queue");
        abort();
    }
    ESP_ERROR_CHECK(ble_display_service_initialize(bluetooth_event_callback,
                                                   display_event_queue));

    ble_display_event_t event;
    bool has_received_text = false;
    while (true) {
        if (xQueueReceive(display_event_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        switch (event.type) {
            case BLE_DISPLAY_EVENT_ADVERTISING:
                if (!has_received_text) {
                    render_status("ADVERTISING", "CODEX DISPLAY");
                }
                break;
            case BLE_DISPLAY_EVENT_CONNECTED:
                render_status("PC CONNECTED", "WAITING FOR TEXT");
                break;
            case BLE_DISPLAY_EVENT_DISCONNECTED:
                if (!has_received_text) {
                    render_status("PC DISCONNECTED", "ADVERTISING");
                }
                break;
            case BLE_DISPLAY_EVENT_TEXT_RECEIVED:
                has_received_text = true;
                render_text_message(event.text);
                break;
        }
    }
}
