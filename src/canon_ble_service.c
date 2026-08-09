#include "canon_ble_service.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs.h"
#include "services/gap/ble_svc_gap.h"

#define EVENT_HOST_SYNCED BIT0
#define EVENT_SCAN_FOUND BIT1
#define EVENT_SCAN_DONE BIT2
#define EVENT_CONNECTED BIT3
#define EVENT_DISCOVERY_DONE BIT4
#define EVENT_SECURITY_DONE BIT5
#define EVENT_DISCONNECTED BIT6

#define HOST_SYNC_TIMEOUT_MS 5000U
#define CONNECT_TIMEOUT_MS 15000U
#define SECURITY_TIMEOUT_MS 15000U
#define GATT_TIMEOUT_MS 10000U
#define DISCONNECT_TIMEOUT_MS 5000U

#define CANON_NVS_NAMESPACE "canon_remote"
#define CANON_NVS_ADDRESS_KEY "camera_addr"
#define CANON_REMOTE_NAME "ESP32 Remote"

#define CANON_BUTTON_RELEASE 0x80U
#define CANON_BUTTON_FOCUS 0x40U
#define CANON_MODE_IMMEDIATE 0x0cU

static const char *TAG = "canon_ble";

static const ble_uuid128_t CANON_SERVICE_UUID =
    BLE_UUID128_INIT(0x21, 0xa8, 0xff, 0x2f, 0x49, 0xd8, 0x00, 0x00,
                     0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00);
static const ble_uuid128_t CANON_PAIRING_UUID =
    BLE_UUID128_INIT(0x21, 0xa8, 0xff, 0x2f, 0x49, 0xd8, 0x00, 0x00,
                     0x00, 0x10, 0x00, 0x00, 0x02, 0x00, 0x05, 0x00);
static const ble_uuid128_t CANON_TRIGGER_UUID =
    BLE_UUID128_INIT(0x21, 0xa8, 0xff, 0x2f, 0x49, 0xd8, 0x00, 0x00,
                     0x00, 0x10, 0x00, 0x00, 0x03, 0x00, 0x05, 0x00);

typedef struct {
    EventGroupHandle_t events;
    SemaphoreHandle_t operation_mutex;
    bool initialized;
    volatile bool host_synced;
    volatile bool paired;
    volatile bool connected;
    volatile bool encrypted;
    volatile bool ready;
    volatile bool scanning;
    volatile bool disconnect_requested;
    uint8_t own_address_type;
    ble_addr_t camera_address;
    ble_addr_t scan_address;
    uint16_t connection_handle;
    uint16_t service_start_handle;
    uint16_t service_end_handle;
    uint16_t discovered_value_handle;
    volatile int operation_result;
    volatile int last_ble_error;
} canon_ble_context_t;

static canon_ble_context_t context = {
    .connection_handle = BLE_HS_CONN_HANDLE_NONE,
};

/* ESP-IDF's NimBLE examples expose this store initializer this way. */
void ble_store_config_init(void);

static int gap_event_handler(struct ble_gap_event *event, void *argument);

static void format_address(const ble_addr_t *address, char output[18])
{
    snprintf(output, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             address->val[5], address->val[4], address->val[3],
             address->val[2], address->val[1], address->val[0]);
}

static esp_err_t result_from_ble_error(int error)
{
    context.last_ble_error = error;
    if (error == 0) {
        return ESP_OK;
    }
    if (error == BLE_HS_ENOENT) {
        return ESP_ERR_NOT_FOUND;
    }
    if (error == BLE_HS_ETIMEOUT || error == BLE_HS_ETIMEOUT_HCI) {
        return ESP_ERR_TIMEOUT;
    }
    if (error == BLE_HS_EBUSY || error == BLE_HS_EALREADY) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_FAIL;
}

