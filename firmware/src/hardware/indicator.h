#ifndef HARDWARE_INDICATOR_H
#define HARDWARE_INDICATOR_H

#include <stdbool.h>

int hardware_indicator_initialize(void);
int hardware_indicator_start(void);
bool hardware_indicator_available(void);

#endif
