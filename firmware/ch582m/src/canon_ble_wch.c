#include "canon_ble_wch.h"

#include <stdio.h>
#include <string.h>

#include "CONFIG.h"
#include "canon_protocol.h"

#define START_DEVICE_EVT 0x0001U
#define START_GATT_DISCOVERY_EVT 0x0002U
#define OPERATION_TIMEOUT_EVT 0x0004U
#define BUTTON_RELEASE_EVT 0x0008U

#define CAMERA_RECORD_ADDRESS 0x7c00U
#define CAMERA_RECORD_PAGE_SIZE 256U
#define GATT_START_DELAY_TICKS MS1_TO_SYSTEM_TIME(400U)
#define BUTTON_HOLD_TICKS MS1_TO_SYSTEM_TIME(200U)
#define OPERATION_GRACE_SECONDS 30U
#define REMOTE_NAME "CH582 Remote"

typedef enum {
    OPERATION_NONE,
    OPERATION_PAIR,
    OPERATION_CONNECT,
    OPERATION_SHUTTER,
    OPERATION_FOCUS,
    OPERATION_DISCONNECT,
    OPERATION_FORGET,
} operation_t;

typedef enum {
    DISCOVERY_IDLE,
    DISCOVERY_SERVICE,
    DISCOVERY_PAIRING_CHARACTERISTIC,
    DISCOVERY_TRIGGER_CHARACTERISTIC,
} discovery_t;

typedef struct {
    tmosTaskID task_id;
    bool initialized;
    bool host_ready;
    bool paired;
    bool connected;
    bool encrypted;
    bool ready;
    bool scanning;
    bool busy;
    bool scan_match;
    bool trigger_discovered;
    uint8_t camera_address[CANON_PEER_ADDRESS_SIZE];
    uint8_t camera_address_type;
    uint8_t scan_address[CANON_PEER_ADDRESS_SIZE];
    uint8_t scan_address_type;
    uint8_t last_ble_error;
    uint16_t connection_handle;
    uint16_t service_start_handle;
    uint16_t service_end_handle;
    uint16_t characteristic_handle;
    operation_t operation;
    discovery_t discovery;
} canon_context_t;

static canon_context_t context = {
    .connection_handle = INVALID_CONNHANDLE,
};

static __attribute__((aligned(4))) uint8_t storage_page[CAMERA_RECORD_PAGE_SIZE];

static tmosEvents process_events(tmosTaskID task_id, tmosEvents events);
static void central_event_callback(gapRoleEvent_t *event);
static void pairing_state_callback(uint16_t connection_handle,
                                   uint8_t state, uint8_t status);
static void passcode_callback(uint8_t *device_address,
                              uint16_t connection_handle,
                              uint8_t ui_inputs, uint8_t ui_outputs);
static void rssi_callback(uint16_t connection_handle, int8_t rssi);
static void data_length_callback(uint16_t connection_handle,
                                 uint16_t max_tx_octets,
                                 uint16_t max_rx_octets);

static gapCentralRoleCB_t central_callbacks = {
    rssi_callback,
    central_event_callback,
    data_length_callback,
};

static gapBondCBs_t bond_callbacks = {
    passcode_callback,
    pairing_state_callback,
    NULL,
};

static const char *operation_name(operation_t operation)
{
    switch (operation) {
    case OPERATION_PAIR:
        return "Pairing";
    case OPERATION_CONNECT:
        return "Connection";
    case OPERATION_SHUTTER:
        return "Shutter";
    case OPERATION_FOCUS:
        return "Focus";
    case OPERATION_DISCONNECT:
        return "Disconnect";
    case OPERATION_FORGET:
        return "Forget";
    default:
        return "BLE operation";
    }
}

static uint16_t read_u16_le(const uint8_t bytes[2])
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U);
}

static void stop_operation_timeout(void)
{
    (void)tmos_stop_task(context.task_id, OPERATION_TIMEOUT_EVT);
}

static void finish_operation(void)
{
    const operation_t completed = context.operation;
    stop_operation_timeout();
    context.operation = OPERATION_NONE;
    context.busy = false;
    context.last_ble_error = SUCCESS;
    PRINT("\r\n%s succeeded\n", operation_name(completed));
}

