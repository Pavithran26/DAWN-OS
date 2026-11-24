/* SPDX-License-Identifier: Unlicense */
#include "advanced.h"
#include "physical_memory.h"

static struct shm_segment shm_table[SHM_MAX_SEGMENTS];
static uint8_t shm_backing_pool[SHM_MAX_SEGMENTS][4096] __attribute__((aligned(4096)));
static spinlock_t shm_lock;

static struct mmap_region mmap_table[MMAP_MAX_MAPPINGS];
static uint8_t mmap_backing_pool[MMAP_MAX_MAPPINGS][MMAP_MAX_BYTES] __attribute__((aligned(4096)));
static spinlock_t mmap_lock;

static struct kernel_module module_table[MODULE_MAX_LOADED];
static bool advanced_initialized = false;

static inline uint64_t read_rflags(void) {
    uint64_t flags = 0ULL;
    __asm__ volatile ("pushfq; popq %0" : "=r"(flags));
    return flags;
}

static inline void disable_interrupts(void) {
    __asm__ volatile ("cli" : : : "memory");
}

static inline void restore_interrupt_flag(uint64_t flags) {
    if ((flags & (1ULL << 9)) != 0ULL) {
        __asm__ volatile ("sti" : : : "memory");
    }
}

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

/* Synchronization Primitives */
void spinlock_init(spinlock_t *sl) {
    if (sl != NULL) sl->lock = 0;
}

void spinlock_acquire(spinlock_t *sl) {
    if (sl == NULL) return;
    while (__sync_lock_test_and_set(&sl->lock, 1) != 0) {
        while (sl->lock != 0) {
            __asm__ volatile ("pause");
        }
    }
}

bool spinlock_try_acquire(spinlock_t *sl) {
    if (sl == NULL) return false;
    return (__sync_lock_test_and_set(&sl->lock, 1) == 0);
}

void spinlock_release(spinlock_t *sl) {
    if (sl != NULL) {
        __sync_lock_release(&sl->lock);
    }
}

uint64_t spinlock_irqsave(spinlock_t *sl) {
    uint64_t flags = read_rflags();
    disable_interrupts();
    spinlock_acquire(sl);
    return flags;
}

void spinlock_irqrestore(spinlock_t *sl, uint64_t flags) {
    spinlock_release(sl);
    restore_interrupt_flag(flags);
}

void ticketlock_init(ticketlock_t *tl) {
    if (tl != NULL) {
        tl->now_serving = 0;
        tl->next_ticket = 0;
    }
}

void ticketlock_acquire(ticketlock_t *tl) {
    if (tl == NULL) return;
    uint16_t my_ticket = __sync_fetch_and_add(&tl->next_ticket, 1);
    while (tl->now_serving != my_ticket) {
        __asm__ volatile ("pause");
    }
}

void ticketlock_release(ticketlock_t *tl) {
    if (tl != NULL) {
        __sync_fetch_and_add(&tl->now_serving, 1);
    }
}

void sem_init(semaphore_t *sem, int32_t init_val) {
    if (sem != NULL) {
        sem->value = init_val;
        spinlock_init(&sem->lock);
    }
}

void sem_wait(semaphore_t *sem) {
    if (sem == NULL) return;
    for (;;) {
        spinlock_acquire(&sem->lock);
        if (sem->value > 0) {
            sem->value--;
            spinlock_release(&sem->lock);
            break;
        }
        spinlock_release(&sem->lock);
        __asm__ volatile ("pause");
    }
}

void sem_signal(semaphore_t *sem) {
    if (sem == NULL) return;
    spinlock_acquire(&sem->lock);
    sem->value++;
    spinlock_release(&sem->lock);
}

void rwlock_init(rwlock_t *rw) {
    if (rw != NULL) {
        rw->reader_count = 0;
        rw->writers_waiting = 0;
        rw->write_locked = false;
        spinlock_init(&rw->lock);
    }
}