static esp_err_t wait_for_event(EventBits_t event_bit, uint32_t timeout_ms)
{
    const EventBits_t bits = xEventGroupWaitBits(
        context.events, event_bit, pdTRUE, pdFALSE,
        pdMS_TO_TICKS(timeout_ms));
    if ((bits & event_bit) == 0) {
        context.last_ble_error = BLE_HS_ETIMEOUT;
        return ESP_ERR_TIMEOUT;
    }
    return result_from_ble_error(context.operation_result);
}

static esp_err_t load_camera_address(void)
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open(CANON_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (result != ESP_OK) {
        return result;
    }

    size_t address_size = sizeof(context.camera_address);
    result = nvs_get_blob(handle, CANON_NVS_ADDRESS_KEY,
                          &context.camera_address, &address_size);
    nvs_close(handle);
    if (result == ESP_OK && address_size == sizeof(context.camera_address)) {
        context.paired = true;
        return ESP_OK;
    }
    return result == ESP_OK ? ESP_ERR_INVALID_SIZE : result;
}

static esp_err_t save_camera_address(void)
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open(CANON_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    result = nvs_set_blob(handle, CANON_NVS_ADDRESS_KEY,
                          &context.camera_address,
                          sizeof(context.camera_address));
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    if (result == ESP_OK) {
        context.paired = true;
    }
    return result;
}

static esp_err_t erase_camera_address(void)
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open(CANON_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }
    result = nvs_erase_key(handle, CANON_NVS_ADDRESS_KEY);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        result = ESP_OK;
    }
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}

static bool advertisement_has_canon_service(
    const struct ble_gap_disc_desc *discovery)
{
    struct ble_hs_adv_fields fields;
    const int result = ble_hs_adv_parse_fields(
        &fields, discovery->data, discovery->length_data);
    if (result != 0) {
        return false;
    }

    for (uint8_t index = 0; index < fields.num_uuids128; ++index) {
        if (ble_uuid_cmp(&fields.uuids128[index].u,
                         &CANON_SERVICE_UUID.u) == 0) {
            return true;
        }
    }
    return false;
}

static int service_discovery_callback(
    uint16_t connection_handle, const struct ble_gatt_error *error,
    const struct ble_gatt_svc *service, void *argument)
{
    (void)connection_handle;
    (void)argument;
    if (error->status == 0 && service != NULL) {
        context.service_start_handle = service->start_handle;
        context.service_end_handle = service->end_handle;
        return 0;
    }

    context.operation_result =
        error->status == BLE_HS_EDONE && context.service_start_handle != 0
            ? 0
            : error->status;
    if (error->status == BLE_HS_EDONE &&
        context.service_start_handle == 0) {
        context.operation_result = BLE_HS_ENOENT;
    }
    xEventGroupSetBits(context.events, EVENT_DISCOVERY_DONE);
    return 0;
}

static int characteristic_discovery_callback(
    uint16_t connection_handle, const struct ble_gatt_error *error,
    const struct ble_gatt_chr *characteristic, void *argument)
{
    (void)connection_handle;
    (void)argument;
    if (error->status == 0 && characteristic != NULL) {
        context.discovered_value_handle = characteristic->val_handle;
        return 0;
    }

    context.operation_result =
        error->status == BLE_HS_EDONE &&
                context.discovered_value_handle != 0
            ? 0
            : error->status;
    if (error->status == BLE_HS_EDONE &&
        context.discovered_value_handle == 0) {
        context.operation_result = BLE_HS_ENOENT;
    }
    xEventGroupSetBits(context.events, EVENT_DISCOVERY_DONE);
    return 0;
}

static esp_err_t discover_service(void)
{
    context.service_start_handle = 0;
    context.service_end_handle = 0;
    context.operation_result = 0;
    xEventGroupClearBits(context.events, EVENT_DISCOVERY_DONE);
    const int result = ble_gattc_disc_svc_by_uuid(
        context.connection_handle, &CANON_SERVICE_UUID.u,
        service_discovery_callback, NULL);
    if (result != 0) {
        return result_from_ble_error(result);
    }
    return wait_for_event(EVENT_DISCOVERY_DONE, GATT_TIMEOUT_MS);
}

