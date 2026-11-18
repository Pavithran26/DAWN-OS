/* SPDX-License-Identifier: Unlicense */
#include "power.h"
#include "gfx.h"

#define SERIAL_COM1 0x3F8U

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static void serial_write(const char *s) {
    if (s == NULL) return;
    while (*s != '\0') {
        for (uint32_t a = 0; a < 10000; a++) {
            if ((inb((uint16_t)(SERIAL_COM1 + 5U)) & 0x20U) != 0U) {
                outb(SERIAL_COM1, (uint8_t)*s);
                break;
            }
        }
        s++;
    }
}

static size_t hex_to_str(uint64_t val, char *buf) {
    const char hex_chars[] = "0123456789ABCDEF";
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 15; i >= 0; i--) {
        buf[2 + (15 - i)] = hex_chars[(val >> (i * 4)) & 0xFU];
    }
    buf[18] = '\0';
    return 18;
}

bool power_initialize(void) {
    return true;
}

void power_cpu_idle(void) {
    __asm__ volatile ("sti; hlt" : : : "memory");
}

void power_reboot(void) {
    serial_write("\nDAWN-OS: Initiating hardware reset / reboot...\n");

    /* 1. Pulse 8042 PS/2 keyboard controller reset line */
    uint8_t status;
    do {
        status = inb(0x64);
        if (status & 0x01) (void)inb(0x60);
    } while (status & 0x02);
    outb(0x64, 0xFE);

    /* 2. ACPI reset register fallback (I/O port 0xCF9 write 0x06) */
    outb(0xCF9, 0x06);

    /* 3. Triple fault fallback */
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) null_idt = { 0, 0 };
    __asm__ volatile ("lidt %0; int3" : : "m"(null_idt));

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

void power_shutdown(void) {
    serial_write("\nDAWN-OS: Initiating system shutdown...\n");

    /* 1. QEMU ACPI shutdown port */
    outw(0x604, 0x2000);

    /* 2. Bochs / older QEMU port */
    outw(0xB004, 0x2000);

    /* 3. VirtualBox ACPI shutdown */
    outw(0x4004, 0x3400);

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

bool power_format_crash_report(const char *reason, const struct cpu_crash_context *ctx, char *buf, size_t max_len) {
    if (buf == NULL || max_len < 256) return false;

    size_t pos = 0;
    const char *header = "DAWN-OS KERNEL PANIC: ";
    while (*header && pos + 1 < max_len) buf[pos++] = *header++;

    if (reason != NULL) {
        while (*reason && pos + 1 < max_len) buf[pos++] = *reason++;
    }
    if (pos + 1 < max_len) buf[pos++] = '\n';

    if (ctx != NULL) {
        #define APPEND_REG(name, val) do { \
            for (const char *n = name "="; *n && pos + 1 < max_len; n++) buf[pos++] = *n; \
            char hbuf[20]; hex_to_str(val, hbuf); \
            for (char *h = hbuf; *h && pos + 1 < max_len; h++) buf[pos++] = *h; \
            if (pos + 1 < max_len) buf[pos++] = ' '; \
        } while (0)

        APPEND_REG("RIP", ctx->rip);
        APPEND_REG("RSP", ctx->rsp);
        APPEND_REG("RBP", ctx->rbp);
        if (pos + 1 < max_len) buf[pos++] = '\n';
        APPEND_REG("RAX", ctx->rax);
        APPEND_REG("RBX", ctx->rbx);
        APPEND_REG("RCX", ctx->rcx);
        APPEND_REG("RDX", ctx->rdx);
        if (pos + 1 < max_len) buf[pos++] = '\n';
        APPEND_REG("RSI", ctx->rsi);
        APPEND_REG("RDI", ctx->rdi);
        APPEND_REG("FLG", ctx->rflags);
        if (pos + 1 < max_len) buf[pos++] = '\n';
        APPEND_REG("CR2", ctx->cr2);
        APPEND_REG("CR3", ctx->cr3);
        if (pos + 1 < max_len) buf[pos++] = '\n';
    }

    buf[pos] = '\0';
    return true;
}