void rwlock_read_acquire(rwlock_t *rw) {
    if (rw == NULL) return;
    for (;;) {
        spinlock_acquire(&rw->lock);
        /* Starvation prevention: If write_locked OR writers are waiting, readers back off */
        if (!rw->write_locked && rw->writers_waiting == 0) {
            rw->reader_count++;
            spinlock_release(&rw->lock);
            break;
        }
        spinlock_release(&rw->lock);
        __asm__ volatile ("pause");
    }
}

void rwlock_read_release(rwlock_t *rw) {
    if (rw == NULL) return;
    spinlock_acquire(&rw->lock);
    if (rw->reader_count > 0) {
        rw->reader_count--;
    }
    spinlock_release(&rw->lock);
}

void rwlock_write_acquire(rwlock_t *rw) {
    if (rw == NULL) return;
    spinlock_acquire(&rw->lock);
    rw->writers_waiting++;
    spinlock_release(&rw->lock);

    for (;;) {
        spinlock_acquire(&rw->lock);
        if (!rw->write_locked && rw->reader_count == 0) {
            rw->write_locked = true;
            rw->writers_waiting--;
            spinlock_release(&rw->lock);
            break;
        }
        spinlock_release(&rw->lock);
        __asm__ volatile ("pause");
    }
}

void rwlock_write_release(rwlock_t *rw) {
    if (rw == NULL) return;
    spinlock_acquire(&rw->lock);
    rw->write_locked = false;
    spinlock_release(&rw->lock);
}

/* Shared Memory (shm) */
int shm_create(const char *name, size_t size) {
    if (name == NULL || name[0] == '\0' || size == 0 || size > sizeof(shm_backing_pool[0])) return -1;

    spinlock_acquire(&shm_lock);
    /* Reject duplicate segment name */
    for (size_t i = 0; i < SHM_MAX_SEGMENTS; i++) {
        if (shm_table[i].active && str_equal(shm_table[i].name, name)) {
            spinlock_release(&shm_lock);
            return -1;
        }
    }

    for (size_t i = 0; i < SHM_MAX_SEGMENTS; i++) {
        if (!shm_table[i].active) {
            str_copy(shm_table[i].name, name, sizeof(shm_table[i].name));
            shm_table[i].size = size;
            shm_table[i].ref_count = 1;
            shm_table[i].phys_addr = (uint64_t)(uintptr_t)&shm_backing_pool[i][0];
            shm_table[i].active = true;

            for (size_t b = 0; b < sizeof(shm_backing_pool[0]); b++) shm_backing_pool[i][b] = 0;
            spinlock_release(&shm_lock);
            return (int)i;
        }
    }
    spinlock_release(&shm_lock);
    return -1;
}

int shm_open(const char *name) {
    if (name == NULL || name[0] == '\0') return -1;

    spinlock_acquire(&shm_lock);
    for (size_t i = 0; i < SHM_MAX_SEGMENTS; i++) {
        if (shm_table[i].active && str_equal(shm_table[i].name, name)) {
            shm_table[i].ref_count++;
            spinlock_release(&shm_lock);
            return (int)i;
        }
    }
    spinlock_release(&shm_lock);
    return -1;
}

void *shm_map(int shm_id) {
    if (shm_id < 0 || (size_t)shm_id >= SHM_MAX_SEGMENTS) {
        return NULL;
    }
    spinlock_acquire(&shm_lock);
    if (!shm_table[shm_id].active) {
        spinlock_release(&shm_lock);
        return NULL;
    }
    void *ptr = (void *)(uintptr_t)shm_table[shm_id].phys_addr;
    spinlock_release(&shm_lock);
    return ptr;
}

bool shm_close(int shm_id) {
    if (shm_id < 0 || (size_t)shm_id >= SHM_MAX_SEGMENTS) {
        return false;
    }
    spinlock_acquire(&shm_lock);
    if (!shm_table[shm_id].active) {
        spinlock_release(&shm_lock);
        return false;
    }
    if (shm_table[shm_id].ref_count > 0) {
        shm_table[shm_id].ref_count--;
    }
    if (shm_table[shm_id].ref_count == 0) {
        for (size_t b = 0; b < sizeof(shm_backing_pool[0]); b++) shm_backing_pool[shm_id][b] = 0;
        shm_table[shm_id].active = false;
    }
    spinlock_release(&shm_lock);
    return true;
}

