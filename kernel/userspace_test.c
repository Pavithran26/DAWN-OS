/* SPDX-License-Identifier: Unlicense */
#include "userspace_test.h"
#include "elf_loader.h"
#include "process.h"
#include "scheduler.h"
#include "syscall.h"
#include "physical_memory.h"
#include "page_table.h"
#include "vfs.h"
#include "block_dev.h"
#include "iso9660.h"

#ifdef DAWN_KERNEL_TEST

#ifndef ELF_IDENT_CLASS_64
#define ELF_IDENT_CLASS_64 2U
#endif
#ifndef ELF_IDENT_DATA_LSB
#define ELF_IDENT_DATA_LSB 1U
#endif
#ifndef ELF_IDENT_VERSION
#define ELF_IDENT_VERSION  1U
#endif
#ifndef ELF_TYPE_EXEC
#define ELF_TYPE_EXEC      2U
#endif
#ifndef ELF_MACHINE_X86_64
#define ELF_MACHINE_X86_64 62U
#endif
#ifndef ELF_VERSION_CURRENT
#define ELF_VERSION_CURRENT 1U
#endif
#ifndef ELF_PROGRAM_LOAD
#define ELF_PROGRAM_LOAD   1U
#endif
#ifndef ELF_FLAG_EXECUTE
#define ELF_FLAG_EXECUTE   1U
#endif
#ifndef ELF_FLAG_READ
#define ELF_FLAG_READ      4U
#endif
#ifndef ELF64_PAGE_SIZE
#define ELF64_PAGE_SIZE    4096ULL
#endif

struct test_elf64_header {
    uint8_t  ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_header_offset;
    uint64_t section_header_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_header_entry_size;
    uint16_t program_header_count;
    uint16_t section_header_entry_size;
    uint16_t section_header_count;
    uint16_t section_header_string_index;
} __attribute__((packed));

struct test_elf64_program_header {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
} __attribute__((packed));

_Static_assert(sizeof(struct test_elf64_header) == 64U, "ELF64 header must be 64 bytes");
_Static_assert(sizeof(struct test_elf64_program_header) == 56U, "ELF64 program header must be 56 bytes");

static bool build_test_elf(void *buffer, size_t capacity, const uint8_t *code, size_t code_size, size_t *out_size) {
    if (buffer == NULL || out_size == NULL || capacity < 0x1000U + code_size) {
        return false;
    }

    uint8_t *bytes = (uint8_t *)buffer;
    for (size_t i = 0; i < 0x1000U + code_size; i++) {
        bytes[i] = 0;
    }

    struct test_elf64_header header = {0};
    header.ident[0] = 0x7fU;
    header.ident[1] = (uint8_t)'E';
    header.ident[2] = (uint8_t)'L';
    header.ident[3] = (uint8_t)'F';
    header.ident[4] = ELF_IDENT_CLASS_64;
    header.ident[5] = ELF_IDENT_DATA_LSB;
    header.ident[6] = ELF_IDENT_VERSION;
    header.type = ELF_TYPE_EXEC;
    header.machine = ELF_MACHINE_X86_64;
    header.version = ELF_VERSION_CURRENT;
    header.entry = 0x400000ULL;
    header.program_header_offset = sizeof(struct test_elf64_header);
    header.header_size = (uint16_t)sizeof(struct test_elf64_header);
    header.program_header_entry_size = (uint16_t)sizeof(struct test_elf64_program_header);
    header.program_header_count = 1U;

    struct test_elf64_program_header program = {0};
    program.type = ELF_PROGRAM_LOAD;
    program.flags = ELF_FLAG_READ | ELF_FLAG_EXECUTE;
    program.offset = 0x1000ULL;
    program.virtual_address = 0x400000ULL;
    program.physical_address = 0x400000ULL;
    program.file_size = (uint64_t)code_size;
    program.memory_size = ELF64_PAGE_SIZE;
    program.alignment = ELF64_PAGE_SIZE;

    /* Copy ELF header */
    for (size_t i = 0; i < sizeof(header); i++) {
        bytes[i] = ((const uint8_t *)&header)[i];
    }
    /* Copy program header */
    for (size_t i = 0; i < sizeof(program); i++) {
        bytes[sizeof(header) + i] = ((const uint8_t *)&program)[i];
    }
    /* Copy code at offset 0x1000 */
    for (size_t i = 0; i < code_size; i++) {
        bytes[0x1000U + i] = code[i];
    }

    *out_size = 0x1000U + code_size;
    return true;
}

