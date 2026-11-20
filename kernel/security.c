/* SPDX-License-Identifier: Unlicense */
#include "security.h"

static struct user_account user_table[SECURITY_MAX_USERS];
static struct audit_record audit_log_table[SECURITY_MAX_AUDIT_LOGS];
static size_t audit_log_count = 0;
static uint32_t active_user_uid = SECURITY_UID_ROOT;
static bool security_initialized = false;
static uint64_t entropy_state = 0x853C49E6748FEA9BULL;

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

/* -------------------------------------------------------------------------- */
/* Standard FIPS 180-4 SHA-256 Implementation                                  */
/* -------------------------------------------------------------------------- */

struct sha256_ctx {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t  buf[64];
};

static const uint32_t K256[64] = {
    0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U,
    0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
    0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
    0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
    0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU,
    0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
    0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
    0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
    0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
    0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
    0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U,
    0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
    0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U,
    0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
    0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
    0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U
};

static inline uint32_t rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32U - n));
}

#define S256_CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define S256_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define S256_EP0(x)       (rotr32((x), 2U) ^ rotr32((x), 13U) ^ rotr32((x), 22U))
#define S256_EP1(x)       (rotr32((x), 6U) ^ rotr32((x), 11U) ^ rotr32((x), 25U))
#define S256_SIG0(x)      (rotr32((x), 7U) ^ rotr32((x), 18U) ^ ((x) >> 3U))
#define S256_SIG1(x)      (rotr32((x), 17U) ^ rotr32((x), 19U) ^ ((x) >> 10U))

static void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t w[64];
    for (uint32_t i = 0; i < 16U; i++) {
        w[i] = ((uint32_t)block[i * 4U + 0U] << 24) |
               ((uint32_t)block[i * 4U + 1U] << 16) |
               ((uint32_t)block[i * 4U + 2U] << 8)  |
               ((uint32_t)block[i * 4U + 3U]);
    }
    for (uint32_t i = 16U; i < 64U; i++) {
        w[i] = S256_SIG1(w[i - 2U]) + w[i - 7U] + S256_SIG0(w[i - 15U]) + w[i - 16U];
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];

    for (uint32_t i = 0; i < 64U; i++) {
        uint32_t t1 = h + S256_EP1(e) + S256_CH(e, f, g) + K256[i] + w[i];
        uint32_t t2 = S256_EP0(a) + S256_MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

static void sha256_init(struct sha256_ctx *ctx) {
    ctx->state[0] = 0x6A09E667U;
    ctx->state[1] = 0xBB67AE85U;
    ctx->state[2] = 0x3C6EF372U;
    ctx->state[3] = 0xA54FF53AU;
    ctx->state[4] = 0x510E527FU;
    ctx->state[5] = 0x9B05688CU;
    ctx->state[6] = 0x1F83D9ABU;
    ctx->state[7] = 0x5BE0CD19U;
    ctx->bit_count = 0;
    for (size_t i = 0; i < 64U; i++) ctx->buf[i] = 0;
}

static void sha256_update(struct sha256_ctx *ctx, const void *data, size_t len) {
    if (data == NULL || len == 0) return;
    const uint8_t *ptr = (const uint8_t *)data;
    size_t buf_idx = (size_t)((ctx->bit_count >> 3) & 0x3FU);
    ctx->bit_count += (uint64_t)len * 8ULL;

    while (len > 0) {
        size_t space = 64U - buf_idx;
        size_t chunk = (len < space) ? len : space;
        for (size_t i = 0; i < chunk; i++) {
            ctx->buf[buf_idx + i] = ptr[i];
        }
        buf_idx += chunk;
        ptr += chunk;
        len -= chunk;

        if (buf_idx == 64U) {
            sha256_transform(ctx->state, ctx->buf);
            buf_idx = 0;
        }
    }
}

static void sha256_final(struct sha256_ctx *ctx, uint8_t digest[SECURITY_HASH_SIZE]) {
    uint8_t pad_byte = 0x80U;
    uint64_t total_bits = ctx->bit_count;

    sha256_update(ctx, &pad_byte, 1U);
    uint8_t zero_byte = 0U;
    while (((ctx->bit_count >> 3) & 0x3FU) != 56U) {
        sha256_update(ctx, &zero_byte, 1U);
    }

    uint8_t len_bytes[8];
    for (int i = 0; i < 8; i++) {
        len_bytes[i] = (uint8_t)(total_bits >> ((7 - i) * 8));
    }
    sha256_update(ctx, len_bytes, 8U);

    for (uint32_t i = 0; i < 8U; i++) {
        digest[i * 4U + 0U] = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 4U + 1U] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4U + 2U] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4U + 3U] = (uint8_t)(ctx->state[i]);
    }
}

