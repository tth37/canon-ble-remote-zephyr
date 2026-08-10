#!/usr/bin/env python3
"""Emulate the Canon BLE remote GATT service on a desktop computer."""

from __future__ import annotations

import argparse
import asyncio
import logging
import signal
import sys
from dataclasses import dataclass
from datetime import datetime
from typing import Callable

from bless import (
    BlessGATTCharacteristic,
    BlessServer,
    GATTAttributePermissions,
    GATTCharacteristicProperties,
)


CANON_SERVICE_UUID = "00050000-0000-1000-0000-d8492fffa821"
CANON_PAIRING_UUID = "00050002-0000-1000-0000-d8492fffa821"
CANON_TRIGGER_UUID = "00050003-0000-1000-0000-d8492fffa821"

EXPECTED_PAIRING_PAYLOAD = b"\x03ESP32 Remote\x00"
SHUTTER_PRESS = b"\x8c"
FOCUS_PRESS = b"\x4c"
BUTTON_RELEASE = b"\x0c"


def timestamped(message: str) -> None:
    """Print a line immediately so BLE events are easy to follow."""
    now = datetime.now().astimezone().strftime("%H:%M:%S")
    print(f"[{now}] {message}", flush=True)


@dataclass
class ProtocolStats:
    pairing_requests: int = 0
    shutter_presses: int = 0
    focus_presses: int = 0
    releases: int = 0
    unexpected_writes: int = 0


class CanonProtocol:
    """Validate Canon protocol writes and retain a small test summary."""

    def __init__(self, emit: Callable[[str], None] = timestamped) -> None:
        self.emit = emit
        self.stats = ProtocolStats()
        self.pending_button: str | None = None

    def handle_write(
        self, characteristic: BlessGATTCharacteristic, value: bytes
    ) -> None:
        """Bless callback for writes from the ESP32-C6 central."""
        self.handle_payload(characteristic.uuid, bytes(value))

    def handle_payload(self, characteristic_uuid: str, payload: bytes) -> None:
        """Decode one write; separated from Bless so it can be self-tested."""
        normalized_uuid = characteristic_uuid.lower()
        if normalized_uuid == CANON_PAIRING_UUID:
            self._handle_pairing(payload)
        elif normalized_uuid == CANON_TRIGGER_UUID:
            self._handle_trigger(payload)
        else:
            self._unexpected(normalized_uuid, payload, "unknown characteristic")

    def _handle_pairing(self, payload: bytes) -> None:
        if payload == EXPECTED_PAIRING_PAYLOAD:
            self.stats.pairing_requests += 1
            self.emit("PAIR OK: received device name 'ESP32 Remote'")
            return
        self._unexpected(CANON_PAIRING_UUID, payload, "invalid pairing payload")

    def _handle_trigger(self, payload: bytes) -> None:
        if payload == SHUTTER_PRESS:
            self.stats.shutter_presses += 1
            self.pending_button = "shutter"
            self.emit("SHUTTER PRESS OK (0x8C)")
        elif payload == FOCUS_PRESS:
            self.stats.focus_presses += 1
            self.pending_button = "focus"
            self.emit("FOCUS PRESS OK (0x4C)")
        elif payload == BUTTON_RELEASE:
            self.stats.releases += 1
            if self.pending_button is None:
                self.emit("BUTTON RELEASE received without a preceding press (0x0C)")
            else:
                self.emit(f"{self.pending_button.upper()} RELEASE OK (0x0C)")
                self.pending_button = None
        else:
            self._unexpected(CANON_TRIGGER_UUID, payload, "unknown trigger payload")

    def _unexpected(self, uuid: str, payload: bytes, reason: str) -> None:
        self.stats.unexpected_writes += 1
        rendered = payload.hex(" ") or "<empty>"
        self.emit(f"UNEXPECTED: {reason}; uuid={uuid}, bytes={rendered}")

    def summary(self) -> str:
        return (
            "pairing={0.pairing_requests}, shutter={0.shutter_presses}, "
            "focus={0.focus_presses}, releases={0.releases}, "
            "unexpected={0.unexpected_writes}"
        ).format(self.stats)


