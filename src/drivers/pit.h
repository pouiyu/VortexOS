#ifndef _DRIVERS_PIT_H
#define _DRIVERS_PIT_H

#include <stdint.h>

void pitInit(uint32_t frequency);
void pitHandler(void);

#endif