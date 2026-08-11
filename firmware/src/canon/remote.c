#include "remote.h"

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

#include "protocol_internal.h"

LOG_MODULE_REGISTER(canon_remote, LOG_LEVEL_INF);

#define CANON_REMOTE_NAME CONFIG_BT_DEVICE_NAME
#define CANON_SETTINGS_PEER_KEY "canon/peer"

BUILD_ASSERT(sizeof(CANON_REMOTE_NAME) > 1U,
             "Canon remote name must not be empty");
BUILD_ASSERT(sizeof(CANON_REMOTE_NAME) - 1U <= CANON_REMOTE_NAME_MAX,
             "Canon remote name exceeds the protocol limit");

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
#define BUTTON_OPERATION_TIMEOUT_MS 6000U
#define BUTTON_CANCEL_POLL_MS 20U
#define BUTTON_THREAD_STACK_SIZE 3072U
#define BUTTON_THREAD_PRIORITY 5

#define BUTTON_FOCUS_BIT CANON_REMOTE_BUTTON_FOCUS
#define BUTTON_SHUTTER_BIT CANON_REMOTE_BUTTON_SHUTTER
#define BUTTON_STATE_MASK (BIT(BUTTON_FOCUS_BIT) | BIT(BUTTON_SHUTTER_BIT))

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
    struct k_spinlock connection_lock;
    struct k_sem scan_sem;
    struct k_sem connected_sem;
    struct k_sem security_sem;
    struct k_sem discovery_sem;
    struct k_sem disconnected_sem;
    struct k_sem button_event_sem;

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
    atomic_t requested_buttons;
    atomic_t applied_buttons;
    atomic_t button_generation;
    atomic_t button_cancel_generation;

    bt_addr_le_t camera_address;
    bt_addr_le_t scan_address;
    struct bt_conn *connection;
    struct bt_gatt_discover_params discovery_parameters;
    uint16_t service_start_handle;
    uint16_t service_end_handle;
    uint16_t discovered_value_handle;
    int operation_result;
    int last_ble_error;
} canon_remote_context_t;

static canon_remote_context_t context;
K_THREAD_STACK_DEFINE(button_thread_stack, BUTTON_THREAD_STACK_SIZE);
static struct k_thread button_thread;

typedef struct {
    atomic_val_t cancel_generation;
    int64_t deadline_ms;
} operation_guard_t;

static void button_thread_entry(void *first, void *second, void *third);

static struct bt_conn *connection_ref(void)
{
    k_spinlock_key_t key = k_spin_lock(&context.connection_lock);
    struct bt_conn *connection = context.connection;

    if (connection != NULL) {
        bt_conn_ref(connection);
    }
    k_spin_unlock(&context.connection_lock, key);
    return connection;
}

static bool connection_install(struct bt_conn *connection)
{
    k_spinlock_key_t key = k_spin_lock(&context.connection_lock);
    const bool installed = context.connection == NULL;

    if (installed) {
        context.connection = connection;
    }
    k_spin_unlock(&context.connection_lock, key);
    return installed;
}

static struct bt_conn *connection_detach(struct bt_conn *connection)
{
    k_spinlock_key_t key = k_spin_lock(&context.connection_lock);
    struct bt_conn *detached = NULL;

    if (context.connection == connection) {
        detached = context.connection;
        context.connection = NULL;
    }
    k_spin_unlock(&context.connection_lock, key);
    return detached;
}

static void clear_link_state(void)
{
    atomic_set(&context.connected, 0);
    atomic_set(&context.encrypted, 0);
    atomic_set(&context.ready, 0);
    atomic_set(&context.secure_on_connect, 0);
    atomic_set(&context.force_pair_on_connect, 0);
    atomic_set(&context.applied_buttons, 0);
}

