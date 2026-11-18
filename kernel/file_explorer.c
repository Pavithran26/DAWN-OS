/* SPDX-License-Identifier: Unlicense */
#include "file_explorer.h"

static inline void exp_str_copy(char *dest, const char *src, size_t max_len) {
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < max_len) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static inline bool exp_str_equal(const char *s1, const char *s2) {
    size_t i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] != s2[i]) return false;
        i++;
    }
    return s1[i] == s2[i];
}

static inline bool exp_str_contains(const char *haystack, const char *needle) {
    if (haystack == NULL || needle == NULL) return false;
    if (needle[0] == '\0') return true;
    for (size_t i = 0; haystack[i] != '\0'; i++) {
        size_t j = 0;
        while (haystack[i + j] != '\0' && needle[j] != '\0' && haystack[i + j] == needle[j]) {
            j++;
        }
        if (needle[j] == '\0') return true;
    }
    return false;
}

static inline size_t exp_str_len(const char *s) {
    size_t n=0U;
    if (s==NULL) return 0U;
    while (s[n]!='\0') ++n;
    return n;
}

static bool exp_join_path(const char *base,const char *name,char out[VFS_MAX_PATH]) {
    if (base==NULL || name==NULL || out==NULL || name[0]=='\0') return false;
    const size_t base_len=exp_str_len(base),name_len=exp_str_len(name);
    const bool separator=base_len>0U && base[base_len-1U]!='/';
    const size_t needed=base_len+(separator?1U:0U)+name_len;
    if (base_len==0U || needed>=VFS_MAX_PATH) return false;
    size_t at=0U;
    for (size_t i=0U;i<base_len;i++) out[at++]=base[i];
    if (separator) out[at++]='/';
    for (size_t i=0U;i<name_len;i++) out[at++]=name[i];
    out[at]='\0';
    return true;
}
bool explorer_list_directory(const char *path,struct explorer_listing *out_listing) {
    if (path==NULL || out_listing==NULL) return false;
    struct vfs_node *dir=NULL;
    if (!vfs_resolve_path(path,&dir) || dir==NULL || dir->type!=VFS_TYPE_DIRECTORY) return false;
    *out_listing=(struct explorer_listing){0};
    if (exp_str_len(path)>=VFS_MAX_PATH) return false;
    exp_str_copy(out_listing->current_path,path,VFS_MAX_PATH);
    for (uint32_t i=0U;i<EXPLORER_MAX_ITEMS;i++) {
        struct vfs_dirent entry={0};
        if (vfs_readdir(dir,i,&entry)!=0) break;
        if (out_listing->count>=EXPLORER_MAX_ITEMS) break;
        struct explorer_file_item *item=&out_listing->items[out_listing->count];
        if (!exp_join_path(path,entry.name,item->path)) {
            *out_listing=(struct explorer_listing){0};
            return false;
        }
        exp_str_copy(item->name,entry.name,VFS_MAX_NAME);
        item->size=entry.size;
        item->type=(entry.type==VFS_TYPE_DIRECTORY)?EXPLORER_ITEM_DIRECTORY:EXPLORER_ITEM_FILE;
        if (UINT64_MAX-out_listing->total_size<entry.size) {
            *out_listing=(struct explorer_listing){0};
            return false;
        }
        out_listing->total_size+=entry.size;
        ++out_listing->count;
    }
    return true;
}
static void search_recursive(struct vfs_node *node,const char *base_path,
                             const char *query,struct explorer_listing *results,int depth) {
    if (node==NULL || depth>8 || results==NULL || results->count>=EXPLORER_MAX_ITEMS) return;
    for (uint32_t i=0U;i<EXPLORER_MAX_ITEMS;i++) {
        struct vfs_dirent entry={0};
        if (vfs_readdir(node,i,&entry)!=0) break;
        char full_path[VFS_MAX_PATH];
        if (!exp_join_path(base_path,entry.name,full_path)) continue;
        if (exp_str_contains(entry.name,query) && results->count<EXPLORER_MAX_ITEMS) {
            struct explorer_file_item *item=&results->items[results->count++];
            exp_str_copy(item->name,entry.name,VFS_MAX_NAME);
            exp_str_copy(item->path,full_path,VFS_MAX_PATH);
            item->size=entry.size;
            item->type=(entry.type==VFS_TYPE_DIRECTORY)?EXPLORER_ITEM_DIRECTORY:EXPLORER_ITEM_FILE;
        }
        if (entry.type==VFS_TYPE_DIRECTORY) {
            struct vfs_node *child=vfs_finddir(node,entry.name);
            if (child!=NULL) search_recursive(child,full_path,query,results,depth+1);
        }
        if (results->count>=EXPLORER_MAX_ITEMS) break;
    }
}
bool explorer_search(const char *root_path, const char *query, struct explorer_listing *out_results) {
    if (root_path == NULL || query == NULL || out_results == NULL) return false;
    struct vfs_node *root = NULL;
    if (!vfs_resolve_path(root_path, &root) || root == NULL) return false;

    *out_results = (struct explorer_listing){0};
    exp_str_copy(out_results->current_path, root_path, VFS_MAX_PATH);
    search_recursive(root, root_path, query, out_results, 0);
    return true;
}