static void fail_operation(uint8_t status, const char *reason)
{
    const operation_t failed = context.operation;
    stop_operation_timeout();
    (void)tmos_stop_task(context.task_id, START_GATT_DISCOVERY_EVT);
    (void)tmos_stop_task(context.task_id, BUTTON_RELEASE_EVT);
    context.last_ble_error = status;
    context.scanning = false;
    context.busy = false;
    context.operation = OPERATION_NONE;
    context.discovery = DISCOVERY_IDLE;
    context.ready = false;
    PRINT("\r\n%s failed: %s (WCH BLE 0x%02X)\n",
          operation_name(failed), reason, status);
    if (context.connected) {
        (void)GAPRole_TerminateLink(context.connection_handle);
    }
}

static bool load_camera_address(void)
{
    if (EEPROM_READ(CAMERA_RECORD_ADDRESS, storage_page,
                    CANON_PEER_RECORD_SIZE) != SUCCESS) {
        return false;
    }
    return canon_peer_record_decode(storage_page,
                                    &context.camera_address_type,
                                    context.camera_address);
}

static bool save_camera_address(void)
{
    memset(storage_page, 0xff, sizeof(storage_page));
    if (!canon_peer_record_encode(context.camera_address_type,
                                  context.camera_address, storage_page)) {
        return false;
    }
    if (EEPROM_ERASE(CAMERA_RECORD_ADDRESS,
                     CAMERA_RECORD_PAGE_SIZE) != SUCCESS) {
        return false;
    }
    if (EEPROM_WRITE(CAMERA_RECORD_ADDRESS, storage_page,
                     CANON_PEER_RECORD_SIZE) != SUCCESS) {
        return false;
    }
    context.paired = true;
    return true;
}

static bool erase_camera_address(void)
{
    if (EEPROM_ERASE(CAMERA_RECORD_ADDRESS,
                     CAMERA_RECORD_PAGE_SIZE) != SUCCESS) {
        return false;
    }
    memset(context.camera_address, 0, sizeof(context.camera_address));
    context.camera_address_type = 0U;
    context.paired = false;
    return true;
}

static bool advertisement_has_canon_service(const uint8_t *data,
                                            uint8_t data_length)
{
    uint8_t offset = 0U;
    while (offset < data_length) {
        const uint8_t field_length = data[offset];
        if (field_length == 0U ||
            (uint16_t)offset + (uint16_t)field_length + 1U > data_length) {
            break;
        }
        const uint8_t type = data[offset + 1U];
        if ((type == GAP_ADTYPE_128BIT_MORE ||
             type == GAP_ADTYPE_128BIT_COMPLETE) && field_length > 1U) {
            const uint8_t value_length = (uint8_t)(field_length - 1U);
            for (uint8_t uuid_offset = 0U;
                 uuid_offset + CANON_UUID_SIZE <= value_length;
                 uuid_offset = (uint8_t)(uuid_offset + CANON_UUID_SIZE)) {
                if (memcmp(&data[offset + 2U + uuid_offset],
                           canon_service_uuid_le, CANON_UUID_SIZE) == 0) {
                    return true;
                }
            }
        }
        offset = (uint8_t)(offset + field_length + 1U);
    }
    return false;
}

static void set_pairing_mode(uint8_t mode)
{
    (void)GAPBondMgr_SetParameter(GAPBOND_CENT_PAIRING_MODE,
                                  sizeof(mode), &mode);
}

static bStatus_t establish_link(const uint8_t address[6],
                                uint8_t address_type)
{
    context.ready = false;
    context.encrypted = false;
    context.trigger_discovered = false;
    context.characteristic_handle = 0U;
    context.service_start_handle = 0U;
    context.service_end_handle = 0U;
    context.discovery = DISCOVERY_IDLE;
    return GAPRole_CentralEstablishLink(FALSE, FALSE, address_type,
                                        (uint8_t *)address);
}

static bStatus_t start_characteristic_discovery(discovery_t discovery,
                                                const uint8_t uuid[16])
{
    attReadByTypeReq_t request;
    memset(&request, 0, sizeof(request));
    request.startHandle = context.service_start_handle;
    request.endHandle = context.service_end_handle;
    request.type.len = CANON_UUID_SIZE;
    memcpy(request.type.uuid, uuid, CANON_UUID_SIZE);

    context.discovery = discovery;
    context.characteristic_handle = 0U;
    return GATT_DiscCharsByUUID(context.connection_handle, &request,
                                context.task_id);
}

