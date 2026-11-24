/* SPDX-License-Identifier: Unlicense */
#include "file_explorer.h"
#include "gfx.h"
#include "window_manager.h"

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

/* ========================================================================= */
/* Files GUI Application (Milestone D5)                                      */
/* ========================================================================= */

static struct window *explorer_win = NULL;
static struct explorer_listing current_listing;
static char current_directory[VFS_MAX_PATH] = "/";
static int32_t selected_item_idx = -1;
static uint32_t explorer_render_buf[EXPLORER_WIDTH * EXPLORER_HEIGHT];
static bool explorer_gui_initialized = false;

static bool validate_explorer_path(const char *path) {
    if (path == NULL) return false;
    size_t len = exp_str_len(path);
    if (len == 0 || len >= VFS_MAX_PATH) return false;
    if (path[0] != '/') return false;
    for (size_t i = 0; i < len; i++) {
        if ((unsigned char)path[i] < 32 || (unsigned char)path[i] > 126) return false;
    }
    return true;
}

static void format_file_size(uint64_t bytes, char *out, size_t max) {
    if (out == NULL || max == 0) return;
    if (bytes < 1024ULL) {
        size_t pos = 0;
        char num[32];
        size_t nlen = 0;
        uint64_t v = bytes;
        if (v == 0) { num[nlen++] = '0'; }
        else {
            char tmp[32]; size_t t = 0;
            while (v > 0) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
            while (t > 0) { num[nlen++] = tmp[--t]; }
        }
        for (size_t i = 0; i < nlen && pos + 3 < max; i++) out[pos++] = num[i];
        out[pos++] = ' '; out[pos++] = 'B'; out[pos] = '\0';
    } else if (bytes < 1024ULL * 1024ULL) {
        uint64_t kb = bytes / 1024ULL;
        uint64_t frac = (bytes % 1024ULL) * 10ULL / 1024ULL;
        size_t pos = 0;
        char num[32]; size_t nlen = 0; uint64_t v = kb;
        if (v == 0) num[nlen++] = '0';
        else {
            char tmp[32]; size_t t = 0;
            while (v > 0) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
            while (t > 0) { num[nlen++] = tmp[--t]; }
        }
        for (size_t i = 0; i < nlen && pos + 6 < max; i++) out[pos++] = num[i];
        out[pos++] = '.';
        out[pos++] = (char)('0' + (frac % 10));
        out[pos++] = ' '; out[pos++] = 'K'; out[pos++] = 'B'; out[pos] = '\0';
    } else {
        uint64_t mb = bytes / (1024ULL * 1024ULL);
        uint64_t frac = ((bytes % (1024ULL * 1024ULL)) * 10ULL) / (1024ULL * 1024ULL);
        size_t pos = 0;
        char num[32]; size_t nlen = 0; uint64_t v = mb;
        if (v == 0) num[nlen++] = '0';
        else {
            char tmp[32]; size_t t = 0;
            while (v > 0) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
            while (t > 0) { num[nlen++] = tmp[--t]; }
        }
        for (size_t i = 0; i < nlen && pos + 6 < max; i++) out[pos++] = num[i];
        out[pos++] = '.';
        out[pos++] = (char)('0' + (frac % 10));
        out[pos++] = ' '; out[pos++] = 'M'; out[pos++] = 'B'; out[pos] = '\0';
    }
}

static void u64_to_str_exp(uint64_t val, char *out, size_t max) {
    if (out == NULL || max == 0) return;
    if (val == 0) {
        if (max > 1) { out[0] = '0'; out[1] = '\0'; }
        return;
    }
    char tmp[32]; size_t t = 0;
    while (val > 0 && t < sizeof(tmp)) {
        tmp[t++] = (char)('0' + (val % 10));
        val /= 10;
    }
    size_t pos = 0;
    while (t > 0 && pos + 1 < max) {
        out[pos++] = tmp[--t];
    }
    out[pos] = '\0';
}

const char *file_explorer_get_current_path(void) {
    return current_directory;
}

int32_t file_explorer_get_selected_index(void) {
    return selected_item_idx;
}