static canon_remote_result_t result_from_error(int error)
{
    if (error == 0) {
        context.last_ble_error = 0;
        return CANON_REMOTE_OK;
    }
    if (context.last_ble_error == 0) {
        context.last_ble_error = error;
    }
    if (error == -EBUSY || error == -EALREADY) {
        return CANON_REMOTE_BUSY;
    }
    if (error == -ENOENT) {
        return CANON_REMOTE_NOT_FOUND;
    }
    if (error == -ETIMEDOUT) {
        return CANON_REMOTE_TIMEOUT;
    }
    if (error == -EINVAL) {
        return CANON_REMOTE_INVALID_ARGUMENT;
    }
    return CANON_REMOTE_STACK_ERROR;
}

static bool operation_cancelled(const operation_guard_t *guard)
{
    return guard != NULL &&
           atomic_get(&context.button_cancel_generation) !=
               guard->cancel_generation;
}

static canon_remote_result_t wait_for_operation(
    struct k_sem *semaphore, uint32_t timeout_ms,
    const operation_guard_t *guard)
{
    if (guard == NULL) {
        if (k_sem_take(semaphore, K_MSEC(timeout_ms)) != 0) {
            return result_from_error(-ETIMEDOUT);
        }
        return result_from_error(context.operation_result);
    }

    const int64_t local_deadline = k_uptime_get() + timeout_ms;
    const int64_t deadline =
        MIN(local_deadline, guard->deadline_ms);
    while (!operation_cancelled(guard)) {
        const int64_t remaining_ms = deadline - k_uptime_get();
        if (remaining_ms <= 0) {
            return result_from_error(-ETIMEDOUT);
        }

        const uint32_t wait_ms = (uint32_t)MIN(
            remaining_ms, (int64_t)BUTTON_CANCEL_POLL_MS);
        if (k_sem_take(semaphore, K_MSEC(wait_ms)) == 0) {
            if (operation_cancelled(guard)) {
                return CANON_REMOTE_CANCELLED;
            }
            return result_from_error(context.operation_result);
        }
    }

    return CANON_REMOTE_CANCELLED;
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
        k_spinlock_key_t key = k_spin_lock(&context.connection_lock);
        if (context.connection != connection) {
            k_spin_unlock(&context.connection_lock, key);
            return;
        }

        const bool requested =
            atomic_get(&context.disconnect_requested) != 0;
        context.connection = NULL;
        context.last_ble_error = error;
        context.operation_result = requested ? 0 : -ECONNREFUSED;
        clear_link_state();
        k_sem_give(&context.connected_sem);
        k_sem_give(&context.security_sem);
        k_sem_give(&context.discovery_sem);
        k_sem_give(&context.disconnected_sem);
        k_spin_unlock(&context.connection_lock, key);
        bt_conn_unref(connection);
        return;
    }

    k_spinlock_key_t key = k_spin_lock(&context.connection_lock);
    if (context.connection != connection) {
        k_spin_unlock(&context.connection_lock, key);
        return;
    }

    atomic_set(&context.connected, 1);
    context.operation_result = 0;
    const bool secure_on_connect =
        atomic_get(&context.secure_on_connect) != 0;
    const bool force_pair =
        atomic_get(&context.force_pair_on_connect) != 0;
    k_spin_unlock(&context.connection_lock, key);

    int security_result = 0;
    if (secure_on_connect) {
        bt_security_t level = BT_SECURITY_L2;
        if (force_pair) {
            level = (bt_security_t)(level | BT_SECURITY_FORCE_PAIR);
        }
        security_result = bt_conn_set_security(connection, level);
    }

    key = k_spin_lock(&context.connection_lock);
    if (context.connection == connection) {
        if (security_result != 0 && security_result != -EALREADY) {
            context.last_ble_error = security_result;
            context.operation_result = security_result;
            k_sem_give(&context.security_sem);
        }
        k_sem_give(&context.connected_sem);
    }
    k_spin_unlock(&context.connection_lock, key);
}

static void connection_security_changed(struct bt_conn *connection,
                                        bt_security_t level,
                                        enum bt_security_err error)
{
    k_spinlock_key_t key = k_spin_lock(&context.connection_lock);
    if (context.connection != connection) {
        k_spin_unlock(&context.connection_lock, key);
        return;
    }