static bool build_test_elf_with_payload(void *buffer, size_t capacity,
                                        const uint8_t *code, size_t code_size,
                                        const uint8_t *payload, size_t payload_size,
                                        size_t *out_size) {
    if (buffer == NULL || out_size == NULL || capacity < 0x1000U + 0x200U + payload_size) {
        return false;
    }

    uint8_t *bytes = (uint8_t *)buffer;
    for (size_t i = 0; i < 0x1000U + 0x200U + payload_size; i++) {
        bytes[i] = 0;
    }

    struct test_elf64_header header = {0};
    header.ident[0] = 0x7fU;
    header.ident[1] = (uint8_t)'E';
    header.ident[2] = (uint8_t)'L';
    header.ident[3] = (uint8_t)'F';
    header.ident[4] = ELF_IDENT_CLASS_64;
    header.ident[5] = ELF_IDENT_DATA_LSB;
    header.ident[6] = ELF_IDENT_VERSION;
    header.type = ELF_TYPE_EXEC;
    header.machine = ELF_MACHINE_X86_64;
    header.version = ELF_VERSION_CURRENT;
    header.entry = 0x400000ULL;
    header.program_header_offset = sizeof(struct test_elf64_header);
    header.header_size = (uint16_t)sizeof(struct test_elf64_header);
    header.program_header_entry_size = (uint16_t)sizeof(struct test_elf64_program_header);
    header.program_header_count = 1U;

    uint64_t total_payload = 0x200ULL + (uint64_t)payload_size;
    uint64_t mem_size = ((total_payload + ELF64_PAGE_SIZE - 1ULL) / ELF64_PAGE_SIZE) * ELF64_PAGE_SIZE;

    struct test_elf64_program_header program = {0};
    program.type = ELF_PROGRAM_LOAD;
    program.flags = ELF_FLAG_READ | ELF_FLAG_EXECUTE;
    program.offset = 0x1000ULL;
    program.virtual_address = 0x400000ULL;
    program.physical_address = 0x400000ULL;
    program.file_size = total_payload;
    program.memory_size = mem_size;
    program.alignment = ELF64_PAGE_SIZE;

    /* Copy ELF header */
    for (size_t i = 0; i < sizeof(header); i++) {
        bytes[i] = ((const uint8_t *)&header)[i];
    }
    /* Copy program header */
    for (size_t i = 0; i < sizeof(program); i++) {
        bytes[sizeof(header) + i] = ((const uint8_t *)&program)[i];
    }
    /* Copy code at offset 0x1000 */
    for (size_t i = 0; i < code_size; i++) {
        bytes[0x1000U + i] = code[i];
    }
    /* Copy payload at offset 0x1200 */
    if (payload != NULL && payload_size > 0U) {
        for (size_t i = 0; i < payload_size; i++) {
            bytes[0x1200U + i] = payload[i];
        }
    }

    *out_size = 0x1000U + 0x200U + payload_size;
    return true;
}