static bStatus_t start_service_discovery(void)
{
    uint8_t uuid[CANON_UUID_SIZE];
    memcpy(uuid, canon_service_uuid_le, sizeof(uuid));
    context.discovery = DISCOVERY_SERVICE;
    context.service_start_handle = 0U;
    context.service_end_handle = 0U;
    return GATT_DiscPrimaryServiceByUUID(context.connection_handle, uuid,
                                         sizeof(uuid), context.task_id);
}

static bStatus_t write_characteristic(uint16_t handle,
                                      const uint8_t *data, uint8_t length)
{
    attWriteReq_t request;
    memset(&request, 0, sizeof(request));
    request.cmd = TRUE;
    request.sig = FALSE;
    request.handle = handle;
    request.len = length;
    request.pValue = GATT_bm_alloc(context.connection_handle,
                                  ATT_WRITE_REQ, length, NULL, 0U);
    if (request.pValue == NULL) {
        return bleMemAllocError;
    }
    memcpy(request.pValue, data, length);
    const bStatus_t status =
        GATT_WriteNoRsp(context.connection_handle, &request);
    if (status != SUCCESS) {
        GATT_bm_free((gattMsg_t *)&request, ATT_WRITE_REQ);
    }
    return status;
}

static void send_pending_button(void)
{
    canon_button_t button = context.operation == OPERATION_FOCUS
                                ? CANON_BUTTON_FOCUS
                                : CANON_BUTTON_SHUTTER;
    const uint8_t press = canon_protocol_button_press(button);
    const bStatus_t status = write_characteristic(
        context.characteristic_handle, &press, sizeof(press));
    if (status != SUCCESS) {
        fail_operation(status, "button press write failed");
        return;
    }
    tmos_start_task(context.task_id, BUTTON_RELEASE_EVT,
                    BUTTON_HOLD_TICKS);
}

static void maybe_finish_control_connection(void)
{
    if (!context.trigger_discovered ||
        !linkDB_State(context.connection_handle, LINK_ENCRYPTED)) {
        return;
    }
    context.encrypted = true;
    context.ready = true;
    if (context.operation == OPERATION_PAIR ||
        context.operation == OPERATION_CONNECT) {
        finish_operation();
    } else if (context.operation == OPERATION_SHUTTER ||
               context.operation == OPERATION_FOCUS) {
        send_pending_button();
    }
}

static void pairing_characteristic_ready(void)
{
    canon_packet_t packet;
    if (!canon_protocol_make_pairing_packet(REMOTE_NAME, &packet)) {
        fail_operation(INVALIDPARAMETER, "pairing packet invalid");
        return;
    }
    const bStatus_t status = write_characteristic(
        context.characteristic_handle, packet.data, packet.length);
    if (status != SUCCESS) {
        fail_operation(status, "pairing write failed");
        return;
    }
    PRINT("\r\nCamera found; waiting for BLE security...\n");
}

static void characteristic_discovery_complete(void)
{
    const discovery_t completed = context.discovery;
    context.discovery = DISCOVERY_IDLE;
    if (context.characteristic_handle == 0U) {
        fail_operation(FAILURE, "Canon characteristic not found");
        return;
    }

    if (completed == DISCOVERY_PAIRING_CHARACTERISTIC) {
        pairing_characteristic_ready();
    } else {
        context.trigger_discovered = true;
        maybe_finish_control_connection();
    }
}

