/* SPDX-License-Identifier: Unlicense */
#include "vfs.h"

#define RAMFS_MAX_CHILDREN 16U
#define RAMFS_MAX_FILE_SIZE 32768U

struct ramfs_node_data {
    uint8_t buffer[RAMFS_MAX_FILE_SIZE];
    struct vfs_node *children[RAMFS_MAX_CHILDREN];
    uint32_t child_count;
};

static struct vfs_node node_pool[VFS_MAX_NODES];
static struct ramfs_node_data ramfs_data_pool[VFS_MAX_NODES];
static struct vfs_mount mount_table[VFS_MAX_MOUNTS];
static struct vfs_node *root_node;
static uint64_t next_inode;
static bool initialized;

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
    while (s[len] != '\0') ++len;
    return len;
}

static struct vfs_node *allocate_node(enum vfs_node_type type, const char *name) {
    for (size_t i = 0; i < VFS_MAX_NODES; i++) {
        if (node_pool[i].type == VFS_TYPE_INVALID &&
            node_pool[i].inode == 0ULL) {
            struct vfs_node *n = &node_pool[i];
            *n = (struct vfs_node){0};
            n->inode = next_inode++;
            n->type = type;
            n->permissions = VFS_FLAG_READ | VFS_FLAG_WRITE;
            n->device_or_fs_data = &ramfs_data_pool[i];
            ramfs_data_pool[i] = (struct ramfs_node_data){0};
            str_copy(n->name, name, VFS_MAX_NAME);
            return n;
        }
    }
    return NULL;
}

static int64_t ramfs_read(struct vfs_node *node, uint64_t offset, size_t size, void *buffer) {
    if (node == NULL || buffer == NULL || node->type != VFS_TYPE_FILE) return -1;
    struct ramfs_node_data *data = (struct ramfs_node_data *)node->device_or_fs_data;
    if (data == NULL) return -1;
    if (offset >= node->size) return 0;
    size_t available = (size_t)(node->size - offset);
    size_t to_read = (size < available) ? size : available;
    uint8_t *dest = (uint8_t *)buffer;
    for (size_t i = 0; i < to_read; i++) {
        dest[i] = data->buffer[offset + i];
    }
    return (int64_t)to_read;
}

static int64_t ramfs_write(struct vfs_node *node, uint64_t offset, size_t size, const void *buffer) {
    if (node == NULL || buffer == NULL || node->type != VFS_TYPE_FILE) return -1;
    struct ramfs_node_data *data = (struct ramfs_node_data *)node->device_or_fs_data;
    if (data == NULL) return -1;
    if (offset > RAMFS_MAX_FILE_SIZE || size > RAMFS_MAX_FILE_SIZE - (size_t)offset) return -1;
    const uint8_t *src = (const uint8_t *)buffer;
    for (size_t i = 0; i < size; i++) {
        data->buffer[offset + i] = src[i];
    }
    if (offset + size > node->size) {
        node->size = offset + size;
    }
    return (int64_t)size;
}

static int ramfs_readdir(struct vfs_node *node, uint32_t index, struct vfs_dirent *out_dirent) {
    if (node == NULL || out_dirent == NULL || node->type != VFS_TYPE_DIRECTORY) return -1;
    struct ramfs_node_data *data = (struct ramfs_node_data *)node->device_or_fs_data;
    if (data == NULL || index >= data->child_count) return -1;
    struct vfs_node *child = data->children[index];
    if (child == NULL) return -1;
    str_copy(out_dirent->name, child->name, VFS_MAX_NAME);
    out_dirent->inode = child->inode;
    out_dirent->type = child->type;
    out_dirent->size = child->size;
    return 0;
}

static struct vfs_node *ramfs_finddir(struct vfs_node *node, const char *name) {
    if (node == NULL || name == NULL || node->type != VFS_TYPE_DIRECTORY) return NULL;
    if (str_equal(name, ".")) return node;
    if (str_equal(name, "..")) return node->parent ? node->parent : node;
    struct ramfs_node_data *data = (struct ramfs_node_data *)node->device_or_fs_data;
    if (data == NULL) return NULL;
    for (uint32_t i = 0; i < data->child_count; i++) {
        if (data->children[i] != NULL && str_equal(data->children[i]->name, name)) {
            struct vfs_node *found = data->children[i];
            if (found->mount_point != NULL) return found->mount_point;
            return found;
        }
    }
    return NULL;
}