bool userspace_runtime_self_test(void) {
    struct physical_allocator_stats before = {0};
    struct physical_allocator_stats after = {0};
    physical_allocator_get_stats(&before);

    /* Test 1: File descriptor table operations */
    {
        uint64_t fd_pid = 0;
        if (!process_create(&fd_pid) || fd_pid == 0) return false;

        struct dawn_fd_entry entry = {0};
        /* Standard descriptors 0, 1, 2 must exist */
        if (!process_fd_get(fd_pid, 0, &entry) ||
            !process_fd_get(fd_pid, 1, &entry) ||
            !process_fd_get(fd_pid, 2, &entry)) {
            (void)process_mark_exited(fd_pid);
            (void)process_reap(fd_pid);
            return false;
        }

        /* Dup stdout (fd 1) */
        int dup_fd = -1;
        if (!process_fd_dup(fd_pid, 1, &dup_fd) || dup_fd < 3) {
            (void)process_mark_exited(fd_pid);
            (void)process_reap(fd_pid);
            return false;
        }

        /* Dup2 to fd 8 */
        if (!process_fd_dup2(fd_pid, 1, 8)) {
            (void)process_mark_exited(fd_pid);
            (void)process_reap(fd_pid);
            return false;
        }

        /* Close duplicated descriptors */
        if (!process_fd_close(fd_pid, dup_fd) || !process_fd_close(fd_pid, 8)) {
            (void)process_mark_exited(fd_pid);
            (void)process_reap(fd_pid);
            return false;
        }

        /* Closing an already-closed or invalid descriptor must return false */
        if (process_fd_close(fd_pid, dup_fd) || process_fd_close(fd_pid, 15)) {
            (void)process_mark_exited(fd_pid);
            (void)process_reap(fd_pid);
            return false;
        }

        if (!process_mark_exited(fd_pid) || !process_reap(fd_pid)) {
            return false;
        }
    }

    /* Test 2: Native Init (PID 1) execution under the scheduler */
    {
        static uint8_t init_image[8192];
        size_t init_size = 0;
        uint64_t init_pid = 0;
        const char *init_args[] = {"init", NULL};

        if (!elf64_build_init_image(init_image, sizeof(init_image), &init_size)) {
            return false;
        }

        if (!process_spawn(init_image, init_size, 1, init_args, NULL, &init_pid) || init_pid == 0) {
            return false;
        }

        /* Execute Init until it finishes its work and exits */
        if (!scheduler_run_until_idle()) {
            return false;
        }

        struct process_info pinfo = {0};
        if (!process_get_info(init_pid, &pinfo)) {
            return false;
        }

        /* Init must have completed successfully and exited with status 0 */
        if (pinfo.state != PROCESS_STATE_EXITED || pinfo.exit_status != 0) {
            return false;
        }

        if (!process_reap(init_pid)) {
            return false;
        }
    }

    /* Test 3: CPL3 Child process with non-zero exit status (exit 42) */
    {
        static const uint8_t exit42_code[] = {
            /* mov $42, %rdi */
            0x48U, 0xc7U, 0xc7U, 0x2aU, 0x00U, 0x00U, 0x00U,
            /* mov $2, %rax (DAWN_SYS_EXIT) */
            0x48U, 0xc7U, 0xc0U, 0x02U, 0x00U, 0x00U, 0x00U,
            /* int $0x80 */
            0xcdU, 0x80U,
            /* ud2 */
            0x0fU, 0x0bU
        };

        static uint8_t exit42_elf[8192];
        size_t exit42_size = 0;
        uint64_t child_pid = 0;
        const char *child_args[] = {"child42", NULL};

        if (!build_test_elf(exit42_elf, sizeof(exit42_elf), exit42_code, sizeof(exit42_code), &exit42_size)) {
            return false;
        }

        if (!process_spawn(exit42_elf, exit42_size, 1, child_args, NULL, &child_pid) || child_pid == 0) {
            return false;
        }

        if (!scheduler_run_until_idle()) {
            return false;
        }

        struct process_info cinfo = {0};
        if (!process_get_info(child_pid, &cinfo)) {
            return false;
        }

        if (cinfo.state != PROCESS_STATE_EXITED || cinfo.exit_status != 42) {
            return false;
        }

        if (!process_reap(child_pid)) {
            return false;
        }
    }

    /* Test 4: Fault isolation — user page fault (vector 14) terminates process, kernel survives */
    {
        static const uint8_t pf_code[] = {
            /* Dereference unmapped address 0xdeadbeef000 to trigger Page Fault (vector 14) */
            0x48U, 0xb8U, 0x00U, 0xf0U, 0xeeU, 0xbdU, 0xadU, 0x0dU, 0x00U, 0x00U,
            0x48U, 0x8bU, 0x00U, /* mov (%rax), %rax */
            /* ud2 */
            0x0fU, 0x0bU
        };

        static uint8_t pf_elf[8192];
        size_t pf_size = 0;
        uint64_t fault_pid = 0;
        const char *pf_args[] = {"fault_task", NULL};

        if (!build_test_elf(pf_elf, sizeof(pf_elf), pf_code, sizeof(pf_code), &pf_size)) {
            return false;
        }

        if (!process_spawn(pf_elf, pf_size, 1, pf_args, NULL, &fault_pid) || fault_pid == 0) {
            return false;
        }

        /* Scheduler runs faulting task; kernel exception handler must terminate child safely */
        if (!scheduler_run_until_idle()) {
            return false;
        }

        struct process_info finfo = {0};
        if (!process_get_info(fault_pid, &finfo)) {
            return false;
        }

        if (finfo.state != PROCESS_STATE_EXITED) {
            return false;
        }

        if (!process_reap(fault_pid)) {
            return false;
        }
    }

    /* Test 5: Fault isolation — user privileged instruction (cli in CPL3) triggers GPF (vector 13) */
    {
        static const uint8_t gpf_code[] = {
            /* cli instruction in CPL3 causes GPF */
            0xfaU,
            /* ud2 */
            0x0fU, 0x0bU
        };

        static uint8_t gpf_elf[8192];
        size_t gpf_size = 0;
        uint64_t gpf_pid = 0;
        const char *gpf_args[] = {"gpf_task", NULL};

        if (!build_test_elf(gpf_elf, sizeof(gpf_elf), gpf_code, sizeof(gpf_code), &gpf_size)) {
            return false;
        }

        if (!process_spawn(gpf_elf, gpf_size, 1, gpf_args, NULL, &gpf_pid) || gpf_pid == 0) {
            return false;
        }

        if (!scheduler_run_until_idle()) {
            return false;
        }

        struct process_info ginfo = {0};
        if (!process_get_info(gpf_pid, &ginfo)) {
            return false;
        }

        if (ginfo.state != PROCESS_STATE_EXITED) {
            return false;
        }

        if (!process_reap(gpf_pid)) {
            return false;
        }
    }

    /* Test 6: Malformed ELF and invalid argument rejection with clean rollback */
    {
        uint64_t dummy_pid = 0;
        struct physical_allocator_stats test6_before = {0};
        struct physical_allocator_stats test6_after = {0};
        physical_allocator_get_stats(&test6_before);

        /* Null image pointer must fail */
        if (process_spawn(NULL, 1024, 0, NULL, NULL, &dummy_pid)) {
            return false;
        }

        /* Zero image size must fail */
        uint8_t dummy_byte = 0;
        if (process_spawn(&dummy_byte, 0, 0, NULL, NULL, &dummy_pid)) {
            return false;
        }

        /* Null out_pid must fail */
        if (process_spawn(&dummy_byte, 1, 0, NULL, NULL, NULL)) {
            return false;
        }

        /* Malformed ELF magic must fail and roll back */
        static uint8_t bad_elf[512];
        for (size_t i = 0; i < sizeof(bad_elf); i++) bad_elf[i] = 0;
        bad_elf[0] = 0x7fU;
        bad_elf[1] = (uint8_t)'X';
        bad_elf[2] = (uint8_t)'Y';
        bad_elf[3] = (uint8_t)'Z';
        if (process_spawn(bad_elf, sizeof(bad_elf), 0, NULL, NULL, &dummy_pid)) {
            return false;
        }

        /* Verify zero page frame leaks during failed spawns */
        physical_allocator_get_stats(&test6_after);
        if (test6_before.free_frames != test6_after.free_frames) {
            return false;
        }
    }

    /* Test 7: Memory boundary isolation and kernel space protection */
    {
        /* NULL address range must be rejected */
        if (vm_user_range_valid(0x0ULL, 4096ULL, true)) {
            return false;
        }

        /* Kernel virtual base must be rejected */
        if (vm_user_range_valid(0xffffffff80000000ULL, 4096ULL, false)) {
            return false;
        }

        /* HHDM virtual range must be rejected */
        if (vm_user_range_valid(0xffff800000000000ULL, 4096ULL, false)) {
            return false;
        }

        /* Canonical address ceiling overflow must be rejected */
        if (vm_user_range_valid((1ULL << 47) - 2048ULL, 4096ULL, false)) {
            return false;
        }

        /* Syscall boundary protection: passing kernel buffer to SYS_WRITE must return -EFAULT */
        int64_t w_ret = syscall_dispatch((uint64_t)DAWN_SYS_WRITE, (uint64_t)DAWN_STDOUT_FD,
                                         0xffffffff80000000ULL, 32ULL, 0ULL, 0ULL, 0ULL);
        if (w_ret != -(int64_t)DAWN_ERRNO_EFAULT) {
            return false;
        }
    }

    /* Test 8: Process wait on non-existent child */
    {
        uint64_t exited_child = 0ULL;
        if (process_has_children(99999ULL, -1LL, &exited_child)) {
            return false;
        }
    }

    /* Test 9: Repeated spawn/exit/reap cycles and process slot reuse */
    {
        static const uint8_t cycle_code[] = {
            /* mov $2, %rax (DAWN_SYS_EXIT) */
            0x48U, 0xc7U, 0xc0U, 0x02U, 0x00U, 0x00U, 0x00U,
            /* mov $7, %rdi (status 7) */
            0x48U, 0xc7U, 0xc7U, 0x07U, 0x00U, 0x00U, 0x00U,
            /* int $0x80 */
            0xcdU, 0x80U,
            /* ud2 */
            0x0fU, 0x0bU
        };

        static uint8_t cycle_elf[8192];
        size_t cycle_size = 0;
        if (!build_test_elf(cycle_elf, sizeof(cycle_elf), cycle_code, sizeof(cycle_code), &cycle_size)) {
            return false;
        }

        for (uint32_t i = 0U; i < 5U; i++) {
            uint64_t cpid = 0ULL;
            const char *cargs[] = {"cycle_task", NULL};
            if (!process_spawn(cycle_elf, cycle_size, 1, cargs, NULL, &cpid) || cpid == 0ULL) {
                return false;
            }

            if (!scheduler_run_until_idle()) {
                return false;
            }

            struct process_info cinfo = {0};
            if (!process_get_info(cpid, &cinfo)) {
                return false;
            }

            if (cinfo.state != PROCESS_STATE_EXITED || cinfo.exit_status != 7) {
                return false;
            }

            if (!process_reap(cpid)) {
                return false;
            }
        }
    }

    /* Verify no physical page frame leaks across all process creations and teardowns */
    physical_allocator_get_stats(&after);
    if (before.free_frames != after.free_frames) {
        return false;
    }

    return true;
}