static void process_gatt_discovery(gattMsgEvent_t *message)
{
    if (context.discovery == DISCOVERY_SERVICE) {
        if (message->method == ATT_FIND_BY_TYPE_VALUE_RSP &&
            message->msg.findByTypeValueRsp.numInfo > 0U) {
            const uint8_t *handles =
                message->msg.findByTypeValueRsp.pHandlesInfo;
            context.service_start_handle = read_u16_le(&handles[0]);
            context.service_end_handle = read_u16_le(&handles[2]);
        }
        if ((message->method == ATT_FIND_BY_TYPE_VALUE_RSP &&
             message->hdr.status == bleProcedureComplete) ||
            message->method == ATT_ERROR_RSP) {
            context.discovery = DISCOVERY_IDLE;
            if (context.service_start_handle == 0U) {
                fail_operation(FAILURE, "Canon service not found");
                return;
            }
            const bool pairing = context.operation == OPERATION_PAIR;
            const bStatus_t status = start_characteristic_discovery(
                pairing ? DISCOVERY_PAIRING_CHARACTERISTIC
                        : DISCOVERY_TRIGGER_CHARACTERISTIC,
                pairing ? canon_pairing_uuid_le : canon_trigger_uuid_le);
            if (status != SUCCESS) {
                fail_operation(status, "characteristic discovery failed");
            }
        }
        return;
    }

    if (context.discovery == DISCOVERY_PAIRING_CHARACTERISTIC ||
        context.discovery == DISCOVERY_TRIGGER_CHARACTERISTIC) {
        if (message->method == ATT_READ_BY_TYPE_RSP &&
            message->msg.readByTypeRsp.numPairs > 0U &&
            message->msg.readByTypeRsp.len >= 5U) {
            const uint8_t *pair = message->msg.readByTypeRsp.pDataList;
            context.characteristic_handle = read_u16_le(&pair[3]);
        }
        if ((message->method == ATT_READ_BY_TYPE_RSP &&
             message->hdr.status == bleProcedureComplete) ||
            message->method == ATT_ERROR_RSP) {
            characteristic_discovery_complete();
        }
    }
}

static void process_stack_message(tmos_event_hdr_t *header)
{
    if (header->event != GATT_MSG_EVENT) {
        return;
    }
    gattMsgEvent_t *message = (gattMsgEvent_t *)header;
    if (context.connected && context.discovery != DISCOVERY_IDLE) {
        process_gatt_discovery(message);
    }
    GATT_bm_free(&message->msg, message->method);
}

static tmosEvents process_events(tmosTaskID task_id, tmosEvents events)
{
    (void)task_id;
    if ((events & SYS_EVENT_MSG) != 0U) {
        uint8_t *message = tmos_msg_receive(context.task_id);
        if (message != NULL) {
            process_stack_message((tmos_event_hdr_t *)message);
            tmos_msg_deallocate(message);
        }
        return events ^ SYS_EVENT_MSG;
    }
    if ((events & START_DEVICE_EVT) != 0U) {
        const bStatus_t status = GAPRole_CentralStartDevice(
            context.task_id, &bond_callbacks, &central_callbacks);
        if (status != SUCCESS) {
            context.last_ble_error = status;
            PRINT("\r\nBLE host start failed: 0x%02X\n", status);
        }
        return events ^ START_DEVICE_EVT;
    }
    if ((events & START_GATT_DISCOVERY_EVT) != 0U) {
        const bStatus_t status = start_service_discovery();
        if (status != SUCCESS) {
            fail_operation(status, "service discovery failed");
        }
        return events ^ START_GATT_DISCOVERY_EVT;
    }
    if ((events & BUTTON_RELEASE_EVT) != 0U) {
        const uint8_t release = canon_protocol_button_release();
        const bStatus_t status = write_characteristic(
            context.characteristic_handle, &release, sizeof(release));
        if (status == SUCCESS) {
            finish_operation();
        } else {
            fail_operation(status, "button release write failed");
        }
        return events ^ BUTTON_RELEASE_EVT;
    }
    if ((events & OPERATION_TIMEOUT_EVT) != 0U) {
        if (context.busy) {
            if (context.scanning) {
                (void)GAPRole_CentralCancelDiscovery();
            }
            const bool link_pending = !context.connected && !context.scanning;
            fail_operation(bleTimeout, "operation timed out");
            if (link_pending) {
                (void)GAPRole_TerminateLink(GAP_CONNHANDLE_INIT);
            }
        }
        return events ^ OPERATION_TIMEOUT_EVT;
    }
    return 0U;
}

static void start_control_discovery(void)
{
    tmos_start_task(context.task_id, START_GATT_DISCOVERY_EVT,
                    GATT_START_DELAY_TICKS);
}

