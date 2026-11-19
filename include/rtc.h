/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_RTC_H
#define DAWN_RTC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct rtc_datetime {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
};

bool rtc_initialize(void);
bool rtc_get_datetime(struct rtc_datetime *dt);
void rtc_format_time(const struct rtc_datetime *dt, char *buf, size_t max_len);
void rtc_format_time_sec(const struct rtc_datetime *dt, char *buf, size_t max_len);
void rtc_format_date(const struct rtc_datetime *dt, char *buf, size_t max_len);
bool rtc_self_test(void);

#endif /* DAWN_RTC_H */