bool userspace_spawn_wait_test(void) {
    struct physical_allocator_stats before = {0};
    struct physical_allocator_stats after = {0};
    physical_allocator_get_stats(&before);

    /* 1. Build child ELF (exits with status 42) */
    static const uint8_t exit42_code[] = {
        /* mov $42, %rdi */
        0x48U, 0xc7U, 0xc7U, 0x2aU, 0x00U, 0x00U, 0x00U,
        /* mov $2, %rax (DAWN_SYS_EXIT) */
        0x48U, 0xc7U, 0xc0U, 0x02U, 0x00U, 0x00U, 0x00U,
        /* int $0x80 */
        0xcdU, 0x80U,
        /* ud2 */
        0x0fU, 0x0bU
    };

    static uint8_t child_elf[8192];
    size_t child_size = 0;
    if (!build_test_elf(child_elf, sizeof(child_elf), exit42_code, sizeof(exit42_code), &child_size)) {
        return false;
    }

    /* 2. Parent code */
    uint8_t parent_code[] = {
        /* Step 1: Negative wait - non-existent child 99999 */
        0x48U, 0xc7U, 0xc7U, 0x9fU, 0x86U, 0x01U, 0x00U, /* mov $99999, %rdi */
        0x48U, 0x31U, 0xf6U,                               /* xor %rsi, %rsi */
        0x48U, 0xc7U, 0xc0U, 0x05U, 0x00U, 0x00U, 0x00U, /* mov $5, %rax (SYS_WAIT) */
        0xcdU, 0x80U,                                     /* int $0x80 */
        0x48U, 0x83U, 0xf8U, 0xf6U,                         /* cmp $-10, %rax (-ECHILD) */
        0x74U, 0x12U,                                     /* je +18 */
        0x48U, 0xc7U, 0xc7U, 0x5bU, 0x00U, 0x00U, 0x00U, /* mov $91, %rdi */
        0x48U, 0xc7U, 0xc0U, 0x02U, 0x00U, 0x00U, 0x00U, /* mov $2, %rax (SYS_EXIT) */
        0xcdU, 0x80U,                                     /* int $0x80 */
        0x0fU, 0x0bU,                                     /* ud2 */

        /* Step 2: Negative wait - kernel pointer */
        0x48U, 0xc7U, 0xc7U, 0x9fU, 0x86U, 0x01U, 0x00U, /* mov $99999, %rdi */
        0x48U, 0xbeU, 0x00U, 0x00U, 0x00U, 0x80U, 0xffU, 0xffU, 0xffU, 0xffU, /* movabs $0xffffffff80000000, %rsi */
        0x48U, 0xc7U, 0xc0U, 0x05U, 0x00U, 0x00U, 0x00U, /* mov $5, %rax (SYS_WAIT) */
        0xcdU, 0x80U,                                     /* int $0x80 */
        0x48U, 0x83U, 0xf8U, 0xf2U,                         /* cmp $-14, %rax (-EFAULT) */
        0x74U, 0x12U,                                     /* je +18 */
        0x48U, 0xc7U, 0xc7U, 0x5cU, 0x00U, 0x00U, 0x00U, /* mov $92, %rdi */
        0x48U, 0xc7U, 0xc0U, 0x02U, 0x00U, 0x00U, 0x00U, /* mov $2, %rax (SYS_EXIT) */
        0xcdU, 0x80U,                                     /* int $0x80 */
        0x0fU, 0x0bU,                                     /* ud2 */

        /* Step 3: Spawn child */
        0x48U, 0xc7U, 0xc7U, 0x00U, 0x02U, 0x40U, 0x00U, /* mov $0x400200, %rdi */
        0x48U, 0xc7U, 0xc6U, 0x00U, 0x00U, 0x00U, 0x00U, /* mov $child_size, %rsi (offset 103) */
        0x48U, 0x31U, 0xd2U,                               /* xor %rdx, %rdx */
        0x4dU, 0x31U, 0xd2U,                               /* xor %r10, %r10 */
        0x48U, 0xc7U, 0xc0U, 0x09U, 0x00U, 0x00U, 0x00U, /* mov $9, %rax (SYS_SPAWN) */
        0xcdU, 0x80U,                                     /* int $0x80 */
        0x48U, 0x83U, 0xf8U, 0x00U,                         /* cmp $0, %rax */
        0x7fU, 0x12U,                                     /* jg +18 */
        0x48U, 0xc7U, 0xc7U, 0x5dU, 0x00U, 0x00U, 0x00U, /* mov $93, %rdi */
        0x48U, 0xc7U, 0xc0U, 0x02U, 0x00U, 0x00U, 0x00U, /* mov $2, %rax (SYS_EXIT) */
        0xcdU, 0x80U,                                     /* int $0x80 */
        0x0fU, 0x0bU,                                     /* ud2 */
        0x48U, 0x89U, 0xc3U,                               /* mov %rax, %rbx (child_pid) */

        /* Step 4: Wait for child */
        0x48U, 0x83U, 0xecU, 0x10U,                         /* sub $16, %rsp */
        0x48U, 0xc7U, 0x04U, 0x24U, 0x00U, 0x00U, 0x00U, 0x00U, /* movq $0, (%rsp) */
        0x48U, 0x89U, 0xdfU,                               /* mov %rbx, %rdi */
        0x48U, 0x89U, 0xe6U,                               /* mov %rsp, %rsi */
        0x48U, 0xc7U, 0xc0U, 0x05U, 0x00U, 0x00U, 0x00U, /* mov $5, %rax (SYS_WAIT) */
        0xcdU, 0x80U,                                     /* int $0x80 */
        0x48U, 0x39U, 0xd8U,                               /* cmp %rbx, %rax */
        0x74U, 0x12U,                                     /* je +18 */
        0x48U, 0xc7U, 0xc7U, 0x5eU, 0x00U, 0x00U, 0x00U, /* mov $94, %rdi */
        0x48U, 0xc7U, 0xc0U, 0x02U, 0x00U, 0x00U, 0x00U, /* mov $2, %rax (SYS_EXIT) */
        0xcdU, 0x80U,                                     /* int $0x80 */
        0x0fU, 0x0bU,                                     /* ud2 */
        0x48U, 0x8bU, 0x04U, 0x24U,                         /* mov (%rsp), %rax */
        0x48U, 0x83U, 0xc4U, 0x10U,                         /* add $16, %rsp */
        0x48U, 0x83U, 0xf8U, 0x2aU,                         /* cmp $42, %rax */
        0x74U, 0x12U,                                     /* je +18 */
        0x48U, 0xc7U, 0xc7U, 0x5fU, 0x00U, 0x00U, 0x00U, /* mov $95, %rdi */
        0x48U, 0xc7U, 0xc0U, 0x02U, 0x00U, 0x00U, 0x00U, /* mov $2, %rax (SYS_EXIT) */
        0xcdU, 0x80U,                                     /* int $0x80 */
        0x0fU, 0x0bU,                                     /* ud2 */

        /* Step 5: Exit 0 */
        0x48U, 0x31U, 0xffU,                               /* xor %rdi, %rdi */
        0x48U, 0xc7U, 0xc0U, 0x02U, 0x00U, 0x00U, 0x00U, /* mov $2, %rax (SYS_EXIT) */
        0xcdU, 0x80U,                                     /* int $0x80 */
        0x0fU, 0x0bU                                      /* ud2 */
    };

    /* Patch child_size into parent_code at offset 103 */
    parent_code[103] = (uint8_t)(child_size & 0xffU);
    parent_code[104] = (uint8_t)((child_size >> 8) & 0xffU);
    parent_code[105] = (uint8_t)((child_size >> 16) & 0xffU);
    parent_code[106] = (uint8_t)((child_size >> 24) & 0xffU);

    static uint8_t parent_elf[16384];
    size_t parent_size = 0;
    if (!build_test_elf_with_payload(parent_elf, sizeof(parent_elf),
                                     parent_code, sizeof(parent_code),
                                     child_elf, child_size, &parent_size)) {
        return false;
    }

    uint64_t parent_pid = 0;
    const char *parent_args[] = {"parent_task", NULL};
    if (!process_spawn(parent_elf, parent_size, 1, parent_args, NULL, &parent_pid) || parent_pid == 0) {
        return false;
    }

    if (!scheduler_run_until_idle()) {
        return false;
    }

    struct process_info pinfo = {0};
    if (!process_get_info(parent_pid, &pinfo)) {
        return false;
    }

    if (pinfo.state != PROCESS_STATE_EXITED || pinfo.exit_status != 0) {
        return false;
    }

    if (!process_reap(parent_pid)) {
        return false;
    }

    /* Verify no physical frame leaks */
    physical_allocator_get_stats(&after);
    if (before.free_frames != after.free_frames) {
        return false;
    }

    return true;
}