    const bool secured = error == BT_SECURITY_ERR_SUCCESS &&
                         level >= BT_SECURITY_L2;
    atomic_set(&context.encrypted, secured ? 1 : 0);
    if (!secured) {
        atomic_set(&context.ready, 0);
        atomic_set(&context.applied_buttons, 0);
    }
    context.operation_result = secured ? 0 : -EACCES;
    context.last_ble_error = secured ? 0 : (int)error;
    k_sem_give(&context.security_sem);
    k_spin_unlock(&context.connection_lock, key);
}

static void connection_disconnected(struct bt_conn *connection,
                                    uint8_t reason)
{
    k_spinlock_key_t key = k_spin_lock(&context.connection_lock);
    if (context.connection != connection) {
        k_spin_unlock(&context.connection_lock, key);
        return;
    }

    const bool requested = atomic_get(&context.disconnect_requested) != 0;
    context.connection = NULL;
    clear_link_state();
    context.operation_result = requested ? 0 : -ENOTCONN;
    if (!requested) {
        context.last_ble_error = reason;
    }

    k_sem_give(&context.connected_sem);
    k_sem_give(&context.security_sem);
    k_sem_give(&context.discovery_sem);
    k_sem_give(&context.disconnected_sem);
    k_spin_unlock(&context.connection_lock, key);
    bt_conn_unref(connection);
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
    k_spinlock_key_t key = k_spin_lock(&context.connection_lock);
    if (context.connection != connection) {
        k_spin_unlock(&context.connection_lock, key);
        return BT_GATT_ITER_STOP;
    }

    if (attribute == NULL) {
        context.operation_result = -ENOENT;
        k_sem_give(&context.discovery_sem);
        k_spin_unlock(&context.connection_lock, key);
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
        k_spin_unlock(&context.connection_lock, key);
        return BT_GATT_ITER_STOP;
    }

    context.operation_result = 0;
    k_sem_give(&context.discovery_sem);
    k_spin_unlock(&context.connection_lock, key);
    return BT_GATT_ITER_STOP;
}

static canon_remote_result_t discover_service(
    const operation_guard_t *guard)
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

    struct bt_conn *connection = connection_ref();
    if (connection == NULL) {
        return result_from_error(-ENOTCONN);
    }
    const int result = bt_gatt_discover(
        connection, &context.discovery_parameters);
    bt_conn_unref(connection);
    if (result != 0) {
        return result_from_error(result);
    }
    return wait_for_operation(&context.discovery_sem,
                              GATT_TIMEOUT_SECONDS * 1000U, guard);
}

static canon_remote_result_t discover_characteristic(
    const struct bt_uuid *uuid, uint16_t *value_handle,
    const operation_guard_t *guard)
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

    struct bt_conn *connection = connection_ref();
    if (connection == NULL) {
        return result_from_error(-ENOTCONN);
    }
    const int result = bt_gatt_discover(
        connection, &context.discovery_parameters);
    bt_conn_unref(connection);
    if (result != 0) {
        return result_from_error(result);
    }

    const canon_remote_result_t wait_result = wait_for_operation(
        &context.discovery_sem, GATT_TIMEOUT_SECONDS * 1000U, guard);
    if (wait_result == CANON_REMOTE_OK) {
        *value_handle = context.discovered_value_handle;
    }
    return wait_result;
}

