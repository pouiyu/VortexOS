#ifndef _DRIVERS_RTC_H
#define _DRIVERS_RTC_H

#include <stdint.h>

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} RtcTime;

void rtcInit(void);
void rtcGetTime(RtcTime* time);

#endif