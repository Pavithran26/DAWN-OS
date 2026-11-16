/* SPDX-License-Identifier: Unlicense */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "limine_requests.h"

#define SERIAL_COM1 0x3F8U
#define QEMU_DEBUG_EXIT_PORT 0xF4U
#define QEMU_TEST_PASS 0x10U
#define QEMU_TEST_FAIL 0x11U
#define SERIAL_POLL_LIMIT 100000U

/* Keep the request block writable: Limine writes the response pointers and
 * updates the base-revision tag when it accepts the requested revision. */
__attribute__((used, aligned(8), section(".limine_requests_start")))
volatile uint64_t limine_requests_start[] = {
    LIMINE_REQUESTS_START_MARKER
};

__attribute__((used, aligned(8), section(".limine_requests")))
volatile uint64_t limine_base_revision[] = {
    LIMINE_BASE_REVISION(6)
};

__attribute__((used, aligned(8), section(".limine_requests")))
volatile struct limine_framebuffer_request limine_framebuffer_request = {
    .id = { LIMINE_FRAMEBUFFER_REQUEST_ID },
    .revision = 0,
    .response = NULL
};

__attribute__((used, aligned(8), section(".limine_requests_end")))
volatile uint64_t limine_requests_end[] = {
    LIMINE_REQUESTS_END_MARKER
};

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void cpu_halt_forever(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt" : : : "memory");
    }
}

static void serial_init(void) {
    outb((uint16_t)(SERIAL_COM1 + 1U), 0x00U);
    outb((uint16_t)(SERIAL_COM1 + 3U), 0x80U);
    outb((uint16_t)SERIAL_COM1, 0x01U);
    outb((uint16_t)(SERIAL_COM1 + 1U), 0x00U);
    outb((uint16_t)(SERIAL_COM1 + 3U), 0x03U);
    outb((uint16_t)(SERIAL_COM1 + 2U), 0xC7U);
    outb((uint16_t)(SERIAL_COM1 + 4U), 0x0BU);
}

static bool serial_write_char(char value) {
    for (uint32_t attempt = 0; attempt < SERIAL_POLL_LIMIT; ++attempt) {
        if ((inb((uint16_t)(SERIAL_COM1 + 5U)) & 0x20U) != 0U) {
            outb((uint16_t)SERIAL_COM1, (uint8_t)value);
            return true;
        }
    }
    return false;
}

static void serial_write(const char *text) {
    while (*text != '\0') {
        if (*text == '\n' && !serial_write_char('\r')) {
            return;
        }
        if (!serial_write_char(*text)) {
            return;
        }
        ++text;
    }
}

static bool protocol_revision_supported(void) {
    return LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision);
}

static bool framebuffer_response_valid(void) {
    struct limine_framebuffer_response *response =
        limine_framebuffer_request.response;

    return response != NULL && response->framebuffer_count > 0U &&
           response->framebuffers != NULL && response->framebuffers[0] != NULL;
}

#ifdef DAWN_KERNEL_TEST
static void qemu_debug_exit(uint8_t code) {
    outb((uint16_t)QEMU_DEBUG_EXIT_PORT, code);
}
#endif

void kernel_main(void) {
    serial_init();
    serial_write("DAWN-OS: kernel entry reached\n");
    serial_write("DAWN-OS: x86_64 freestanding bootstrap\n");

    if (!protocol_revision_supported()) {
        serial_write("DAWN-OS: FAIL: Limine base revision 6 was not accepted\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: Limine base revision 6 accepted\n");

    if (framebuffer_response_valid()) {
        serial_write("DAWN-OS: framebuffer request fulfilled\n");
    } else {
        serial_write("DAWN-OS: framebuffer unavailable (optional)\n");
    }

#ifdef DAWN_KERNEL_TEST
    if (!framebuffer_response_valid()) {
        serial_write("DAWN-TEST: FAIL: framebuffer response missing\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }

    serial_write("DAWN-TEST: PASS: Limine revision and framebuffer response\n");
    serial_write("DAWN-TEST: RESULT=PASS\n");
    qemu_debug_exit(QEMU_TEST_PASS);
#endif

    serial_write("DAWN-OS: Phase A bootstrap idle; interrupts remain disabled\n");
    cpu_halt_forever();
}
