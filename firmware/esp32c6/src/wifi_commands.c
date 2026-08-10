#include "wifi_commands.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_netif.h"
#include "linenoise/linenoise.h"
#include "wifi_service.h"

#define DEFAULT_SCAN_LIMIT 15
#define MAX_SCAN_LIMIT 30
#define JOIN_TIMEOUT_MS 15000

static const char *authentication_name(wifi_auth_mode_t mode)
{
    switch (mode) {
    case WIFI_AUTH_OPEN:
        return "OPEN";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2/WPA3";
    case WIFI_AUTH_OWE:
        return "OWE";
    case WIFI_AUTH_ENTERPRISE:
    case WIFI_AUTH_WPA3_ENT_192:
    case WIFI_AUTH_WPA3_ENTERPRISE:
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
    case WIFI_AUTH_WPA_ENTERPRISE:
        return "ENTERPRISE";
    default:
        return "OTHER";
    }
}

static void print_usage(void)
{
    printf("Wi-Fi commands:\n");
    printf("  wifi scan [limit]              List nearby access points\n");
    printf("  wifi join <ssid> [password]    Connect using RAM-only credentials\n");
    printf("  wifi status                    Show connection and IP details\n");
    printf("  wifi leave                     Disconnect and forget credentials\n");
    printf("Quote an SSID or password containing spaces. Command history is disabled.\n");
}

static int scan_limit_from_arguments(int argc, char **argv)
{
    if (argc < 3) {
        return DEFAULT_SCAN_LIMIT;
    }
    char *end = NULL;
    errno = 0;
    const long value = strtol(argv[2], &end, 10);
    if (errno != 0 || end == argv[2] || *end != '\0' || value < 1 ||
        value > MAX_SCAN_LIMIT) {
        return -1;
    }
    return (int)value;
}

static int command_scan(int argc, char **argv)
{
    const int limit = scan_limit_from_arguments(argc, argv);
    if (limit < 0) {
        printf("limit must be between 1 and %d\n", MAX_SCAN_LIMIT);
        return 1;
    }

    wifi_ap_record_t records[MAX_SCAN_LIMIT];
    uint16_t count = (uint16_t)limit;
    printf("Scanning...\n");
    const esp_err_t result = wifi_service_scan(records, &count);
    if (result != ESP_OK) {
        printf("Scan failed: %s\n", esp_err_to_name(result));
        return 1;
    }

    printf("%-3s %-32s %5s %3s %-11s %s\n", "#", "SSID", "RSSI", "CH",
           "SECURITY", "PHY");
    for (uint16_t index = 0; index < count; ++index) {
        const wifi_ap_record_t *record = &records[index];
        const char *ssid = record->ssid[0] == '\0' ? "<hidden>"
                                                    : (char *)record->ssid;
        printf("%-3u %-32.32s %5d %3u %-11s %s\n", index + 1U, ssid,
               record->rssi, record->primary,
               authentication_name(record->authmode),
               record->phy_11ax != 0 ? "11ax" :
               record->phy_11n != 0 ? "11n" : "legacy");
    }
    printf("%u access point%s shown\n", count, count == 1 ? "" : "s");
    return 0;
}

static int command_join(int argc, char **argv)
{
    if (argc < 3 || argc > 4) {
        printf("Usage: wifi join <ssid> [password]\n");
        return 1;
    }
    const char *password = argc == 4 ? argv[3] : "";
    printf("Connecting to '%s'...\n", argv[2]);
    const esp_err_t result =
        wifi_service_join(argv[2], password, JOIN_TIMEOUT_MS);
    if (result != ESP_OK) {
        printf("Connection failed: %s\n", esp_err_to_name(result));
        return 1;
    }
    printf("Connected. Run 'wifi status' for network details.\n");
    return 0;
}

static int command_status(void)
{
    wifi_service_status_t status;
    const esp_err_t result = wifi_service_get_status(&status);
    if (result != ESP_OK) {
        printf("Status failed: %s\n", esp_err_to_name(result));
        return 1;
    }

    if (!status.connected) {
        printf("State: disconnected\n");
        if (status.disconnect_reason != 0) {
            printf("Last disconnect reason: %u\n", status.disconnect_reason);
        }
        return 0;
    }

    printf("State:    %s\n", status.has_ip ? "online" : "connected, awaiting IP");
    printf("SSID:     %.32s\n", status.access_point.ssid);
    printf("BSSID:    %02X:%02X:%02X:%02X:%02X:%02X\n",
           status.access_point.bssid[0], status.access_point.bssid[1],
           status.access_point.bssid[2], status.access_point.bssid[3],
           status.access_point.bssid[4], status.access_point.bssid[5]);
    printf("Channel:  %u\n", status.access_point.primary);
    printf("RSSI:     %d dBm\n", status.access_point.rssi);
    printf("Security: %s\n", authentication_name(status.access_point.authmode));
    if (status.has_ip) {
        printf("IPv4:     " IPSTR "\n", IP2STR(&status.ip.ip));
        printf("Gateway:  " IPSTR "\n", IP2STR(&status.ip.gw));
        printf("Netmask:  " IPSTR "\n", IP2STR(&status.ip.netmask));
    }
    return 0;
}

static int command_wifi(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "help") == 0) {
        print_usage();
        return 0;
    }
    if (strcmp(argv[1], "scan") == 0) {
        return command_scan(argc, argv);
    }
    if (strcmp(argv[1], "join") == 0) {
        const int result = command_join(argc, argv);
        if (argc >= 4) {
            memset(argv[3], 0, strlen(argv[3]));
        }
        // The REPL adds the line to history before invoking this handler.
        // With a one-entry history, adding a blank line evicts credentials.
        linenoiseHistoryAdd("");
        return result;
    }
    if (strcmp(argv[1], "status") == 0) {
        return command_status();
    }
    if (strcmp(argv[1], "leave") == 0) {
        const esp_err_t result = wifi_service_leave();
        if (result != ESP_OK) {
            printf("Disconnect failed: %s\n", esp_err_to_name(result));
            return 1;
        }
        printf("Disconnected. Credentials were stored only in RAM.\n");
        return 0;
    }
    printf("Unknown Wi-Fi action: %s\n", argv[1]);
    print_usage();
    return 1;
}

esp_err_t wifi_commands_register(void)
{
    const esp_console_cmd_t command = {
        .command = "wifi",
        .help = "Scan, join, inspect, or leave Wi-Fi networks",
        .hint = "<scan|join|status|leave|help>",
        .func = command_wifi,
    };
    return esp_console_cmd_register(&command);
}
