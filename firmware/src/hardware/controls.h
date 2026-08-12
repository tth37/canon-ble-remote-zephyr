#ifndef HARDWARE_CONTROLS_H
#define HARDWARE_CONTROLS_H

#include <stdbool.h>
#include <stdint.h>

#define HARDWARE_PAIR_SCAN_SECONDS 30U

typedef struct {
    bool focus_pressed;
    bool shutter_pressed;
    bool pair_pressed;
    bool pairing_active;
    uint32_t pair_hold_remaining_ms;
} hardware_controls_status_t;

/* Configure the physical inputs. */
int hardware_controls_initialize(void);

/* Start debounced camera inputs and the dedicated pairing-button handler. */
int hardware_controls_start(void);

void hardware_controls_get_status(hardware_controls_status_t *status);
uint32_t hardware_controls_activity_generation(void);
bool hardware_controls_available(void);

#endif