bool file_explorer_navigate(const char *path) {
    if (!validate_explorer_path(path)) return false;
    struct explorer_listing temp = {0};
    if (!explorer_list_directory(path, &temp)) return false;
    current_listing = temp;
    exp_str_copy(current_directory, path, VFS_MAX_PATH);
    selected_item_idx = -1;
    file_explorer_render();
    if (explorer_win != NULL) {
        wm_mark_window_damaged(explorer_win);
    }
    return true;
}

bool file_explorer_navigate_up(void) {
    if (exp_str_equal(current_directory, "/")) {
        return file_explorer_navigate("/");
    }
    size_t len = exp_str_len(current_directory);
    if (len == 0) return file_explorer_navigate("/");
    size_t last_slash = 0;
    for (size_t i = 0; i < len; i++) {
        if (current_directory[i] == '/') last_slash = i;
    }
    char parent[VFS_MAX_PATH];
    if (last_slash == 0) {
        parent[0] = '/';
        parent[1] = '\0';
    } else {
        for (size_t i = 0; i < last_slash; i++) {
            parent[i] = current_directory[i];
        }
        parent[last_slash] = '\0';
    }
    return file_explorer_navigate(parent);
}

void file_explorer_render(void) {
    struct gfx_surface surf = {
        .buffer = explorer_render_buf,
        .width = EXPLORER_WIDTH,
        .height = EXPLORER_HEIGHT,
        .pitch = EXPLORER_WIDTH * sizeof(uint32_t)
    };

    /* Background: Deep emerald base */
    gfx_clear(&surf, GFX_COLOR_EMERALD_BASE);

    /* Header toolbar: y: 6..32, h: 26 */
    gfx_draw_glass_panel(&surf, 6, 6, EXPLORER_WIDTH - 12, 26, GFX_COLOR_GLASS_PANEL);
    gfx_draw_rect_outline(&surf, 6, 6, EXPLORER_WIDTH - 12, 26, GFX_COLOR_GLASS_BORDER);

    /* Back Button [..] */
    bool is_root = exp_str_equal(current_directory, "/");
    gfx_draw_glass_panel(&surf, 10, 8, 36, 22, GFX_COLOR_EMERALD_SURFACE);
    gfx_draw_rect_outline(&surf, 10, 8, 36, 22, is_root ? GFX_COLOR_DISABLED : GFX_COLOR_EMERALD_ACCENT);
    gfx_draw_string(&surf, 14, 11, "[..]", is_root ? GFX_COLOR_DISABLED : GFX_COLOR_EMERALD_HIGHLIGHT);

    /* Refresh Button [R] */
    gfx_draw_glass_panel(&surf, 50, 8, 32, 22, GFX_COLOR_EMERALD_SURFACE);
    gfx_draw_rect_outline(&surf, 50, 8, 32, 22, GFX_COLOR_EMERALD_ACCENT);
    gfx_draw_string(&surf, 56, 11, "[R]", GFX_COLOR_EMERALD_HIGHLIGHT);

    /* Breadcrumb Path Box */
    gfx_draw_glass_panel(&surf, 88, 8, EXPLORER_WIDTH - 96, 22, GFX_COLOR_EMERALD_DEEP);
    gfx_draw_rect_outline(&surf, 88, 8, EXPLORER_WIDTH - 96, 22, GFX_COLOR_GLASS_BORDER);
    char pstr[VFS_MAX_PATH + 16];
    exp_str_copy(pstr, "Path: ", sizeof(pstr));
    size_t plen = exp_str_len(pstr);
    for (size_t c = 0; current_directory[c] != '\0' && plen + 1 < sizeof(pstr); c++) {
        pstr[plen++] = current_directory[c];
    }
    pstr[plen] = '\0';
    gfx_draw_string(&surf, 94, 11, pstr, GFX_COLOR_TEXT_PRIMARY);

    /* Table Column Header Row: y: 36..54, h: 18 */
    gfx_draw_rect_filled(&surf, 6, 36, EXPLORER_WIDTH - 12, 18, GFX_COLOR_EMERALD_PRIMARY);
    gfx_draw_string(&surf, 12, 38, "TYPE", GFX_COLOR_TEXT_SECONDARY);
    gfx_draw_string(&surf, 76, 38, "NAME", GFX_COLOR_TEXT_SECONDARY);
    gfx_draw_string(&surf, 360, 38, "SIZE", GFX_COLOR_TEXT_SECONDARY);
    gfx_draw_string(&surf, 470, 38, "STATUS", GFX_COLOR_TEXT_SECONDARY);

    /* Directory items list */
    if (current_listing.count == 0) {
        gfx_draw_string(&surf, 16, 62, "(Empty directory)", GFX_COLOR_TEXT_MUTED);
    } else {
        for (uint32_t i = 0; i < current_listing.count && i < 12U; i++) {
            uint32_t ry = 58U + i * 22U;
            const struct explorer_file_item *item = &current_listing.items[i];

            if ((int32_t)i == selected_item_idx) {
                gfx_draw_rect_filled(&surf, 6, ry - 2, EXPLORER_WIDTH - 12, 22, GFX_COLOR_EMERALD_PRIMARY);
                gfx_draw_rect_outline(&surf, 6, ry - 2, EXPLORER_WIDTH - 12, 22, GFX_COLOR_EMERALD_ACCENT);
            } else if (i % 2 == 1) {
                gfx_draw_rect_filled(&surf, 6, ry - 2, EXPLORER_WIDTH - 12, 22, 0x300E3D2AU);
            }

            if (item->type == EXPLORER_ITEM_DIRECTORY) {
                gfx_draw_string(&surf, 12, ry, "[DIR]", GFX_COLOR_EMERALD_ACCENT);
            } else {
                gfx_draw_string(&surf, 12, ry, "[FILE]", GFX_COLOR_TEXT_SECONDARY);
            }

            gfx_draw_string(&surf, 76, ry, item->name, GFX_COLOR_TEXT_PRIMARY);

            if (item->type == EXPLORER_ITEM_DIRECTORY) {
                gfx_draw_string(&surf, 360, ry, "<DIR>", GFX_COLOR_TEXT_MUTED);
            } else {
                char sbuf[32];
                format_file_size(item->size, sbuf, sizeof(sbuf));
                gfx_draw_string(&surf, 360, ry, sbuf, GFX_COLOR_TEXT_PRIMARY);
            }

            gfx_draw_string(&surf, 470, ry, "Ready", GFX_COLOR_SUCCESS);
        }
    }

    /* Footer status bar: y: 328..354, h: 26 */
    gfx_draw_glass_panel(&surf, 6, 328, EXPLORER_WIDTH - 12, 26, GFX_COLOR_GLASS_PANEL);
    gfx_draw_rect_outline(&surf, 6, 328, EXPLORER_WIDTH - 12, 26, GFX_COLOR_GLASS_BORDER);

    char foot[128];
    char cstr[16];
    u64_to_str_exp((uint64_t)current_listing.count, cstr, sizeof(cstr));
    exp_str_copy(foot, cstr, sizeof(foot));
    size_t flen = exp_str_len(foot);
    const char *mid = " items | Total: ";
    for (size_t m = 0; mid[m] != '\0' && flen + 1 < sizeof(foot); m++) foot[flen++] = mid[m];
    char tsize[32];
    format_file_size(current_listing.total_size, tsize, sizeof(tsize));
    for (size_t t = 0; tsize[t] != '\0' && flen + 1 < sizeof(foot); t++) foot[flen++] = tsize[t];

    if (selected_item_idx >= 0 && (uint32_t)selected_item_idx < current_listing.count) {
        const char *sel_pre = " | Selected: ";
        for (size_t s = 0; sel_pre[s] != '\0' && flen + 1 < sizeof(foot); s++) foot[flen++] = sel_pre[s];
        const char *sname = current_listing.items[selected_item_idx].name;
        for (size_t n = 0; sname[n] != '\0' && flen + 1 < sizeof(foot); n++) foot[flen++] = sname[n];
    }
    foot[flen] = '\0';
    gfx_draw_string(&surf, 12, 334, foot, GFX_COLOR_TEXT_SECONDARY);
}

