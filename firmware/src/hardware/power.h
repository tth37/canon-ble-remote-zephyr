#ifndef HARDWARE_POWER_H
#define HARDWARE_POWER_H

#include <stdbool.h>

int hardware_power_initialize(void);
int hardware_power_start(void);
bool hardware_power_woke_from_button(void);

#endif
