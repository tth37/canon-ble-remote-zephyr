#include "serial_console.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "CONFIG.h"
#include "canon_ble_wch.h"

#define CONSOLE_LINE_SIZE 96U
#define CONSOLE_ARG_COUNT 4U
#define DEFAULT_SCAN_SECONDS 15U
#define MAX_SCAN_SECONDS 40U

static char line_buffer[CONSOLE_LINE_SIZE];
static size_t line_length;
static bool ignore_next_lf;

static void print_prompt(void)
{
    PRINT("ch582m> ");
}

static void print_help(void)
{
    PRINT("Commands:\n");
    PRINT("  help                    Show this help\n");
    PRINT("  sysinfo                 Show target, BLE SDK, and uptime\n");
    PRINT("  reboot                  Restart the CH582M\n");
    PRINT("  camera pair [seconds]   Pair a Canon camera\n");
    PRINT("  camera connect          Connect to saved camera\n");
    PRINT("  camera shutter          Take photo/toggle recording\n");
    PRINT("  camera focus            Send focus command\n");
    PRINT("  camera status           Show BLE camera state\n");
    PRINT("  camera disconnect       Disconnect camera\n");
    PRINT("  camera forget           Erase camera and BLE bonds\n");
}

static void print_address(const uint8_t address[6])
{
    PRINT("%02X:%02X:%02X:%02X:%02X:%02X",
          address[5], address[4], address[3],
          address[2], address[1], address[0]);
}

static void print_camera_status(void)
{
    canon_wch_status_t status;
    canon_ble_wch_get_status(&status);

    PRINT("BLE service: initialized\n");
    PRINT("BLE host:    %s\n", status.host_ready ? "ready" : "starting");
    PRINT("Paired:      %s\n", status.paired ? "yes" : "no");
    PRINT("Camera:      ");
    if (status.paired) {
        print_address(status.camera_address);
        PRINT(" (type %u)\n", status.camera_address_type);
    } else {
        PRINT("<none>\n");
    }
    PRINT("Connection:  %s\n",
          status.connected ? "connected" : "disconnected");
    PRINT("Encrypted:   %s\n", status.encrypted ? "yes" : "no");
    PRINT("Control:     %s\n", status.ready ? "ready" : "not ready");
    PRINT("Scanning:    %s\n", status.scanning ? "yes" : "no");
    PRINT("Operation:   %s\n", status.busy ? "busy" : "idle");
    if (status.last_ble_error != SUCCESS) {
        PRINT("Last WCH BLE status: 0x%02X\n", status.last_ble_error);
    }
}

static void print_result(const char *operation, canon_wch_result_t result)
{
    if (result == CANON_WCH_OK) {
        canon_wch_status_t status;
        canon_ble_wch_get_status(&status);
        if (status.busy) {
            PRINT("%s started\n", operation);
        }
    } else {
        PRINT("%s failed: %s\n", operation,
              canon_ble_wch_result_name(result));
    }
}

static bool parse_scan_seconds(const char *text, uint8_t *seconds)
{
    uint16_t value = 0U;
    if (text[0] == '\0') {
        return false;
    }
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
        value = (uint16_t)(value * 10U + (uint16_t)(*cursor - '0'));
        if (value > MAX_SCAN_SECONDS) {
            return false;
        }
    }
    if (value == 0U) {
        return false;
    }
    *seconds = (uint8_t)value;
    return true;
}

static void camera_command(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "help") == 0) {
        print_help();
        return;
    }

    if (strcmp(argv[1], "pair") == 0) {
        uint8_t seconds = DEFAULT_SCAN_SECONDS;
        if (argc > 3 ||
            (argc == 3 && !parse_scan_seconds(argv[2], &seconds))) {
            PRINT("Usage: camera pair [1-%u]\n", MAX_SCAN_SECONDS);
            return;
        }
        PRINT("Put the camera in Bluetooth Remote pairing mode now.\n");
        print_result("Pairing", canon_ble_wch_pair(seconds));
        return;
    }
    if (argc != 2) {
        PRINT("Usage: camera <pair|connect|shutter|focus|status|disconnect|forget>\n");
        return;
    }
    if (strcmp(argv[1], "connect") == 0) {
        print_result("Connection", canon_ble_wch_connect());
    } else if (strcmp(argv[1], "shutter") == 0) {
        print_result("Shutter", canon_ble_wch_shutter());
    } else if (strcmp(argv[1], "focus") == 0) {
        print_result("Focus", canon_ble_wch_focus());
    } else if (strcmp(argv[1], "status") == 0) {
        print_camera_status();
    } else if (strcmp(argv[1], "disconnect") == 0) {
        print_result("Disconnect", canon_ble_wch_disconnect());
    } else if (strcmp(argv[1], "forget") == 0) {
        print_result("Forget", canon_ble_wch_forget());
    } else {
        PRINT("Unknown camera action: %s\n", argv[1]);
    }
}

static void execute_line(char *line)
{
    char *argv[CONSOLE_ARG_COUNT];
    int argc = 0;
    char *cursor = line;

    while (*cursor != '\0' && argc < (int)CONSOLE_ARG_COUNT) {
        while (*cursor == ' ') {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        argv[argc++] = cursor;
        while (*cursor != '\0' && *cursor != ' ') {
            ++cursor;
        }
        if (*cursor != '\0') {
            *cursor++ = '\0';
        }
    }

    if (argc == 0) {
        return;
    }
    if (strcmp(argv[0], "help") == 0) {
        print_help();
    } else if (strcmp(argv[0], "sysinfo") == 0) {
        PRINT("Target: CH582M (RV32IMAC)\n");
        PRINT("WCH BLE library: %s\n", VER_LIB);
        PRINT("Uptime: %lu s\n",
              (unsigned long)(TMOS_GetSystemClock() / 1600U));
    } else if (strcmp(argv[0], "reboot") == 0) {
        PRINT("Restarting...\n");
        SYS_ResetExecute();
    } else if (strcmp(argv[0], "camera") == 0) {
        camera_command(argc, argv);
    } else {
        PRINT("Unknown command: %s (try help)\n", argv[0]);
    }
}

void serial_console_init(void)
{
    line_length = 0U;
    ignore_next_lf = false;
    PRINT("Type help for commands.\n");
    print_prompt();
}

void serial_console_poll(void)
{
    while (R8_UART1_RFC != 0U) {
        const char character = (char)UART1_RecvByte();

        if (character == '\n' && ignore_next_lf) {
            ignore_next_lf = false;
            continue;
        }
        ignore_next_lf = false;

        if (character == '\r' || character == '\n') {
            ignore_next_lf = character == '\r';
            PRINT("\r\n");
            line_buffer[line_length] = '\0';
            execute_line(line_buffer);
            line_length = 0U;
            print_prompt();
        } else if ((character == '\b' || character == 0x7f) &&
                   line_length > 0U) {
            --line_length;
            PRINT("\b \b");
        } else if (character >= 0x20 && character <= 0x7e &&
                   line_length + 1U < sizeof(line_buffer)) {
            line_buffer[line_length++] = character;
            UART1_SendByte((uint8_t)character);
        }
    }
}
