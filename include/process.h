/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_PROCESS_H
#define DAWN_PROCESS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DAWN_PROCESS_MAX 4U
#define DAWN_PROCESS_MAX_USER_PAGES 1024U
#define DAWN_PROCESS_MAX_FDS 16U

enum dawn_fd_type {
    DAWN_FD_TYPE_UNUSED = 0,
    DAWN_FD_TYPE_SERIAL = 1,
    DAWN_FD_TYPE_NULL = 2,
    DAWN_FD_TYPE_VFS = 3
};

#define DAWN_O_RDONLY 0x0001U
#define DAWN_O_WRONLY 0x0002U
#define DAWN_O_RDWR   0x0003U

struct dawn_fd_entry {
    enum dawn_fd_type type;
    uint32_t flags;
    uint64_t offset;
    void *node;
};

enum process_state {
    PROCESS_STATE_FREE = 0,
    PROCESS_STATE_CREATED,
    PROCESS_STATE_READY,
    PROCESS_STATE_EXITED
};

struct process_info {
    uint64_t id;
    uint64_t parent_id;
    enum process_state state;
    uint64_t address_space_root;
    uint64_t kernel_stack_top;
    int64_t exit_status;
    uint32_t uid;
    uint32_t gid;
};

/* Process credentials */
bool process_get_credentials(uint64_t process_id, uint32_t *out_uid, uint32_t *out_gid);
bool process_set_credentials(uint64_t process_id, uint32_t uid, uint32_t gid);

/* Process metadata, isolated mappings, user-frame ownership, and lifecycle. */
bool process_manager_initialize(void);
bool process_create(uint64_t *process_id);
/* Return metadata only when child_id belongs to parent_id. */
bool process_get_child_info(uint64_t parent_id,uint64_t child_id,
                            struct process_info *info);
/* Check if parent_id owns matching children; sets out_exited_child_id if one has EXITED. */
bool process_has_children(uint64_t parent_id,int64_t target_child_id,
                          uint64_t *out_exited_child_id);
/* Collect an EXITED child, optionally copying its status to parent user memory.
 * The parent must be READY; status_address==0 discards the status. */
bool process_collect_child(uint64_t parent_id,uint64_t child_id,
                           uint64_t status_address);
bool process_mark_ready(uint64_t process_id);
bool process_mark_exited(uint64_t process_id);
/* Record the status supplied by the active process's SYS_EXIT call. */
bool process_set_exit_status(uint64_t process_id,int64_t exit_status);
/* Activation switches CR3 and TSS.RSP0; caller must be CPL0 with IF clear. */
bool process_activate(uint64_t process_id);
bool process_switch_to(uint64_t process_id);
bool process_deactivate(void);
uint64_t process_active_id(void);
bool process_get_info(uint64_t process_id, struct process_info *info);
bool process_get_info_by_index(size_t slot_index, struct process_info *info);
size_t process_get_user_page_count(uint64_t process_id);
/* ELF image builder APIs: only CREATED processes may acquire additional user pages. */
bool process_user_page_checkpoint(uint64_t process_id,size_t *checkpoint);
bool process_allocate_user_page(uint64_t process_id,uint64_t virtual_address,uint32_t flags);
bool process_user_memory_write(uint64_t process_id,uint64_t destination,const void *source,size_t length);
bool process_user_memory_zero(uint64_t process_id,uint64_t destination,size_t length);
bool process_user_memory_rollback(uint64_t process_id,size_t checkpoint);
bool process_reset_for_exec(uint64_t process_id);
bool process_reap(uint64_t process_id);
/* File descriptor table operations */
bool process_fd_get(uint64_t process_id,int fd,struct dawn_fd_entry *entry);
bool process_fd_set(uint64_t process_id,int fd,const struct dawn_fd_entry *entry);
bool process_fd_allocate(uint64_t process_id,enum dawn_fd_type type,uint32_t flags,
                         void *node,int *out_fd);
bool process_fd_close(uint64_t process_id,int fd);
bool process_fd_dup(uint64_t process_id,int oldfd,int *out_newfd);
bool process_fd_dup2(uint64_t process_id,int oldfd,int newfd);
/* Process spawning from in-memory ELF image */
bool process_spawn(const void *image,size_t image_size,
                   int argc,const char *const argv[],const char *const envp[],
                   uint64_t *out_pid);

/* Dynamic memory break and process working directory */
uint64_t process_brk(uint64_t process_id, uint64_t new_brk);
bool process_getcwd(uint64_t process_id, char *out_buf, size_t buf_size);
bool process_chdir(uint64_t process_id, const char *path);

#ifdef DAWN_KERNEL_TEST
bool process_manager_self_test(void);
bool process_spawn_self_test(void);
#endif
#endif
