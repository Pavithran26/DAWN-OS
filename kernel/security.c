/* SPDX-License-Identifier: Unlicense */
#include "security.h"

static struct user_account user_table[SECURITY_MAX_USERS];
static struct audit_record audit_log_table[SECURITY_MAX_AUDIT_LOGS];
static size_t audit_log_count = 0;
static uint32_t active_user_uid = SECURITY_UID_ROOT;
static bool security_initialized = false;

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

void security_hash_password(const char *password, uint32_t salt, uint8_t digest[SECURITY_HASH_SIZE]) {
    uint32_t state[8] = {
        0x6A09E667U ^ salt,
        0xBB67AE85U + salt,
        0x3C6EF372U ^ (salt << 1),
        0xA54FF53AU + (salt << 2),
        0x510E527FU ^ (salt >> 1),
        0x9B05688CU + (salt >> 2),
        0x1F83D9ABU ^ (salt * 31U),
        0x5BE0CD19U + (salt * 17U)
    };

    size_t len = 0;
    while (password != NULL && password[len] != '\0') {
        uint32_t ch = (uint8_t)password[len];
        uint32_t idx = (uint32_t)(len % 8);
        state[idx] = (state[idx] ^ ch) * 16777619U;
        state[(idx + 1) % 8] = (state[(idx + 1) % 8] + ch + (uint32_t)len) ^
                               ((state[idx] << 5) | (state[idx] >> 27));
        len++;
    }

    /* 16 mixing rounds for cryptographic diffusion */
    for (uint32_t round = 0; round < 16U; round++) {
        for (uint32_t i = 0; i < 8U; i++) {
            uint32_t next = (i + 1) % 8U;
            state[i] = ((state[i] << 7) | (state[i] >> 25)) ^ (state[next] + round + 0x9E3779B9U);
            state[next] ^= (state[i] >> 3);
        }
    }

    for (uint32_t i = 0; i < 8U; i++) {
        digest[i * 4U + 0U] = (uint8_t)(state[i] >> 24);
        digest[i * 4U + 1U] = (uint8_t)(state[i] >> 16);
        digest[i * 4U + 2U] = (uint8_t)(state[i] >> 8);
        digest[i * 4U + 3U] = (uint8_t)(state[i]);
    }
}

bool security_verify_password(const char *password, uint32_t salt, const uint8_t digest[SECURITY_HASH_SIZE]) {
    if (password == NULL || digest == NULL) return false;

    uint8_t computed[SECURITY_HASH_SIZE] = {0};
    security_hash_password(password, salt, computed);

    /* Constant-time memory comparison to mitigate timing attacks */
    uint8_t diff = 0;
    for (size_t i = 0; i < SECURITY_HASH_SIZE; i++) {
        diff |= (computed[i] ^ digest[i]);
    }

    return diff == 0;
}

void security_audit_log(enum audit_event_type type, uint32_t uid, uint32_t pid, const char *details) {
    size_t slot = audit_log_count % SECURITY_MAX_AUDIT_LOGS;
    audit_log_table[slot].type = type;
    audit_log_table[slot].uid = uid;
    audit_log_table[slot].pid = pid;
    str_copy(audit_log_table[slot].details, (details != NULL) ? details : "", sizeof(audit_log_table[slot].details));
    audit_log_count++;
}

size_t security_get_audit_count(void) {
    return audit_log_count;
}

bool security_add_user(const char *username, const char *password, uint32_t uid, uint32_t gid, const char *home, const char *shell) {
    if (username == NULL || password == NULL) return false;

    for (size_t i = 0; i < SECURITY_MAX_USERS; i++) {
        if (!user_table[i].active || user_table[i].uid == uid) {
            user_table[i].uid = uid;
            user_table[i].gid = gid;
            user_table[i].salt = 0xDA3E0000U + uid;
            str_copy(user_table[i].username, username, sizeof(user_table[i].username));
            security_hash_password(password, user_table[i].salt, user_table[i].password_hash);
            str_copy(user_table[i].home_dir, (home != NULL) ? home : "/home", sizeof(user_table[i].home_dir));
            str_copy(user_table[i].shell, (shell != NULL) ? shell : "/bin/dsh", sizeof(user_table[i].shell));
            user_table[i].active = true;
            return true;
        }
    }

    return false;
}

bool security_authenticate_user(const char *username, const char *password, uint32_t *out_uid) {
    if (username == NULL || password == NULL) {
        security_audit_log(AUDIT_AUTH_FAILURE, 0, 0, "Invalid auth parameters");
        return false;
    }

    for (size_t i = 0; i < SECURITY_MAX_USERS; i++) {
        if (user_table[i].active && str_equal(user_table[i].username, username)) {
            if (security_verify_password(password, user_table[i].salt, user_table[i].password_hash)) {
                if (out_uid != NULL) *out_uid = user_table[i].uid;
                security_audit_log(AUDIT_AUTH_SUCCESS, user_table[i].uid, 0, "User logged in");
                return true;
            } else {
                security_audit_log(AUDIT_AUTH_FAILURE, user_table[i].uid, 0, "Invalid credentials");
                return false;
            }
        }
    }

    security_audit_log(AUDIT_AUTH_FAILURE, 0, 0, "User not found");
    return false;
}

