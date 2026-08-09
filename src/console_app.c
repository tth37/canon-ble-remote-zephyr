#include "console_app.h"

#include "esp_console.h"
#include "system_commands.h"
#include "wifi_commands.h"

#define CONSOLE_PROMPT "c6>"
#define CONSOLE_LINE_LENGTH 256

esp_err_t console_app_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config =
        ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = CONSOLE_PROMPT;
    repl_config.max_cmdline_length = CONSOLE_LINE_LENGTH;

    // ESP-IDF's linenoise rejects zero. Keep its minimum valid history size;
    // credential-bearing commands evict themselves after execution.
    repl_config.max_history_len = 1;
    repl_config.history_save_path = NULL;

    ESP_ERROR_CHECK(esp_console_register_help_command());
    ESP_ERROR_CHECK(system_commands_register());
    ESP_ERROR_CHECK(wifi_commands_register());

    esp_console_dev_uart_config_t uart_config =
        ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(
        esp_console_new_repl_uart(&uart_config, &repl_config, &repl));
    return esp_console_start_repl(repl);
}
