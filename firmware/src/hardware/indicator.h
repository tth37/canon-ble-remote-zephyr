#ifndef HARDWARE_INDICATOR_H
#define HARDWARE_INDICATOR_H

#include <stdbool.h>

int hardware_indicator_initialize(void);
int hardware_indicator_start(void);
void hardware_indicator_show_deep_sleep_wake(void);
void hardware_indicator_prepare_for_sleep(void);
void hardware_indicator_resume_after_sleep_abort(void);
bool hardware_indicator_available(void);

#endif
