#include "canon_ble_commands.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "canon_ble_service.h"
#include "esp_console.h"

#define DEFAULT_SCAN_SECONDS 15
#define MAX_SCAN_SECONDS 60

static void print_usage(void)
{
    printf("Canon camera commands:\n");
    printf("  camera pair [seconds]  Scan for and pair a camera\n");
    printf("  camera connect         Connect to the paired camera\n");
    printf("  camera shutter         Take a photo or toggle movie recording\n");
    printf("  camera focus           Send a focus command\n");
    printf("  camera status          Show pairing and connection state\n");
    printf("  camera disconnect      Close the BLE connection\n");
    printf("  camera forget          Remove the saved camera and BLE bond\n");
}

static int print_result(const char *action, esp_err_t result)
{
    if (result == ESP_OK) {
        printf("%s succeeded\n", action);
        return 0;
    }

    canon_ble_status_t status;
    canon_ble_service_get_status(&status);
    printf("%s failed: %s", action, esp_err_to_name(result));
    if (status.last_ble_error != 0) {
        printf(" (NimBLE status %d)", status.last_ble_error);
    }
    printf("\n");
    return 1;
}

static int scan_seconds_from_args(int argc, char **argv)
{
    if (argc < 3) {
        return DEFAULT_SCAN_SECONDS;
    }

    char *end = NULL;
    errno = 0;
    const long value = strtol(argv[2], &end, 10);
    if (errno != 0 || end == argv[2] || *end != '\0' || value < 1 ||
        value > MAX_SCAN_SECONDS) {
        return -1;
    }
    return (int)value;
}

static int command_pair(int argc, char **argv)
{
    if (argc > 3) {
        printf("Usage: camera pair [seconds]\n");
        return 1;
    }
    const int seconds = scan_seconds_from_args(argc, argv);
    if (seconds < 0) {
        printf("seconds must be between 1 and %d\n", MAX_SCAN_SECONDS);
        return 1;
    }

    printf("Put the camera in Bluetooth Remote pairing mode now.\n");
    printf("Scanning for Canon service for %d seconds...\n", seconds);
    return print_result("Pairing",
                        canon_ble_service_pair((uint32_t)seconds));
}

static int command_status(void)
{
    canon_ble_status_t status;
    canon_ble_service_get_status(&status);
    printf("BLE service: %s\n", status.initialized ? "initialized" : "unavailable");
    printf("BLE host:    %s\n", status.host_synced ? "ready" : "not synchronized");
    printf("Paired:      %s\n", status.paired ? "yes" : "no");
    printf("Camera:      %s\n",
           status.paired ? status.camera_address : "<none>");
    printf("Connection:  %s\n", status.connected ? "connected" : "disconnected");
    printf("Encrypted:   %s\n", status.encrypted ? "yes" : "no");
    printf("Control:     %s\n", status.ready ? "ready" : "not ready");
    printf("Scanning:    %s\n", status.scanning ? "yes" : "no");
    if (status.last_ble_error != 0) {
        printf("Last NimBLE status: %d\n", status.last_ble_error);
    }
    return 0;
}

static int command_camera(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "help") == 0) {
        print_usage();
        return 0;
    }
    if (strcmp(argv[1], "pair") == 0) {
        return command_pair(argc, argv);
    }
    if (argc != 2) {
        print_usage();
        return 1;
    }
    if (strcmp(argv[1], "connect") == 0) {
        return print_result("Connection", canon_ble_service_connect());
    }
    if (strcmp(argv[1], "shutter") == 0) {
        return print_result("Shutter", canon_ble_service_shutter());
    }
    if (strcmp(argv[1], "focus") == 0) {
        return print_result("Focus", canon_ble_service_focus());
    }
    if (strcmp(argv[1], "status") == 0) {
        return command_status();
    }
    if (strcmp(argv[1], "disconnect") == 0) {
        return print_result("Disconnect", canon_ble_service_disconnect());
    }
    if (strcmp(argv[1], "forget") == 0) {
        return print_result("Forget", canon_ble_service_forget());
    }

    printf("Unknown camera action: %s\n", argv[1]);
    print_usage();
    return 1;
}

esp_err_t canon_ble_commands_register(void)
{
    const esp_console_cmd_t command = {
        .command = "camera",
        .help = "Pair and control a Canon camera over BLE",
        .hint = "<pair|connect|shutter|focus|status|disconnect|forget|help>",
        .func = command_camera,
    };
    return esp_console_cmd_register(&command);
}
