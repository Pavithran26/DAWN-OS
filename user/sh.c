/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

static void cmd_help(void) {
    dawn_println("DAWN-OS Native Shell built-in commands:");
    dawn_println("  help            - Display this help message");
    dawn_println("  echo [args...]  - Print text to standard output");
    dawn_println("  cat <file>      - Display contents of a file");
    dawn_println("  ls [dir]        - List files in directory");
    dawn_println("  pwd             - Print current working directory");
    dawn_println("  cd <dir>        - Change working directory");
    dawn_println("  mkdir <dir>     - Create a directory");
    dawn_println("  rm <file>       - Remove a file");
    dawn_println("  touch <file>    - Create an empty file");
    dawn_println("  cp <src> <dst>  - Copy a file");
    dawn_println("  head [-n N] <f> - Display first N lines of a file");
    dawn_println("  tail [-n N] <f> - Display last N lines of a file");
    dawn_println("  wc <file>       - Count lines, words, and bytes");
    dawn_println("  uname [-a]      - Display operating system information");
    dawn_println("  fetch           - Display system summary & emerald logo");
    dawn_println("  ifconfig        - Display network interface configuration");
    dawn_println("  ping <ip>       - Send ICMP echo requests");
    dawn_println("  date            - Show system UTC date and time");
    dawn_println("  uptime          - Show system uptime report");
    dawn_println("  nproc           - Print number of processing units / CPU cores");
    dawn_println("  free [-h|-m]    - Display amount of free and used memory");
    dawn_println("  whoami          - Print effective user name");
    dawn_println("  sleep <sec>     - Pause for specified seconds");
    dawn_println("  grep [-i|-v|-n] - Search for PATTERN in file or stdin");
    dawn_println("  pid             - Print shell process identifier");
    dawn_println("  kill <pid>      - Terminate a process");
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

static void cmd_ls(const char *path) {
    char target[128];
    if (path != NULL) {
        dawn_strcpy(target, path);
    } else {
        if (dawn_getcwd(target, sizeof(target)) < 0) {
            dawn_strcpy(target, "/");
        }
    }

    int64_t fd = dawn_open(target, DAWN_O_RDONLY, 0);
    if (fd < 0) {
        dawn_printf("ls: cannot access '%s'\n", target);
        return;
    }

    struct dawn_dirent dent;
    int count = 0;
    while (dawn_getdents((uint64_t)fd, &dent, sizeof(dent)) > 0) {
        count++;
        if (dent.d_type == 2) {
            dawn_printf("[DIR]  %-20s\n", dent.d_name);
        } else {
            dawn_printf("       %-20s %u B\n", dent.d_name, (unsigned int)dent.d_size);
        }
    }
    if (count == 0) {
        dawn_println("(empty directory)");
    }
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

static void cmd_mkdir(const char *path) {
    if (path == NULL) {
        dawn_println("Usage: mkdir <directory>");
        return;
    }
    if (dawn_mkdir(path, 0755) < 0) {
        dawn_printf("mkdir: failed to create '%s'\n", path);
    }
}

static void cmd_rm(const char *path) {
    if (path == NULL) {
        dawn_println("Usage: rm <file>");
        return;
    }
    if (dawn_unlink(path) < 0) {
        dawn_printf("rm: failed to remove '%s'\n", path);
    }
}

static void cmd_date(void) {
    uint64_t epoch = 0;
    dawn_time(&epoch);
    uint64_t seconds = epoch % 60;
    uint64_t total_minutes = epoch / 60;
    uint64_t minutes = total_minutes % 60;
    uint64_t total_hours = total_minutes / 60;
    uint64_t hours = total_hours % 24;
    uint64_t days = total_hours / 24;

    uint64_t year = 1970;
    while (1) {
        bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        uint64_t diy = leap ? 366 : 365;
        if (days < diy) break;
        days -= diy;
        year++;
    }

    bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    static const uint8_t mdays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    uint64_t month = 1;
    for (size_t m = 0; m < 12; m++) {
        uint64_t dim = mdays[m];
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
}

static void cmd_uptime(void) {
    struct dawn_sysinfo info;
    if (dawn_sysinfo(&info) == 0) {
        uint64_t sec = info.uptime_seconds;
        uint32_t days = (uint32_t)(sec / 86400ULL);
        uint32_t hours = (uint32_t)((sec % 86400ULL) / 3600ULL);
        uint32_t mins = (uint32_t)((sec % 3600ULL) / 60ULL);
        uint32_t secs = (uint32_t)(sec % 60ULL);
        if (days > 0) {
            dawn_printf("up %u day%s, %u:%02u:%02u, %u cpus (SMP active)\n",
                        days, days == 1 ? "" : "s", hours, mins, secs, info.online_cpu_count);
        } else if (hours > 0) {
            dawn_printf("up %u:%02u:%02u, %u cpus (SMP active)\n",
                        hours, mins, secs, info.online_cpu_count);
        } else {
            dawn_printf("up %u min, %u sec, %u cpus (SMP active)\n",
                        mins, secs, info.online_cpu_count);
        }
    } else {
        uint64_t epoch = 0;
        dawn_time(&epoch);
        dawn_printf("System time: %u seconds since Unix epoch\n", (unsigned int)epoch);
    }
}

static void cmd_kill(const char *pid_str) {
    if (pid_str == NULL) {
        dawn_println("Usage: kill <pid>");
        return;
    }
    uint64_t pid = 0;
    while (*pid_str >= '0' && *pid_str <= '9') {
        pid = pid * 10 + (uint64_t)(*pid_str - '0');
        pid_str++;
    }
    if (pid == 0) {
        dawn_println("kill: invalid PID");
        return;
    }
    if (dawn_kill(pid, 9) < 0) {
        dawn_printf("kill: failed to signal process %u\n", (unsigned int)pid);
    }
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

static bool try_exec(const char *path, int argc, char *argv[]) {
    int64_t fd = dawn_open(path, DAWN_O_RDONLY, 0);
    if (fd < 0) return false;

    struct dawn_posix_stat st;
    if (dawn_stat(path, &st) < 0 || st.st_size <= 0 || st.st_size > 256 * 1024) {
        dawn_close((uint64_t)fd);
        return false;
    }

    void *image = dawn_malloc((size_t)st.st_size);
    if (image == NULL) {
        dawn_close((uint64_t)fd);
        return false;
    }

    int64_t rd = dawn_read((uint64_t)fd, image, (uint64_t)st.st_size);
    dawn_close((uint64_t)fd);
    if (rd != st.st_size) {
        dawn_free(image);
        return false;
    }

    const char *args[18];
    int a = 0;
    args[a++] = path;
    for (int i = 1; i < argc && a < 16; i++) {
        args[a++] = argv[i];
    }
    args[a] = NULL;

    int64_t pid = dawn_spawn(image, (uint64_t)st.st_size, (const char *const *)args, NULL);
    dawn_free(image);

    if (pid > 0) {
        int64_t status = 0;
        dawn_wait(pid, &status);
        return true;
    }
    return false;
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
        } else if (dawn_strcmp(cmd, "ls") == 0) {
            cmd_ls(token_count > 1 ? tokens[1] : NULL);
        } else if (dawn_strcmp(cmd, "pwd") == 0) {
            cmd_pwd();
        } else if (dawn_strcmp(cmd, "cd") == 0) {
            cmd_cd(token_count > 1 ? tokens[1] : "/");
        } else if (dawn_strcmp(cmd, "mkdir") == 0) {
            cmd_mkdir(token_count > 1 ? tokens[1] : NULL);
        } else if (dawn_strcmp(cmd, "rm") == 0) {
            cmd_rm(token_count > 1 ? tokens[1] : NULL);
        } else if (dawn_strcmp(cmd, "date") == 0) {
            cmd_date();
        } else if (dawn_strcmp(cmd, "uptime") == 0) {
            cmd_uptime();
        } else if (dawn_strcmp(cmd, "pid") == 0) {
            dawn_printf("PID: %u\n", (unsigned int)dawn_getpid());
        } else if (dawn_strcmp(cmd, "kill") == 0) {
            cmd_kill(token_count > 1 ? tokens[1] : NULL);
        } else if (dawn_strcmp(cmd, "mem") == 0) {
            cmd_mem();
        } else if (dawn_strcmp(cmd, "uname") == 0) {
            dawn_println("DAWN-OS dawn-kernel 1.0.0-emerald x86_64 GNU/DAWN");
        } else if (dawn_strcmp(cmd, "clear") == 0) {
            dawn_print("\033[2J\033[H");
        } else if (dawn_strcmp(cmd, "exit") == 0) {
            dawn_println("Exiting shell session.");
            break;
        } else {
            /* Try executing binary from /bin/<cmd>, /bin/<cmd>.elf, or direct path */
            char bin_path[128];
            bool executed = false;

            if (dawn_strchr(cmd, '/') != NULL) {
                executed = try_exec(cmd, token_count, tokens);
            }
            if (!executed) {
                dawn_snprintf(bin_path, sizeof(bin_path), "/bin/%s.elf", cmd);
                executed = try_exec(bin_path, token_count, tokens);
            }
            if (!executed) {
                dawn_snprintf(bin_path, sizeof(bin_path), "/bin/%s", cmd);
                executed = try_exec(bin_path, token_count, tokens);
            }

            if (!executed) {
                dawn_printf("dsh: command not found: %s\n", cmd);
            }
        }
    }

    return 0;
}
