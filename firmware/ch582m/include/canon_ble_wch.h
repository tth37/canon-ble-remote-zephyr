#ifndef CANON_BLE_WCH_H
#define CANON_BLE_WCH_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CANON_WCH_OK = 0,
    CANON_WCH_BUSY,
    CANON_WCH_NOT_READY,
    CANON_WCH_NOT_PAIRED,
    CANON_WCH_INVALID_ARGUMENT,
    CANON_WCH_STACK_ERROR,
} canon_wch_result_t;

typedef struct {
    bool initialized;
    bool host_ready;
    bool paired;
    bool connected;
    bool encrypted;
    bool ready;
    bool scanning;
    bool busy;
    uint8_t camera_address[6];
    uint8_t camera_address_type;
    uint8_t last_ble_error;
} canon_wch_status_t;

void canon_ble_wch_init(void);
canon_wch_result_t canon_ble_wch_pair(uint8_t scan_seconds);
canon_wch_result_t canon_ble_wch_connect(void);
canon_wch_result_t canon_ble_wch_disconnect(void);
canon_wch_result_t canon_ble_wch_forget(void);
canon_wch_result_t canon_ble_wch_shutter(void);
canon_wch_result_t canon_ble_wch_focus(void);
void canon_ble_wch_get_status(canon_wch_status_t *status);
const char *canon_ble_wch_result_name(canon_wch_result_t result);

#endif