void kernel_panic(const char *reason, const struct cpu_crash_context *ctx) {
    __asm__ volatile ("cli");

    char report[512] = {0};
    power_format_crash_report(reason, ctx, report, sizeof(report));
    serial_write("\n========================================\n");
    serial_write(report);
    serial_write("========================================\nSystem halted.\n");

    /* Render Emerald Screen of Death if framebuffer active */
    struct gfx_surface *fb = gfx_get_framebuffer();
    if (fb != NULL && fb->buffer != NULL) {
        gfx_draw_rect_filled(fb, 0, 0, fb->width, fb->height, 0xFF05150AU);
        gfx_draw_rect_filled(fb, 0, 0, fb->width, 48, 0xFF00C853U);
        gfx_draw_string(fb, 32, 16, "DAWN-OS SYSTEM CRASH — EMERALD DIAGNOSTIC SCREEN", 0xFF05150AU);

        int32_t y = 80;
        gfx_draw_string(fb, 32, y, "A critical kernel exception has occurred and execution was halted.", 0xFFFFFFFFU);
        y += 30;
        gfx_draw_string(fb, 32, y, "Reason: ", 0xFF00E676U);
        gfx_draw_string(fb, 96, y, (reason != NULL) ? reason : "Unknown Fault", 0xFFFFFFFFU);
        y += 40;

        if (ctx != NULL) {
            char reg_line[64];
            char h[20];

            hex_to_str(ctx->rip, h);
            gfx_draw_string(fb, 32, y, "RIP: ", 0xFF00E676U);
            gfx_draw_string(fb, 72, y, h, 0xFFFFFFFFU);

            hex_to_str(ctx->rsp, h);
            gfx_draw_string(fb, 260, y, "RSP: ", 0xFF00E676U);
            gfx_draw_string(fb, 300, y, h, 0xFFFFFFFFU);
            y += 24;

            hex_to_str(ctx->rax, h);
            gfx_draw_string(fb, 32, y, "RAX: ", 0xFF00E676U);
            gfx_draw_string(fb, 72, y, h, 0xFFFFFFFFU);

            hex_to_str(ctx->rbx, h);
            gfx_draw_string(fb, 260, y, "RBX: ", 0xFF00E676U);
            gfx_draw_string(fb, 300, y, h, 0xFFFFFFFFU);
            y += 24;

            hex_to_str(ctx->rcx, h);
            gfx_draw_string(fb, 32, y, "RCX: ", 0xFF00E676U);
            gfx_draw_string(fb, 72, y, h, 0xFFFFFFFFU);

            hex_to_str(ctx->rdx, h);
            gfx_draw_string(fb, 260, y, "RDX: ", 0xFF00E676U);
            gfx_draw_string(fb, 300, y, h, 0xFFFFFFFFU);
            y += 24;

            hex_to_str(ctx->cr2, h);
            gfx_draw_string(fb, 32, y, "CR2: ", 0xFF00E676U);
            gfx_draw_string(fb, 72, y, h, 0xFFFFFFFFU);

            hex_to_str(ctx->cr3, h);
            gfx_draw_string(fb, 260, y, "CR3: ", 0xFF00E676U);
            gfx_draw_string(fb, 300, y, h, 0xFFFFFFFFU);
            y += 40;
            (void)reg_line;
        }

        gfx_draw_string(fb, 32, y, "Please restart your computer. Diagnostic telemetry was recorded to serial COM1.", 0xFF80CBC4U);
        gfx_flip();
    }

    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void kernel_panic_simple(const char *reason) {
    kernel_panic(reason, NULL);
}

/* Self-test */
bool power_self_test(void) {
    if (!power_initialize()) return false;

    struct cpu_crash_context ctx = {
        .rip = 0xFFFF800000102000ULL,
        .rsp = 0xFFFF800000200000ULL,
        .rbp = 0xFFFF800000200040ULL,
        .rax = 0x1234ULL,
        .rbx = 0x5678ULL,
        .rcx = 0x9ABCULL,
        .rdx = 0xDEF0ULL,
        .rsi = 0x1111ULL,
        .rdi = 0x2222ULL,
        .rflags = 0x202ULL,
        .cr2 = 0x00000000DEADBEEFULL,
        .cr3 = 0x0000000001000000ULL
    };

    char buf[512] = {0};
    if (!power_format_crash_report("Page Fault in User Space", &ctx, buf, sizeof(buf))) return false;

    /* Verify formatted content */
    if (buf[0] != 'D' || buf[1] != 'A' || buf[2] != 'W' || buf[3] != 'N') return false;

    return true;
}
