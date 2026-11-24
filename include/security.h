/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_SECURITY_H
#define DAWN_SECURITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SECURITY_UID_ROOT 0U
#define SECURITY_GID_ROOT 0U
#define SECURITY_UID_DEFAULT 1000U
#define SECURITY_GID_DEFAULT 1000U

#define SECURITY_MAX_USERS 16U
#define SECURITY_MAX_AUDIT_LOGS 32U
#define SECURITY_HASH_SIZE 32U
#define SECURITY_SALT_SIZE 16U /* 128-bit cryptographic salt */
#define SECURITY_PBKDF2_ITERATIONS 10000U
#define SECURITY_MAX_FAILED_ATTEMPTS 5U

/* Access modes (POSIX compatible) */
#define SECURITY_ACCESS_READ    0x04U
#define SECURITY_ACCESS_WRITE   0x02U
#define SECURITY_ACCESS_EXECUTE 0x01U

/* Standard Cryptographic Primitives */
void sha256(const void *data, size_t len, uint8_t digest[SECURITY_HASH_SIZE]);
void hmac_sha256(const void *key, size_t key_len, const void *data, size_t data_len, uint8_t out[SECURITY_HASH_SIZE]);
void pbkdf2_hmac_sha256(const char *password, size_t pass_len, const uint8_t *salt, size_t salt_len, uint32_t iterations, uint8_t out[SECURITY_HASH_SIZE]);
void security_generate_salt(uint8_t salt[SECURITY_SALT_SIZE]);
bool security_is_first_boot(void);

/* PBKDF2-HMAC-SHA256 password hashing and authentication */
void security_hash_password(const char *password, const uint8_t salt[SECURITY_SALT_SIZE], uint8_t digest[SECURITY_HASH_SIZE]);
bool security_verify_password(const char *password, const uint8_t salt[SECURITY_SALT_SIZE], const uint8_t digest[SECURITY_HASH_SIZE]);

/* User Account Structure */
struct user_account {
    uint32_t uid;
    uint32_t gid;
    uint8_t  salt[SECURITY_SALT_SIZE];
    char     username[32];
    uint8_t  password_hash[SECURITY_HASH_SIZE];
    char     home_dir[64];
    char     shell[32];
    uint32_t failed_login_attempts;
    bool     active;
};

/* Security Audit Logging */
enum audit_event_type {
    AUDIT_AUTH_SUCCESS = 0,
    AUDIT_AUTH_FAILURE = 1,
    AUDIT_PERMISSION_DENIED = 2,
    AUDIT_SYSCALL_VIOLATION = 3,
    AUDIT_PRIVILEGE_CHANGE = 4
};

struct audit_record {
    enum audit_event_type type;
    uint32_t uid;
    uint32_t pid;
    char details[64];
};

/* Core Security API */
bool security_initialize(void);
bool security_add_user(const char *username, const char *password, uint32_t uid, uint32_t gid, const char *home, const char *shell);
bool security_authenticate_user(const char *username, const char *password, uint32_t *out_uid);
bool security_set_current_uid(uint32_t uid);
uint32_t security_get_current_uid(void);

/* Permission & Memory Boundary Verification */
bool security_check_file_permission(uint32_t uid, uint32_t gid, uint32_t file_uid, uint32_t file_gid, uint32_t file_mode, uint32_t requested_access);
bool security_validate_user_pointer(const void *ptr, size_t len);
void security_audit_log(enum audit_event_type type, uint32_t uid, uint32_t pid, const char *details);
size_t security_get_audit_count(void);

/* Self-test */
bool security_self_test(void);

#endif /* DAWN_SECURITY_H */
