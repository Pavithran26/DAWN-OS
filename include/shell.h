/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_SHELL_H
#define DAWN_SHELL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "vfs.h"

#define SHELL_CMD_MAX      256U
#define SHELL_ARGS_MAX     16U
#define SHELL_HISTORY_MAX  16U
#define SHELL_ENV_MAX      16U
#define SHELL_OUT_MAX      4096U

struct shell_env_var {
    char name[32];
    char value[64];
    bool active;
};

struct shell_context {
    char cwd[VFS_MAX_PATH];
    char history[SHELL_HISTORY_MAX][SHELL_CMD_MAX];
    size_t history_count;
    int32_t history_cursor;
    struct shell_env_var env[SHELL_ENV_MAX];
    char output_buf[SHELL_OUT_MAX];
    size_t output_len;
    int last_exit_code;
};

bool shell_initialize(struct shell_context *ctx);
int shell_execute(struct shell_context *ctx, const char *cmdline);
void shell_output_write(struct shell_context *ctx, const char *text);
void shell_output_clear(struct shell_context *ctx);

/* Environment variables */
bool shell_set_env(struct shell_context *ctx, const char *name, const char *val);
const char *shell_get_env(struct shell_context *ctx, const char *name);

bool shell_self_test(void);

#endif /* DAWN_SHELL_H */
