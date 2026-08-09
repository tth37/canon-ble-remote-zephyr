#include "system_commands.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_check.h"
#include "esp_chip_info.h"
#include "esp_console.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"

static int command_sysinfo(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    esp_chip_info_t chip;
    uint32_t flash_size = 0;
    uint8_t mac[6];
    esp_chip_info(&chip);

    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK ||
        esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        printf("Could not read chip information\n");
        return 1;
    }

    printf("Target:       %s\n", CONFIG_IDF_TARGET);
    printf("ESP-IDF:      %s\n", esp_get_idf_version());
    printf("Revision:     %u\n", chip.revision);
    printf("CPU cores:    %u\n", chip.cores);
    printf("Flash:        %" PRIu32 " MB\n", flash_size / (1024U * 1024U));
    printf("Wi-Fi MAC:    %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1],
           mac[2], mac[3], mac[4], mac[5]);
    printf("Radios:       %s%s%s\n",
           (chip.features & CHIP_FEATURE_WIFI_BGN) != 0 ? "Wi-Fi " : "",
           (chip.features & CHIP_FEATURE_BLE) != 0 ? "BLE " : "",
           (chip.features & CHIP_FEATURE_IEEE802154) != 0 ? "802.15.4" : "");
    printf("Uptime:       %" PRIu64 " s\n",
           (uint64_t)(esp_timer_get_time() / 1000000));
    return 0;
}

static int command_heap(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Free heap:    %" PRIu32 " bytes\n", esp_get_free_heap_size());
    printf("Minimum free: %" PRIu32 " bytes\n",
           esp_get_minimum_free_heap_size());
    printf("Largest block:%zu bytes\n",
           heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return 0;
}

static int command_reboot(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Restarting...\n");
    fflush(stdout);
    esp_restart();
    return 0;
}

static esp_err_t register_command(const char *name, const char *help,
                                  esp_console_cmd_func_t function)
{
    const esp_console_cmd_t command = {
        .command = name,
        .help = help,
        .func = function,
    };
    return esp_console_cmd_register(&command);
}

esp_err_t system_commands_register(void)
{
    ESP_RETURN_ON_ERROR(
        register_command("sysinfo", "Show chip, SDK, radio, and uptime information",
                         command_sysinfo),
        "system_commands", "Could not register sysinfo");
    ESP_RETURN_ON_ERROR(
        register_command("heap", "Show current and minimum free heap", command_heap),
        "system_commands", "Could not register heap");
    return register_command("reboot", "Restart the ESP32-C6", command_reboot);
}
