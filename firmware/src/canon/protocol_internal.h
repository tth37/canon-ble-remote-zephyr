#ifndef CANON_PROTOCOL_INTERNAL_H
#define CANON_PROTOCOL_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CANON_UUID_SIZE 16U
#define CANON_REMOTE_NAME_MAX 18U
#define CANON_PACKET_MAX_SIZE (CANON_REMOTE_NAME_MAX + 2U)
#define CANON_PEER_ADDRESS_SIZE 6U
#define CANON_PEER_RECORD_SIZE 16U

/* Bluetooth UUID byte order, suitable for NimBLE and Zephyr GATT APIs. */
#define CANON_SERVICE_UUID_LE_BYTES                                         \
    0x21, 0xa8, 0xff, 0x2f, 0x49, 0xd8, 0x00, 0x00,                      \
        0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00
#define CANON_PAIRING_UUID_LE_BYTES                                         \
    0x21, 0xa8, 0xff, 0x2f, 0x49, 0xd8, 0x00, 0x00,                      \
        0x00, 0x10, 0x00, 0x00, 0x02, 0x00, 0x05, 0x00
#define CANON_TRIGGER_UUID_LE_BYTES                                         \
    0x21, 0xa8, 0xff, 0x2f, 0x49, 0xd8, 0x00, 0x00,                      \
        0x00, 0x10, 0x00, 0x00, 0x03, 0x00, 0x05, 0x00

typedef enum {
    CANON_BUTTON_SHUTTER,
    CANON_BUTTON_FOCUS,
} canon_button_t;

typedef struct {
    uint8_t data[CANON_PACKET_MAX_SIZE];
    uint8_t length;
} canon_packet_t;

bool canon_protocol_make_pairing_packet(const char *remote_name,
                                        canon_packet_t *packet);
uint8_t canon_protocol_button_press(canon_button_t button);
uint8_t canon_protocol_button_release(void);
uint8_t canon_protocol_button_state(bool focus_pressed,
                                    bool shutter_pressed);

/* Stable storage encoding; never persist a vendor BLE address structure. */
bool canon_peer_record_encode(uint8_t address_type,
                              const uint8_t address[CANON_PEER_ADDRESS_SIZE],
                              uint8_t record[CANON_PEER_RECORD_SIZE]);
bool canon_peer_record_decode(const uint8_t record[CANON_PEER_RECORD_SIZE],
                              uint8_t *address_type,
                              uint8_t address[CANON_PEER_ADDRESS_SIZE]);

#endif