static esp_err_t discover_characteristic(const ble_uuid_t *uuid,
                                         uint16_t *value_handle)
{
    context.discovered_value_handle = 0;
    context.operation_result = 0;
    xEventGroupClearBits(context.events, EVENT_DISCOVERY_DONE);
    const int result = ble_gattc_disc_chrs_by_uuid(
        context.connection_handle, context.service_start_handle,
        context.service_end_handle, uuid,
        characteristic_discovery_callback, NULL);
    if (result != 0) {
        return result_from_ble_error(result);
    }

    const esp_err_t wait_result =
        wait_for_event(EVENT_DISCOVERY_DONE, GATT_TIMEOUT_MS);
    if (wait_result == ESP_OK) {
        *value_handle = context.discovered_value_handle;
    }
    return wait_result;
}

static esp_err_t disconnect_link(void)
{
    if (!context.connected ||
        context.connection_handle == BLE_HS_CONN_HANDLE_NONE) {
        context.connected = false;
        context.encrypted = false;
        context.ready = false;
        return ESP_OK;
    }

    context.disconnect_requested = true;
    context.operation_result = 0;
    xEventGroupClearBits(context.events, EVENT_DISCONNECTED);
    const int result = ble_gap_terminate(
        context.connection_handle, BLE_ERR_REM_USER_CONN_TERM);
    if (result == BLE_HS_ENOTCONN) {
        context.connected = false;
        context.encrypted = false;
        context.ready = false;
        context.connection_handle = BLE_HS_CONN_HANDLE_NONE;
        context.disconnect_requested = false;
        return ESP_OK;
    }
    if (result != 0) {
        context.disconnect_requested = false;
        return result_from_ble_error(result);
    }
    return wait_for_event(EVENT_DISCONNECTED, DISCONNECT_TIMEOUT_MS);
}

static esp_err_t connect_link(const ble_addr_t *address)
{
    context.operation_result = 0;
    context.ready = false;
    context.encrypted = false;
    xEventGroupClearBits(context.events, EVENT_CONNECTED);
    const int result = ble_gap_connect(
        context.own_address_type, address, CONNECT_TIMEOUT_MS, NULL,
        gap_event_handler, NULL);
    if (result != 0) {
        return result_from_ble_error(result);
    }
    return wait_for_event(EVENT_CONNECTED, CONNECT_TIMEOUT_MS + 1000U);
}

static esp_err_t secure_link(void)
{
    struct ble_gap_conn_desc description;
    if (ble_gap_conn_find(context.connection_handle, &description) == 0 &&
        description.sec_state.encrypted) {
        context.encrypted = true;
        context.last_ble_error = 0;
        return ESP_OK;
    }

    context.operation_result = 0;
    xEventGroupClearBits(context.events, EVENT_SECURITY_DONE);
    const int result = ble_gap_security_initiate(context.connection_handle);
    if (result != 0 && result != BLE_HS_EALREADY) {
        return result_from_ble_error(result);
    }
    return wait_for_event(EVENT_SECURITY_DONE, SECURITY_TIMEOUT_MS);
}

static esp_err_t connect_for_control(void)
{
    if (!context.paired) {
        return ESP_ERR_NOT_FOUND;
    }
    if (context.ready && context.connected && context.encrypted) {
        return ESP_OK;
    }
    if (context.connected) {
        const esp_err_t disconnect_result = disconnect_link();
        if (disconnect_result != ESP_OK) {
            return disconnect_result;
        }
    }

    esp_err_t result = connect_link(&context.camera_address);
    if (result != ESP_OK) {
        return result;
    }
    result = secure_link();
    if (result != ESP_OK) {
        disconnect_link();
        return result;
    }
    result = discover_service();
    if (result != ESP_OK) {
        disconnect_link();
        return result;
    }
    uint16_t trigger_handle = 0;
    result = discover_characteristic(&CANON_TRIGGER_UUID.u, &trigger_handle);
    if (result != ESP_OK) {
        disconnect_link();
        return result;
    }
    context.discovered_value_handle = trigger_handle;
    context.ready = true;
    return ESP_OK;
}

