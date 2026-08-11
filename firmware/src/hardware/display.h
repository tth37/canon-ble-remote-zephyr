#ifndef HARDWARE_DISPLAY_H
#define HARDWARE_DISPLAY_H

#include <stdbool.h>

int hardware_display_initialize(void);
int hardware_display_start(void);
bool hardware_display_available(void);

#endif