static canon_remote_result_t disconnect_link(void)
{
    struct bt_conn *connection = connection_ref();
    if (connection == NULL) {
        clear_link_state();
        atomic_set(&context.disconnect_requested, 0);
        return CANON_REMOTE_OK;
    }

    context.operation_result = 0;
    atomic_set(&context.disconnect_requested, 1);
    k_sem_reset(&context.disconnected_sem);
    const int result = bt_conn_disconnect(
        connection, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    if (result == -ENOTCONN) {
        struct bt_conn *detached = connection_detach(connection);
        if (detached != NULL) {
            bt_conn_unref(detached);
        }
        atomic_set(&context.disconnect_requested, 0);
        clear_link_state();
        bt_conn_unref(connection);
        return CANON_REMOTE_OK;
    }
    if (result != 0) {
        atomic_set(&context.disconnect_requested, 0);
        bt_conn_unref(connection);
        return result_from_error(result);
    }

    bt_conn_unref(connection);
    const canon_remote_result_t wait_result = wait_for_operation(
        &context.disconnected_sem, DISCONNECT_TIMEOUT_SECONDS * 1000U,
        NULL);
    atomic_set(&context.disconnect_requested, 0);
    return wait_result;
}

static void disconnect_after_failure(void)
{
    const int failed_error = context.last_ble_error;
    (void)disconnect_link();
    context.last_ble_error = failed_error;
}

static canon_remote_result_t connect_link(const bt_addr_le_t *address,
                                          bool force_pair,
                                          const operation_guard_t *guard)
{
    struct bt_conn *existing_connection = connection_ref();
    if (existing_connection != NULL) {
        bt_conn_unref(existing_connection);
        return result_from_error(-EBUSY);
    }

    context.operation_result = 0;
    context.last_ble_error = 0;
    atomic_set(&context.ready, 0);
    atomic_set(&context.encrypted, 0);
    atomic_set(&context.secure_on_connect, 1);
    atomic_set(&context.force_pair_on_connect, force_pair ? 1 : 0);
    k_sem_reset(&context.connected_sem);
    k_sem_reset(&context.security_sem);

    struct bt_conn *new_connection = NULL;
    const int result = bt_conn_le_create(
        address, &canon_create_parameters, &canon_connection_parameters,
        &new_connection);
    if (result != 0) {
        atomic_set(&context.secure_on_connect, 0);
        atomic_set(&context.force_pair_on_connect, 0);
        return result_from_error(result);
    }
    if (!connection_install(new_connection)) {
        (void)bt_conn_disconnect(new_connection,
                                 BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        bt_conn_unref(new_connection);
        atomic_set(&context.secure_on_connect, 0);
        atomic_set(&context.force_pair_on_connect, 0);
        return result_from_error(-EBUSY);
    }

    const canon_remote_result_t wait_result = wait_for_operation(
        &context.connected_sem, CONNECT_TIMEOUT_SECONDS * 1000U, guard);
    if (wait_result != CANON_REMOTE_OK) {
        return wait_result;
    }
    return CANON_REMOTE_OK;
}

static canon_remote_result_t secure_link(const operation_guard_t *guard)
{
    struct bt_conn *connection = connection_ref();
    if (connection == NULL) {
        return result_from_error(-ENOTCONN);
    }
    if (bt_conn_get_security(connection) >= BT_SECURITY_L2) {
        k_spinlock_key_t key = k_spin_lock(&context.connection_lock);
        const bool active = context.connection == connection &&
                            atomic_get(&context.connected) != 0;
        if (active) {
            atomic_set(&context.encrypted, 1);
            context.last_ble_error = 0;
        }
        k_spin_unlock(&context.connection_lock, key);
        bt_conn_unref(connection);
        return active ? CANON_REMOTE_OK : result_from_error(-ENOTCONN);
    }
    bt_conn_unref(connection);

    const canon_remote_result_t result = wait_for_operation(
        &context.security_sem, SECURITY_TIMEOUT_SECONDS * 1000U, guard);
    atomic_set(&context.secure_on_connect, 0);
    atomic_set(&context.force_pair_on_connect, 0);
    return result;
}

static canon_remote_result_t scan_for_camera(uint32_t scan_seconds)
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
        return CANON_REMOTE_OK;
    }
    return result_from_error(-ENOENT);
}

