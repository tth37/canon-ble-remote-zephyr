/********************************** (C) COPYRIGHT ******************************
 * Derived from the WCH CH58x BLE SDK configuration template.
 * This file and the linked WCH BLE binary are for WCH microcontrollers only.
 ******************************************************************************/

#ifndef __CONFIG_H
#define __CONFIG_H

#include "CH58xBLE_LIB.h"
#include "CH58x_common.h"

#define CHIP_ID ID_CH582

#define BLE_MAC FALSE
#define DCDC_ENABLE FALSE
#define HAL_SLEEP FALSE
#define HAL_KEY FALSE
#define HAL_LED FALSE
#define TEM_SAMPLE TRUE
#define BLE_CALIBRATION_ENABLE TRUE
#define BLE_CALIBRATION_PERIOD 120000

#define BLE_SNV TRUE
#define BLE_SNV_ADDR (0x77E00U - FLASH_ROM_MAX_SIZE)
#define BLE_SNV_BLOCK 256U
#define BLE_SNV_NUM 1U

/* Internal 32 kHz oscillator; most small CH582M boards omit a 32 kHz crystal. */
#define CLK_OSC32K 1

#define BLE_MEMHEAP_SIZE (6U * 1024U)
#define BLE_BUFF_MAX_LEN 27U
#define BLE_BUFF_NUM 5U
#define BLE_TX_NUM_EVENT 1U
#define BLE_TX_POWER LL_TX_POWEER_0_DBM
#define PERIPHERAL_MAX_CONNECTION 0U
#define CENTRAL_MAX_CONNECTION 1U

#define SLEEP_RTC_MIN_TIME US_TO_RTC(1000)
#define SLEEP_RTC_MAX_TIME \
    MS_TO_RTC(RTC_TO_MS(RTC_TIMER_MAX_VALUE) - 1000U * 60U * 60U)
#define WAKE_UP_RTC_MAX_TIME US_TO_RTC(1400)

extern uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4U];

#endif
