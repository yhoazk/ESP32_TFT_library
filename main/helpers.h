#ifndef _HELPERS_
#define _HELPERS_

#include "tftspi.h"

unsigned int rand_interval(unsigned int min, unsigned int max);
color_t random_color();
int Wait(int ms);

#define STATE_TFT_ON (1U)
#define STATE_TFT_OFF (2U)
#define TRANSITION_ON_TO_OFF (4U)
#define TRANSITION_OFF_TO_ON (8U)

uint32_t fsm_calc_next(uint32_t current_state);

#endif  // _HELPERS_