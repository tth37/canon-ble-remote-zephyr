#include "canon_ble_zephyr.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "canon_protocol.h"

LOG_MODULE_REGISTER(canon_ble, LOG_LEVEL_INF);

#define CANON_REMOTE_NAME "ESP32 Remote"
#define CANON_SETTINGS_PEER_KEY "canon/peer"

#define CANON_SCAN_INTERVAL 16U
#define CANON_SCAN_WINDOW 16U
#define CANON_CONNECTION_INTERVAL 24U
#define CANON_CONNECTION_LATENCY 0U
#define CANON_SUPERVISION_TIMEOUT 72U

#define CONNECT_TIMEOUT_SECONDS 16U
#define SECURITY_TIMEOUT_SECONDS 15U
#define GATT_TIMEOUT_SECONDS 10U
#define DISCONNECT_TIMEOUT_SECONDS 5U
#define PAIRING_BOND_SETTLE_MS 1000U
#define PAIRING_RECONNECT_DELAY_MS 1000U
#define BUTTON_HOLD_MS 200U
#define BUTTON_SETTLE_MS 50U

static struct bt_uuid_128 canon_service_uuid =
    BT_UUID_INIT_128(CANON_SERVICE_UUID_LE_BYTES);
static struct bt_uuid_128 canon_pairing_uuid =
    BT_UUID_INIT_128(CANON_PAIRING_UUID_LE_BYTES);
static struct bt_uuid_128 canon_trigger_uuid =
    BT_UUID_INIT_128(CANON_TRIGGER_UUID_LE_BYTES);

static const struct bt_le_scan_param canon_scan_parameters =
    BT_LE_SCAN_PARAM_INIT(BT_LE_SCAN_TYPE_ACTIVE,
                          BT_LE_SCAN_OPT_FILTER_DUPLICATE,
                          CANON_SCAN_INTERVAL, CANON_SCAN_WINDOW);
static const struct bt_conn_le_create_param canon_create_parameters =
    BT_CONN_LE_CREATE_PARAM_INIT(BT_CONN_LE_OPT_NONE,
                                 CANON_SCAN_INTERVAL,
                                 CANON_SCAN_WINDOW);
static const struct bt_le_conn_param canon_connection_parameters =
    BT_LE_CONN_PARAM_INIT(CANON_CONNECTION_INTERVAL,
                          CANON_CONNECTION_INTERVAL,
                          CANON_CONNECTION_LATENCY,
                          CANON_SUPERVISION_TIMEOUT);

typedef struct {
    struct k_mutex operation_mutex;
    struct k_sem scan_sem;
    struct k_sem connected_sem;
    struct k_sem security_sem;
    struct k_sem discovery_sem;
    struct k_sem disconnected_sem;

    atomic_t initialized;
    atomic_t host_ready;
    atomic_t paired;
    atomic_t connected;
    atomic_t encrypted;
    atomic_t ready;
    atomic_t scanning;
    atomic_t scan_found;
    atomic_t busy;
    atomic_t secure_on_connect;
    atomic_t force_pair_on_connect;
    atomic_t disconnect_requested;

    bt_addr_le_t camera_address;
    bt_addr_le_t scan_address;
    struct bt_conn *connection;
    struct bt_gatt_discover_params discovery_parameters;
    uint16_t service_start_handle;
    uint16_t service_end_handle;
    uint16_t discovered_value_handle;
    int operation_result;
    int last_ble_error;
} canon_ble_context_t;

static canon_ble_context_t context;

static canon_zephyr_result_t result_from_error(int error)
{
    if (error == 0) {
        context.last_ble_error = 0;
        return CANON_ZEPHYR_OK;
    }
    if (context.last_ble_error == 0) {
        context.last_ble_error = error;
    }
    if (error == -EBUSY || error == -EALREADY) {
        return CANON_ZEPHYR_BUSY;
    }
    if (error == -ENOENT) {
        return CANON_ZEPHYR_NOT_FOUND;
    }
    if (error == -ETIMEDOUT) {
        return CANON_ZEPHYR_TIMEOUT;
    }
    if (error == -EINVAL) {
        return CANON_ZEPHYR_INVALID_ARGUMENT;
    }
    return CANON_ZEPHYR_STACK_ERROR;
}