void sha256(const void *data, size_t len, uint8_t digest[SECURITY_HASH_SIZE]) {
    struct sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

/* -------------------------------------------------------------------------- */
/* RFC 2104 HMAC-SHA256                                                       */
/* -------------------------------------------------------------------------- */

void hmac_sha256(const void *key, size_t key_len, const void *data, size_t data_len, uint8_t out[SECURITY_HASH_SIZE]) {
    uint8_t k_pad[64];
    for (size_t i = 0; i < 64U; i++) k_pad[i] = 0;

    if (key_len > 64U) {
        sha256(key, key_len, k_pad);
    } else if (key != NULL && key_len > 0) {
        const uint8_t *k = (const uint8_t *)key;
        for (size_t i = 0; i < key_len; i++) k_pad[i] = k[i];
    }

    uint8_t ipad[64];
    uint8_t opad[64];
    for (size_t i = 0; i < 64U; i++) {
        ipad[i] = (uint8_t)(k_pad[i] ^ 0x36U);
        opad[i] = (uint8_t)(k_pad[i] ^ 0x5CU);
    }

    struct sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, ipad, 64U);
    sha256_update(&ctx, data, data_len);
    uint8_t inner[SECURITY_HASH_SIZE];
    sha256_final(&ctx, inner);

    sha256_init(&ctx);
    sha256_update(&ctx, opad, 64U);
    sha256_update(&ctx, inner, SECURITY_HASH_SIZE);
    sha256_final(&ctx, out);
}

/* -------------------------------------------------------------------------- */
/* RFC 2898 / RFC 6070 PBKDF2-HMAC-SHA256                                     */
/* -------------------------------------------------------------------------- */