static bool ramfs_name_valid(const char *name) {
    if (name==NULL || name[0]=='\0' || str_equal(name,".") || str_equal(name,"..")) return false;
    for (size_t i=0U;i<VFS_MAX_NAME;i++) {
        if (name[i]=='\0') return i>0U;
        if (name[i]=='/') return false;
    }
    return false;
}

static int ramfs_create(struct vfs_node *parent,const char *name,uint32_t flags,struct vfs_node **out_node) {
    (void)flags;
    if (parent==NULL || !ramfs_name_valid(name) || parent->type!=VFS_TYPE_DIRECTORY) return -1;
    struct ramfs_node_data *pdata=(struct ramfs_node_data *)parent->device_or_fs_data;
    if (pdata==NULL || pdata->child_count>=RAMFS_MAX_CHILDREN || ramfs_finddir(parent,name)!=NULL) return -1;
    struct vfs_node *child=allocate_node(VFS_TYPE_FILE,name);
    if (child==NULL) return -1;
    child->parent=parent; child->ops=parent->ops;
    pdata->children[pdata->child_count++]=child;
    if (out_node!=NULL) *out_node=child;
    return 0;
}

static int ramfs_mkdir(struct vfs_node *parent,const char *name,uint32_t flags,struct vfs_node **out_node) {
    (void)flags;
    if (parent==NULL || !ramfs_name_valid(name) || parent->type!=VFS_TYPE_DIRECTORY) return -1;
    struct ramfs_node_data *pdata=(struct ramfs_node_data *)parent->device_or_fs_data;
    if (pdata==NULL || pdata->child_count>=RAMFS_MAX_CHILDREN || ramfs_finddir(parent,name)!=NULL) return -1;
    struct vfs_node *child=allocate_node(VFS_TYPE_DIRECTORY,name);
    if (child==NULL) return -1;
    child->parent=parent; child->ops=parent->ops;
    pdata->children[pdata->child_count++]=child;
    if (out_node!=NULL) *out_node=child;
    return 0;
}

static int ramfs_unlink(struct vfs_node *parent,const char *name) {
    if (parent==NULL || !ramfs_name_valid(name) || parent->type!=VFS_TYPE_DIRECTORY) return -1;
    struct ramfs_node_data *pdata=(struct ramfs_node_data *)parent->device_or_fs_data;
    if (pdata==NULL) return -1;
    for (uint32_t i=0U;i<pdata->child_count;i++) {
        struct vfs_node *victim=pdata->children[i];
        if (victim==NULL || !str_equal(victim->name,name)) continue;
        struct ramfs_node_data *victim_data=(struct ramfs_node_data *)victim->device_or_fs_data;
        if (victim->mount_point!=NULL ||
            (victim->type==VFS_TYPE_DIRECTORY && victim_data!=NULL && victim_data->child_count!=0U)) return -1;
        if (victim_data!=NULL) *victim_data=(struct ramfs_node_data){0};
        victim->type=VFS_TYPE_INVALID; victim->permissions=0U; victim->size=0ULL;
        victim->parent=NULL; victim->mount_point=NULL; victim->ops=(struct vfs_operations){0};
        victim->device_or_fs_data=NULL; victim->name[0]='\0';
        /* Keep inode as a tombstone so stale pointers cannot alias reused nodes. */
        for (uint32_t j=i;j+1U<pdata->child_count;j++) pdata->children[j]=pdata->children[j+1U];
        pdata->children[--pdata->child_count]=NULL;
        return 0;
    }
    return -1;
}
static const struct vfs_operations ramfs_ops = {
    .read = ramfs_read,
    .write = ramfs_write,
    .open = NULL,
    .close = NULL,
    .readdir = ramfs_readdir,
    .finddir = ramfs_finddir,
    .create = ramfs_create,
    .mkdir = ramfs_mkdir,
    .unlink = ramfs_unlink
};

struct vfs_node *vfs_ramfs_create_root(void) {
    struct vfs_node *root = allocate_node(VFS_TYPE_DIRECTORY, "/");
    if (root == NULL) return NULL;
    root->parent = root;
    root->ops = ramfs_ops;
    return root;
}