static void handle_link_established(gapRoleEvent_t *event)
{
    if (!context.busy) {
        if (event->gap.hdr.status == SUCCESS) {
            (void)GAPRole_TerminateLink(event->linkCmpl.connectionHandle);
        }
        return;
    }
    if (event->gap.hdr.status != SUCCESS) {
        context.connected = false;
        context.connection_handle = INVALID_CONNHANDLE;
        fail_operation(event->gap.hdr.status, "connection failed");
        return;
    }
    context.connected = true;
    context.encrypted = false;
    context.ready = false;
    context.connection_handle = event->linkCmpl.connectionHandle;
    memcpy(context.scan_address, event->linkCmpl.devAddr,
           sizeof(context.scan_address));
    context.scan_address_type = event->linkCmpl.devAddrType;
    start_control_discovery();
}

static void handle_link_terminated(gapRoleEvent_t *event)
{
    (void)event;
    context.connected = false;
    context.encrypted = false;
    context.ready = false;
    context.trigger_discovered = false;
    context.connection_handle = INVALID_CONNHANDLE;
    context.discovery = DISCOVERY_IDLE;

    if (context.operation == OPERATION_DISCONNECT) {
        finish_operation();
    } else if (context.operation == OPERATION_FORGET) {
        (void)GAPBondMgr_SetParameter(GAPBOND_ERASE_ALLBONDS, 0U, NULL);
        if (erase_camera_address()) {
            finish_operation();
        } else {
            fail_operation(FAILURE, "camera record erase failed");
        }
    } else if (context.busy) {
        fail_operation(bleNotConnected, "camera disconnected");
    }
}

static void central_event_callback(gapRoleEvent_t *event)
{
    switch (event->gap.opcode) {
    case GAP_DEVICE_INIT_DONE_EVENT:
        context.host_ready = event->gap.hdr.status == SUCCESS;
        context.last_ble_error = event->gap.hdr.status;
        PRINT("\r\nBLE host %s\n",
              context.host_ready ? "ready" : "failed to start");
        break;

    case GAP_DEVICE_INFO_EVENT:
        if (context.scanning &&
            advertisement_has_canon_service(event->deviceInfo.pEvtData,
                                            event->deviceInfo.dataLen)) {
            memcpy(context.scan_address, event->deviceInfo.addr,
                   sizeof(context.scan_address));
            context.scan_address_type = event->deviceInfo.addrType;
            context.scan_match = true;
            (void)GAPRole_CentralCancelDiscovery();
        }
        break;

    case GAP_DEVICE_DISCOVERY_EVENT:
        context.scanning = false;
        if (!context.busy || context.operation != OPERATION_PAIR) {
            break;
        }
        if (!context.scan_match) {
            fail_operation(FAILURE, "Canon camera not found");
            break;
        }
        if (establish_link(context.scan_address,
                           context.scan_address_type) != SUCCESS) {
            fail_operation(FAILURE, "could not start connection");
        }
        break;

    case GAP_LINK_ESTABLISHED_EVENT:
        handle_link_established(event);
        break;

    case GAP_LINK_TERMINATED_EVENT:
        handle_link_terminated(event);
        break;

    default:
        break;
    }
}

static void pairing_state_callback(uint16_t connection_handle,
                                   uint8_t state, uint8_t status)
{
    (void)connection_handle;
    const bool pair_complete = state == GAPBOND_PAIRING_STATE_COMPLETE;
    const bool bonded_reconnect = state == GAPBOND_PAIRING_STATE_BONDED &&
                                  context.operation != OPERATION_PAIR;
    if (!pair_complete && !bonded_reconnect) {
        return;
    }
    if (status != SUCCESS) {
        if (context.busy) {
            fail_operation(status, "BLE security failed");
        }
        return;
    }

    context.encrypted = true;
    if (context.operation == OPERATION_PAIR && pair_complete) {
        memcpy(context.camera_address, context.scan_address,
               sizeof(context.camera_address));
        context.camera_address_type = context.scan_address_type;
        if (!save_camera_address()) {
            fail_operation(FAILURE, "camera record save failed");
            return;
        }
        const bStatus_t discovery_status = start_characteristic_discovery(
            DISCOVERY_TRIGGER_CHARACTERISTIC, canon_trigger_uuid_le);
        if (discovery_status != SUCCESS) {
            fail_operation(discovery_status,
                           "trigger discovery failed after pairing");
        }
    } else {
        maybe_finish_control_connection();
    }
}