static esp_err_t scan_for_camera(uint32_t scan_seconds)
{
    struct ble_gap_disc_params parameters = {0};
    parameters.filter_duplicates = 1;
    parameters.passive = 0;
    parameters.itvl = 0;
    parameters.window = 0;
    parameters.filter_policy = 0;
    parameters.limited = 0;

    context.operation_result = 0;
    context.scanning = true;
    xEventGroupClearBits(context.events, EVENT_SCAN_FOUND | EVENT_SCAN_DONE);
    const int result = ble_gap_disc(
        context.own_address_type, (int32_t)(scan_seconds * 1000U),
        &parameters, gap_event_handler, NULL);
    if (result != 0) {
        context.scanning = false;
        return result_from_ble_error(result);
    }

    const EventBits_t bits = xEventGroupWaitBits(
        context.events, EVENT_SCAN_FOUND | EVENT_SCAN_DONE, pdTRUE, pdFALSE,
        pdMS_TO_TICKS(scan_seconds * 1000U + 1000U));
    context.scanning = false;
    if ((bits & EVENT_SCAN_FOUND) != 0) {
        context.last_ble_error = 0;
        return ESP_OK;
    }
    if (ble_gap_disc_active()) {
        ble_gap_disc_cancel();
    }
    context.last_ble_error = BLE_HS_ENOENT;
    return (bits & EVENT_SCAN_DONE) != 0 ? ESP_ERR_NOT_FOUND
                                         : ESP_ERR_TIMEOUT;
}

static void host_reset_callback(int reason)
{
    context.host_synced = false;
    context.last_ble_error = reason;
    ESP_LOGE(TAG, "NimBLE host reset; reason=%d", reason);
}

static void host_sync_callback(void)
{
    int result = ble_hs_util_ensure_addr(0);
    if (result == 0) {
        result = ble_hs_id_infer_auto(0, &context.own_address_type);
    }
    context.operation_result = result;
    context.last_ble_error = result;
    context.host_synced = result == 0;
    xEventGroupSetBits(context.events, EVENT_HOST_SYNCED);
}

static void nimble_host_task(void *argument)
{
    (void)argument;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static int handle_passkey_action(struct ble_gap_event *event)
{
    struct ble_sm_io response = {0};
    response.action = event->passkey.params.action;
    if (response.action == BLE_SM_IOACT_NUMCMP) {
        response.numcmp_accept = 1;
    } else if (response.action == BLE_SM_IOACT_DISP ||
               response.action == BLE_SM_IOACT_INPUT) {
        response.passkey = 123456U;
    } else {
        return 0;
    }
    return ble_sm_inject_io(event->passkey.conn_handle, &response);
}

static int gap_event_handler(struct ble_gap_event *event, void *argument)
{
    (void)argument;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        if (context.scanning &&
            advertisement_has_canon_service(&event->disc)) {
            context.scan_address = event->disc.addr;
            context.scanning = false;
            context.operation_result = 0;
            xEventGroupSetBits(context.events, EVENT_SCAN_FOUND);
            ble_gap_disc_cancel();
        }
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (context.scanning) {
            context.scanning = false;
            context.operation_result = BLE_HS_ENOENT;
            xEventGroupSetBits(context.events, EVENT_SCAN_DONE);
        }
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        context.operation_result = event->connect.status;
        if (event->connect.status == 0) {
            context.connection_handle = event->connect.conn_handle;
            context.connected = true;
        } else {
            context.connection_handle = BLE_HS_CONN_HANDLE_NONE;
            context.connected = false;
        }
        xEventGroupSetBits(context.events, EVENT_CONNECTED);
        return 0;

    case BLE_GAP_EVENT_DISCONNECT: {
        const bool requested = context.disconnect_requested;
        context.disconnect_requested = false;
        context.connected = false;
        context.encrypted = false;
        context.ready = false;
        context.connection_handle = BLE_HS_CONN_HANDLE_NONE;
        context.operation_result = requested ? 0 : event->disconnect.reason;
        xEventGroupSetBits(context.events, EVENT_DISCONNECTED |
                                              EVENT_DISCOVERY_DONE |
                                              EVENT_SECURITY_DONE);
        return 0;
    }

    case BLE_GAP_EVENT_ENC_CHANGE: {
        struct ble_gap_conn_desc description;
        const int find_result = ble_gap_conn_find(
            event->enc_change.conn_handle, &description);
        context.encrypted = event->enc_change.status == 0 &&
                            find_result == 0 &&
                            description.sec_state.encrypted;
        context.operation_result = context.encrypted
                                       ? 0
                                       : event->enc_change.status;
        if (!context.encrypted && context.operation_result == 0) {
            context.operation_result = BLE_HS_EAUTHEN;
        }
        xEventGroupSetBits(context.events, EVENT_SECURITY_DONE);
        return 0;
    }

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        return handle_passkey_action(event);

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc description;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle,
                              &description) == 0) {
            ble_store_util_delete_peer(&description.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    default:
        return 0;
    }
}