void pbkdf2_hmac_sha256(const char *password, size_t pass_len,
                        const uint8_t *salt, size_t salt_len,
                        uint32_t iterations, uint8_t out[SECURITY_HASH_SIZE]) {
    if (password == NULL || salt == NULL || iterations == 0 || out == NULL) return;

    /* For dkLen = 32 bytes (block index 1) */
    uint8_t salt_block[72];
    size_t s_len = (salt_len > 64U) ? 64U : salt_len;
    for (size_t i = 0; i < s_len; i++) salt_block[i] = salt[i];
    salt_block[s_len + 0U] = 0x00U;
    salt_block[s_len + 1U] = 0x00U;
    salt_block[s_len + 2U] = 0x00U;
    salt_block[s_len + 3U] = 0x01U;

    uint8_t u_prev[SECURITY_HASH_SIZE];
    uint8_t u_curr[SECURITY_HASH_SIZE];
    hmac_sha256(password, pass_len, salt_block, s_len + 4U, u_prev);

    for (size_t i = 0; i < SECURITY_HASH_SIZE; i++) {
        out[i] = u_prev[i];
    }

    for (uint32_t iter = 1U; iter < iterations; iter++) {
        hmac_sha256(password, pass_len, u_prev, SECURITY_HASH_SIZE, u_curr);
        for (size_t i = 0; i < SECURITY_HASH_SIZE; i++) {
            out[i] ^= u_curr[i];
            u_prev[i] = u_curr[i];
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Kernel Entropy Source & Salt Generation                                    */
/* -------------------------------------------------------------------------- */

static inline void cpuid_query(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile ("cpuid"
                      : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                      : "a"(leaf)
                      : "memory");
}

static inline bool rdrand_u32(uint32_t *val) {
    uint8_t ok = 0;
    __asm__ volatile ("rdrand %0; setc %1" : "=r"(*val), "=qm"(ok) : : "cc");
    return ok != 0;
}

static inline uint64_t read_tsc(void) {
    uint32_t lo = 0, hi = 0;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

static inline uint8_t cmos_read_byte(uint8_t reg) {
    __asm__ volatile ("outb %0, %1" : : "a"(reg), "Nd"((uint16_t)0x70));
    uint8_t val = 0;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"((uint16_t)0x71));
    return val;
}

/*
 * Generates a per-user cryptographic salt.
 * Uses CPU hardware RDRAND when present (CPUID.01H:ECX.RDRAND[bit 30]).
 * When RDRAND is not supported, mixes cycle-accurate RDTSC jitter, CMOS RTC
 * registers, and a splitmix64-style PRNG.
 * Limitation note: Fallback entropy quality depends on CPU cycle timing jitter
 * and RTC real-time state; production virtualization hosts should provide RDRAND.
 */
uint32_t security_generate_salt(void) {
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    cpuid_query(1U, &eax, &ebx, &ecx, &edx);
    if ((ecx & (1U << 30)) != 0U) {
        uint32_t rnd = 0;
        for (int retry = 0; retry < 10; retry++) {
            if (rdrand_u32(&rnd) && rnd != 0U && rnd != 0xFFFFFFFFU) {
                return rnd;
            }
        }
    }

    uint64_t tsc = read_tsc();
    uint8_t rtc_sec = cmos_read_byte(0x00U);
    uint8_t rtc_min = cmos_read_byte(0x02U);
    uint8_t rtc_hr  = cmos_read_byte(0x04U);
    uint8_t rtc_day = cmos_read_byte(0x07U);

    uint64_t rtc_val = ((uint64_t)rtc_day << 24) | ((uint64_t)rtc_hr << 16) |
                       ((uint64_t)rtc_min << 8)  | (uint64_t)rtc_sec;

    entropy_state ^= (tsc + rtc_val + 0x9E3779B97F4A7C15ULL);
    entropy_state = (entropy_state ^ (entropy_state >> 30)) * 0xBF58476D1CE4E5B9ULL;
    entropy_state = (entropy_state ^ (entropy_state >> 27)) * 0x94D049BB133111EBULL;
    entropy_state ^= (entropy_state >> 31);

    uint32_t salt = (uint32_t)(entropy_state ^ (entropy_state >> 32));
    if (salt == 0U) salt = 0xA5A5A5A5U;
    return salt;
}

/* -------------------------------------------------------------------------- */
/* Password Hashing & Verification (PBKDF2-HMAC-SHA256, 10,000 Iterations)   */
/* -------------------------------------------------------------------------- */

void security_hash_password(const char *password, uint32_t salt, uint8_t digest[SECURITY_HASH_SIZE]) {
    if (password == NULL || digest == NULL) return;
    size_t pass_len = 0;
    while (password[pass_len] != '\0') pass_len++;

    uint8_t salt_bytes[4];
    salt_bytes[0] = (uint8_t)(salt >> 24);
    salt_bytes[1] = (uint8_t)(salt >> 16);
    salt_bytes[2] = (uint8_t)(salt >> 8);
    salt_bytes[3] = (uint8_t)(salt);

    pbkdf2_hmac_sha256(password, pass_len, salt_bytes, sizeof(salt_bytes),
                       SECURITY_PBKDF2_ITERATIONS, digest);
}

bool security_verify_password(const char *password, uint32_t salt, const uint8_t digest[SECURITY_HASH_SIZE]) {
    if (password == NULL || digest == NULL) return false;

    uint8_t computed[SECURITY_HASH_SIZE] = {0};
    security_hash_password(password, salt, computed);

    /* Constant-time memory comparison to mitigate timing attacks */
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < SECURITY_HASH_SIZE; i++) {
        diff = (uint8_t)(diff | (computed[i] ^ digest[i]));
    }

    return diff == 0;
}

/* -------------------------------------------------------------------------- */
/* Audit Logging                                                              */
/* -------------------------------------------------------------------------- */

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

/* -------------------------------------------------------------------------- */
/* User Account Management                                                    */
/* -------------------------------------------------------------------------- */

bool security_add_user(const char *username, const char *password, uint32_t uid, uint32_t gid, const char *home, const char *shell) {
    if (username == NULL || password == NULL) return false;

    for (size_t i = 0; i < SECURITY_MAX_USERS; i++) {
        if (!user_table[i].active || user_table[i].uid == uid) {
            user_table[i].uid = uid;
            user_table[i].gid = gid;
            user_table[i].salt = security_generate_salt();
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
    if (uid == SECURITY_UID_ROOT) {
        if ((requested_access & SECURITY_ACCESS_EXECUTE) != 0U) {
            return (file_mode & 0111U) != 0U;
        }
        return true;
    }

    uint32_t perm_bits = 0;
    if (uid == file_uid) {
        perm_bits = (file_mode >> 6) & 0x7U;
    } else if (gid == file_gid) {
        perm_bits = (file_mode >> 3) & 0x7U;
    } else {
        perm_bits = file_mode & 0x7U;
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
    if (addr >= 0xFFFF800000000000ULL || (addr + len) < addr || (addr + len) >= 0xFFFF800000000000ULL) {
        security_audit_log(AUDIT_SYSCALL_VIOLATION, active_user_uid, 0, "Kernel space address intrusion");
        return false;
    }

    return true;
}

bool security_is_first_boot(void) {
    return !user_table[0].active;
}

bool security_initialize(void) {
    if (security_initialized) return true;

    for (size_t i = 0; i < SECURITY_MAX_USERS; i++) user_table[i] = (struct user_account){0};
    for (size_t i = 0; i < SECURITY_MAX_AUDIT_LOGS; i++) audit_log_table[i] = (struct audit_record){0};
    audit_log_count = 0;
    active_user_uid = SECURITY_UID_ROOT;

#ifdef DAWN_KERNEL_TEST
    /* In self-test builds, configure test accounts */
    security_add_user("root", "toor", SECURITY_UID_ROOT, SECURITY_GID_ROOT, "/root", "/bin/dsh");
    security_add_user("dawn", "dawn", SECURITY_UID_DEFAULT, SECURITY_GID_DEFAULT, "/home/dawn", "/bin/dsh");
#else
    /* Production builds: do not use hardcoded passwords.
     * Mark root account reserved and inactive, forcing first-boot password setup. */
    user_table[0].uid = SECURITY_UID_ROOT;
    user_table[0].gid = SECURITY_GID_ROOT;
    user_table[0].salt = security_generate_salt();
    str_copy(user_table[0].username, "root", sizeof(user_table[0].username));
    str_copy(user_table[0].home_dir, "/root", sizeof(user_table[0].home_dir));
    str_copy(user_table[0].shell, "/bin/dsh", sizeof(user_table[0].shell));
    user_table[0].active = false;
#endif

    security_initialized = true;
    return true;
}

/* -------------------------------------------------------------------------- */
/* Cryptographic Hardening Self-Tests                                         */
/* -------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------- */
/* Cryptographic Hardening Self-Tests                                         */
/* -------------------------------------------------------------------------- */

bool security_self_test(void) {
    if (!security_initialize()) return false;

    /* 1. SHA-256 NIST Vector 1: Empty string "" */
    static const uint8_t nist_empty_expected[32] = {
        0xE3, 0xB0, 0xC4, 0x42, 0x98, 0xFC, 0x1C, 0x14,
        0x9A, 0xFB, 0xF4, 0xC8, 0x99, 0x6F, 0xB9, 0x24,
        0x27, 0xAE, 0x41, 0xE4, 0x64, 0x9B, 0x93, 0x4C,
        0xA4, 0x95, 0x99, 0x1B, 0x78, 0x52, 0xB8, 0x55
    };
    uint8_t digest_empty[32];
    sha256("", 0, digest_empty);
    for (size_t i = 0; i < 32U; i++) {
        if (digest_empty[i] != nist_empty_expected[i]) return false;
    }

    /* 2. SHA-256 NIST Vector 2: "abc" */
    static const uint8_t nist_abc_expected[32] = {
        0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA,
        0x41, 0x41, 0x40, 0xDE, 0x5D, 0xAE, 0x22, 0x23,
        0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C,
        0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD
    };
    uint8_t digest_abc[32];
    sha256("abc", 3, digest_abc);
    for (size_t i = 0; i < 32U; i++) {
        if (digest_abc[i] != nist_abc_expected[i]) return false;
    }

    /* 3. PBKDF2-HMAC-SHA256 Test Case 1: P="password", S="salt", c=1, dkLen=32 */
    static const uint8_t rfc6070_c1_expected[32] = {
        0x12, 0x0F, 0xB6, 0xCF, 0xFC, 0xF8, 0xB3, 0x2C,
        0x43, 0xE7, 0x22, 0x52, 0x56, 0xC4, 0xF8, 0x37,
        0xA8, 0x65, 0x48, 0xC9, 0x2C, 0xCC, 0x35, 0x48,
        0x08, 0x05, 0x98, 0x7C, 0xB7, 0x0B, 0xE1, 0x7B
    };
    uint8_t pbkdf2_c1_out[32];
    pbkdf2_hmac_sha256("password", 8, (const uint8_t *)"salt", 4, 1U, pbkdf2_c1_out);
    for (size_t i = 0; i < 32U; i++) {
        if (pbkdf2_c1_out[i] != rfc6070_c1_expected[i]) return false;
    }

    /* 4. PBKDF2-HMAC-SHA256 Test Case 2: P="password", S="salt", c=2, dkLen=32 */
    static const uint8_t rfc6070_c2_expected[32] = {
        0xAE, 0x4D, 0x0C, 0x95, 0xAF, 0x6B, 0x46, 0xD3,
        0x2D, 0x0A, 0xDF, 0xF9, 0x28, 0xF0, 0x6D, 0xD0,
        0x2A, 0x30, 0x3F, 0x8E, 0xF3, 0xC2, 0x51, 0xDF,
        0xD6, 0xE2, 0xD8, 0x5A, 0x95, 0x47, 0x4C, 0x43
    };
    uint8_t pbkdf2_c2_out[32];
    pbkdf2_hmac_sha256("password", 8, (const uint8_t *)"salt", 4, 2U, pbkdf2_c2_out);
    for (size_t i = 0; i < 32U; i++) {
        if (pbkdf2_c2_out[i] != rfc6070_c2_expected[i]) return false;
    }

    /* 5. Per-user dynamic salt generation test: distinct salts */
    uint32_t salt_a = security_generate_salt();
    uint32_t salt_b = security_generate_salt();
    if (salt_a == salt_b) return false;

    /* 6. Password hashing and verification test */
    uint8_t hash1[SECURITY_HASH_SIZE];
    uint8_t hash2[SECURITY_HASH_SIZE];
    security_hash_password("admin_pass_2026", salt_a, hash1);
    security_hash_password("admin_pass_2026", salt_a, hash2);

    for (size_t i = 0; i < SECURITY_HASH_SIZE; i++) {
        if (hash1[i] != hash2[i]) return false;
    }

    /* 7. Verification and wrong-password rejection */
    if (!security_verify_password("admin_pass_2026", salt_a, hash1)) return false;
    if (security_verify_password("wrong_password", salt_a, hash1)) return false;
    if (security_verify_password("admin_pass_2026", salt_b, hash1)) return false;

    /* 8. User authentication test */
    uint32_t auth_uid = 0;
    if (!security_authenticate_user("root", "toor", &auth_uid) || auth_uid != SECURITY_UID_ROOT) return false;
    if (!security_authenticate_user("dawn", "dawn", &auth_uid) || auth_uid != SECURITY_UID_DEFAULT) return false;
    if (security_authenticate_user("dawn", "wrongpass", &auth_uid)) return false;

    /* 9. File permission tests */
    if (!security_check_file_permission(1000, 1000, 1000, 1000, 0700, SECURITY_ACCESS_READ)) return false;
    if (security_check_file_permission(1001, 1001, 1000, 1000, 0700, SECURITY_ACCESS_READ)) return false;
    if (!security_check_file_permission(SECURITY_UID_ROOT, SECURITY_GID_ROOT, 1000, 1000, 0700, SECURITY_ACCESS_READ)) return false;

    /* 10. Memory boundary validation */
    const void *valid_user_ptr = (const void *)0x0000000040000000ULL;
    if (!security_validate_user_pointer(valid_user_ptr, 64)) return false;

    const void *invalid_kernel_ptr = (const void *)0xFFFF800000001000ULL;
    if (security_validate_user_pointer(invalid_kernel_ptr, 64)) return false;

    return true;
}
