/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_INSTALLER_H
#define DAWN_INSTALLER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "storage_manager.h"

enum install_stage {
    INSTALL_STAGE_IDLE = 0,
    INSTALL_STAGE_DISK_SELECT = 1,
    INSTALL_STAGE_PARTITIONING = 2,
    INSTALL_STAGE_FORMATTING = 3,
    INSTALL_STAGE_COPYING_FILES = 4,
    INSTALL_STAGE_BOOTLOADER_INSTALL = 5,
    INSTALL_STAGE_VERIFYING = 6,
    INSTALL_STAGE_COMPLETE = 7,
    INSTALL_STAGE_FAILED = 8
};

enum install_target_type {
    INSTALL_TARGET_MBR_BIOS = 0,
    INSTALL_TARGET_GPT_UEFI = 1,
    INSTALL_TARGET_HYBRID = 2
};

struct install_config {
    uint32_t target_disk_id;
    enum install_target_type target_type;
    uint64_t esp_size_bytes;      /* Typically 100 MiB for EFI system partition */
    uint64_t system_size_bytes;   /* Root system partition */
    uint64_t data_size_bytes;     /* User data partition */
    char hostname[32];
    char default_user[32];
};

struct install_status {
    enum install_stage current_stage;
    uint8_t progress_percent; /* 0..100 */
    char current_file[64];
    char status_message[128];
    bool failed;
    char error_message[128];
};

struct recovery_status {
    bool success;
    uint32_t issues_found;
    uint32_t issues_repaired;
    char details[128];
};

/* Core Installer API */
bool installer_initialize(void);
struct install_status *installer_get_status(void);
bool installer_start(const struct install_config *cfg);
bool installer_step(void);
bool installer_run_full(const struct install_config *cfg);
bool installer_log(const char *msg);

/* Recovery Environment & System Repair */
bool recovery_verify_filesystem(uint32_t disk_id, uint32_t partition_id, struct recovery_status *out);
bool recovery_reinstall_bootloader(uint32_t disk_id, enum install_target_type type, struct recovery_status *out);
int rescue_shell_execute(const char *cmd_line, char *output, size_t max_output);

/* Self-test */
bool installer_self_test(void);

#endif /* DAWN_INSTALLER_H */