void file_explorer_handle_click(int32_t local_x, int32_t local_y) {
    if (local_x < 0 || local_x >= (int32_t)EXPLORER_WIDTH || local_y < 0 || local_y >= (int32_t)EXPLORER_HEIGHT) return;

    /* Toolbar buttons (y in [8, 30]) */
    if (local_y >= 8 && local_y <= 30) {
        if (local_x >= 10 && local_x <= 46) {
            (void)file_explorer_navigate_up();
            return;
        }
        if (local_x >= 50 && local_x <= 82) {
            (void)file_explorer_navigate(current_directory);
            return;
        }
    }

    /* File rows (y in [56, 324]) */
    if (local_y >= 56 && local_y <= 324) {
        uint32_t row = (uint32_t)(local_y - 56) / 22U;
        if (row < current_listing.count) {
            if ((int32_t)row == selected_item_idx && current_listing.items[row].type == EXPLORER_ITEM_DIRECTORY) {
                char next_path[VFS_MAX_PATH];
                if (exp_join_path(current_directory, current_listing.items[row].name, next_path)) {
                    (void)file_explorer_navigate(next_path);
                    return;
                }
            } else {
                selected_item_idx = (int32_t)row;
                file_explorer_render();
                if (explorer_win != NULL) wm_mark_window_damaged(explorer_win);
            }
        }
    }
}