static void passcode_callback(uint8_t *device_address,
                              uint16_t connection_handle,
                              uint8_t ui_inputs, uint8_t ui_outputs)
{
    (void)device_address;
    (void)ui_inputs;
    (void)ui_outputs;
    GAPBondMgr_PasscodeRsp(connection_handle, SUCCESS, 0U);
}

static void rssi_callback(uint16_t connection_handle, int8_t rssi)
{
    (void)connection_handle;
    (void)rssi;
}

static void data_length_callback(uint16_t connection_handle,
                                 uint16_t max_tx_octets,
                                 uint16_t max_rx_octets)
{
    (void)connection_handle;
    (void)max_tx_octets;
    (void)max_rx_octets;
}

static canon_wch_result_t begin_operation(operation_t operation,
                                          uint32_t timeout_seconds)
{
    if (!context.initialized || !context.host_ready) {
        return CANON_WCH_NOT_READY;
    }
    if (context.busy) {
        return CANON_WCH_BUSY;
    }
    context.operation = operation;
    context.busy = true;
    context.last_ble_error = SUCCESS;
    tmos_start_task(context.task_id, OPERATION_TIMEOUT_EVT,
                    timeout_seconds * 1600U);
    return CANON_WCH_OK;
}

static canon_wch_result_t begin_control(operation_t operation)
{
    if (!context.paired) {
        return CANON_WCH_NOT_PAIRED;
    }
    const canon_wch_result_t result =
        begin_operation(operation, OPERATION_GRACE_SECONDS);
    if (result != CANON_WCH_OK) {
        return result;
    }

    if (context.ready && context.connected && context.encrypted) {
        if (operation == OPERATION_CONNECT) {
            finish_operation();
        } else {
            send_pending_button();
        }
        return CANON_WCH_OK;
    }

    set_pairing_mode(GAPBOND_PAIRING_MODE_INITIATE);
    if (context.connected) {
        start_control_discovery();
    } else {
        const bStatus_t status = establish_link(context.camera_address,
                                                context.camera_address_type);
        if (status != SUCCESS) {
            fail_operation(status, "could not start connection");
            return CANON_WCH_STACK_ERROR;
        }
    }
    return CANON_WCH_OK;
}

void canon_ble_wch_init(void)
{
    context.paired = load_camera_address();
    context.initialized = true;
    context.task_id = TMOS_ProcessEventRegister(process_events);

    GAP_SetParamValue(TGAP_CONN_EST_INT_MIN, 20U);
    GAP_SetParamValue(TGAP_CONN_EST_INT_MAX, 100U);
    GAP_SetParamValue(TGAP_CONN_EST_SUPERV_TIMEOUT, 200U);

    uint32_t passkey = 0U;
    uint8_t mitm = FALSE;
    uint8_t secure_connections = TRUE;
    uint8_t auto_sync_resolving_list = TRUE;
    uint8_t io_capability = GAPBOND_IO_CAP_NO_INPUT_NO_OUTPUT;
    uint8_t bonding = TRUE;
    uint8_t pairing_mode = GAPBOND_PAIRING_MODE_WAIT_FOR_REQ;
    GAPBondMgr_SetParameter(GAPBOND_CENT_DEFAULT_PASSCODE,
                            sizeof(passkey), &passkey);
    GAPBondMgr_SetParameter(GAPBOND_CENT_PAIRING_MODE,
                            sizeof(pairing_mode), &pairing_mode);
    GAPBondMgr_SetParameter(GAPBOND_CENT_MITM_PROTECTION,
                            sizeof(mitm), &mitm);
    GAPBondMgr_SetParameter(GAPBOND_CENT_IO_CAPABILITIES,
                            sizeof(io_capability), &io_capability);
    GAPBondMgr_SetParameter(GAPBOND_CENT_BONDING_ENABLED,
                            sizeof(bonding), &bonding);
    GAPBondMgr_SetParameter(GAPBOND_CENT_SC_PROTECTION,
                            sizeof(secure_connections), &secure_connections);
    GAPBondMgr_SetParameter(GAPBOND_AUTO_SYNC_RL,
                            sizeof(auto_sync_resolving_list),
                            &auto_sync_resolving_list);

    GATT_InitClient();
    GATT_RegisterForInd(context.task_id);
    tmos_set_event(context.task_id, START_DEVICE_EVT);
}