static canon_remote_result_t connect_for_control(
    const operation_guard_t *guard)
{
    if (!atomic_get(&context.paired)) {
        return CANON_REMOTE_NOT_PAIRED;
    }
    if (atomic_get(&context.ready) && atomic_get(&context.connected) &&
        atomic_get(&context.encrypted)) {
        struct bt_conn *ready_connection = connection_ref();
        if (ready_connection != NULL) {
            bt_conn_unref(ready_connection);
            return CANON_REMOTE_OK;
        }
        clear_link_state();
    }

    struct bt_conn *existing_connection = connection_ref();
    if (existing_connection != NULL) {
        bt_conn_unref(existing_connection);
        const canon_remote_result_t disconnect_result = disconnect_link();
        if (disconnect_result != CANON_REMOTE_OK) {
            return disconnect_result;
        }
    }

    canon_remote_result_t result = connect_link(
        &context.camera_address, false, guard);
    if (result == CANON_REMOTE_OK) {
        result = secure_link(guard);
    }
    if (result == CANON_REMOTE_OK) {
        result = discover_service(guard);
    }

    uint16_t trigger_handle = 0U;
    if (result == CANON_REMOTE_OK) {
        result = discover_characteristic(&canon_trigger_uuid.uuid,
                                         &trigger_handle, guard);
    }
    if (result != CANON_REMOTE_OK) {
        disconnect_after_failure();
        return result;
    }

    context.discovered_value_handle = trigger_handle;
    atomic_set(&context.ready, 1);
    return CANON_REMOTE_OK;
}

static canon_remote_result_t begin_operation(bool button_worker)
{
    if (!atomic_get(&context.initialized) ||
        !atomic_get(&context.host_ready)) {
        return CANON_REMOTE_NOT_READY;
    }
    if (!button_worker &&
        (atomic_get(&context.requested_buttons) != 0 ||
         atomic_get(&context.applied_buttons) != 0)) {
        return CANON_REMOTE_BUSY;
    }
    if (k_mutex_lock(&context.operation_mutex, K_NO_WAIT) != 0) {
        return CANON_REMOTE_BUSY;
    }
    atomic_set(&context.busy, 1);
    context.last_ble_error = 0;
    return CANON_REMOTE_OK;
}

static void finish_operation(void)
{
    atomic_set(&context.busy, 0);
    k_mutex_unlock(&context.operation_mutex);
}

canon_remote_result_t canon_remote_initialize(void)
{
    if (atomic_get(&context.initialized)) {
        return CANON_REMOTE_OK;
    }

    k_mutex_init(&context.operation_mutex);
    k_sem_init(&context.scan_sem, 0, 1);
    k_sem_init(&context.connected_sem, 0, 1);
    k_sem_init(&context.security_sem, 0, 1);
    k_sem_init(&context.discovery_sem, 0, 1);
    k_sem_init(&context.disconnected_sem, 0, 1);
    k_sem_init(&context.button_event_sem, 0, 1);

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

    k_tid_t button_thread_id = k_thread_create(
        &button_thread, button_thread_stack,
        K_THREAD_STACK_SIZEOF(button_thread_stack), button_thread_entry,
        NULL, NULL, NULL, BUTTON_THREAD_PRIORITY, 0, K_NO_WAIT);
    (void)k_thread_name_set(button_thread_id, "canon_buttons");
    return CANON_REMOTE_OK;
}