bool vfs_initialize(void) {
    if (initialized) return true;
    for (size_t i = 0; i < VFS_MAX_NODES; i++) {
        node_pool[i] = (struct vfs_node){0};
        ramfs_data_pool[i] = (struct ramfs_node_data){0};
    }
    for (size_t i = 0; i < VFS_MAX_MOUNTS; i++) {
        mount_table[i] = (struct vfs_mount){0};
    }
    next_inode = 1;
    root_node = vfs_ramfs_create_root();
    if (root_node == NULL) return false;

    /* Mount root filesystem */
    mount_table[0].active = true;
    str_copy(mount_table[0].path, "/", VFS_MAX_PATH);
    mount_table[0].root_node = root_node;
    mount_table[0].mount_node = NULL;
    mount_table[0].fs_name = "ramfs";

    /* Create standard directory hierarchy */
    struct vfs_node *dummy = NULL;
    ramfs_mkdir(root_node, "boot", 0, &dummy);
    ramfs_mkdir(root_node, "dev", 0, &dummy);
    ramfs_mkdir(root_node, "mnt", 0, &dummy);
    ramfs_mkdir(root_node, "tmp", 0, &dummy);
    ramfs_mkdir(root_node, "bin", 0, &dummy);

    initialized = true;
    return true;
}

struct vfs_node *vfs_get_root(void) {
    if (!initialized) vfs_initialize();
    return root_node;
}

bool vfs_mount(const char *mount_path,struct vfs_node *fs_root,const char *fs_name) {
    if (!initialized || mount_path==NULL || fs_root==NULL || fs_root->type!=VFS_TYPE_DIRECTORY) return false;
    const size_t len=str_len(mount_path);
    if (len<2U || len>=VFS_MAX_PATH || mount_path[0]!='/' || mount_path[len-1U]=='/') return false;
    struct vfs_node *target=NULL;
    if (!vfs_resolve_path(mount_path,&target) || target==NULL ||
        target==root_node || target->type!=VFS_TYPE_DIRECTORY ||
        target->mount_point!=NULL) return false;
    for (size_t i=1U;i<VFS_MAX_MOUNTS;i++)
        if (mount_table[i].active && str_equal(mount_table[i].path,mount_path)) return false;
    for (size_t i=1U;i<VFS_MAX_MOUNTS;i++) {
        if (mount_table[i].active) continue;
        mount_table[i].active=true;
        str_copy(mount_table[i].path,mount_path,VFS_MAX_PATH);
        mount_table[i].root_node=fs_root;
        mount_table[i].mount_node=target;
        mount_table[i].fs_name=fs_name!=NULL?fs_name:"unknown";
        target->mount_point=fs_root;
        return true;
    }
    return false;
}

bool vfs_unmount(const char *mount_path) {
    if (!initialized || mount_path==NULL || str_equal(mount_path,"/")) return false;
    const size_t len=str_len(mount_path);
    if (len==0U || len>=VFS_MAX_PATH) return false;
    for (size_t i=1U;i<VFS_MAX_MOUNTS;i++) {
        if (!mount_table[i].active || !str_equal(mount_table[i].path,mount_path)) continue;
        for (size_t child=1U;child<VFS_MAX_MOUNTS;child++) {
            if (child==i || !mount_table[child].active) continue;
            const size_t child_len=str_len(mount_table[child].path);
            if (child_len>len && mount_table[child].path[len]=='/') {
                bool prefix=true;
                for (size_t j=0U;j<len;j++) if (mount_table[child].path[j]!=mount_path[j]) prefix=false;
                if (prefix) return false;
            }
        }
        if (mount_table[i].mount_node!=NULL &&
            mount_table[i].mount_node->mount_point==mount_table[i].root_node)
            mount_table[i].mount_node->mount_point=NULL;
        mount_table[i]=(struct vfs_mount){0};
        return true;
    }
    return false;
}
bool vfs_resolve_path(const char *path,struct vfs_node **out_node) {
    if (!initialized || path==NULL || out_node==NULL || path[0]=='\0') return false;
    const size_t len=str_len(path);
    if (len>=VFS_MAX_PATH) return false;
    struct vfs_node *current=root_node;
    size_t i=(path[0]=='/')?1U:0U;
    char component[VFS_MAX_NAME];
    while (path[i]!='\0') {
        while (path[i]=='/') ++i;
        if (path[i]=='\0') break;
        size_t n=0U;
        while (path[i]!='\0' && path[i]!='/') {
            if (n+1U>=VFS_MAX_NAME) return false;
            component[n++]=path[i++];
        }
        component[n]='\0';
        if (current==NULL || current->type!=VFS_TYPE_DIRECTORY || current->ops.finddir==NULL) return false;
        current=current->ops.finddir(current,component);
        if (current==NULL) return false;
    }
    if (len>1U && path[len-1U]=='/' && current->type!=VFS_TYPE_DIRECTORY) return false;
    *out_node=current;
    return true;
}
int64_t vfs_read(struct vfs_node *node,uint64_t offset,size_t size,void *buffer) {
    if (node==NULL || node->type!=VFS_TYPE_FILE ||
        (node->permissions&VFS_FLAG_READ)==0U || node->ops.read==NULL ||
        (size!=0U && buffer==NULL)) return -1;
    if (size==0U) return 0;
    return node->ops.read(node,offset,size,buffer);
}

