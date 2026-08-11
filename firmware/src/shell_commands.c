#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/version.h>

#include "canon/remote.h"

#define DEFAULT_SCAN_SECONDS 15U
#define MAX_SCAN_SECONDS 60U

static int print_result(const struct shell *shell, const char *operation,
                        canon_remote_result_t result)
{
    if (result == CANON_REMOTE_OK) {
        shell_print(shell, "%s succeeded", operation);
        return 0;
    }

    canon_remote_status_t status;
    canon_remote_get_status(&status);
    if (status.last_ble_error != 0) {
        shell_error(shell, "%s failed: %s (Zephyr BLE error %d)",
                    operation, canon_remote_result_name(result),
                    status.last_ble_error);
    } else {
        shell_error(shell, "%s failed: %s", operation,
                    canon_remote_result_name(result));
    }
    return -ENOEXEC;
}

static int parse_scan_seconds(const char *text, uint32_t *seconds)
{
    char *end = NULL;
    errno = 0;
    const unsigned long value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0U ||
        value > MAX_SCAN_SECONDS) {
        return -EINVAL;
    }
    *seconds = (uint32_t)value;
    return 0;
}

static int command_camera_pair(const struct shell *shell, size_t argc,
                               char **argv)
{
    uint32_t seconds = DEFAULT_SCAN_SECONDS;
    if (argc == 2U && parse_scan_seconds(argv[1], &seconds) != 0) {
        shell_error(shell, "seconds must be between 1 and %u",
                    MAX_SCAN_SECONDS);
        return -EINVAL;
    }

    shell_print(shell, "Put the camera in Bluetooth Remote pairing mode.");
    shell_print(shell, "Scanning for the Canon service for %u seconds...",
                seconds);
    return print_result(shell, "Pairing",
                        canon_remote_pair(seconds));
}

static int command_camera_connect(const struct shell *shell, size_t argc,
                                  char **argv)
{
    (void)argc;
    (void)argv;
    return print_result(shell, "Connection", canon_remote_connect());
}

static int command_camera_shutter(const struct shell *shell, size_t argc,
                                  char **argv)
{
    if (argc == 1U) {
        return print_result(shell, "Shutter", canon_remote_shutter());
    }

    bool pressed;
    if (strcmp(argv[1], "press") == 0) {
        pressed = true;
    } else if (strcmp(argv[1], "release") == 0) {
        pressed = false;
    } else {
        shell_error(shell, "usage: camera shutter [press|release]");
        return -EINVAL;
    }

    const canon_remote_result_t result = canon_remote_set_button(
        CANON_REMOTE_BUTTON_SHUTTER, pressed);
    if (result != CANON_REMOTE_OK) {
        return print_result(shell, "Shutter input", result);
    }
    shell_print(shell, "Shutter %s queued", pressed ? "press" : "release");
    return 0;
}

static int command_camera_focus(const struct shell *shell, size_t argc,
                                char **argv)
{
    if (argc == 1U) {
        return print_result(shell, "Focus", canon_remote_focus());
    }

    bool pressed;
    if (strcmp(argv[1], "press") == 0) {
        pressed = true;
    } else if (strcmp(argv[1], "release") == 0) {
        pressed = false;
    } else {
        shell_error(shell, "usage: camera focus [press|release]");
        return -EINVAL;
    }

    const canon_remote_result_t result = canon_remote_set_button(
        CANON_REMOTE_BUTTON_FOCUS, pressed);
    if (result != CANON_REMOTE_OK) {
        return print_result(shell, "Focus input", result);
    }
    shell_print(shell, "Focus %s queued", pressed ? "press" : "release");
    return 0;
}

