/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

static void cmd_help(void) {
    dawn_println("DAWN-OS Native Shell built-in commands:");
    dawn_println("  help            - Display this help message");
    dawn_println("  echo [args...]  - Print text to standard output");
    dawn_println("  cat <file>      - Display contents of a file");
    dawn_println("  pwd             - Print current working directory");
    dawn_println("  cd <dir>        - Change working directory");
    dawn_println("  date            - Show system Unix epoch timestamp");
    dawn_println("  pid             - Print shell process identifier");
    dawn_println("  mem             - Test dynamic heap memory allocator");
    dawn_println("  clear           - Clear terminal display");
    dawn_println("  exit            - Terminate shell session");
}

static void cmd_echo(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        dawn_print(argv[i]);
        if (i + 1 < argc) dawn_putchar(' ');
    }
    dawn_putchar('\n');
}

static void cmd_cat(const char *path) {
    if (path == NULL) {
        dawn_println("Usage: cat <file>");
        return;
    }
    int64_t fd = dawn_open(path, DAWN_O_RDONLY, 0);
    if (fd < 0) {
        dawn_printf("cat: cannot open '%s'\n", path);
        return;
    }
    char buf[256];
    int64_t bytes_read = 0;
    while ((bytes_read = dawn_read((uint64_t)fd, buf, sizeof(buf) - 1)) > 0) {
        buf[bytes_read] = '\0';
        dawn_print(buf);
    }
    dawn_putchar('\n');
    dawn_close((uint64_t)fd);
}

static void cmd_pwd(void) {
    char cwd[128];
    if (dawn_getcwd(cwd, sizeof(cwd)) >= 0) {
        dawn_println(cwd);
    } else {
        dawn_println("pwd: error retrieving working directory");
    }
}

static void cmd_cd(const char *path) {
    if (path == NULL) path = "/";
    if (dawn_chdir(path) < 0) {
        dawn_printf("cd: '%s': No such file or directory\n", path);
    }
}

static void cmd_date(void) {
    uint64_t now = 0;
    dawn_time(&now);
    dawn_printf("Unix Timestamp: %u seconds since epoch\n", (unsigned int)now);
}

static void cmd_mem(void) {
    dawn_print("Testing dynamic heap allocator (malloc/free)... ");
    void *p1 = dawn_malloc(128);
    void *p2 = dawn_malloc(1024);
    if (p1 != NULL && p2 != NULL) {
        dawn_memset(p1, 0xAA, 128);
        dawn_memset(p2, 0xBB, 1024);
        dawn_free(p1);
        dawn_free(p2);
        dawn_println("[PASS] 1152 bytes allocated and freed successfully.");
    } else {
        dawn_println("[FAIL] Memory allocation failed.");
    }
}

int main(int argc, char *argv[], char *envp[]) {
    (void)argc;
    (void)argv;
    (void)envp;

    dawn_println("DAWN-OS POSIX Shell (dsh v1.0 - Ring 3)");
    dawn_println("Type 'help' to see built-in commands.\n");

    char line_buf[256];
    char cwd[128];

    for (;;) {
        if (dawn_getcwd(cwd, sizeof(cwd)) < 0) {
            dawn_strcpy(cwd, "/");
        }
        dawn_printf("dawn:%s$ ", cwd);

        size_t len = dawn_readline(line_buf, sizeof(line_buf));
        if (len == 0) continue;

        /* Tokenize command line */
        char *tokens[16];
        int token_count = 0;
        char *ptr = line_buf;

        while (*ptr != '\0' && token_count < 16) {
            while (*ptr == ' ' || *ptr == '\t') ptr++;
            if (*ptr == '\0') break;
            tokens[token_count++] = ptr;
            while (*ptr != '\0' && *ptr != ' ' && *ptr != '\t') ptr++;
            if (*ptr != '\0') {
                *ptr = '\0';
                ptr++;
            }
        }

        if (token_count == 0) continue;

        const char *cmd = tokens[0];
        if (dawn_strcmp(cmd, "help") == 0) {
            cmd_help();
        } else if (dawn_strcmp(cmd, "echo") == 0) {
            cmd_echo(token_count, tokens);
        } else if (dawn_strcmp(cmd, "cat") == 0) {
            cmd_cat(token_count > 1 ? tokens[1] : NULL);
        } else if (dawn_strcmp(cmd, "pwd") == 0) {
            cmd_pwd();
        } else if (dawn_strcmp(cmd, "cd") == 0) {
            cmd_cd(token_count > 1 ? tokens[1] : "/");
        } else if (dawn_strcmp(cmd, "date") == 0) {
            cmd_date();
        } else if (dawn_strcmp(cmd, "pid") == 0) {
            dawn_printf("PID: %u\n", (unsigned int)dawn_getpid());
        } else if (dawn_strcmp(cmd, "mem") == 0) {
            cmd_mem();
        } else if (dawn_strcmp(cmd, "clear") == 0) {
            dawn_print("\033[2J\033[H");
        } else if (dawn_strcmp(cmd, "exit") == 0) {
            dawn_println("Exiting shell session.");
            break;
        } else {
            dawn_printf("dsh: command not found: %s\n", cmd);
        }
    }

    return 0;
}
