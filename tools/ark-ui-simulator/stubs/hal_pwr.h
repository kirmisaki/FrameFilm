#ifndef ARK_SIM_HAL_PWR_H
#define ARK_SIM_HAL_PWR_H
#include <stdint.h>
int hal_pwr_wake_condition_met(void);
uint32_t hal_pwr_get_timer_wakeup(void);
#endif
