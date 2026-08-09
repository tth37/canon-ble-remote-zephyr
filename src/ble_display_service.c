#include "ble_display_service.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_display";
static uint8_t own_address_type;
static char current_text[BLE_DISPLAY_MAX_TEXT_LENGTH + 1] = "READY";
static ble_display_event_callback_t event_callback;
static void *event_callback_context;

// UUIDs are stored least-significant byte first by BLE_UUID128_INIT.
static const ble_uuid128_t display_service_uuid =
    BLE_UUID128_INIT(0x40, 0x3c, 0x2e, 0x1d, 0x7f, 0x4a, 0x2d, 0x9c,
                     0x7a, 0x4b, 0x8f, 0x6d, 0x01, 0x00, 0x1e, 0x7b);
static const ble_uuid128_t display_text_uuid =
    BLE_UUID128_INIT(0x40, 0x3c, 0x2e, 0x1d, 0x7f, 0x4a, 0x2d, 0x9c,
                     0x7a, 0x4b, 0x8f, 0x6d, 0x02, 0x00, 0x1e, 0x7b);

static void emit_event(ble_display_event_type_t type, const char *text)
{
    if (event_callback == NULL) {
        return;
    }
    ble_display_event_t event = {.type = type};
    if (text != NULL) {
        (void)strncpy(event.text, text, BLE_DISPLAY_MAX_TEXT_LENGTH);
        event.text[BLE_DISPLAY_MAX_TEXT_LENGTH] = '\0';
    }
    event_callback(&event, event_callback_context);
}

static int text_characteristic_access(uint16_t connection_handle, uint16_t attribute_handle,
                                      struct ble_gatt_access_ctxt *context, void *argument)
{
    (void)connection_handle;
    (void)attribute_handle;
    (void)argument;

    if (context->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const int result = os_mbuf_append(context->om, current_text, strlen(current_text));
        return result == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    const uint16_t incoming_length = OS_MBUF_PKTLEN(context->om);
    if (incoming_length == 0 || incoming_length > BLE_DISPLAY_MAX_TEXT_LENGTH) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    uint16_t copied_length = 0;
    const int result = ble_hs_mbuf_to_flat(context->om, current_text,
                                           BLE_DISPLAY_MAX_TEXT_LENGTH, &copied_length);
    if (result != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    current_text[copied_length] = '\0';
    ESP_LOGI(TAG, "Received %u bytes: %s", (unsigned)copied_length, current_text);
    emit_event(BLE_DISPLAY_EVENT_TEXT_RECEIVED, current_text);
    return 0;
}

static const struct ble_gatt_svc_def display_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &display_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &display_text_uuid.u,
                .access_cb = text_characteristic_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
            },
            {0},
        },
    },
    {0},
};

static int start_advertising(void);

static int gap_event(struct ble_gap_event *event, void *argument)
{
    (void)argument;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                ESP_LOGI(TAG, "PC connected; handle=%u", event->connect.conn_handle);
                emit_event(BLE_DISPLAY_EVENT_CONNECTED, NULL);
            } else {
                ESP_LOGW(TAG, "Connection failed; status=%d", event->connect.status);
                (void)start_advertising();
            }
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Disconnected; reason=%d", event->disconnect.reason);
            emit_event(BLE_DISPLAY_EVENT_DISCONNECTED, NULL);
            (void)start_advertising();
            return 0;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            (void)start_advertising();
            return 0;

        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "MTU updated to %u", event->mtu.value);
            return 0;

        default:
            return 0;
    }
}

static int start_advertising(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&display_service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    int result = ble_gap_adv_set_fields(&fields);
    if (result != 0) {
        ESP_LOGE(TAG, "Could not set advertising fields; rc=%d", result);
        return result;
    }

    // The flags, 128-bit UUID, and complete name do not all fit in the
    // 31-byte legacy advertisement. Put the name in the scan response.
    struct ble_hs_adv_fields scan_response = {0};
    scan_response.name = (uint8_t *)BLE_DISPLAY_DEVICE_NAME;
    scan_response.name_len = strlen(BLE_DISPLAY_DEVICE_NAME);
    scan_response.name_is_complete = 1;
    result = ble_gap_adv_rsp_set_fields(&scan_response);
    if (result != 0) {
        ESP_LOGE(TAG, "Could not set scan-response fields; rc=%d", result);
        return result;
    }

    struct ble_gap_adv_params parameters = {0};
    parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
    parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
    result = ble_gap_adv_start(own_address_type, NULL, BLE_HS_FOREVER,
                               &parameters, gap_event, NULL);
    if (result == 0) {
        ESP_LOGI(TAG, "Advertising as '%s'", BLE_DISPLAY_DEVICE_NAME);
        emit_event(BLE_DISPLAY_EVENT_ADVERTISING, NULL);
    } else {
        ESP_LOGE(TAG, "Could not start advertising; rc=%d", result);
    }
    return result;
}

static void host_on_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE host reset; reason=%d", reason);
}

static void host_on_sync(void)
{
    int result = ble_hs_util_ensure_addr(0);
    assert(result == 0);
    result = ble_hs_id_infer_auto(0, &own_address_type);
    assert(result == 0);
    (void)start_advertising();
}

static void host_task(void *parameter)
{
    (void)parameter;
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_display_service_initialize(ble_display_event_callback_t callback, void *context)
{
    event_callback = callback;
    event_callback_context = context;

    esp_err_t error = nimble_port_init();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Could not initialize NimBLE: %s", esp_err_to_name(error));
        return error;
    }

    ble_hs_cfg.reset_cb = host_on_reset;
    ble_hs_cfg.sync_cb = host_on_sync;
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int result = ble_gatts_count_cfg(display_services);
    if (result == 0) {
        result = ble_gatts_add_svcs(display_services);
    }
    if (result == 0) {
        result = ble_svc_gap_device_name_set(BLE_DISPLAY_DEVICE_NAME);
    }
    if (result != 0) {
        ESP_LOGE(TAG, "Could not configure GATT service; rc=%d", result);
        return ESP_FAIL;
    }

    nimble_port_freertos_init(host_task);
    return ESP_OK;
}
