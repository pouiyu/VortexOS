#include "rtc.h"
#include <io.h>

#define RTC_INDEX 0x70
#define RTC_DATA  0x71

static uint8_t rtcRead(uint8_t reg) {
    outb(RTC_INDEX, reg);
    return inb(RTC_DATA);
}

static uint8_t bcdToBin(uint8_t bcd) {
    return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

void rtcInit(void) {
    // 读一次确认可访问
    rtcRead(0x0A);
}

void rtcGetTime(RtcTime* time) {
    time->second = bcdToBin(rtcRead(0x00));
    time->minute = bcdToBin(rtcRead(0x02));

    uint8_t hour = rtcRead(0x04);
    if (hour & 0x80) {
        // 12 小时制
        hour &= 0x7F;
        if (hour & 0x20) {
            // PM
            hour = ((hour & 0x0F) + 12) % 24;
        } else {
            // AM
            hour &= 0x0F;
            if (hour == 12) hour = 0;
        }
    } else {
        // 24 小时制
        hour = bcdToBin(hour);
    }

    // 转 UTC+8
    hour = (hour + 8) % 24;

    time->hour = hour;
    time->day = bcdToBin(rtcRead(0x07));
    time->month = bcdToBin(rtcRead(0x08));
    time->year = bcdToBin(rtcRead(0x09)) + 2000;
}