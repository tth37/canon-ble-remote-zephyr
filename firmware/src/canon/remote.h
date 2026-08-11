#ifndef CANON_REMOTE_H
#define CANON_REMOTE_H

#include <stdbool.h>
#include <stdint.h>

#define CANON_REMOTE_ADDRESS_TEXT_SIZE 30U

typedef enum {
    CANON_REMOTE_OK = 0,
    CANON_REMOTE_BUSY,
    CANON_REMOTE_NOT_READY,
    CANON_REMOTE_NOT_PAIRED,
    CANON_REMOTE_NOT_FOUND,
    CANON_REMOTE_TIMEOUT,
    CANON_REMOTE_CANCELLED,
    CANON_REMOTE_INVALID_ARGUMENT,
    CANON_REMOTE_STACK_ERROR,
} canon_remote_result_t;

typedef enum {
    CANON_REMOTE_BUTTON_FOCUS = 0,
    CANON_REMOTE_BUTTON_SHUTTER,
} canon_remote_button_t;

typedef struct {
    bool initialized;
    bool host_ready;
    bool paired;
    bool connected;
    bool encrypted;
    bool ready;
    bool scanning;
    bool busy;
    bool focus_requested;
    bool shutter_requested;
    bool focus_applied;
    bool shutter_applied;
    char camera_address[CANON_REMOTE_ADDRESS_TEXT_SIZE];
    int last_ble_error;
} canon_remote_status_t;

canon_remote_result_t canon_remote_initialize(void);
canon_remote_result_t canon_remote_pair(uint32_t scan_seconds);
canon_remote_result_t canon_remote_connect(void);
canon_remote_result_t canon_remote_disconnect(void);
canon_remote_result_t canon_remote_forget(void);
canon_remote_result_t canon_remote_shutter(void);
canon_remote_result_t canon_remote_focus(void);

/*
 * Queue a physical button state without blocking. This function only uses
 * atomic operations and wakes a dedicated worker, so GPIO callbacks may call
 * it directly, including from interrupt context. Debouncing belongs in the
 * board-specific GPIO layer.
 */
canon_remote_result_t canon_remote_set_button(canon_remote_button_t button,
                                              bool pressed);
void canon_remote_get_status(canon_remote_status_t *status);
const char *canon_remote_result_name(canon_remote_result_t result);

#endif