static void file_explorer_on_window_close(struct window *win) {
    (void)win;
    explorer_win = NULL;
}

struct window *file_explorer_window_open(void) {
    if (explorer_win != NULL) {
        if (explorer_win->minimized) wm_minimize_window(explorer_win->id);
        wm_focus_window(explorer_win->id);
        return explorer_win;
    }

    explorer_win = wm_create_window("Files", 120, 80, EXPLORER_WIDTH, EXPLORER_HEIGHT,
                                    WM_FLAG_MOVABLE | WM_FLAG_TITLEBAR | WM_FLAG_RESIZABLE);
    if (explorer_win != NULL) {
        explorer_win->client_buffer = explorer_render_buf;
        explorer_win->client_pitch = EXPLORER_WIDTH * sizeof(uint32_t);
        explorer_win->client_width = EXPLORER_WIDTH;
        explorer_win->client_height = EXPLORER_HEIGHT;
        explorer_win->on_click = file_explorer_handle_click;
        explorer_win->on_close = file_explorer_on_window_close;
        if (!explorer_gui_initialized) {
            (void)file_explorer_navigate("/");
            explorer_gui_initialized = true;
        } else {
            file_explorer_render();
        }
        wm_mark_window_damaged(explorer_win);
    }
    return explorer_win;
}

#ifdef DAWN_KERNEL_TEST
bool explorer_self_test(void) {
    struct explorer_listing listing = {0};
    if (!explorer_list_directory("/", &listing)) return false;
    if (listing.count < 3) return false; /* boot, dev, tmp etc */

    /* Path safety gate verification */
    if (explorer_list_directory(NULL, &listing)) return false;
    if (file_explorer_navigate(NULL)) return false;
    if (file_explorer_navigate("relative/path")) return false;
    if (file_explorer_navigate("")) return false;
    if (file_explorer_navigate("/test\x01malformed")) return false;

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

    /* Files GUI window and navigation integration test */
    struct window *ewin = file_explorer_window_open();
    if (ewin == NULL) return false;
    if (ewin->client_buffer != explorer_render_buf) return false;
    if (ewin->client_width != EXPLORER_WIDTH || ewin->client_height != EXPLORER_HEIGHT) return false;
    if (!exp_str_equal(file_explorer_get_current_path(), "/")) return false;

    /* Navigate to /tmp and back up to / */
    if (!file_explorer_navigate("/tmp")) return false;
    if (!exp_str_equal(file_explorer_get_current_path(), "/tmp")) return false;

    if (!file_explorer_navigate_up()) return false;
    if (!exp_str_equal(file_explorer_get_current_path(), "/")) return false;

    /* Simulate clicking item row 0 and toolbar navigation */
    file_explorer_handle_click(100, 68);
    if (file_explorer_get_selected_index() != 0) return false;

    file_explorer_handle_click(20, 16); /* Click [..] */

    /* Close Files window */
    wm_destroy_window(ewin->id);
    if (explorer_win != NULL) return false;

    return true;
}
#endif
