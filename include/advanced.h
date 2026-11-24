/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_ADVANCED_H
#define DAWN_ADVANCED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "vfs.h"

#define SHM_MAX_SEGMENTS  8U
#define MMAP_MAX_MAPPINGS 16U
#define MODULE_MAX_LOADED 8U

/* Advanced Synchronization Primitives */
typedef struct {
    volatile uint32_t lock;
} spinlock_t;

typedef struct {
    volatile uint16_t now_serving;
    volatile uint16_t next_ticket;
} ticketlock_t;

typedef struct {
    volatile int32_t value;
    spinlock_t lock;
} semaphore_t;

typedef struct {
    volatile int32_t reader_count;
    volatile int32_t writers_waiting;
    volatile bool    write_locked;
    spinlock_t       lock;
} rwlock_t;

void spinlock_init(spinlock_t *sl);
void spinlock_acquire(spinlock_t *sl);
bool spinlock_try_acquire(spinlock_t *sl);
void spinlock_release(spinlock_t *sl);
uint64_t spinlock_irqsave(spinlock_t *sl);
void spinlock_irqrestore(spinlock_t *sl, uint64_t flags);

void ticketlock_init(ticketlock_t *tl);
void ticketlock_acquire(ticketlock_t *tl);
void ticketlock_release(ticketlock_t *tl);

void sem_init(semaphore_t *sem, int32_t init_val);
void sem_wait(semaphore_t *sem);
void sem_signal(semaphore_t *sem);

void rwlock_init(rwlock_t *rw);
void rwlock_read_acquire(rwlock_t *rw);
void rwlock_read_release(rwlock_t *rw);
void rwlock_write_acquire(rwlock_t *rw);
void rwlock_write_release(rwlock_t *rw);

/* Shared Memory (shm) */
struct shm_segment {
    char     name[32];
    uint64_t phys_addr;
    size_t   size;
    uint32_t ref_count;
    bool     active;
};

int  shm_create(const char *name, size_t size);
int  shm_open(const char *name);
void *shm_map(int shm_id);
bool shm_close(int shm_id);

/* Memory Mapped Files (mmap) */
#define MMAP_MAX_BYTES (64U * 1024U) /* 64 KB per mapping */
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define MAP_SHARED 0x1
#define MAP_PRIVATE 0x2

struct mmap_region {
    uint64_t         vaddr;
    size_t           length;
    int              prot;
    int              flags;
    struct vfs_node *file_node;
    uint64_t         file_offset;
    bool             active;
};

void *mmap_file(struct vfs_node *node, uint64_t offset, size_t length, int prot, int flags);
bool  munmap_file(void *addr, size_t length);

/* Kernel Module Architecture */
struct kernel_module {
    char name[32];
    char version[16];
    int (*init)(void);
    void (*cleanup)(void);
    bool active;
};

bool module_register(const struct kernel_module *mod);
bool module_unregister(const char *name);

/* Subsystem Init & Self-Test */
bool advanced_features_initialize(void);
bool advanced_self_test(void);

#endif /* DAWN_ADVANCED_H */
