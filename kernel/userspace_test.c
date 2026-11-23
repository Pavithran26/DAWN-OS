/* SPDX-License-Identifier: Unlicense */
#include "userspace_test.h"
#include "elf_loader.h"
#include "process.h"
#include "scheduler.h"
#include "syscall.h"
#include "physical_memory.h"
#include "page_table.h"

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

#endif /* DAWN_KERNEL_TEST */