bool userspace_exec_test(void) {
    struct physical_allocator_stats before = {0};
    struct physical_allocator_stats after = {0};
    physical_allocator_get_stats(&before);

    /* 1. Build replacement ELF (exits with status 84) */
    static const uint8_t exit84_code[] = {
        /* mov $84, %rdi */
        0x48U, 0xc7U, 0xc7U, 0x54U, 0x00U, 0x00U, 0x00U,
        /* mov $2, %rax (DAWN_SYS_EXIT) */
        0x48U, 0xc7U, 0xc0U, 0x02U, 0x00U, 0x00U, 0x00U,
        /* int $0x80 */
        0xcdU, 0x80U,
        /* ud2 */
        0x0fU, 0x0bU
    };

    static uint8_t repl_elf[8192];
    size_t repl_size = 0;
    if (!build_test_elf(repl_elf, sizeof(repl_elf), exit84_code, sizeof(exit84_code), &repl_size)) {
        return false;
    }

    /* 2. Initial process code (invokes SYS_EXEC with replacement ELF) */
    uint8_t init_code[] = {
        0x48U, 0xc7U, 0xc7U, 0x00U, 0x02U, 0x40U, 0x00U, /* mov $0x400200, %rdi */
        0x48U, 0xc7U, 0xc6U, 0x00U, 0x00U, 0x00U, 0x00U, /* mov $repl_size, %rsi (offset 10) */
        0x48U, 0x31U, 0xd2U,                               /* xor %rdx, %rdx */
        0x4dU, 0x31U, 0xd2U,                               /* xor %r10, %r10 */
        0x48U, 0xc7U, 0xc0U, 0x0aU, 0x00U, 0x00U, 0x00U, /* mov $10, %rax (DAWN_SYS_EXEC) */
        0xcdU, 0x80U,                                     /* int $0x80 */

        /* Fail exit if exec returns */
        0x48U, 0xc7U, 0xc7U, 0x60U, 0x00U, 0x00U, 0x00U, /* mov $96, %rdi */
        0x48U, 0xc7U, 0xc0U, 0x02U, 0x00U, 0x00U, 0x00U, /* mov $2, %rax (SYS_EXIT) */
        0xcdU, 0x80U,                                     /* int $0x80 */
        0x0fU, 0x0bU                                      /* ud2 */
    };

    /* Patch repl_size into init_code at offset 10 */
    init_code[10] = (uint8_t)(repl_size & 0xffU);
    init_code[11] = (uint8_t)((repl_size >> 8) & 0xffU);
    init_code[12] = (uint8_t)((repl_size >> 16) & 0xffU);
    init_code[13] = (uint8_t)((repl_size >> 24) & 0xffU);

    static uint8_t init_elf[16384];
    size_t init_proc_size = 0;
    if (!build_test_elf_with_payload(init_elf, sizeof(init_elf),
                                     init_code, sizeof(init_code),
                                     repl_elf, repl_size, &init_proc_size)) {
        return false;
    }

    uint64_t exec_pid = 0;
    const char *exec_args[] = {"exec_task", NULL};
    if (!process_spawn(init_elf, init_proc_size, 1, exec_args, NULL, &exec_pid) || exec_pid == 0) {
        return false;
    }

    if (!scheduler_run_until_idle()) {
        return false;
    }

    struct process_info einfo = {0};
    if (!process_get_info(exec_pid, &einfo)) {
        return false;
    }

    /* The process must have preserved its PID and exited with replacement status 84 */
    if (einfo.state != PROCESS_STATE_EXITED || einfo.exit_status != 84) {
        return false;
    }

    if (!process_reap(exec_pid)) {
        return false;
    }

    /* Verify no physical frame leaks */
    physical_allocator_get_stats(&after);
    if (before.free_frames != after.free_frames) {
        return false;
    }

    return true;
}

