/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

static char to_lower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + ('a' - 'A'));
    return c;
}

static bool match_line(const char *line, const char *pat, bool ignore_case) {
    size_t line_len = dawn_strlen(line);
    size_t pat_len = dawn_strlen(pat);
    if (pat_len == 0) return true;
    if (line_len < pat_len) return false;

    for (size_t i = 0; i <= line_len - pat_len; i++) {
        bool found = true;
        for (size_t j = 0; j < pat_len; j++) {
            char lc = line[i + j];
            char pc = pat[j];
            if (ignore_case) {
                lc = to_lower(lc);
                pc = to_lower(pc);
            }
            if (lc != pc) {
                found = false;
                break;
            }
        }
        if (found) return true;
    }
    return false;
}

static bool grep_fd(int64_t fd, const char *pat, bool ignore_case, bool invert,
                    bool line_num, const char *filename, bool print_filename) {
    char buf[512];
    char line[512];
    size_t line_idx = 0;
    uint32_t line_count = 0;
    bool any_match = false;
    int64_t bytes_read = 0;

    while ((bytes_read = dawn_read((uint64_t)fd, buf, sizeof(buf))) > 0) {
        for (int64_t i = 0; i < bytes_read; i++) {
            char c = buf[(size_t)i];
            if (c == '\n' || line_idx + 1 >= sizeof(line)) {
                line[line_idx] = '\0';
                line_count++;
                bool matched = match_line(line, pat, ignore_case);
                if (matched != invert) {
                    any_match = true;
                    if (print_filename && filename != NULL) {
                        dawn_printf("%s:", filename);
                    }
                    if (line_num) {
                        dawn_printf("%u:", line_count);
                    }
                    dawn_println(line);
                }
                line_idx = 0;
            } else if (c != '\r') {
                line[line_idx++] = c;
            }
        }
    }

    if (line_idx > 0) {
        line[line_idx] = '\0';
        line_count++;
        bool matched = match_line(line, pat, ignore_case);
        if (matched != invert) {
            any_match = true;
            if (print_filename && filename != NULL) {
                dawn_printf("%s:", filename);
            }
            if (line_num) {
                dawn_printf("%u:", line_count);
            }
            dawn_println(line);
        }
    }

    return any_match;
}

int main(int argc, char *argv[], char *envp[]) {
    (void)envp;
    bool ignore_case = false;
    bool invert = false;
    bool line_num = false;
    const char *pattern = NULL;
    int file_start = 0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && pattern == NULL) {
            if (dawn_strcmp(argv[i], "-i") == 0) {
                ignore_case = true;
            } else if (dawn_strcmp(argv[i], "-v") == 0) {
                invert = true;
            } else if (dawn_strcmp(argv[i], "-n") == 0) {
                line_num = true;
            } else if (dawn_strcmp(argv[i], "--help") == 0) {
                dawn_printf("Usage: grep [OPTION]... PATTERN [FILE]...\n"
                            "Search for PATTERN in each FILE or standard input.\n\n"
                            "  -i             ignore case distinctions\n"
                            "  -v             select non-matching lines\n"
                            "  -n             print line number with output lines\n"
                            "      --help     display this help and exit\n");
                return 0;
            } else {
                dawn_printf("grep: unrecognized option '%s'\n", argv[i]);
                return 2;
            }
        } else if (pattern == NULL) {
            pattern = argv[i];
            file_start = i + 1;
            break;
        }
    }

    if (pattern == NULL) {
        dawn_printf("Usage: grep [OPTION]... PATTERN [FILE]...\n");
        return 2;
    }

    bool matched = false;
    if (file_start >= argc) {
        /* Stdin */
        matched = grep_fd(DAWN_STDIN_FILENO, pattern, ignore_case, invert, line_num, NULL, false);
    } else {
        bool multiple = (argc - file_start) > 1;
        for (int i = file_start; i < argc; i++) {
            const char *path = argv[i];
            int64_t fd = dawn_open(path, DAWN_O_RDONLY, 0);
            if (fd < 0) {
                dawn_printf("grep: %s: No such file or directory\n", path);
                continue;
            }
            if (grep_fd(fd, pattern, ignore_case, invert, line_num, path, multiple)) {
                matched = true;
            }
            dawn_close((uint64_t)fd);
        }
    }

    return matched ? 0 : 1;
}
