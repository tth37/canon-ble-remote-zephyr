"""Apply the ESP-IDF 6 NimBLE central-connect compatibility patch."""

from pathlib import Path

Import("env")  # type: ignore[name-defined]  # Provided by PlatformIO/SCons.


NIMBLE_GAP_SOURCE = Path(
    "components/bt/host/nimble/nimble/nimble/host/src/ble_gap.c"
)

ORIGINAL_DECLARATION = """#if NIMBLE_BLE_CONNECT
    struct ble_hs_conn *conn;
    int rc;
    bool master_match = false;
"""
PATCHED_DECLARATION = """#if NIMBLE_BLE_CONNECT
    struct ble_gap_event event;
    struct ble_hs_conn *conn;
    int rc;
    bool master_match = false;
"""

ORIGINAL_CONNECT_DISPATCH = """    if (evt->role == BLE_HCI_LE_CONN_COMPLETE_ROLE_SLAVE) {
        ble_gap_rd_rem_ver_tx(evt->connection_handle);
    } else {
        ble_gap_rd_rem_sup_feat_tx(evt->connection_handle);
    }
"""
PATCHED_CONNECT_DISPATCH = """    if (evt->role == BLE_HCI_LE_CONN_COMPLETE_ROLE_MASTER) {
        memset(&event, 0, sizeof event);
        event.type = BLE_GAP_EVENT_CONNECT;
        event.connect.status = 0;
        event.connect.conn_handle = evt->connection_handle;
        ble_gap_event_listener_call(&event);
        ble_gap_call_conn_event_cb(&event, evt->connection_handle);
    } else {
        ble_gap_rd_rem_ver_tx(evt->connection_handle);
    }
"""

ORIGINAL_REMOTE_VERSION_DISPATCH = """    if (!(conn->bhc_flags & BLE_HS_CONN_F_MASTER)) {
        ble_gap_rd_rem_sup_feat_tx(ev->conn_handle);
    } else {
        ble_gap_event_connect_call(ev->conn_handle, 0);
    }
"""
PATCHED_REMOTE_VERSION_DISPATCH = """    if (!(conn->bhc_flags & BLE_HS_CONN_F_MASTER)) {
        ble_gap_rd_rem_sup_feat_tx(ev->conn_handle);
    }
"""


def patch_nimble_gap(source_path: Path) -> None:
    """Restore immediate central callbacks used by upstream Apache NimBLE."""
    source = source_path.read_text(encoding="utf-8")
    patched_fragments = (
        PATCHED_DECLARATION,
        PATCHED_CONNECT_DISPATCH,
        PATCHED_REMOTE_VERSION_DISPATCH,
    )
    if all(fragment in source for fragment in patched_fragments):
        return

    original_fragments = (
        ORIGINAL_DECLARATION,
        ORIGINAL_CONNECT_DISPATCH,
        ORIGINAL_REMOTE_VERSION_DISPATCH,
    )
    if not all(fragment in source for fragment in original_fragments):
        raise RuntimeError(
            "Unsupported ESP-IDF NimBLE ble_gap.c; refusing to apply a "
            "partial Canon compatibility patch"
        )

    source = source.replace(
        ORIGINAL_DECLARATION, PATCHED_DECLARATION, 1
    )
    source = source.replace(
        ORIGINAL_CONNECT_DISPATCH, PATCHED_CONNECT_DISPATCH, 1
    )
    source = source.replace(
        ORIGINAL_REMOTE_VERSION_DISPATCH,
        PATCHED_REMOTE_VERSION_DISPATCH,
        1,
    )
    source_path.write_text(source, encoding="utf-8")
    print("Applied ESP-IDF NimBLE Canon connect-order compatibility patch")


framework_dir = env.PioPlatform().get_package_dir("framework-espidf")
if not framework_dir:
    raise RuntimeError("PlatformIO framework-espidf package is unavailable")

patch_nimble_gap(Path(framework_dir) / NIMBLE_GAP_SOURCE)
