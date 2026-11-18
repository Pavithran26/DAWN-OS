/* SPDX-License-Identifier: Unlicense */
#include "shell.h"
#include "process.h"
#include "storage_manager.h"
#include "physical_memory.h"
#include "file_explorer.h"

static inline void str_copy(char *dest, const char *src, size_t max_len) {
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < max_len) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static inline bool str_equal(const char *s1, const char *s2) {
    size_t i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] != s2[i]) return false;
        i++;
    }
    return s1[i] == s2[i];
}

static inline size_t str_len(const char *s) {
    size_t len = 0;
    while (s[len] != '\0') len++;
    return len;
}

static inline void str_append(char *dest, const char *src, size_t max_len) {
    size_t dlen = str_len(dest);
    size_t i = 0;
    while (src[i] != '\0' && dlen + i + 1 < max_len) {
        dest[dlen + i] = src[i];
        i++;
    }
    dest[dlen + i] = '\0';
}

static inline void u64_to_str(uint64_t val, char *buf) {
    char tmp[24];
    size_t i = 0;
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while (val > 0) {
        tmp[i++] = (char)('0' + (val % 10U));
        val /= 10U;
    }
    for (size_t j = 0; j < i; j++) {
        buf[j] = tmp[i - 1 - j];
    }
    buf[i] = '\0';
}

void shell_output_clear(struct shell_context *ctx) {
    if (ctx == NULL) return;
    ctx->output_buf[0] = '\0';
    ctx->output_len = 0;
}

void shell_output_write(struct shell_context *ctx, const char *text) {
    if (ctx == NULL || text == NULL) return;
    str_append(ctx->output_buf, text, SHELL_OUT_MAX);
    ctx->output_len = str_len(ctx->output_buf);
}

bool shell_initialize(struct shell_context *ctx) {
    if (ctx == NULL) return false;
    *ctx = (struct shell_context){0};
    str_copy(ctx->cwd, "/", VFS_MAX_PATH);
    ctx->history_count = 0;
    ctx->history_cursor = -1;

    shell_set_env(ctx, "OS", "DAWN-OS");
    shell_set_env(ctx, "ARCH", "x86_64");
    shell_set_env(ctx, "VERSION", "1.0.0");
    shell_set_env(ctx, "SHELL", "/bin/dsh");
    shell_set_env(ctx, "USER", "admin");

    return true;
}

bool shell_set_env(struct shell_context *ctx, const char *name, const char *val) {
    if (ctx == NULL || name == NULL || val == NULL) return false;
    for (size_t i = 0; i < SHELL_ENV_MAX; i++) {
        if (ctx->env[i].active && str_equal(ctx->env[i].name, name)) {
            str_copy(ctx->env[i].value, val, 64);
            return true;
        }
    }
    for (size_t i = 0; i < SHELL_ENV_MAX; i++) {
        if (!ctx->env[i].active) {
            str_copy(ctx->env[i].name, name, 32);
            str_copy(ctx->env[i].value, val, 64);
            ctx->env[i].active = true;
            return true;
        }
    }
    return false;
}

const char *shell_get_env(struct shell_context *ctx, const char *name) {
    if (ctx == NULL || name == NULL) return NULL;
    for (size_t i = 0; i < SHELL_ENV_MAX; i++) {
        if (ctx->env[i].active && str_equal(ctx->env[i].name, name)) {
            return ctx->env[i].value;
        }
    }
    return NULL;
}

static void add_history(struct shell_context *ctx, const char *cmd) {
    if (ctx->history_count < SHELL_HISTORY_MAX) {
        str_copy(ctx->history[ctx->history_count++], cmd, SHELL_CMD_MAX);
    } else {
        for (size_t i = 0; i + 1 < SHELL_HISTORY_MAX; i++) {
            str_copy(ctx->history[i], ctx->history[i + 1], SHELL_CMD_MAX);
        }
        str_copy(ctx->history[SHELL_HISTORY_MAX - 1], cmd, SHELL_CMD_MAX);
    }
}