canon_wch_result_t canon_ble_wch_pair(uint8_t scan_seconds)
{
    if (scan_seconds == 0U || scan_seconds > 40U) {
        return CANON_WCH_INVALID_ARGUMENT;
    }
    if (context.connected) {
        return CANON_WCH_BUSY;
    }
    const canon_wch_result_t result = begin_operation(
        OPERATION_PAIR, (uint32_t)scan_seconds + OPERATION_GRACE_SECONDS);
    if (result != CANON_WCH_OK) {
        return result;
    }

    context.scan_match = false;
    context.scanning = true;
    set_pairing_mode(GAPBOND_PAIRING_MODE_WAIT_FOR_REQ);
    GAP_SetParamValue(TGAP_DISC_SCAN, (uint16_t)scan_seconds * 1600U);
    const bStatus_t status = GAPRole_CentralStartDiscovery(
        DEVDISC_MODE_ALL, TRUE, FALSE);
    if (status != SUCCESS) {
        context.scanning = false;
        fail_operation(status, "could not start scan");
        return CANON_WCH_STACK_ERROR;
    }
    return CANON_WCH_OK;
}

canon_wch_result_t canon_ble_wch_connect(void)
{
    return begin_control(OPERATION_CONNECT);
}

canon_wch_result_t canon_ble_wch_disconnect(void)
{
    const canon_wch_result_t result = begin_operation(
        OPERATION_DISCONNECT, OPERATION_GRACE_SECONDS);
    if (result != CANON_WCH_OK) {
        return result;
    }
    if (!context.connected) {
        finish_operation();
        return CANON_WCH_OK;
    }
    const bStatus_t status =
        GAPRole_TerminateLink(context.connection_handle);
    if (status != SUCCESS) {
        fail_operation(status, "disconnect request failed");
        return CANON_WCH_STACK_ERROR;
    }
    return CANON_WCH_OK;
}

canon_wch_result_t canon_ble_wch_forget(void)
{
    const canon_wch_result_t result = begin_operation(
        OPERATION_FORGET, OPERATION_GRACE_SECONDS);
    if (result != CANON_WCH_OK) {
        return result;
    }
    if (context.connected) {
        const bStatus_t status =
            GAPRole_TerminateLink(context.connection_handle);
        if (status != SUCCESS) {
            fail_operation(status, "disconnect before erase failed");
            return CANON_WCH_STACK_ERROR;
        }
        return CANON_WCH_OK;
    }
    GAPBondMgr_SetParameter(GAPBOND_ERASE_ALLBONDS, 0U, NULL);
    if (!erase_camera_address()) {
        fail_operation(FAILURE, "camera record erase failed");
        return CANON_WCH_STACK_ERROR;
    }
    finish_operation();
    return CANON_WCH_OK;
}

canon_wch_result_t canon_ble_wch_shutter(void)
{
    return begin_control(OPERATION_SHUTTER);
}

canon_wch_result_t canon_ble_wch_focus(void)
{
    return begin_control(OPERATION_FOCUS);
}

void canon_ble_wch_get_status(canon_wch_status_t *status)
{
    if (status == NULL) {
        return;
    }
    status->initialized = context.initialized;
    status->host_ready = context.host_ready;
    status->paired = context.paired;
    status->connected = context.connected;
    status->encrypted = context.encrypted;
    status->ready = context.ready;
    status->scanning = context.scanning;
    status->busy = context.busy;
    memcpy(status->camera_address, context.camera_address,
           sizeof(status->camera_address));
    status->camera_address_type = context.camera_address_type;
    status->last_ble_error = context.last_ble_error;
}

const char *canon_ble_wch_result_name(canon_wch_result_t result)
{
    switch (result) {
    case CANON_WCH_OK:
        return "ok";
    case CANON_WCH_BUSY:
        return "another BLE operation is active";
    case CANON_WCH_NOT_READY:
        return "BLE host is not ready";
    case CANON_WCH_NOT_PAIRED:
        return "no camera is paired";
    case CANON_WCH_INVALID_ARGUMENT:
        return "invalid argument";
    case CANON_WCH_STACK_ERROR:
        return "WCH BLE stack rejected the operation";
    default:
        return "unknown error";
    }
}