def run_self_test() -> None:
    """Exercise the protocol decoder without requiring Bluetooth hardware."""
    messages: list[str] = []
    protocol = CanonProtocol(messages.append)
    protocol.handle_payload(CANON_PAIRING_UUID, EXPECTED_PAIRING_PAYLOAD)
    protocol.handle_payload(CANON_TRIGGER_UUID, SHUTTER_PRESS)
    protocol.handle_payload(CANON_TRIGGER_UUID, BUTTON_RELEASE)
    protocol.handle_payload(CANON_TRIGGER_UUID, FOCUS_PRESS)
    protocol.handle_payload(CANON_TRIGGER_UUID, BUTTON_RELEASE)

    assert protocol.stats == ProtocolStats(1, 1, 1, 2, 0)
    assert protocol.pending_button is None
    assert len(messages) == 5
    print(f"Self-test passed: {protocol.summary()}")


async def add_canon_gatt(server: BlessServer, require_encryption: bool) -> None:
    """Publish the Canon service and its two writable characteristics."""
    write_properties = (
        GATTCharacteristicProperties.write
        | GATTCharacteristicProperties.write_without_response
    )
    await server.add_new_service(CANON_SERVICE_UUID)
    pairing_permissions = GATTAttributePermissions.writeable
    if require_encryption:
        # CoreBluetooth only permits bonding after a central accesses an
        # encryption-protected attribute. Protecting the pairing write makes
        # macOS initiate its system-managed security procedure.
        pairing_permissions |= GATTAttributePermissions.write_encryption_required
    await server.add_new_characteristic(
        CANON_SERVICE_UUID,
        CANON_PAIRING_UUID,
        write_properties,
        None,
        pairing_permissions,
    )

    trigger_permissions = GATTAttributePermissions.writeable
    if require_encryption:
        trigger_permissions |= GATTAttributePermissions.write_encryption_required
    await server.add_new_characteristic(
        CANON_SERVICE_UUID,
        CANON_TRIGGER_UUID,
        write_properties,
        None,
        trigger_permissions,
    )


async def run_server(name: str, require_encryption: bool) -> None:
    """Advertise until interrupted and print every protocol event."""
    protocol = CanonProtocol()
    server = BlessServer(name=name)
    server.write_request_func = protocol.handle_write
    await add_canon_gatt(server, require_encryption)

    stop_event = asyncio.Event()
    loop = asyncio.get_running_loop()
    for handled_signal in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(handled_signal, stop_event.set)
        except NotImplementedError:
            pass

    timestamped("Starting Canon BLE mock...")
    # macOS otherwise drops 128-bit service UUIDs when a long local name is
    # prioritized. Explicitly prioritize service discovery instead.
    if sys.platform == "darwin":
        await server.start(prioritize_local_name=False)
    else:
        await server.start()

    security = "encryption required" if require_encryption else "mock/unsecured"
    timestamped(f"Advertising as '{name}' ({security})")
    timestamped(f"Service: {CANON_SERVICE_UUID}")
    timestamped("Run `camera mock-pair 20` on the C6. Stop with Ctrl+C.")
    try:
        await stop_event.wait()
    finally:
        timestamped("Stopping advertisement...")
        await server.stop()
        timestamped(f"Summary: {protocol.summary()}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Emulate a Canon BLE camera for ESP32-C6 command testing."
    )
    parser.add_argument(
        "--name",
        default="CanonMock",
        help="advertised BLE name (keep it short on macOS)",
    )
    parser.add_argument(
        "--require-encryption",
        action="store_true",
        help="experimental CoreBluetooth encryption (normal mock mode is unsecured)",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="enable Bless/CoreBluetooth debug logging",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="test payload decoding without starting Bluetooth",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        run_self_test()
        return 0
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.WARNING)
    try:
        asyncio.run(
            run_server(
                name=args.name,
                require_encryption=args.require_encryption,
            )
        )
    except KeyboardInterrupt:
        pass
    except Exception as error:  # Surface platform Bluetooth failures clearly.
        print(f"Canon mock failed: {type(error).__name__}: {error}", file=sys.stderr)
        if sys.platform == "darwin":
            print(
                "Check System Settings > Privacy & Security > Bluetooth and "
                "allow your terminal/Python process.",
                file=sys.stderr,
            )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