bool explorer_copy_file(const char *src_path,const char *dst_path) {
    if (src_path==NULL || dst_path==NULL || exp_str_len(src_path)>=VFS_MAX_PATH ||
        exp_str_len(dst_path)>=VFS_MAX_PATH) return false;
    struct vfs_node *source=NULL;
    if (!vfs_resolve_path(src_path,&source) || source==NULL || source->type!=VFS_TYPE_FILE) return false;
    struct vfs_node *destination=NULL;
    if (vfs_create(dst_path,VFS_FLAG_READ|VFS_FLAG_WRITE,&destination)!=0 || destination==NULL) return false;
    uint8_t buffer[512];
    uint64_t offset=0ULL;
    while (offset<source->size) {
        size_t chunk=sizeof(buffer);
        if ((uint64_t)chunk>source->size-offset) chunk=(size_t)(source->size-offset);
        const int64_t got=vfs_read(source,offset,chunk,buffer);
        if (got<=0 || (uint64_t)got>(uint64_t)chunk) { (void)vfs_unlink(dst_path); return false; }
        const int64_t put=vfs_write(destination,offset,(size_t)got,buffer);
        if (put!=got) { (void)vfs_unlink(dst_path); return false; }
        offset+=(uint64_t)got;
    }
    if (offset!=source->size) { (void)vfs_unlink(dst_path); return false; }
    return true;
}
bool explorer_move_file(const char *src_path,const char *dst_path) {
    if (!explorer_copy_file(src_path,dst_path)) return false;
    if (explorer_delete_file(src_path)) return true;
    (void)explorer_delete_file(dst_path);
    return false;
}

bool explorer_delete_file(const char *path) {
    struct vfs_node *node=NULL;
    if (path==NULL || !vfs_resolve_path(path,&node) || node==NULL || node->type!=VFS_TYPE_FILE) return false;
    return vfs_unlink(path)==0;
}

bool explorer_create_directory(const char *path) {
    if (path == NULL) return false;
    struct vfs_node *node = NULL;
    return vfs_mkdir(path, 0755, &node) == 0;
}

bool explorer_create_file(const char *path) {
    if (path == NULL) return false;
    struct vfs_node *node = NULL;
    return vfs_create(path, VFS_FLAG_READ | VFS_FLAG_WRITE, &node) == 0;
}
#ifdef DAWN_KERNEL_TEST
bool explorer_self_test(void) {
    struct explorer_listing listing = {0};
    if (!explorer_list_directory("/", &listing)) return false;
    if (listing.count < 3) return false; /* boot, dev, tmp etc */

    /* Create test file */
    struct vfs_node *created = NULL;
    if (vfs_create("/tmp/explorer_test.txt", VFS_FLAG_READ | VFS_FLAG_WRITE, &created) != 0)
        return false;
    static const char sample_data[] = "Emerald Green Explorer Test Content";
    vfs_write(created, 0, sizeof(sample_data), sample_data);

    /* Search for explorer_test */
    struct explorer_listing search_res = {0};
    if (!explorer_search("/", "explorer_test", &search_res) || search_res.count == 0)
        return false;

    /* Copy file */
    if (!explorer_copy_file("/tmp/explorer_test.txt", "/tmp/copied.txt"))
        return false;

    struct vfs_node *copied = NULL;
    if (!vfs_resolve_path("/tmp/copied.txt", &copied) || copied == NULL || copied->size != sizeof(sample_data))
        return false;

    /* Clean up */
    explorer_delete_file("/tmp/explorer_test.txt");
    explorer_delete_file("/tmp/copied.txt");

    /* Directory and file creation via explorer backend */
    if (!explorer_create_directory("/tmp/exp_subdir")) return false;
    if (!explorer_create_file("/tmp/exp_subdir/test.dat")) return false;
    explorer_delete_file("/tmp/exp_subdir/test.dat");
    vfs_unlink("/tmp/exp_subdir");

    return true;
}
#endif
