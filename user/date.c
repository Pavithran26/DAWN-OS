/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)argc;
    (void)argv;
    (void)envp;
    uint64_t epoch = 0;
    int64_t res = dawn_time(&epoch);
    if (res < 0) {
        dawn_printf("date: cannot retrieve system time\n");
        return 1;
    }

    /* Convert epoch seconds to UTC date/time */
    uint64_t seconds = epoch % 60;
    uint64_t total_minutes = epoch / 60;
    uint64_t minutes = total_minutes % 60;
    uint64_t total_hours = total_minutes / 60;
    uint64_t hours = total_hours % 24;
    uint64_t days = total_hours / 24;

    /* Basic Gregorian year calculation starting from 1970 */
    uint64_t year = 1970;
    while (1) {
        bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        uint64_t days_in_year = leap ? 366 : 365;
        if (days < days_in_year) break;
        days -= days_in_year;
        year++;
    }

    bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    static const uint8_t month_days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    uint64_t month = 1;
    for (size_t m = 0; m < 12; m++) {
        uint64_t dim = month_days[m];
        if (m == 1 && leap) dim = 29;
        if (days < dim) {
            month = m + 1;
            break;
        }
        days -= dim;
    }
    uint64_t day = days + 1;

    dawn_printf("%u-%02u-%02u %02u:%02u:%02u UTC\n",
                (unsigned int)year, (unsigned int)month, (unsigned int)day,
                (unsigned int)hours, (unsigned int)minutes, (unsigned int)seconds);
    return 0;
}