static int command_camera_status(const struct shell *shell, size_t argc,
                                 char **argv)
{
    (void)argc;
    (void)argv;

    canon_remote_status_t status;
    canon_remote_get_status(&status);
    shell_print(shell, "BLE service: %s",
                status.initialized ? "initialized" : "unavailable");
    shell_print(shell, "BLE host:    %s",
                status.host_ready ? "ready" : "not ready");
    shell_print(shell, "Paired:      %s", status.paired ? "yes" : "no");
    shell_print(shell, "Camera:      %s",
                status.paired ? status.camera_address : "<none>");
    shell_print(shell, "Connection:  %s",
                status.connected ? "connected" : "disconnected");
    shell_print(shell, "Encrypted:   %s",
                status.encrypted ? "yes" : "no");
    shell_print(shell, "Control:     %s",
                status.ready ? "ready" : "not ready");
    shell_print(shell, "Scanning:    %s",
                status.scanning ? "yes" : "no");
    shell_print(shell, "Operation:   %s", status.busy ? "busy" : "idle");
    shell_print(shell, "Focus input: %s",
                status.focus_requested ? "pressed" : "released");
    shell_print(shell, "Focus sent:  %s",
                status.focus_applied ? "pressed" : "released");
    shell_print(shell, "Shutter input: %s",
                status.shutter_requested ? "pressed" : "released");
    shell_print(shell, "Shutter sent:  %s",
                status.shutter_applied ? "pressed" : "released");
    if (status.last_ble_error != 0) {
        shell_print(shell, "Last Zephyr BLE error: %d",
                    status.last_ble_error);
    }
    return 0;
}

static int command_camera_disconnect(const struct shell *shell, size_t argc,
                                     char **argv)
{
    (void)argc;
    (void)argv;
    return print_result(shell, "Disconnect",
                        canon_remote_disconnect());
}

static int command_camera_forget(const struct shell *shell, size_t argc,
                                 char **argv)
{
    (void)argc;
    (void)argv;
    return print_result(shell, "Forget", canon_remote_forget());
}

static int command_camera_help(const struct shell *shell, size_t argc,
                               char **argv)
{
    (void)argc;
    (void)argv;
    shell_print(shell, "Canon camera commands:");
    shell_print(shell, "  camera pair [seconds]");
    shell_print(shell, "  camera connect");
    shell_print(shell, "  camera shutter [press|release]");
    shell_print(shell, "  camera focus [press|release]");
    shell_print(shell, "  camera status");
    shell_print(shell, "  camera disconnect");
    shell_print(shell, "  camera forget");
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    camera_subcommands,
    SHELL_CMD_ARG(pair, NULL, "Pair a Canon camera [seconds]",
                  command_camera_pair, 1, 1),
    SHELL_CMD(connect, NULL, "Connect to the saved camera",
              command_camera_connect),
    SHELL_CMD_ARG(shutter, NULL,
                  "Pulse shutter or queue [press|release]",
                  command_camera_shutter, 1, 1),
    SHELL_CMD_ARG(focus, NULL, "Pulse focus or queue [press|release]",
                  command_camera_focus, 1, 1),
    SHELL_CMD(status, NULL, "Show camera connection state",
              command_camera_status),
    SHELL_CMD(disconnect, NULL, "Close the camera connection",
              command_camera_disconnect),
    SHELL_CMD(forget, NULL, "Remove the saved camera and BLE bond",
              command_camera_forget),
    SHELL_CMD(help, NULL, "Show Canon camera commands", command_camera_help),
    SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(camera, &camera_subcommands,
                   "Pair and control a Canon camera over BLE", NULL);

static int command_sysinfo(const struct shell *shell, size_t argc, char **argv)
{
    (void)argc;
    (void)argv;
    shell_print(shell, "Target: %s", CONFIG_BOARD_TARGET);
    shell_print(shell, "Zephyr: %s", KERNEL_VERSION_STRING);
    shell_print(shell, "Uptime: %lld s",
                (long long)(k_uptime_get() / 1000));
    return 0;
}

SHELL_CMD_REGISTER(sysinfo, NULL, "Show target, Zephyr, and uptime",
                   command_sysinfo);

static int command_reboot(const struct shell *shell, size_t argc, char **argv)
{
    (void)argc;
    (void)argv;
    shell_print(shell, "Restarting...");
    k_msleep(50);
    sys_reboot(SYS_REBOOT_COLD);
    return 0;
}

SHELL_CMD_REGISTER(reboot, NULL, "Restart the board", command_reboot);
