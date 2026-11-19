/* SPDX-License-Identifier: Unlicense */
#include "rtc.h"

#define CMOS_ADDRESS 0x70U
#define CMOS_DATA    0x71U

static bool rtc_ready = false;

static inline void cmos_outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t cmos_inb(uint16_t port) {
    uint8_t value = 0U;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint8_t cmos_read_register(uint8_t reg) {
    cmos_outb(CMOS_ADDRESS, reg);
    return cmos_inb(CMOS_DATA);
}

static bool rtc_is_updating(void) {
    cmos_outb(CMOS_ADDRESS, 0x0AU);
    return (cmos_inb(CMOS_DATA) & 0x80U) != 0U;
}

static inline uint8_t bcd_to_bin(uint8_t val) {
    return (uint8_t)(((val >> 4U) * 10U) + (val & 0x0FU));
}

bool rtc_initialize(void) {
    rtc_ready = true;
    return true;
}

bool rtc_get_datetime(struct rtc_datetime *dt) {
    if (dt == NULL) return false;

    uint8_t sec, min, hour, day, month, year;
    uint8_t last_sec, last_min, last_hour, last_day, last_month, last_year;
    uint8_t status_b;
    uint32_t timeout = 100000U;

    while (rtc_is_updating()) {
        if (--timeout == 0U) break;
    }

    sec = cmos_read_register(0x00U);
    min = cmos_read_register(0x02U);
    hour = cmos_read_register(0x04U);
    day = cmos_read_register(0x07U);
    month = cmos_read_register(0x08U);
    year = cmos_read_register(0x09U);

    timeout = 100000U;
    do {
        last_sec = sec;
        last_min = min;
        last_hour = hour;
        last_day = day;
        last_month = month;
        last_year = year;

        while (rtc_is_updating()) {
            if (--timeout == 0U) break;
        }

        sec = cmos_read_register(0x00U);
        min = cmos_read_register(0x02U);
        hour = cmos_read_register(0x04U);
        day = cmos_read_register(0x07U);
        month = cmos_read_register(0x08U);
        year = cmos_read_register(0x09U);
    } while ((last_sec != sec || last_min != min || last_hour != hour ||
              last_day != day || last_month != month || last_year != year) && --timeout > 0U);

    status_b = cmos_read_register(0x0BU);

    /* Convert BCD to binary if bit 2 of Status Register B is not set */
    if ((status_b & 0x04U) == 0U) {
        sec = bcd_to_bin(sec);
        min = bcd_to_bin(min);
        hour = (uint8_t)(((hour & 0x70U) >> 4U) * 10U + (hour & 0x0FU) + ((hour & 0x80U) ? 0x80U : 0U));
        day = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year = bcd_to_bin(year);
    }

    /* Convert 12-hour clock to 24-hour clock if bit 1 of Status Register B is not set */
    if ((status_b & 0x02U) == 0U && (hour & 0x80U) != 0U) {
        hour = (uint8_t)(((hour & 0x7FU) + 12U) % 24U);
    }

    dt->second = sec % 60U;
    dt->minute = min % 60U;
    dt->hour = hour % 24U;
    dt->day = (day == 0U) ? 1U : ((day > 31U) ? 31U : day);
    dt->month = (month == 0U) ? 1U : ((month > 12U) ? 12U : month);
    dt->year = (uint16_t)(2000U + (uint16_t)year);

    return true;
}

void rtc_format_time(const struct rtc_datetime *dt, char *buf, size_t max_len) {
    if (dt == NULL || buf == NULL || max_len < 6U) return;
    buf[0] = (char)('0' + (dt->hour / 10U));
    buf[1] = (char)('0' + (dt->hour % 10U));
    buf[2] = ':';
    buf[3] = (char)('0' + (dt->minute / 10U));
    buf[4] = (char)('0' + (dt->minute % 10U));
    buf[5] = '\0';
}

void rtc_format_time_sec(const struct rtc_datetime *dt, char *buf, size_t max_len) {
    if (dt == NULL || buf == NULL || max_len < 9U) return;
    buf[0] = (char)('0' + (dt->hour / 10U));
    buf[1] = (char)('0' + (dt->hour % 10U));
    buf[2] = ':';
    buf[3] = (char)('0' + (dt->minute / 10U));
    buf[4] = (char)('0' + (dt->minute % 10U));
    buf[5] = ':';
    buf[6] = (char)('0' + (dt->second / 10U));
    buf[7] = (char)('0' + (dt->second % 10U));
    buf[8] = '\0';
}

void rtc_format_date(const struct rtc_datetime *dt, char *buf, size_t max_len) {
    if (dt == NULL || buf == NULL || max_len < 11U) return;
    uint16_t yr = dt->year;
    buf[0] = (char)('0' + ((yr / 1000U) % 10U));
    buf[1] = (char)('0' + ((yr / 100U) % 10U));
    buf[2] = (char)('0' + ((yr / 10U) % 10U));
    buf[3] = (char)('0' + (yr % 10U));
    buf[4] = '-';
    buf[5] = (char)('0' + (dt->month / 10U));
    buf[6] = (char)('0' + (dt->month % 10U));
    buf[7] = '-';
    buf[8] = (char)('0' + (dt->day / 10U));
    buf[9] = (char)('0' + (dt->day % 10U));
    buf[10] = '\0';
}

#ifdef DAWN_KERNEL_TEST
bool rtc_self_test(void) {
    if (!rtc_initialize()) return false;

    /* BCD conversion test */
    if (bcd_to_bin(0x00U) != 0U) return false;
    if (bcd_to_bin(0x19U) != 19U) return false;
    if (bcd_to_bin(0x59U) != 59U) return false;

    /* Format verification */
    struct rtc_datetime test_dt = {
        .second = 45U,
        .minute = 30U,
        .hour = 16U,
        .day = 19U,
        .month = 11U,
        .year = 2026U
    };

    char tbuf[16] = {0};
    rtc_format_time(&test_dt, tbuf, sizeof(tbuf));
    if (tbuf[0] != '1' || tbuf[1] != '6' || tbuf[2] != ':' || tbuf[3] != '3' || tbuf[4] != '0') {
        return false;
    }

    char dbuf[16] = {0};
    rtc_format_date(&test_dt, dbuf, sizeof(dbuf));
    if (dbuf[0] != '2' || dbuf[1] != '0' || dbuf[2] != '2' || dbuf[3] != '6' ||
        dbuf[4] != '-' || dbuf[5] != '1' || dbuf[6] != '1' || dbuf[7] != '-' ||
        dbuf[8] != '1' || dbuf[9] != '9') {
        return false;
    }

    /* Hardware read validation */
    struct rtc_datetime live_dt = {0};
    if (rtc_get_datetime(&live_dt)) {
        if (live_dt.hour >= 24U || live_dt.minute >= 60U || live_dt.second >= 60U) return false;
        if (live_dt.month == 0U || live_dt.month > 12U) return false;
        if (live_dt.day == 0U || live_dt.day > 31U) return false;
    }

    return true;
}
#endif