int64_t vfs_write(struct vfs_node *node,uint64_t offset,size_t size,const void *buffer) {
    if (node==NULL || node->type!=VFS_TYPE_FILE ||
        (node->permissions&VFS_FLAG_WRITE)==0U || node->ops.write==NULL ||
        (size!=0U && buffer==NULL)) return -1;
    if (size==0U) return 0;
    return node->ops.write(node,offset,size,buffer);
}
int vfs_readdir(struct vfs_node *node, uint32_t index, struct vfs_dirent *out_dirent) {
    if (node == NULL || node->ops.readdir == NULL) return -1;
    return node->ops.readdir(node, index, out_dirent);
}

struct vfs_node *vfs_finddir(struct vfs_node *node, const char *name) {
    if (node == NULL || node->ops.finddir == NULL) return NULL;
    return node->ops.finddir(node, name);
}

static bool vfs_split_parent(const char *path,char dir_path[VFS_MAX_PATH],const char **base_name) {
    if (path==NULL || dir_path==NULL || base_name==NULL) return false;
    const size_t len=str_len(path);
    if (len==0U || len>=VFS_MAX_PATH || path[len-1U]=='/') return false;
    size_t slash=len;
    while (slash>0U && path[slash-1U]!='/') --slash;
    *base_name=path+slash;
    const size_t base_len=len-slash;
    if (base_len==0U || base_len>=VFS_MAX_NAME ||
        str_equal(*base_name,".") || str_equal(*base_name,"..")) return false;
    if (slash<=1U) str_copy(dir_path,"/",VFS_MAX_PATH);
    else {
        const size_t dir_len=slash-1U;
        for (size_t i=0U;i<dir_len;i++) dir_path[i]=path[i];
        dir_path[dir_len]='\0';
    }
    return true;
}

int vfs_create(const char *path,uint32_t mode,struct vfs_node **out_node) {
    char dir_path[VFS_MAX_PATH]; const char *base=NULL; struct vfs_node *parent=NULL;
    if (!vfs_split_parent(path,dir_path,&base) || !vfs_resolve_path(dir_path,&parent) ||
        parent==NULL || parent->type!=VFS_TYPE_DIRECTORY || parent->ops.create==NULL) return -1;
    return parent->ops.create(parent,base,mode,out_node);
}

int vfs_mkdir(const char *path,uint32_t mode,struct vfs_node **out_node) {
    char dir_path[VFS_MAX_PATH]; const char *base=NULL; struct vfs_node *parent=NULL;
    if (!vfs_split_parent(path,dir_path,&base) || !vfs_resolve_path(dir_path,&parent) ||
        parent==NULL || parent->type!=VFS_TYPE_DIRECTORY || parent->ops.mkdir==NULL) return -1;
    return parent->ops.mkdir(parent,base,mode,out_node);
}

