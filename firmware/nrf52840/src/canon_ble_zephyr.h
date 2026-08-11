#ifndef CANON_BLE_ZEPHYR_H
#define CANON_BLE_ZEPHYR_H

#include <stdbool.h>
#include <stdint.h>

#define CANON_ZEPHYR_ADDRESS_TEXT_SIZE 30U

typedef enum {
    CANON_ZEPHYR_OK = 0,
    CANON_ZEPHYR_BUSY,
    CANON_ZEPHYR_NOT_READY,
    CANON_ZEPHYR_NOT_PAIRED,
    CANON_ZEPHYR_NOT_FOUND,
    CANON_ZEPHYR_TIMEOUT,
    CANON_ZEPHYR_INVALID_ARGUMENT,
    CANON_ZEPHYR_STACK_ERROR,
} canon_zephyr_result_t;

typedef struct {
    bool initialized;
    bool host_ready;
    bool paired;
    bool connected;
    bool encrypted;
    bool ready;
    bool scanning;
    bool busy;
    char camera_address[CANON_ZEPHYR_ADDRESS_TEXT_SIZE];
    int last_ble_error;
} canon_zephyr_status_t;

canon_zephyr_result_t canon_ble_zephyr_initialize(void);
canon_zephyr_result_t canon_ble_zephyr_pair(uint32_t scan_seconds);
canon_zephyr_result_t canon_ble_zephyr_connect(void);
canon_zephyr_result_t canon_ble_zephyr_disconnect(void);
canon_zephyr_result_t canon_ble_zephyr_forget(void);
canon_zephyr_result_t canon_ble_zephyr_shutter(void);
canon_zephyr_result_t canon_ble_zephyr_focus(void);
void canon_ble_zephyr_get_status(canon_zephyr_status_t *status);
const char *canon_ble_zephyr_result_name(canon_zephyr_result_t result);

#endif