static int parse_cmdline(const char *cmdline, char args[SHELL_ARGS_MAX][64], size_t *out_argc) {
    size_t argc = 0;
    size_t i = 0;

    while (cmdline[i] != '\0' && argc < SHELL_ARGS_MAX) {
        /* Skip leading whitespace */
        while (cmdline[i] == ' ' || cmdline[i] == '\t') i++;
        if (cmdline[i] == '\0') break;

        size_t a_idx = 0;
        if (cmdline[i] == '"') {
            i++;
            while (cmdline[i] != '\0' && cmdline[i] != '"' && a_idx + 1 < 64) {
                args[argc][a_idx++] = cmdline[i++];
            }
            if (cmdline[i] == '"') i++;
        } else {
            while (cmdline[i] != '\0' && cmdline[i] != ' ' && cmdline[i] != '\t' && a_idx + 1 < 64) {
                args[argc][a_idx++] = cmdline[i++];
            }
        }
        args[argc][a_idx] = '\0';
        argc++;
    }
    *out_argc = argc;
    return 0;
}

int shell_execute(struct shell_context *ctx, const char *cmdline) {
    if (ctx == NULL || cmdline == NULL) return -1;
    while (*cmdline == ' ') cmdline++;
    if (*cmdline == '\0') return 0;

    add_history(ctx, cmdline);
    shell_output_clear(ctx);

    char args[SHELL_ARGS_MAX][64];
    size_t argc = 0;
    parse_cmdline(cmdline, args, &argc);
    if (argc == 0) return 0;

    const char *cmd = args[0];

    /* Built-in: help */
    if (str_equal(cmd, "help")) {
        shell_output_write(ctx, "DAWN-OS Shell Built-in Commands:\n");
        shell_output_write(ctx, "  help       - Show this help message\n");
        shell_output_write(ctx, "  echo [str] - Echo argument text\n");
        shell_output_write(ctx, "  clear      - Clear terminal screen\n");
        shell_output_write(ctx, "  pwd        - Print current working directory\n");
        shell_output_write(ctx, "  cd [dir]   - Change working directory\n");
        shell_output_write(ctx, "  ls [path]  - List directory contents\n");
        shell_output_write(ctx, "  cat [file] - Display file contents\n");
        shell_output_write(ctx, "  touch [f]  - Create empty file\n");
        shell_output_write(ctx, "  mkdir [d]  - Create directory\n");
        shell_output_write(ctx, "  rm [file]  - Remove file or directory\n");
        shell_output_write(ctx, "  cp [s] [d] - Copy file\n");
        shell_output_write(ctx, "  mv [s] [d] - Move/rename file\n");
        shell_output_write(ctx, "  ps         - List active processes\n");
        shell_output_write(ctx, "  disks      - List storage disks & partitions\n");
        shell_output_write(ctx, "  uname      - Display system information\n");
        shell_output_write(ctx, "  mem        - Display memory allocation statistics\n");
        shell_output_write(ctx, "  env        - Display environment variables\n");
        ctx->last_exit_code = 0;
        return 0;
    }

    /* Built-in: echo */
    if (str_equal(cmd, "echo")) {
        for (size_t a = 1; a < argc; a++) {
            shell_output_write(ctx, args[a]);
            if (a + 1 < argc) shell_output_write(ctx, " ");
        }
        shell_output_write(ctx, "\n");
        ctx->last_exit_code = 0;
        return 0;
    }

    /* Built-in: pwd */
    if (str_equal(cmd, "pwd")) {
        shell_output_write(ctx, ctx->cwd);
        shell_output_write(ctx, "\n");
        ctx->last_exit_code = 0;
        return 0;
    }

    /* Built-in: cd */
    if (str_equal(cmd, "cd")) {
        const char *target = (argc > 1) ? args[1] : "/";
        if (str_equal(target, "/")) {
            str_copy(ctx->cwd, "/", VFS_MAX_PATH);
        } else if (str_equal(target, "..")) {
            size_t len = str_len(ctx->cwd);
            while (len > 1 && ctx->cwd[len - 1] != '/') len--;
            if (len > 1) ctx->cwd[len - 1] = '\0';
            else str_copy(ctx->cwd, "/", VFS_MAX_PATH);
        } else if (!str_equal(target, ".")) {
            struct vfs_node *node = NULL;
            if (vfs_resolve_path(target, &node) && node->type == VFS_TYPE_DIRECTORY) {
                if (target[0] == '/') {
                    str_copy(ctx->cwd, target, VFS_MAX_PATH);
                } else {
                    if (!str_equal(ctx->cwd, "/")) str_append(ctx->cwd, "/", VFS_MAX_PATH);
                    str_append(ctx->cwd, target, VFS_MAX_PATH);
                }
            } else {
                shell_output_write(ctx, "cd: no such directory\n");
                ctx->last_exit_code = 1;
                return 1;
            }
        }
        ctx->last_exit_code = 0;
        return 0;
    }

    /* Built-in: ls */
    if (str_equal(cmd, "ls")) {
        const char *target = (argc > 1) ? args[1] : ctx->cwd;
        struct vfs_node *dir = NULL;
        if (!vfs_resolve_path(target, &dir) || dir->type != VFS_TYPE_DIRECTORY) {
            shell_output_write(ctx, "ls: cannot access directory\n");
            ctx->last_exit_code = 1;
            return 1;
        }

        struct vfs_dirent entry;
        uint32_t idx = 0;
        while (vfs_readdir(dir, idx++, &entry) == 0) {
            shell_output_write(ctx, (entry.type == VFS_TYPE_DIRECTORY) ? "[DIR]  " : "       ");
            shell_output_write(ctx, entry.name);
            shell_output_write(ctx, "\n");
        }
        ctx->last_exit_code = 0;
        return 0;
    }

    /* Built-in: cat */
    if (str_equal(cmd, "cat")) {
        if (argc < 2) {
            shell_output_write(ctx, "usage: cat <file>\n");
            return 1;
        }
        struct vfs_node *file = NULL;
        if (!vfs_resolve_path(args[1], &file) || file == NULL) {
            shell_output_write(ctx, "cat: cannot open file\n");
            return 1;
        }
        char read_buf[256];
        int64_t n = vfs_read(file, 0, sizeof(read_buf) - 1, read_buf);
        if (n > 0) {
            read_buf[n] = '\0';
            shell_output_write(ctx, read_buf);
            shell_output_write(ctx, "\n");
        }
        ctx->last_exit_code = 0;
        return 0;
    }

    /* Built-in: touch */
    if (str_equal(cmd, "touch")) {
        if (argc < 2) {
            shell_output_write(ctx, "usage: touch <file>\n");
            return 1;
        }
        struct vfs_node *f = NULL;
        if (vfs_create(args[1], 0644, &f) != 0) {
            shell_output_write(ctx, "touch: failed to create file\n");
            return 1;
        }
        ctx->last_exit_code = 0;
        return 0;
    }

    /* Built-in: mkdir */
    if (str_equal(cmd, "mkdir")) {
        if (argc < 2) {
            shell_output_write(ctx, "usage: mkdir <directory>\n");
            return 1;
        }
        if (vfs_mkdir(args[1], 0755, NULL) != 0) {
            shell_output_write(ctx, "mkdir: failed to create directory\n");
            return 1;
        }
        ctx->last_exit_code = 0;
        return 0;
    }

    /* Built-in: rm */
    if (str_equal(cmd, "rm")) {
        if (argc < 2) {
            shell_output_write(ctx, "usage: rm <file>\n");
            return 1;
        }
        if (vfs_unlink(args[1]) != 0) {
            shell_output_write(ctx, "rm: failed to unlink\n");
            return 1;
        }
        ctx->last_exit_code = 0;
        return 0;
    }

    /* Built-in: uname */
    if (str_equal(cmd, "uname")) {
        shell_output_write(ctx, "DAWN-OS 1.0.0-release x86_64 Bare-Metal (Emerald Green Edition)\n");
        ctx->last_exit_code = 0;
        return 0;
    }

    /* Built-in: disks */
    if (str_equal(cmd, "disks")) {
        size_t dcount = storage_get_disk_count();
        char num[16];
        u64_to_str(dcount, num);
        shell_output_write(ctx, "Discovered Physical Disks: ");
        shell_output_write(ctx, num);
        shell_output_write(ctx, "\n");

        for (size_t d = 0; d < dcount; d++) {
            const struct storage_disk_desc *desc = storage_get_disk_info(d);
            if (desc == NULL) continue;
            shell_output_write(ctx, "  Disk: ");
            shell_output_write(ctx, desc->name);
            shell_output_write(ctx, " (");
            shell_output_write(ctx, desc->model);
            shell_output_write(ctx, ")\n");
            for (uint32_t p = 0; p < desc->partition_count; p++) {
                shell_output_write(ctx, "    Partition: ");
                shell_output_write(ctx, desc->partitions[p].slice_name);
                shell_output_write(ctx, " [");
                shell_output_write(ctx, desc->partitions[p].fs_name);
                shell_output_write(ctx, "]\n");
            }
        }
        ctx->last_exit_code = 0;
        return 0;
    }

    /* Built-in: mem */
    if (str_equal(cmd, "mem")) {
        char nbuf[24];
        shell_output_write(ctx, "Physical Memory Status:\n");
        u64_to_str(physical_memory_total_bytes(), nbuf);
        shell_output_write(ctx, "  Total RAM: ");
        shell_output_write(ctx, nbuf);
        shell_output_write(ctx, " bytes\n");
        u64_to_str(physical_memory_free_bytes(), nbuf);
        shell_output_write(ctx, "  Free RAM:  ");
        shell_output_write(ctx, nbuf);
        shell_output_write(ctx, " bytes\n");
        ctx->last_exit_code = 0;
        return 0;
    }

    /* Built-in: env */
    if (str_equal(cmd, "env")) {
        for (size_t e = 0; e < SHELL_ENV_MAX; e++) {
            if (ctx->env[e].active) {
                shell_output_write(ctx, ctx->env[e].name);
                shell_output_write(ctx, "=");
                shell_output_write(ctx, ctx->env[e].value);
                shell_output_write(ctx, "\n");
            }
        }
        ctx->last_exit_code = 0;
        return 0;
    }

    /* Command not found */
    shell_output_write(ctx, "dsh: command not found: ");
    shell_output_write(ctx, cmd);
    shell_output_write(ctx, "\n");
    ctx->last_exit_code = 127;
    return 127;
}