canon_remote_result_t canon_remote_pair(uint32_t scan_seconds)
{
    if (scan_seconds == 0U || scan_seconds > 60U) {
        return CANON_REMOTE_INVALID_ARGUMENT;
    }

    canon_remote_result_t result = begin_operation(false);
    if (result != CANON_REMOTE_OK) {
        return result;
    }

    const bool had_previous_camera = atomic_get(&context.paired) != 0;
    bt_addr_le_t previous_camera_address;
    if (had_previous_camera) {
        bt_addr_le_copy(&previous_camera_address,
                        &context.camera_address);
    }

    result = disconnect_link();
    if (result == CANON_REMOTE_OK) {
        result = scan_for_camera(scan_seconds);
    }
    if (result == CANON_REMOTE_OK) {
        const int unpair_result = bt_unpair(BT_ID_DEFAULT,
                                            &context.scan_address);
        if (unpair_result != 0 && unpair_result != -ENOENT) {
            LOG_WRN("Could not remove a stale peer bond: %d",
                    unpair_result);
        }
        result = connect_link(&context.scan_address, true, NULL);
    }
    if (result == CANON_REMOTE_OK) {
        result = secure_link(NULL);
    }
    if (result == CANON_REMOTE_OK) {
        result = discover_service(NULL);
    }

    uint16_t pairing_handle = 0U;
    if (result == CANON_REMOTE_OK) {
        result = discover_characteristic(&canon_pairing_uuid.uuid,
                                         &pairing_handle, NULL);
    }

    canon_packet_t packet;
    if (result == CANON_REMOTE_OK &&
        !canon_protocol_make_pairing_packet(CANON_REMOTE_NAME, &packet)) {
        result = CANON_REMOTE_INVALID_ARGUMENT;
    }
    if (result == CANON_REMOTE_OK) {
        struct bt_conn *connection = connection_ref();
        if (connection == NULL) {
            result = result_from_error(-ENOTCONN);
        } else {
            result = result_from_error(bt_gatt_write_without_response(
                connection, pairing_handle, packet.data,
                packet.length, false));
            bt_conn_unref(connection);
        }
    }

    if (result == CANON_REMOTE_OK) {
        struct bt_conn *connection = connection_ref();
        if (connection == NULL) {
            result = result_from_error(-ENOTCONN);
        } else {
            struct bt_conn_info connection_info;
            const int info_result = bt_conn_get_info(
                connection, &connection_info);
            if (info_result == 0 &&
                connection_info.type == BT_CONN_TYPE_LE) {
                result = result_from_error(
                    save_camera_address(connection_info.le.dst));
            } else {
                result = result_from_error(
                    info_result == 0 ? -EINVAL : info_result);
            }
            bt_conn_unref(connection);
        }
    }

    if (result == CANON_REMOTE_OK && had_previous_camera &&
        bt_addr_le_cmp(&previous_camera_address,
                       &context.camera_address) != 0) {
        const int old_unpair_result = bt_unpair(
            BT_ID_DEFAULT, &previous_camera_address);
        if (old_unpair_result != 0 && old_unpair_result != -ENOENT) {
            LOG_WRN("Could not remove the previous camera bond: %d",
                    old_unpair_result);
        }
    }

    if (result == CANON_REMOTE_OK) {
        k_msleep(PAIRING_BOND_SETTLE_MS);
        const canon_remote_result_t disconnect_result = disconnect_link();
        if (disconnect_result == CANON_REMOTE_OK) {
            k_msleep(PAIRING_RECONNECT_DELAY_MS);
            const canon_remote_result_t reconnect_result =
                connect_for_control(NULL);
            if (reconnect_result != CANON_REMOTE_OK) {
                LOG_WRN("Paired, but reconnect failed: %s (BLE %d)",
                        canon_remote_result_name(reconnect_result),
                        context.last_ble_error);
            }
        }
    } else {
        disconnect_after_failure();
    }

    finish_operation();
    return result;
}

canon_remote_result_t canon_remote_connect(void)
{
    canon_remote_result_t result = begin_operation(false);
    if (result != CANON_REMOTE_OK) {
        return result;
    }
    result = connect_for_control(NULL);
    finish_operation();
    return result;
}

canon_remote_result_t canon_remote_disconnect(void)
{
    canon_remote_result_t result = begin_operation(false);
    if (result != CANON_REMOTE_OK) {
        return result;
    }
    result = disconnect_link();
    finish_operation();
    return result;
}

canon_remote_result_t canon_remote_forget(void)
{
    canon_remote_result_t result = begin_operation(false);
    if (result != CANON_REMOTE_OK) {
        return result;
    }

    result = disconnect_link();
    if (result == CANON_REMOTE_OK && atomic_get(&context.paired)) {
        const int unpair_result = bt_unpair(BT_ID_DEFAULT,
                                            &context.camera_address);
        if (unpair_result != 0 && unpair_result != -ENOENT) {
            result = result_from_error(unpair_result);
        }
    }
    if (result == CANON_REMOTE_OK) {
        const int delete_result = settings_delete(CANON_SETTINGS_PEER_KEY);
        if (delete_result != 0 && delete_result != -ENOENT) {
            result = result_from_error(delete_result);
        }
    }
    if (result == CANON_REMOTE_OK) {
        memset(&context.camera_address, 0,
               sizeof(context.camera_address));
        atomic_set(&context.paired, 0);
        context.last_ble_error = 0;
    }

    finish_operation();
    return result;
}

