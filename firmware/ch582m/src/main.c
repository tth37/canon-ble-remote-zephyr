#include "CONFIG.h"

#include "HAL.h"
#include "canon_ble_wch.h"
#include "serial_console.h"

__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4U];

__HIGH_CODE
__attribute__((noinline))
static void main_loop(void)
{
    for (;;) {
        TMOS_SystemProcess();
        serial_console_poll();
    }
}

int main(void)
{
    SetSysClock(CLK_SOURCE_PLL_60MHz);

    GPIOA_SetBits(bTXD1);
    GPIOA_ModeCfg(bTXD1, GPIO_ModeOut_PP_5mA);
    GPIOA_ModeCfg(bRXD1, GPIO_ModeIN_PU);
    UART1_DefInit();

    PRINT("\nCH582M serial services\n");
    PRINT("WCH BLE library: %s\n", VER_LIB);

    CH58X_BLEInit();
    HAL_Init();
    GAPRole_CentralInit();
    canon_ble_wch_init();
    serial_console_init();
    main_loop();
}