bool security_set_current_uid(uint32_t uid) {
    /* Only root can freely switch UID; non-root can only drop to their own UID */
    if (active_user_uid != SECURITY_UID_ROOT && uid != active_user_uid) {
        security_audit_log(AUDIT_PRIVILEGE_CHANGE, active_user_uid, 0, "Privilege escalation denied");
        return false;
    }

    active_user_uid = uid;
    security_audit_log(AUDIT_PRIVILEGE_CHANGE, uid, 0, "Current UID updated");
    return true;
}

uint32_t security_get_current_uid(void) {
    return active_user_uid;
}

bool security_check_file_permission(uint32_t uid, uint32_t gid, uint32_t file_uid, uint32_t file_gid, uint32_t file_mode, uint32_t requested_access) {
    /* Root bypasses read/write checks */
    if (uid == SECURITY_UID_ROOT) {
        if ((requested_access & SECURITY_ACCESS_EXECUTE) != 0U) {
            /* Root still requires at least one execute bit */
            return (file_mode & 0111U) != 0U;
        }
        return true;
    }

    uint32_t perm_bits = 0;
    if (uid == file_uid) {
        perm_bits = (file_mode >> 6) & 0x7U; /* Owner */
    } else if (gid == file_gid) {
        perm_bits = (file_mode >> 3) & 0x7U; /* Group */
    } else {
        perm_bits = file_mode & 0x7U;        /* World */
    }

    if ((perm_bits & requested_access) == requested_access) {
        return true;
    }

    security_audit_log(AUDIT_PERMISSION_DENIED, uid, 0, "File permission denied");
    return false;
}

bool security_validate_user_pointer(const void *ptr, size_t len) {
    if (ptr == NULL) {
        security_audit_log(AUDIT_SYSCALL_VIOLATION, active_user_uid, 0, "NULL pointer violation");
        return false;
    }

    uint64_t addr = (uint64_t)ptr;
    /* User space address range is below higher-half kernel space */
    if (addr >= 0xFFFF800000000000ULL || (addr + len) < addr || (addr + len) >= 0xFFFF800000000000ULL) {
        security_audit_log(AUDIT_SYSCALL_VIOLATION, active_user_uid, 0, "Kernel space address intrusion");
        return false;
    }

    return true;
}

bool security_initialize(void) {
    if (security_initialized) return true;

    for (size_t i = 0; i < SECURITY_MAX_USERS; i++) user_table[i] = (struct user_account){0};
    for (size_t i = 0; i < SECURITY_MAX_AUDIT_LOGS; i++) audit_log_table[i] = (struct audit_record){0};
    audit_log_count = 0;
    active_user_uid = SECURITY_UID_ROOT;

    /* Create root account */
    security_add_user("root", "toor", SECURITY_UID_ROOT, SECURITY_GID_ROOT, "/root", "/bin/dsh");

    /* Create default dawn user */
    security_add_user("dawn", "dawn", SECURITY_UID_DEFAULT, SECURITY_GID_DEFAULT, "/home/dawn", "/bin/dsh");

    security_initialized = true;
    return true;
}

/* Self-test */
bool security_self_test(void) {
    if (!security_initialize()) return false;

    /* Test password hashing */
    uint8_t hash1[SECURITY_HASH_SIZE];
    uint8_t hash2[SECURITY_HASH_SIZE];
    security_hash_password("password123", 100, hash1);
    security_hash_password("password123", 100, hash2);

    for (size_t i = 0; i < SECURITY_HASH_SIZE; i++) {
        if (hash1[i] != hash2[i]) return false;
    }

    /* Verification test */
    if (!security_verify_password("password123", 100, hash1)) return false;
    if (security_verify_password("wrongpassword", 100, hash1)) return false;

    /* Authentication test */
    uint32_t auth_uid = 0;
    if (!security_authenticate_user("root", "toor", &auth_uid) || auth_uid != SECURITY_UID_ROOT) return false;
    if (!security_authenticate_user("dawn", "dawn", &auth_uid) || auth_uid != SECURITY_UID_DEFAULT) return false;
    if (security_authenticate_user("dawn", "wrongpass", &auth_uid)) return false;

    /* File permission test */
    /* Owner rwx (0700): owner has read */
    if (!security_check_file_permission(1000, 1000, 1000, 1000, 0700, SECURITY_ACCESS_READ)) return false;
    /* Non-owner, non-group cannot read 0700 */
    if (security_check_file_permission(1001, 1001, 1000, 1000, 0700, SECURITY_ACCESS_READ)) return false;
    /* Root can read 0700 */
    if (!security_check_file_permission(SECURITY_UID_ROOT, SECURITY_GID_ROOT, 1000, 1000, 0700, SECURITY_ACCESS_READ)) return false;

    /* Memory boundary validation */
    const void *valid_user_ptr = (const void *)0x0000000040000000ULL;
    if (!security_validate_user_pointer(valid_user_ptr, 64)) return false;

    const void *invalid_kernel_ptr = (const void *)0xFFFF800000001000ULL;
    if (security_validate_user_pointer(invalid_kernel_ptr, 64)) return false;

    return true;
}