static canon_remote_result_t send_button_command(canon_button_t button)
{
    canon_remote_result_t result = begin_operation(false);
    if (result != CANON_REMOTE_OK) {
        return result;
    }

    struct bt_conn *connection = NULL;
    result = connect_for_control(NULL);
    if (result == CANON_REMOTE_OK) {
        connection = connection_ref();
        if (connection == NULL) {
            result = result_from_error(-ENOTCONN);
        }
    }
    if (result == CANON_REMOTE_OK) {
        const uint8_t pressed = canon_protocol_button_press(button);
        result = result_from_error(bt_gatt_write_without_response(
            connection, context.discovered_value_handle,
            &pressed, sizeof(pressed), false));
    }
    if (result == CANON_REMOTE_OK) {
        k_msleep(BUTTON_HOLD_MS);
        const uint8_t released = canon_protocol_button_release();
        result = result_from_error(bt_gatt_write_without_response(
            connection, context.discovered_value_handle,
            &released, sizeof(released), false));
        k_msleep(BUTTON_SETTLE_MS);
    }
    if (connection != NULL) {
        bt_conn_unref(connection);
    }

    finish_operation();
    return result;
}

static canon_remote_result_t write_button_state(atomic_val_t buttons)
{
    struct bt_conn *connection = connection_ref();
    if (connection == NULL) {
        return result_from_error(-ENOTCONN);
    }

    const uint8_t value = canon_protocol_button_state(
        (buttons & BIT(BUTTON_FOCUS_BIT)) != 0,
        (buttons & BIT(BUTTON_SHUTTER_BIT)) != 0);
    int write_result = bt_gatt_write_without_response(
        connection, context.discovered_value_handle, &value,
        sizeof(value), false);
    if (write_result == 0) {
        k_spinlock_key_t key = k_spin_lock(&context.connection_lock);
        if (context.connection == connection &&
            atomic_get(&context.connected) &&
            atomic_get(&context.ready)) {
            atomic_set(&context.applied_buttons, buttons);
        } else {
            write_result = -ENOTCONN;
        }
        k_spin_unlock(&context.connection_lock, key);
    }
    bt_conn_unref(connection);
    return result_from_error(write_result);
}

static canon_remote_result_t begin_button_operation(
    const operation_guard_t *guard)
{
    while (!operation_cancelled(guard)) {
        const canon_remote_result_t result = begin_operation(true);
        if (result != CANON_REMOTE_BUSY) {
            return result;
        }
        if (k_uptime_get() >= guard->deadline_ms) {
            return result_from_error(-ETIMEDOUT);
        }
        k_msleep(BUTTON_CANCEL_POLL_MS);
    }
    return CANON_REMOTE_CANCELLED;
}

static void process_button_state(void)
{
    for (;;) {
        const atomic_val_t desired =
            atomic_get(&context.requested_buttons) & BUTTON_STATE_MASK;
        const atomic_val_t applied =
            atomic_get(&context.applied_buttons) & BUTTON_STATE_MASK;
        if (desired == applied) {
            return;
        }

        const atomic_val_t generation =
            atomic_get(&context.button_generation);
        const operation_guard_t guard = {
            .cancel_generation =
                atomic_get(&context.button_cancel_generation),
            .deadline_ms =
                k_uptime_get() + BUTTON_OPERATION_TIMEOUT_MS,
        };

        canon_remote_result_t result = begin_button_operation(&guard);
        bool operation_started = result == CANON_REMOTE_OK;
        if (operation_started && desired != 0) {
            result = connect_for_control(&guard);
            const atomic_val_t latest =
                atomic_get(&context.requested_buttons) & BUTTON_STATE_MASK;
            if (result == CANON_REMOTE_OK &&
                (operation_cancelled(&guard) || latest == 0)) {
                result = CANON_REMOTE_CANCELLED;
                (void)disconnect_link();
            } else if (result == CANON_REMOTE_OK) {
                result = write_button_state(latest);
            }
        } else if (operation_started) {
            if (applied != 0 && atomic_get(&context.ready) &&
                atomic_get(&context.connected)) {
                result = write_button_state(0);
            }
            atomic_set(&context.applied_buttons, 0);
        }

        if (operation_started) {
            if (result != CANON_REMOTE_OK &&
                result != CANON_REMOTE_CANCELLED) {
                atomic_set(&context.applied_buttons, 0);
                disconnect_after_failure();
            }
            finish_operation();
        }

        if (result == CANON_REMOTE_CANCELLED) {
            continue;
        }
        if (result != CANON_REMOTE_OK) {
            LOG_WRN("Physical button update failed: %s (BLE %d)",
                    canon_remote_result_name(result),
                    context.last_ble_error);
            return;
        }
        if (generation == atomic_get(&context.button_generation) &&
            atomic_get(&context.requested_buttons) ==
                atomic_get(&context.applied_buttons)) {
            return;
        }
    }
}