esp_err_t canon_ble_service_initialize(void)
{
    if (context.initialized) {
        return ESP_OK;
    }

    context.events = xEventGroupCreate();
    context.operation_mutex = xSemaphoreCreateMutex();
    if (context.events == NULL || context.operation_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    const esp_err_t load_result = load_camera_address();
    if (load_result != ESP_OK && load_result != ESP_ERR_NOT_FOUND &&
        load_result != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Could not load saved camera: %s",
                 esp_err_to_name(load_result));
    }

    esp_err_t result = nimble_port_init();
    if (result != ESP_OK) {
        return result;
    }
    ble_hs_cfg.reset_cb = host_reset_callback;
    ble_hs_cfg.sync_cb = host_sync_callback;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC |
                                 BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC |
                                   BLE_SM_PAIR_KEY_DIST_ID;

    const int name_result = ble_svc_gap_device_name_set(CANON_REMOTE_NAME);
    if (name_result != 0) {
        return result_from_ble_error(name_result);
    }
    ble_store_config_init();
    context.initialized = true;
    context.operation_result = 0;
    xEventGroupClearBits(context.events, EVENT_HOST_SYNCED);
    nimble_port_freertos_init(nimble_host_task);
    return wait_for_event(EVENT_HOST_SYNCED, HOST_SYNC_TIMEOUT_MS);
}