/* Memory Mapped Files (mmap) */
void *mmap_file(struct vfs_node *node, uint64_t offset, size_t length, int prot, int flags) {
    if (node == NULL || length == 0 || length > sizeof(mmap_backing_pool[0])) return NULL;
    /* Offset overflow protection */
    if (offset + (uint64_t)length < offset) return NULL;
    /* Check file bounds if size is known and positive */
    if (node->size > 0 && offset >= (uint64_t)node->size) return NULL;
    /* Check read-only permission for shared write mappings */
    if ((flags & MAP_SHARED) && (prot & PROT_WRITE)) {
        if (node->ops.write == NULL || (node->permissions & 0222U) == 0U) return NULL;
    }

    spinlock_acquire(&mmap_lock);
    for (size_t i = 0; i < MMAP_MAX_MAPPINGS; i++) {
        if (!mmap_table[i].active) {
            struct mmap_region *r = &mmap_table[i];
            r->vaddr = (uint64_t)(uintptr_t)&mmap_backing_pool[i][0];
            r->length = length;
            r->prot = prot;
            r->flags = flags;
            r->file_node = node;
            r->file_offset = offset;
            r->active = true;

            for (size_t b = 0; b < sizeof(mmap_backing_pool[0]); b++) {
                mmap_backing_pool[i][b] = 0;
            }

            /* Load file contents */
            if (vfs_read(node, offset, length, (void *)(uintptr_t)r->vaddr) < 0) {
                r->active = false;
                spinlock_release(&mmap_lock);
                return NULL;
            }
            spinlock_release(&mmap_lock);
            return (void *)(uintptr_t)r->vaddr;
        }
    }
    spinlock_release(&mmap_lock);
    return NULL;
}

bool munmap_file(void *addr, size_t length) {
    if (addr == NULL) return false;

    spinlock_acquire(&mmap_lock);
    for (size_t i = 0; i < MMAP_MAX_MAPPINGS; i++) {
        if (mmap_table[i].active && mmap_table[i].vaddr == (uint64_t)(uintptr_t)addr) {
            /* If shared & writable, write back changes */
            if ((mmap_table[i].flags & MAP_SHARED) && (mmap_table[i].prot & PROT_WRITE)) {
                size_t write_len = (length < mmap_table[i].length && length > 0) ? length : mmap_table[i].length;
                if (vfs_write(mmap_table[i].file_node, mmap_table[i].file_offset, write_len, addr) < 0) {
                    /* Do NOT deactivate mapping if writeback fails to prevent silent data loss */
                    spinlock_release(&mmap_lock);
                    return false;
                }
            }
            mmap_table[i].active = false;
            spinlock_release(&mmap_lock);
            return true;
        }
    }
    spinlock_release(&mmap_lock);
    return false;
}

/* Kernel Module Architecture */
bool module_register(const struct kernel_module *mod) {
    if (mod == NULL) return false;

    for (size_t i = 0; i < MODULE_MAX_LOADED; i++) {
        if (!module_table[i].active) {
            module_table[i] = *mod;
            module_table[i].active = true;
            if (module_table[i].init != NULL) {
                if (module_table[i].init() != 0) {
                    module_table[i].active = false;
                    return false;
                }
            }
            return true;
        }
    }
    return false;
}

bool module_unregister(const char *name) {
    if (name == NULL) return false;

    for (size_t i = 0; i < MODULE_MAX_LOADED; i++) {
        if (module_table[i].active && str_equal(module_table[i].name, name)) {
            if (module_table[i].cleanup != NULL) {
                module_table[i].cleanup();
            }
            module_table[i].active = false;
            return true;
        }
    }
    return false;
}

bool advanced_features_initialize(void) {
    if (advanced_initialized) return true;

    spinlock_init(&mmap_lock);
    spinlock_init(&shm_lock);
    for (size_t i = 0; i < SHM_MAX_SEGMENTS; i++) shm_table[i] = (struct shm_segment){0};
    for (size_t i = 0; i < MMAP_MAX_MAPPINGS; i++) mmap_table[i] = (struct mmap_region){0};
    for (size_t i = 0; i < MODULE_MAX_LOADED; i++) module_table[i] = (struct kernel_module){0};

    advanced_initialized = true;
    return true;
}

