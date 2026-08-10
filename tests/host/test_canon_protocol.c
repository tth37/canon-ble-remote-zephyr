#include "canon_protocol.h"

#include <assert.h>
#include <string.h>

static void test_pairing_packet(void)
{
    canon_packet_t packet;
    assert(canon_protocol_make_pairing_packet("ESP32 Remote", &packet));
    static const uint8_t expected[] = {
        0x03U, 'E', 'S', 'P', '3', '2', ' ', 'R',
        'e',   'm', 'o', 't', 'e', 0x00U,
    };
    assert(packet.length == sizeof(expected));
    assert(memcmp(packet.data, expected, sizeof(expected)) == 0);
    assert(!canon_protocol_make_pairing_packet("", &packet));
    assert(!canon_protocol_make_pairing_packet(
        "this remote name is much too long", &packet));
}

static void test_button_packets(void)
{
    assert(canon_protocol_button_press(CANON_BUTTON_SHUTTER) == 0x8cU);
    assert(canon_protocol_button_press(CANON_BUTTON_FOCUS) == 0x4cU);
    assert(canon_protocol_button_release() == 0x0cU);
}

static void test_peer_record(void)
{
    static const uint8_t address[CANON_PEER_ADDRESS_SIZE] = {
        0x10U, 0x20U, 0x30U, 0x40U, 0x50U, 0x60U,
    };
    uint8_t record[CANON_PEER_RECORD_SIZE];
    assert(canon_peer_record_encode(2U, address, record));

    uint8_t decoded_type = 0U;
    uint8_t decoded_address[CANON_PEER_ADDRESS_SIZE] = {0};
    assert(canon_peer_record_decode(record, &decoded_type, decoded_address));
    assert(decoded_type == 2U);
    assert(memcmp(address, decoded_address, sizeof(address)) == 0);

    record[7] ^= 0x01U;
    assert(!canon_peer_record_decode(record, &decoded_type, decoded_address));
}

int main(void)
{
    test_pairing_packet();
    test_button_packets();
    test_peer_record();
    return 0;
}