esp_err_t canon_ble_service_pair(uint32_t scan_seconds)
{
    if (!context.initialized || !context.host_synced) {
        return ESP_ERR_INVALID_STATE;
    }
    if (scan_seconds == 0 || scan_seconds > 60U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(context.operation_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = disconnect_link();
    if (result == ESP_OK) {
        result = scan_for_camera(scan_seconds);
    }
    if (result == ESP_OK) {
        result = connect_link(&context.scan_address);
    }
    if (result == ESP_OK) {
        result = discover_service();
    }
    uint16_t pairing_handle = 0;
    if (result == ESP_OK) {
        result = discover_characteristic(&CANON_PAIRING_UUID.u,
                                         &pairing_handle);
    }
    if (result == ESP_OK) {
        uint8_t payload[sizeof(CANON_REMOTE_NAME) + 1U] = {0x03U};
        memcpy(&payload[1], CANON_REMOTE_NAME, sizeof(CANON_REMOTE_NAME));
        const int write_result = ble_gattc_write_no_rsp_flat(
            context.connection_handle, pairing_handle, payload,
            sizeof(payload));
        result = result_from_ble_error(write_result);
    }
    if (result == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(250U));
        result = secure_link();
    }
    if (result == ESP_OK) {
        struct ble_gap_conn_desc description;
        const int find_result = ble_gap_conn_find(
            context.connection_handle, &description);
        if (find_result == 0) {
            context.camera_address = description.peer_id_addr;
            result = save_camera_address();
        } else {
            result = result_from_ble_error(find_result);
        }
    }

    if (result == ESP_OK) {
        const esp_err_t disconnect_result = disconnect_link();
        if (disconnect_result == ESP_OK) {
            const esp_err_t reconnect_result = connect_for_control();
            if (reconnect_result != ESP_OK) {
                ESP_LOGW(TAG, "Paired, but reconnect failed: %s (BLE %d)",
                         esp_err_to_name(reconnect_result),
                         context.last_ble_error);
            }
        }
    } else {
        disconnect_link();
    }

    xSemaphoreGive(context.operation_mutex);
    return result;
}

esp_err_t canon_ble_service_connect(void)
{
    if (!context.initialized || !context.host_synced) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(context.operation_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t result = connect_for_control();
    xSemaphoreGive(context.operation_mutex);
    return result;
}

esp_err_t canon_ble_service_disconnect(void)
{
    if (!context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(context.operation_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    const esp_err_t result = disconnect_link();
    xSemaphoreGive(context.operation_mutex);
    return result;
}

esp_err_t canon_ble_service_forget(void)
{
    if (!context.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(context.operation_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = disconnect_link();
    if (result == ESP_OK && context.paired) {
        const int delete_result =
            ble_store_util_delete_peer(&context.camera_address);
        if (delete_result != 0 && delete_result != BLE_HS_ENOENT) {
            result = result_from_ble_error(delete_result);
        }
    }
    if (result == ESP_OK) {
        result = erase_camera_address();
    }
    if (result == ESP_OK) {
        memset(&context.camera_address, 0, sizeof(context.camera_address));
        context.paired = false;
        context.last_ble_error = 0;
    }

    xSemaphoreGive(context.operation_mutex);
    return result;
}

static esp_err_t send_button_command(uint8_t pressed_value)
{
    if (!context.initialized || !context.host_synced) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(context.operation_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = connect_for_control();
    if (result == ESP_OK) {
        const int press_result = ble_gattc_write_no_rsp_flat(
            context.connection_handle, context.discovered_value_handle,
            &pressed_value, sizeof(pressed_value));
        result = result_from_ble_error(press_result);
    }
    if (result == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(200U));
        const uint8_t release_value = CANON_MODE_IMMEDIATE;
        const int release_result = ble_gattc_write_no_rsp_flat(
            context.connection_handle, context.discovered_value_handle,
            &release_value, sizeof(release_value));
        result = result_from_ble_error(release_result);
        vTaskDelay(pdMS_TO_TICKS(50U));
    }

    xSemaphoreGive(context.operation_mutex);
    return result;
}

esp_err_t canon_ble_service_shutter(void)
{
    return send_button_command(CANON_MODE_IMMEDIATE |
                               CANON_BUTTON_RELEASE);
}

esp_err_t canon_ble_service_focus(void)
{
    return send_button_command(CANON_MODE_IMMEDIATE |
                               CANON_BUTTON_FOCUS);
}

void canon_ble_service_get_status(canon_ble_status_t *status)
{
    if (status == NULL) {
        return;
    }
    memset(status, 0, sizeof(*status));
    status->initialized = context.initialized;
    status->host_synced = context.host_synced;
    status->paired = context.paired;
    status->connected = context.connected;
    status->encrypted = context.encrypted;
    status->ready = context.ready;
    status->scanning = context.scanning;
    status->last_ble_error = context.last_ble_error;
    if (context.paired) {
        format_address(&context.camera_address, status->camera_address);
    }
}