int vfs_unlink(const char *path) {
    char dir_path[VFS_MAX_PATH]; const char *base=NULL; struct vfs_node *parent=NULL;
    if (path!=NULL && str_equal(path,"/")) return -1;
    if (!vfs_split_parent(path,dir_path,&base) || !vfs_resolve_path(dir_path,&parent) ||
        parent==NULL || parent->type!=VFS_TYPE_DIRECTORY || parent->ops.unlink==NULL) return -1;
    return parent->ops.unlink(parent,base);
}
#ifdef DAWN_KERNEL_TEST
bool vfs_self_test(void) {
    if (!vfs_initialize()) return false;
    struct vfs_node *node=NULL;
    if (!vfs_resolve_path("/",&node) || node==NULL || node->type!=VFS_TYPE_DIRECTORY) return false;
    if (!vfs_resolve_path("/boot",&node) || node==NULL || node->type!=VFS_TYPE_DIRECTORY) return false;
    if (!vfs_resolve_path("/tmp",&node) || node==NULL || node->type!=VFS_TYPE_DIRECTORY) return false;

    struct vfs_node *test_file=NULL;
    if (vfs_create("/tmp/hello.txt",VFS_FLAG_READ|VFS_FLAG_WRITE,&test_file)!=0 || test_file==NULL) return false;
    static const char message[]="Hello DAWN-OS VFS!";
    if (vfs_write(test_file,0U,sizeof(message),message)!=(int64_t)sizeof(message)) return false;
    char read_back[32]={0};
    if (vfs_read(test_file,0U,sizeof(message),read_back)!=(int64_t)sizeof(message) || !str_equal(read_back,message)) return false;
    test_file->permissions=VFS_FLAG_READ;
    if (vfs_write(test_file,0U,sizeof(message),message)!=-1) return false;
    test_file->permissions=VFS_FLAG_READ|VFS_FLAG_WRITE;

    struct vfs_node *tmp_dir=NULL;
    if (!vfs_resolve_path("/tmp",&tmp_dir) || tmp_dir==NULL) return false;
    struct vfs_dirent entry={0};
    if (vfs_readdir(tmp_dir,0U,&entry)!=0 || !str_equal(entry.name,"hello.txt")) return false;
    struct vfs_node *rel_node=NULL;
    if (!vfs_resolve_path("/tmp/../tmp/hello.txt",&rel_node) || rel_node!=test_file) return false;
    if (vfs_unlink("/tmp/hello.txt")!=0 || vfs_resolve_path("/tmp/hello.txt",&rel_node)) return false;

    char long_component[VFS_MAX_NAME+2U];
    long_component[0]='/';
    for (size_t i=1U;i<VFS_MAX_NAME+1U;i++) long_component[i]='x';
    long_component[VFS_MAX_NAME+1U]='\0';
    if (vfs_resolve_path(long_component,&rel_node)) return false;
    char long_path[VFS_MAX_PATH+1U];
    for (size_t i=0U;i<VFS_MAX_PATH;i++) long_path[i]='/';
    long_path[VFS_MAX_PATH]='\0';
    if (vfs_resolve_path(long_path,&rel_node)) return false;

    struct vfs_node *nonempty=NULL,*nested=NULL;
    if (vfs_mkdir("/tmp/nonempty",0U,&nonempty)!=0 || nonempty==NULL ||
        vfs_create("/tmp/nonempty/item",0U,&nested)!=0 || nested==NULL) return false;
    if (vfs_unlink("/tmp/nonempty")==0) return false;
    if (vfs_unlink("/tmp/nonempty/item")!=0 || vfs_unlink("/tmp/nonempty")!=0) return false;

    struct vfs_node *mount_before=NULL,*mounted_root=NULL;
    if (!vfs_resolve_path("/mnt",&mount_before) || mount_before==NULL) return false;
    mounted_root=vfs_ramfs_create_root();
    if (mounted_root==NULL || !vfs_mount("/mnt",mounted_root,"vfs-self-test")) return false;
    if (!vfs_resolve_path("/mnt",&rel_node) || rel_node!=mounted_root) return false;
    if (vfs_mount("/mnt",mounted_root,"duplicate") || !vfs_unmount("/mnt")) return false;
    if (!vfs_resolve_path("/mnt",&rel_node) || rel_node!=mount_before) return false;
    return true;
}
#endif