bool userspace_filesystem_test(void) {
    struct physical_allocator_stats before = {0};
    struct physical_allocator_stats after = {0};
    physical_allocator_get_stats(&before);

    /* Ensure /mnt/testdisk is mounted */
    struct block_device *vda = block_dev_lookup("vda");
    if (vda != NULL) {
        struct vfs_node *test_node = NULL;
        if (!vfs_resolve_path("/mnt/testdisk/test.txt", &test_node)) {
            struct vfs_node *vda_root = iso9660_mount_device(vda);
            if (vda_root != NULL) {
                (void)vfs_mkdir("/mnt", 0755U, NULL);
                (void)vfs_mkdir("/mnt/testdisk", 0755U, NULL);
                (void)vfs_mount("/mnt/testdisk", vda_root, "iso9660");
            }
        }
    }

    /* 1. Code for filesystem test */
    static const uint8_t fs_code[] = {
        /* 1. Open "/mnt/testdisk/test.txt" */
        0x48U, 0xc7U, 0xc7U, 0x00U, 0x02U, 0x40U, 0x00U, /* mov $0x400200, %rdi */
        0x48U, 0x31U, 0xf6U,                               /* xor %rsi, %rsi (DAWN_O_RDONLY) */
        0x48U, 0x31U, 0xd2U,                               /* xor %rdx, %rdx */
        0x48U, 0xc7U, 0xc0U, 0x0bU, 0x00U, 0x00U, 0x00U, /* mov $11, %rax (DAWN_SYS_OPEN) */
        0xcdU, 0x80U,                                     /* int $0x80 */
        0x48U, 0x83U, 0xf8U, 0x00U,                         /* cmp $0, %rax */
        0x7dU, 0x12U,                                     /* jge +18 */
        0x48U, 0xc7U, 0xc7U, 0x51U, 0x00U, 0x00U, 0x00U, /* mov $81, %rdi */
        0x48U, 0xc7U, 0xc0U, 0x02U, 0x00U, 0x00U, 0x00U, /* mov $2, %rax (SYS_EXIT) */
        0xcdU, 0x80U,                                     /* int $0x80 */
        0x0fU, 0x0bU,                                     /* ud2 */
        0x48U, 0x89U, 0xc3U,                               /* mov %rax, %rbx (fd) */

        /* 2. Read 64 bytes */
        0x48U, 0x83U, 0xecU, 0x40U,                         /* sub $64, %rsp */
        0x48U, 0x89U, 0xdfU,                               /* mov %rbx, %rdi */
        0x48U, 0x89U, 0xe6U,                               /* mov %rsp, %rsi */
        0x48U, 0xc7U, 0xc2U, 0x40U, 0x00U, 0x00U, 0x00U, /* mov $64, %rdx */
        0x48U, 0xc7U, 0xc0U, 0x06U, 0x00U, 0x00U, 0x00U, /* mov $6, %rax (DAWN_SYS_READ) */
        0xcdU, 0x80U,                                     /* int $0x80 */
        0x48U, 0x83U, 0xf8U, 0x21U,                         /* cmp $33, %rax */
        0x7dU, 0x12U,                                     /* jge +18 */
        0x48U, 0xc7U, 0xc7U, 0x52U, 0x00U, 0x00U, 0x00U, /* mov $82, %rdi */
        0x48U, 0xc7U, 0xc0U, 0x02U, 0x00U, 0x00U, 0x00U, /* mov $2, %rax (SYS_EXIT) */
        0xcdU, 0x80U,                                     /* int $0x80 */
        0x0fU, 0x0bU,                                     /* ud2 */

        /* Compare 'D','A','W','N' */
        0x8bU, 0x0cU, 0x24U,                               /* mov (%rsp), %ecx */
        0x81U, 0xf9U, 0x44U, 0x41U, 0x57U, 0x4eU,         /* cmp $0x4e574144, %ecx */
        0x74U, 0x12U,                                     /* je +18 */
        0x48U, 0xc7U, 0xc7U, 0x53U, 0x00U, 0x00U, 0x00U, /* mov $83, %rdi */
        0x48U, 0xc7U, 0xc0U, 0x02U, 0x00U, 0x00U, 0x00U, /* mov $2, %rax (SYS_EXIT) */
        0xcdU, 0x80U,                                     /* int $0x80 */
        0x0fU, 0x0bU,                                     /* ud2 */

        /* 3. EOF check */
        0x48U, 0x89U, 0xdfU,                               /* mov %rbx, %rdi */
        0x48U, 0x89U, 0xe6U,                               /* mov %rsp, %rsi */
        0x48U, 0xc7U, 0xc2U, 0x40U, 0x00U, 0x00U, 0x00U, /* mov $64, %rdx */
        0x48U, 0xc7U, 0xc0U, 0x06U, 0x00U, 0x00U, 0x00U, /* mov $6, %rax (DAWN_SYS_READ) */
        0xcdU, 0x80U,                                     /* int $0x80 */
        0x48U, 0x83U, 0xf8U, 0x00U,                         /* cmp $0, %rax */
        0x74U, 0x12U,                                     /* je +18 */
        0x48U, 0xc7U, 0xc7U, 0x54U, 0x00U, 0x00U, 0x00U, /* mov $84, %rdi */
        0x48U, 0xc7U, 0xc0U, 0x02U, 0x00U, 0x00U, 0x00U, /* mov $2, %rax (SYS_EXIT) */
        0xcdU, 0x80U,                                     /* int $0x80 */
        0x0fU, 0x0bU,                                     /* ud2 */

        /* 4. Write rejection */
        0x48U, 0x89U, 0xdfU,                               /* mov %rbx, %rdi */
        0x48U, 0x89U, 0xe6U,                               /* mov %rsp, %rsi */
        0x48U, 0xc7U, 0xc2U, 0x0aU, 0x00U, 0x00U, 0x00U, /* mov $10, %rdx */
        0x48U, 0xc7U, 0xc0U, 0x01U, 0x00U, 0x00U, 0x00U, /* mov $1, %rax (DAWN_SYS_WRITE) */
        0xcdU, 0x80U,                                     /* int $0x80 */
        0x48U, 0x83U, 0xf8U, 0x00U,                         /* cmp $0, %rax */
        0x7cU, 0x12U,                                     /* jl +18 */
        0x48U, 0xc7U, 0xc7U, 0x55U, 0x00U, 0x00U, 0x00U, /* mov $85, %rdi */
        0x48U, 0xc7U, 0xc0U, 0x02U, 0x00U, 0x00U, 0x00U, /* mov $2, %rax (SYS_EXIT) */
        0xcdU, 0x80U,                                     /* int $0x80 */
        0x0fU, 0x0bU,                                     /* ud2 */

        /* 5. Close */
        0x48U, 0x89U, 0xdfU,                               /* mov %rbx, %rdi */
        0x48U, 0xc7U, 0xc0U, 0x07U, 0x00U, 0x00U, 0x00U, /* mov $7, %rax (DAWN_SYS_CLOSE) */
        0xcdU, 0x80U,                                     /* int $0x80 */
        0x48U, 0x83U, 0xf8U, 0x00U,                         /* cmp $0, %rax */
        0x74U, 0x12U,                                     /* je +18 */
        0x48U, 0xc7U, 0xc7U, 0x56U, 0x00U, 0x00U, 0x00U, /* mov $86, %rdi */
        0x48U, 0xc7U, 0xc0U, 0x02U, 0x00U, 0x00U, 0x00U, /* mov $2, %rax (SYS_EXIT) */
        0xcdU, 0x80U,                                     /* int $0x80 */
        0x0fU, 0x0bU,                                     /* ud2 */

        /* 6. Exit 0 */
        0x48U, 0x83U, 0xc4U, 0x40U,                         /* add $64, %rsp */
        0x48U, 0x31U, 0xffU,                               /* xor %rdi, %rdi */
        0x48U, 0xc7U, 0xc0U, 0x02U, 0x00U, 0x00U, 0x00U, /* mov $2, %rax (SYS_EXIT) */
        0xcdU, 0x80U,                                     /* int $0x80 */
        0x0fU, 0x0bU                                      /* ud2 */
    };

    static const char path_payload[] = "/mnt/testdisk/test.txt";
    static uint8_t fs_elf[8192];
    size_t fs_size = 0;
    if (!build_test_elf_with_payload(fs_elf, sizeof(fs_elf),
                                     fs_code, sizeof(fs_code),
                                     (const uint8_t *)path_payload, sizeof(path_payload),
                                     &fs_size)) {
        return false;
    }

    uint64_t fs_pid = 0;
    const char *fs_args[] = {"fs_task", NULL};
    if (!process_spawn(fs_elf, fs_size, 1, fs_args, NULL, &fs_pid) || fs_pid == 0) {
        return false;
    }

    if (!scheduler_run_until_idle()) {
        return false;
    }

    struct process_info fsinfo = {0};
    if (!process_get_info(fs_pid, &fsinfo)) {
        return false;
    }

    if (fsinfo.state != PROCESS_STATE_EXITED || fsinfo.exit_status != 0) {
        return false;
    }

    if (!process_reap(fs_pid)) {
        return false;
    }

    /* Verify no physical frame leaks */
    physical_allocator_get_stats(&after);
    if (before.free_frames != after.free_frames) {
        return false;
    }

    return true;
}

#endif /* DAWN_KERNEL_TEST */