static canon_zephyr_result_t wait_for_operation(struct k_sem *semaphore,
                                                k_timeout_t timeout)
{
    if (k_sem_take(semaphore, timeout) != 0) {
        return result_from_error(-ETIMEDOUT);
    }
    return result_from_error(context.operation_result);
}

static int load_peer_setting(const char *key, size_t length,
                             settings_read_cb read_callback,
                             void *callback_argument)
{
    if (strcmp(key, "peer") != 0 || length != CANON_PEER_RECORD_SIZE) {
        return 0;
    }

    uint8_t record[CANON_PEER_RECORD_SIZE];
    const ssize_t read_length = read_callback(
        callback_argument, record, sizeof(record));
    if (read_length != (ssize_t)sizeof(record)) {
        return read_length < 0 ? (int)read_length : -EINVAL;
    }

    uint8_t address_type = 0U;
    uint8_t address[CANON_PEER_ADDRESS_SIZE];
    if (canon_peer_record_decode(record, &address_type, address) &&
        (address_type == BT_ADDR_LE_PUBLIC ||
         address_type == BT_ADDR_LE_RANDOM)) {
        context.camera_address.type = address_type;
        memcpy(context.camera_address.a.val, address, sizeof(address));
        atomic_set(&context.paired, 1);
    } else {
        LOG_WRN("Ignoring an invalid saved Canon peer record");
    }
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(canon_peer, "canon", NULL,
                               load_peer_setting, NULL, NULL);

static uint8_t portable_address_type(uint8_t address_type)
{
    if (address_type == BT_ADDR_LE_PUBLIC_ID) {
        return BT_ADDR_LE_PUBLIC;
    }
    if (address_type == BT_ADDR_LE_RANDOM_ID) {
        return BT_ADDR_LE_RANDOM;
    }
    return address_type;
}

static int save_camera_address(const bt_addr_le_t *address)
{
    const uint8_t address_type = portable_address_type(address->type);
    uint8_t record[CANON_PEER_RECORD_SIZE];
    if (!canon_peer_record_encode(address_type, address->a.val, record)) {
        return -EINVAL;
    }

    const int result = settings_save_one(CANON_SETTINGS_PEER_KEY,
                                         record, sizeof(record));
    if (result == 0) {
        context.camera_address = *address;
        context.camera_address.type = address_type;
        atomic_set(&context.paired, 1);
    }
    return result;
}

static bool advertisement_field_has_canon_uuid(struct bt_data *data,
                                                void *user_data)
{
    bool *found = user_data;
    if (data->type != BT_DATA_UUID128_ALL &&
        data->type != BT_DATA_UUID128_SOME) {
        return true;
    }

    for (size_t offset = 0U;
         offset + CANON_UUID_SIZE <= data->data_len;
         offset += CANON_UUID_SIZE) {
        if (memcmp(&data->data[offset], canon_service_uuid.val,
                   CANON_UUID_SIZE) == 0) {
            *found = true;
            return false;
        }
    }
    return true;
}

static void scan_received(const bt_addr_le_t *address, int8_t rssi,
                          uint8_t advertisement_type,
                          struct net_buf_simple *advertisement)
{
    (void)rssi;
    (void)advertisement_type;
    if (!atomic_get(&context.scanning) ||
        atomic_get(&context.scan_found)) {
        return;
    }

    bool found = false;
    struct net_buf_simple copy = *advertisement;
    bt_data_parse(&copy, advertisement_field_has_canon_uuid, &found);
    if (found && atomic_cas(&context.scan_found, 0, 1)) {
        bt_addr_le_copy(&context.scan_address, address);
        k_sem_give(&context.scan_sem);
    }
}

static void connection_established(struct bt_conn *connection,
                                   uint8_t error)
{
    if (error != 0U) {
        context.last_ble_error = error;
        context.operation_result = -ECONNREFUSED;
        atomic_set(&context.connected, 0);
        if (context.connection == connection) {
            bt_conn_unref(context.connection);
            context.connection = NULL;
        }
        k_sem_give(&context.connected_sem);
        return;
    }

    atomic_set(&context.connected, 1);
    context.operation_result = 0;

    if (atomic_get(&context.secure_on_connect)) {
        bt_security_t level = BT_SECURITY_L2;
        if (atomic_get(&context.force_pair_on_connect)) {
            level = (bt_security_t)(level | BT_SECURITY_FORCE_PAIR);
        }
        const int security_result = bt_conn_set_security(connection, level);
        if (security_result != 0 && security_result != -EALREADY) {
            context.last_ble_error = security_result;
            context.operation_result = security_result;
            k_sem_give(&context.security_sem);
        }
    }

    k_sem_give(&context.connected_sem);
}

static void connection_security_changed(struct bt_conn *connection,
                                        bt_security_t level,
                                        enum bt_security_err error)
{
    if (connection != context.connection) {
        return;
    }

    const bool secured = error == BT_SECURITY_ERR_SUCCESS &&
                         level >= BT_SECURITY_L2;
    atomic_set(&context.encrypted, secured ? 1 : 0);
    context.operation_result = secured ? 0 : -EACCES;
    context.last_ble_error = secured ? 0 : (int)error;
    k_sem_give(&context.security_sem);
}

static void connection_disconnected(struct bt_conn *connection,
                                    uint8_t reason)
{
    if (connection != context.connection) {
        return;
    }

    const bool requested = atomic_get(&context.disconnect_requested) != 0;
    atomic_set(&context.connected, 0);
    atomic_set(&context.encrypted, 0);
    atomic_set(&context.ready, 0);
    atomic_set(&context.secure_on_connect, 0);
    atomic_set(&context.force_pair_on_connect, 0);
    context.operation_result = requested ? 0 : -ENOTCONN;
    if (!requested) {
        context.last_ble_error = reason;
    }

    bt_conn_unref(context.connection);
    context.connection = NULL;
    k_sem_give(&context.connected_sem);
    k_sem_give(&context.security_sem);
    k_sem_give(&context.discovery_sem);
    k_sem_give(&context.disconnected_sem);
}

BT_CONN_CB_DEFINE(canon_connection_callbacks) = {
    .connected = connection_established,
    .disconnected = connection_disconnected,
    .security_changed = connection_security_changed,
};

static uint8_t discovery_completed(
    struct bt_conn *connection, const struct bt_gatt_attr *attribute,
    struct bt_gatt_discover_params *parameters)
{
    if (connection != context.connection) {
        return BT_GATT_ITER_STOP;
    }

    if (attribute == NULL) {
        context.operation_result = -ENOENT;
        k_sem_give(&context.discovery_sem);
        return BT_GATT_ITER_STOP;
    }

    if (parameters->type == BT_GATT_DISCOVER_PRIMARY) {
        const struct bt_gatt_service_val *service = attribute->user_data;
        context.service_start_handle = (uint16_t)(attribute->handle + 1U);
        context.service_end_handle = service->end_handle;
    } else if (parameters->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
        const struct bt_gatt_chrc *characteristic = attribute->user_data;
        context.discovered_value_handle = characteristic->value_handle;
    } else {
        context.operation_result = -EINVAL;
        k_sem_give(&context.discovery_sem);
        return BT_GATT_ITER_STOP;
    }

    context.operation_result = 0;
    k_sem_give(&context.discovery_sem);
    return BT_GATT_ITER_STOP;
}

static canon_zephyr_result_t discover_service(void)
{
    context.service_start_handle = 0U;
    context.service_end_handle = 0U;
    context.operation_result = 0;
    k_sem_reset(&context.discovery_sem);
    memset(&context.discovery_parameters, 0,
           sizeof(context.discovery_parameters));
    context.discovery_parameters.uuid = &canon_service_uuid.uuid;
    context.discovery_parameters.func = discovery_completed;
    context.discovery_parameters.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    context.discovery_parameters.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    context.discovery_parameters.type = BT_GATT_DISCOVER_PRIMARY;

    const int result = bt_gatt_discover(
        context.connection, &context.discovery_parameters);
    if (result != 0) {
        return result_from_error(result);
    }
    return wait_for_operation(&context.discovery_sem,
                              K_SECONDS(GATT_TIMEOUT_SECONDS));
}

static canon_zephyr_result_t discover_characteristic(
    const struct bt_uuid *uuid, uint16_t *value_handle)
{
    context.discovered_value_handle = 0U;
    context.operation_result = 0;
    k_sem_reset(&context.discovery_sem);
    memset(&context.discovery_parameters, 0,
           sizeof(context.discovery_parameters));
    context.discovery_parameters.uuid = uuid;
    context.discovery_parameters.func = discovery_completed;
    context.discovery_parameters.start_handle =
        context.service_start_handle;
    context.discovery_parameters.end_handle = context.service_end_handle;
    context.discovery_parameters.type = BT_GATT_DISCOVER_CHARACTERISTIC;

    const int result = bt_gatt_discover(
        context.connection, &context.discovery_parameters);
    if (result != 0) {
        return result_from_error(result);
    }

    const canon_zephyr_result_t wait_result = wait_for_operation(
        &context.discovery_sem, K_SECONDS(GATT_TIMEOUT_SECONDS));
    if (wait_result == CANON_ZEPHYR_OK) {
        *value_handle = context.discovered_value_handle;
    }
    return wait_result;
}

static canon_zephyr_result_t disconnect_link(void)
{
    if (context.connection == NULL || !atomic_get(&context.connected)) {
        atomic_set(&context.connected, 0);
        atomic_set(&context.encrypted, 0);
        atomic_set(&context.ready, 0);
        return CANON_ZEPHYR_OK;
    }

    context.operation_result = 0;
    atomic_set(&context.disconnect_requested, 1);
    k_sem_reset(&context.disconnected_sem);
    const int result = bt_conn_disconnect(
        context.connection, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    if (result == -ENOTCONN) {
        struct bt_conn *stale_connection = context.connection;
        context.connection = NULL;
        bt_conn_unref(stale_connection);
        atomic_set(&context.disconnect_requested, 0);
        atomic_set(&context.connected, 0);
        atomic_set(&context.encrypted, 0);
        atomic_set(&context.ready, 0);
        return CANON_ZEPHYR_OK;
    }
    if (result != 0) {
        atomic_set(&context.disconnect_requested, 0);
        return result_from_error(result);
    }

    const canon_zephyr_result_t wait_result = wait_for_operation(
        &context.disconnected_sem, K_SECONDS(DISCONNECT_TIMEOUT_SECONDS));
    atomic_set(&context.disconnect_requested, 0);
    return wait_result;
}

static void disconnect_after_failure(void)
{
    const int failed_error = context.last_ble_error;
    (void)disconnect_link();
    context.last_ble_error = failed_error;
}

static canon_zephyr_result_t connect_link(const bt_addr_le_t *address,
                                          bool force_pair)
{
    context.operation_result = 0;
    context.last_ble_error = 0;
    atomic_set(&context.ready, 0);
    atomic_set(&context.encrypted, 0);
    atomic_set(&context.secure_on_connect, 1);
    atomic_set(&context.force_pair_on_connect, force_pair ? 1 : 0);
    k_sem_reset(&context.connected_sem);
    k_sem_reset(&context.security_sem);

    const int result = bt_conn_le_create(
        address, &canon_create_parameters, &canon_connection_parameters,
        &context.connection);
    if (result != 0) {
        atomic_set(&context.secure_on_connect, 0);
        atomic_set(&context.force_pair_on_connect, 0);
        return result_from_error(result);
    }

    const canon_zephyr_result_t wait_result = wait_for_operation(
        &context.connected_sem, K_SECONDS(CONNECT_TIMEOUT_SECONDS));
    if (wait_result != CANON_ZEPHYR_OK) {
        return wait_result;
    }
    return CANON_ZEPHYR_OK;
}

static canon_zephyr_result_t secure_link(void)
{
    if (context.connection == NULL) {
        return result_from_error(-ENOTCONN);
    }
    if (bt_conn_get_security(context.connection) >= BT_SECURITY_L2) {
        atomic_set(&context.encrypted, 1);
        context.last_ble_error = 0;
        return CANON_ZEPHYR_OK;
    }

    const canon_zephyr_result_t result = wait_for_operation(
        &context.security_sem, K_SECONDS(SECURITY_TIMEOUT_SECONDS));
    atomic_set(&context.secure_on_connect, 0);
    atomic_set(&context.force_pair_on_connect, 0);
    return result;
}

static canon_zephyr_result_t scan_for_camera(uint32_t scan_seconds)
{
    context.operation_result = 0;
    context.last_ble_error = 0;
    atomic_set(&context.scan_found, 0);
    atomic_set(&context.scanning, 1);
    k_sem_reset(&context.scan_sem);

    const int result = bt_le_scan_start(&canon_scan_parameters,
                                        scan_received);
    if (result != 0) {
        atomic_set(&context.scanning, 0);
        return result_from_error(result);
    }

    const int wait_result = k_sem_take(&context.scan_sem,
                                       K_SECONDS(scan_seconds));
    const int stop_result = bt_le_scan_stop();
    atomic_set(&context.scanning, 0);
    if (stop_result != 0 && stop_result != -EALREADY) {
        return result_from_error(stop_result);
    }
    if (wait_result == 0 && atomic_get(&context.scan_found)) {
        return CANON_ZEPHYR_OK;
    }
    return result_from_error(-ENOENT);
}

static canon_zephyr_result_t connect_for_control(void)
{
    if (!atomic_get(&context.paired)) {
        return CANON_ZEPHYR_NOT_PAIRED;
    }
    if (atomic_get(&context.ready) && atomic_get(&context.connected) &&
        atomic_get(&context.encrypted)) {
        return CANON_ZEPHYR_OK;
    }
    if (atomic_get(&context.connected)) {
        const canon_zephyr_result_t disconnect_result = disconnect_link();
        if (disconnect_result != CANON_ZEPHYR_OK) {
            return disconnect_result;
        }
    }

    canon_zephyr_result_t result = connect_link(
        &context.camera_address, false);
    if (result == CANON_ZEPHYR_OK) {
        result = secure_link();
    }
    if (result == CANON_ZEPHYR_OK) {
        result = discover_service();
    }

    uint16_t trigger_handle = 0U;
    if (result == CANON_ZEPHYR_OK) {
        result = discover_characteristic(&canon_trigger_uuid.uuid,
                                         &trigger_handle);
    }
    if (result != CANON_ZEPHYR_OK) {
        disconnect_after_failure();
        return result;
    }

    context.discovered_value_handle = trigger_handle;
    atomic_set(&context.ready, 1);
    return CANON_ZEPHYR_OK;
}

static canon_zephyr_result_t begin_operation(void)
{
    if (!atomic_get(&context.initialized) ||
        !atomic_get(&context.host_ready)) {
        return CANON_ZEPHYR_NOT_READY;
    }
    if (k_mutex_lock(&context.operation_mutex, K_NO_WAIT) != 0) {
        return CANON_ZEPHYR_BUSY;
    }
    atomic_set(&context.busy, 1);
    context.last_ble_error = 0;
    return CANON_ZEPHYR_OK;
}

static void finish_operation(void)
{
    atomic_set(&context.busy, 0);
    k_mutex_unlock(&context.operation_mutex);
}

canon_zephyr_result_t canon_ble_zephyr_initialize(void)
{
    if (atomic_get(&context.initialized)) {
        return CANON_ZEPHYR_OK;
    }

    k_mutex_init(&context.operation_mutex);
    k_sem_init(&context.scan_sem, 0, 1);
    k_sem_init(&context.connected_sem, 0, 1);
    k_sem_init(&context.security_sem, 0, 1);
    k_sem_init(&context.discovery_sem, 0, 1);
    k_sem_init(&context.disconnected_sem, 0, 1);

    int result = bt_enable(NULL);
    if (result != 0) {
        return result_from_error(result);
    }
    bt_set_bondable(true);

    result = settings_load();
    if (result != 0) {
        LOG_WRN("Could not load persistent settings: %d", result);
    }

    atomic_set(&context.host_ready, 1);
    atomic_set(&context.initialized, 1);
    context.last_ble_error = 0;
    return CANON_ZEPHYR_OK;
}

canon_zephyr_result_t canon_ble_zephyr_pair(uint32_t scan_seconds)
{
    if (scan_seconds == 0U || scan_seconds > 60U) {
        return CANON_ZEPHYR_INVALID_ARGUMENT;
    }

    canon_zephyr_result_t result = begin_operation();
    if (result != CANON_ZEPHYR_OK) {
        return result;
    }

    const bool had_previous_camera = atomic_get(&context.paired) != 0;
    bt_addr_le_t previous_camera_address;
    if (had_previous_camera) {
        bt_addr_le_copy(&previous_camera_address,
                        &context.camera_address);
    }

    result = disconnect_link();
    if (result == CANON_ZEPHYR_OK) {
        result = scan_for_camera(scan_seconds);
    }
    if (result == CANON_ZEPHYR_OK) {
        const int unpair_result = bt_unpair(BT_ID_DEFAULT,
                                            &context.scan_address);
        if (unpair_result != 0 && unpair_result != -ENOENT) {
            LOG_WRN("Could not remove a stale peer bond: %d",
                    unpair_result);
        }
        result = connect_link(&context.scan_address, true);
    }
    if (result == CANON_ZEPHYR_OK) {
        result = secure_link();
    }
    if (result == CANON_ZEPHYR_OK) {
        result = discover_service();
    }

    uint16_t pairing_handle = 0U;
    if (result == CANON_ZEPHYR_OK) {
        result = discover_characteristic(&canon_pairing_uuid.uuid,
                                         &pairing_handle);
    }

    canon_packet_t packet;
    if (result == CANON_ZEPHYR_OK &&
        !canon_protocol_make_pairing_packet(CANON_REMOTE_NAME, &packet)) {
        result = CANON_ZEPHYR_INVALID_ARGUMENT;
    }
    if (result == CANON_ZEPHYR_OK) {
        result = result_from_error(bt_gatt_write_without_response(
            context.connection, pairing_handle, packet.data,
            packet.length, false));
    }

    if (result == CANON_ZEPHYR_OK) {
        struct bt_conn_info connection_info;
        const int info_result = bt_conn_get_info(
            context.connection, &connection_info);
        if (info_result == 0 && connection_info.type == BT_CONN_TYPE_LE) {
            result = result_from_error(
                save_camera_address(connection_info.le.dst));
        } else {
            result = result_from_error(
                info_result == 0 ? -EINVAL : info_result);
        }
    }

    if (result == CANON_ZEPHYR_OK && had_previous_camera &&
        bt_addr_le_cmp(&previous_camera_address,
                       &context.camera_address) != 0) {
        const int old_unpair_result = bt_unpair(
            BT_ID_DEFAULT, &previous_camera_address);
        if (old_unpair_result != 0 && old_unpair_result != -ENOENT) {
            LOG_WRN("Could not remove the previous camera bond: %d",
                    old_unpair_result);
        }
    }

    if (result == CANON_ZEPHYR_OK) {
        k_msleep(PAIRING_BOND_SETTLE_MS);
        const canon_zephyr_result_t disconnect_result = disconnect_link();
        if (disconnect_result == CANON_ZEPHYR_OK) {
            k_msleep(PAIRING_RECONNECT_DELAY_MS);
            const canon_zephyr_result_t reconnect_result =
                connect_for_control();
            if (reconnect_result != CANON_ZEPHYR_OK) {
                LOG_WRN("Paired, but reconnect failed: %s (BLE %d)",
                        canon_ble_zephyr_result_name(reconnect_result),
                        context.last_ble_error);
            }
        }
    } else {
        disconnect_after_failure();
    }

    finish_operation();
    return result;
}

canon_zephyr_result_t canon_ble_zephyr_connect(void)
{
    canon_zephyr_result_t result = begin_operation();
    if (result != CANON_ZEPHYR_OK) {
        return result;
    }
    result = connect_for_control();
    finish_operation();
    return result;
}

canon_zephyr_result_t canon_ble_zephyr_disconnect(void)
{
    canon_zephyr_result_t result = begin_operation();
    if (result != CANON_ZEPHYR_OK) {
        return result;
    }
    result = disconnect_link();
    finish_operation();
    return result;
}

canon_zephyr_result_t canon_ble_zephyr_forget(void)
{
    canon_zephyr_result_t result = begin_operation();
    if (result != CANON_ZEPHYR_OK) {
        return result;
    }

    result = disconnect_link();
    if (result == CANON_ZEPHYR_OK && atomic_get(&context.paired)) {
        const int unpair_result = bt_unpair(BT_ID_DEFAULT,
                                            &context.camera_address);
        if (unpair_result != 0 && unpair_result != -ENOENT) {
            result = result_from_error(unpair_result);
        }
    }
    if (result == CANON_ZEPHYR_OK) {
        const int delete_result = settings_delete(CANON_SETTINGS_PEER_KEY);
        if (delete_result != 0 && delete_result != -ENOENT) {
            result = result_from_error(delete_result);
        }
    }
    if (result == CANON_ZEPHYR_OK) {
        memset(&context.camera_address, 0,
               sizeof(context.camera_address));
        atomic_set(&context.paired, 0);
        context.last_ble_error = 0;
    }

    finish_operation();
    return result;
}

static canon_zephyr_result_t send_button_command(canon_button_t button)
{
    canon_zephyr_result_t result = begin_operation();
    if (result != CANON_ZEPHYR_OK) {
        return result;
    }

    result = connect_for_control();
    if (result == CANON_ZEPHYR_OK) {
        const uint8_t pressed = canon_protocol_button_press(button);
        result = result_from_error(bt_gatt_write_without_response(
            context.connection, context.discovered_value_handle,
            &pressed, sizeof(pressed), false));
    }
    if (result == CANON_ZEPHYR_OK) {
        k_msleep(BUTTON_HOLD_MS);
        const uint8_t released = canon_protocol_button_release();
        result = result_from_error(bt_gatt_write_without_response(
            context.connection, context.discovered_value_handle,
            &released, sizeof(released), false));
        k_msleep(BUTTON_SETTLE_MS);
    }

    finish_operation();
    return result;
}

canon_zephyr_result_t canon_ble_zephyr_shutter(void)
{
    return send_button_command(CANON_BUTTON_SHUTTER);
}

canon_zephyr_result_t canon_ble_zephyr_focus(void)
{
    return send_button_command(CANON_BUTTON_FOCUS);
}

void canon_ble_zephyr_get_status(canon_zephyr_status_t *status)
{
    if (status == NULL) {
        return;
    }

    memset(status, 0, sizeof(*status));
    status->initialized = atomic_get(&context.initialized) != 0;
    status->host_ready = atomic_get(&context.host_ready) != 0;
    status->paired = atomic_get(&context.paired) != 0;
    status->connected = atomic_get(&context.connected) != 0;
    status->encrypted = atomic_get(&context.encrypted) != 0;
    status->ready = atomic_get(&context.ready) != 0;
    status->scanning = atomic_get(&context.scanning) != 0;
    status->busy = atomic_get(&context.busy) != 0;
    status->last_ble_error = context.last_ble_error;
    if (status->paired) {
        bt_addr_le_to_str(&context.camera_address, status->camera_address,
                          sizeof(status->camera_address));
    }
}

const char *canon_ble_zephyr_result_name(canon_zephyr_result_t result)
{
    switch (result) {
    case CANON_ZEPHYR_OK:
        return "ok";
    case CANON_ZEPHYR_BUSY:
        return "busy";
    case CANON_ZEPHYR_NOT_READY:
        return "BLE host not ready";
    case CANON_ZEPHYR_NOT_PAIRED:
        return "no saved camera";
    case CANON_ZEPHYR_NOT_FOUND:
        return "Canon camera not found";
    case CANON_ZEPHYR_TIMEOUT:
        return "operation timed out";
    case CANON_ZEPHYR_INVALID_ARGUMENT:
        return "invalid argument";
    case CANON_ZEPHYR_STACK_ERROR:
    default:
        return "Bluetooth stack error";
    }
}