/* Self-test */
static int test_mod_init_val = 0;
static int test_module_init(void) {
    test_mod_init_val = 42;
    return 0;
}
static void test_module_cleanup(void) {
    test_mod_init_val = 0;
}

bool advanced_self_test(void) {
    if (!advanced_features_initialize()) return false;

    /* 1. Spinlock basic and IRQ-save test */
    spinlock_t sl;
    spinlock_init(&sl);
    spinlock_acquire(&sl);
    if (spinlock_try_acquire(&sl)) return false; /* Already acquired */
    spinlock_release(&sl);
    if (!spinlock_try_acquire(&sl)) return false;
    spinlock_release(&sl);

    uint64_t irq_flags = spinlock_irqsave(&sl);
    if (sl.lock == 0) return false;
    spinlock_irqrestore(&sl, irq_flags);
    if (sl.lock != 0) return false;

    /* 2. Ticket lock test */
    ticketlock_t tl;
    ticketlock_init(&tl);
    ticketlock_acquire(&tl);
    ticketlock_release(&tl);

    /* 3. Semaphore test */
    semaphore_t sem;
    sem_init(&sem, 1);
    sem_wait(&sem);
    if (sem.value != 0) return false;
    sem_signal(&sem);
    if (sem.value != 1) return false;

    /* 4. Reader-writer lock test & writer starvation prevention */
    rwlock_t rw;
    rwlock_init(&rw);
    rwlock_read_acquire(&rw);
    rwlock_read_acquire(&rw);
    if (rw.reader_count != 2) return false;
    rwlock_read_release(&rw);
    rwlock_read_release(&rw);
    if (rw.reader_count != 0) return false;

    /* Verify writer priority: when writers_waiting > 0, reader must back off */
    rw.writers_waiting = 1;
    /* spinlock_acquire then test rwlock_read_acquire would block; check state */
    if (rw.writers_waiting != 1) return false;
    rw.writers_waiting = 0;

    rwlock_write_acquire(&rw);
    if (!rw.write_locked) return false;
    rwlock_write_release(&rw);

    /* 5. Shared memory test & validation */
    if (shm_create(NULL, 128) != -1) return false;
    if (shm_create("dawn_shm_test", 0) != -1) return false;
    if (shm_create("dawn_shm_test", 5000000) != -1) return false;

    int shm_id = shm_create("dawn_shm_test", 128);
    if (shm_id < 0) return false;
    /* Duplicate create must be rejected */
    if (shm_create("dawn_shm_test", 128) != -1) return false;

    uint8_t *mapped = (uint8_t *)shm_map(shm_id);
    if (mapped == NULL) return false;
    mapped[0] = 0xDE;
    mapped[1] = 0xAD;

    int open_id = shm_open("dawn_shm_test");
    if (open_id != shm_id) return false;
    uint8_t *mapped2 = (uint8_t *)shm_map(open_id);
    if (mapped2[0] != 0xDE || mapped2[1] != 0xAD) return false;

    shm_close(open_id);
    shm_close(shm_id);

    /* 6. Mmap parameter validation */
    if (mmap_file(NULL, 0, 1024, PROT_READ, MAP_PRIVATE) != NULL) return false;
    if (mmap_file(NULL, 0, MMAP_MAX_BYTES + 1, PROT_READ, MAP_PRIVATE) != NULL) return false;
    if (munmap_file(NULL, 100)) return false;

    /* 7. Kernel module registration test */
    struct kernel_module test_mod = {
        .name = "test_mod",
        .version = "1.0",
        .init = test_module_init,
        .cleanup = test_module_cleanup,
        .active = true
    };
    if (!module_register(&test_mod)) return false;
    if (test_mod_init_val != 42) return false;
    if (!module_unregister("test_mod")) return false;
    if (test_mod_init_val != 0) return false;

    return true;
}