static void button_thread_entry(void *first, void *second, void *third)
{
    (void)first;
    (void)second;
    (void)third;

    for (;;) {
        k_sem_take(&context.button_event_sem, K_FOREVER);
        while (k_sem_take(&context.button_event_sem, K_NO_WAIT) == 0) {
        }
        process_button_state();
    }
}

canon_remote_result_t canon_remote_set_button(canon_remote_button_t button,
                                              bool pressed)
{
    if (button != CANON_REMOTE_BUTTON_FOCUS &&
        button != CANON_REMOTE_BUTTON_SHUTTER) {
        return CANON_REMOTE_INVALID_ARGUMENT;
    }
    if (!atomic_get(&context.initialized) ||
        !atomic_get(&context.host_ready)) {
        return CANON_REMOTE_NOT_READY;
    }

    const bool was_pressed = pressed
                                 ? atomic_test_and_set_bit(
                                       &context.requested_buttons, button)
                                 : atomic_test_and_clear_bit(
                                       &context.requested_buttons, button);
    const bool changed = pressed ? !was_pressed : was_pressed;
    if (!changed) {
        return CANON_REMOTE_OK;
    }

    if (!pressed && atomic_get(&context.requested_buttons) == 0) {
        atomic_inc(&context.button_cancel_generation);
    }
    atomic_inc(&context.button_generation);
    k_sem_give(&context.button_event_sem);
    return CANON_REMOTE_OK;
}

canon_remote_result_t canon_remote_shutter(void)
{
    return send_button_command(CANON_BUTTON_SHUTTER);
}

canon_remote_result_t canon_remote_focus(void)
{
    return send_button_command(CANON_BUTTON_FOCUS);
}

void canon_remote_get_status(canon_remote_status_t *status)
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
    const atomic_val_t requested =
        atomic_get(&context.requested_buttons);
    status->focus_requested =
        (requested & BIT(BUTTON_FOCUS_BIT)) != 0;
    status->shutter_requested =
        (requested & BIT(BUTTON_SHUTTER_BIT)) != 0;
    const atomic_val_t applied = atomic_get(&context.applied_buttons);
    status->focus_applied =
        (applied & BIT(BUTTON_FOCUS_BIT)) != 0;
    status->shutter_applied =
        (applied & BIT(BUTTON_SHUTTER_BIT)) != 0;
    status->last_ble_error = context.last_ble_error;
    if (status->paired) {
        bt_addr_le_to_str(&context.camera_address, status->camera_address,
                          sizeof(status->camera_address));
    }
}

const char *canon_remote_result_name(canon_remote_result_t result)
{
    switch (result) {
    case CANON_REMOTE_OK:
        return "ok";
    case CANON_REMOTE_BUSY:
        return "busy";
    case CANON_REMOTE_NOT_READY:
        return "BLE host not ready";
    case CANON_REMOTE_NOT_PAIRED:
        return "no saved camera";
    case CANON_REMOTE_NOT_FOUND:
        return "Canon camera not found";
    case CANON_REMOTE_TIMEOUT:
        return "operation timed out";
    case CANON_REMOTE_CANCELLED:
        return "operation cancelled";
    case CANON_REMOTE_INVALID_ARGUMENT:
        return "invalid argument";
    case CANON_REMOTE_STACK_ERROR:
    default:
        return "Bluetooth stack error";
    }
}