/* Self-test */
static struct shell_context test_shell_ctx;

bool shell_self_test(void) {
    if (!shell_initialize(&test_shell_ctx)) return false;

    /* Test echo */
    if (shell_execute(&test_shell_ctx, "echo Hello DAWN-OS") != 0) return false;
    if (test_shell_ctx.output_len == 0) return false;

    /* Test pwd */
    if (shell_execute(&test_shell_ctx, "pwd") != 0) return false;
    if (!str_equal(test_shell_ctx.output_buf, "/\n")) return false;

    /* Test uname */
    if (shell_execute(&test_shell_ctx, "uname") != 0) return false;

    /* Test env variables */
    if (shell_set_env(&test_shell_ctx, "TEST_KEY", "TEST_VAL") != true) return false;
    const char *val = shell_get_env(&test_shell_ctx, "TEST_KEY");
    if (val == NULL || !str_equal(val, "TEST_VAL")) return false;

    /* Test mkdir and touch */
    (void)shell_execute(&test_shell_ctx, "mkdir /tmp/sh_test");
    (void)shell_execute(&test_shell_ctx, "touch /tmp/sh_test/file.txt");

    /* Test unknown command */
    int ret = shell_execute(&test_shell_ctx, "nonexistent_cmd_123");
    if (ret != 127 || test_shell_ctx.last_exit_code != 127) return false;

    return true;
}
