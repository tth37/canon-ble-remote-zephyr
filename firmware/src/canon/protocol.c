#include "protocol_internal.h"

#include <string.h>

#define CANON_PAIRING_PREFIX 0x03U
#define CANON_CONTROL_MODE_IMMEDIATE 0x0cU
#define CANON_CONTROL_SHUTTER 0x80U
#define CANON_CONTROL_FOCUS 0x40U
#define CANON_PEER_RECORD_VERSION 1U

static size_t bounded_string_length(const char *text, size_t limit)
{
    size_t length = 0;
    while (length < limit && text[length] != '\0') {
        ++length;
    }
    return length;
}

bool canon_protocol_make_pairing_packet(const char *remote_name,
                                        canon_packet_t *packet)
{
    if (remote_name == NULL || packet == NULL) {
        return false;
    }

    const size_t name_length =
        bounded_string_length(remote_name, CANON_REMOTE_NAME_MAX + 1U);
    if (name_length == 0U || name_length > CANON_REMOTE_NAME_MAX) {
        return false;
    }

    packet->data[0] = CANON_PAIRING_PREFIX;
    memcpy(&packet->data[1], remote_name, name_length);
    packet->data[name_length + 1U] = '\0';
    packet->length = (uint8_t)(name_length + 2U);
    return true;
}

uint8_t canon_protocol_button_press(canon_button_t button)
{
    return canon_protocol_button_state(button == CANON_BUTTON_FOCUS,
                                       button == CANON_BUTTON_SHUTTER);
}

uint8_t canon_protocol_button_release(void)
{
    return canon_protocol_button_state(false, false);
}

uint8_t canon_protocol_button_state(bool focus_pressed,
                                    bool shutter_pressed)
{
    uint8_t state = CANON_CONTROL_MODE_IMMEDIATE;
    if (focus_pressed) {
        state |= CANON_CONTROL_FOCUS;
    }
    if (shutter_pressed) {
        state |= CANON_CONTROL_SHUTTER;
    }
    return state;
}

static uint32_t record_checksum(const uint8_t *bytes, size_t length)
{
    uint32_t hash = 2166136261U;
    for (size_t index = 0; index < length; ++index) {
        hash ^= bytes[index];
        hash *= 16777619U;
    }
    return hash;
}

static void write_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

static uint32_t read_u32_le(const uint8_t *source)
{
    return (uint32_t)source[0] | ((uint32_t)source[1] << 8U) |
           ((uint32_t)source[2] << 16U) | ((uint32_t)source[3] << 24U);
}

bool canon_peer_record_encode(uint8_t address_type,
                              const uint8_t address[CANON_PEER_ADDRESS_SIZE],
                              uint8_t record[CANON_PEER_RECORD_SIZE])
{
    if (address == NULL || record == NULL) {
        return false;
    }

    record[0] = 'C';
    record[1] = 'B';
    record[2] = 'R';
    record[3] = '1';
    record[4] = CANON_PEER_RECORD_VERSION;
    record[5] = address_type;
    memcpy(&record[6], address, CANON_PEER_ADDRESS_SIZE);
    write_u32_le(&record[12], record_checksum(record, 12U));
    return true;
}

bool canon_peer_record_decode(const uint8_t record[CANON_PEER_RECORD_SIZE],
                              uint8_t *address_type,
                              uint8_t address[CANON_PEER_ADDRESS_SIZE])
{
    if (record == NULL || address_type == NULL || address == NULL ||
        record[0] != 'C' || record[1] != 'B' || record[2] != 'R' ||
        record[3] != '1' || record[4] != CANON_PEER_RECORD_VERSION ||
        read_u32_le(&record[12]) != record_checksum(record, 12U)) {
        return false;
    }

    *address_type = record[5];
    memcpy(address, &record[6], CANON_PEER_ADDRESS_SIZE);
    return true;
}
