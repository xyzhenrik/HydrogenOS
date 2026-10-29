#include "ramfs.h"
#include "mm/kheap.h"
#include "errno.h"

typedef struct ramfs_entry {
    const char *name;
    const uint8_t *data;
    const uint8_t *end;
    uint8_t type; // 1=file, 2=dir
} ramfs_entry_t;

/* Dynamic entries created at runtime via ramfs_mkdir / ramfs_create */
#define RAMFS_DYN_NAME_MAX  256
#define RAMFS_DYN_MAX       64

typedef struct ramfs_dyn_entry {
    int used;
    char name[RAMFS_DYN_NAME_MAX];
    uint8_t *data;      /* NULL for directories */
    uint32_t size;
    uint8_t type;       /* 1=file, 2=dir */
} ramfs_dyn_entry_t;

static ramfs_dyn_entry_t dyn_entries[RAMFS_DYN_MAX];

static ramfs_dyn_entry_t *dyn_find(const char *name);

static int ready = 0;
static int use_cpio = 0;
static const uint8_t *cpio_start = 0;
static uint32_t cpio_size = 0;

#define CPIO_HDR_LEN 110
#define CPIO_MODE_MASK 0170000
#define CPIO_MODE_DIR 0040000
#define CPIO_MODE_FILE 0100000

#if defined(__x86_64__)
extern const uint8_t _binary_build_x86_64_user_hello_elf_start[];
extern const uint8_t _binary_build_x86_64_user_hello_elf_end[];
extern const uint8_t _binary_build_x86_64_user_echo_elf_start[];
extern const uint8_t _binary_build_x86_64_user_echo_elf_end[];
extern const uint8_t _binary_build_x86_64_user_cat_elf_start[];
extern const uint8_t _binary_build_x86_64_user_cat_elf_end[];
extern const uint8_t _binary_build_x86_64_user_time_elf_start[];
extern const uint8_t _binary_build_x86_64_user_time_elf_end[];
extern const uint8_t _binary_build_x86_64_user_sleep_elf_start[];
extern const uint8_t _binary_build_x86_64_user_sleep_elf_end[];

static const ramfs_entry_t builtin_entries[] = {
    { "user", _binary_build_x86_64_user_hello_elf_start,
      _binary_build_x86_64_user_hello_elf_end, 1 },
    { "echo", _binary_build_x86_64_user_echo_elf_start,
      _binary_build_x86_64_user_echo_elf_end, 1 },
    { "cat", _binary_build_x86_64_user_cat_elf_start,
      _binary_build_x86_64_user_cat_elf_end, 1 },
    { "time", _binary_build_x86_64_user_time_elf_start,
      _binary_build_x86_64_user_time_elf_end, 1 },
    { "sleep", _binary_build_x86_64_user_sleep_elf_start,
      _binary_build_x86_64_user_sleep_elf_end, 1 },
};
static const uint32_t builtin_count = sizeof(builtin_entries) / sizeof(builtin_entries[0]);
#else
static const ramfs_entry_t *builtin_entries = 0;
static const uint32_t builtin_count = 0;
#endif

void ramfs_set_cpio(const void *data, uint32_t size) {
    cpio_start = (const uint8_t *)data;
    cpio_size = size;
}

static int str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        ++a;
        ++b;
    }
    return (*a == 0 && *b == 0);
}

static int is_root_path(const char *path) {
    if (!path) return 1;
    if (path[0] == 0) return 1;
    if (path[0] == '/' && path[1] == 0) return 1;
    return 0;
}

static const char *strip_slash(const char *path) {
    if (!path) return path;
    while (*path == '/') path++;
    return path;
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static int parse_hex8(const char *p, uint32_t *out) {
    uint32_t val = 0;
    for (int i = 0; i < 8; ++i) {
        int v = hex_val(p[i]);
        if (v < 0) return -1;
        val = (val << 4) | (uint32_t)v;
    }
    if (out) *out = val;
    return 0;
}

static const uint8_t *align4(const uint8_t *p) {
    uintptr_t v = (uintptr_t)p;
    v = (v + 3u) & ~3u;
    return (const uint8_t *)v;
}

typedef struct cpio_entry {
    const char *name;
    const uint8_t *data;
    uint32_t size;
    uint32_t mode;
} cpio_entry_t;

static int cpio_next(const uint8_t *p, const uint8_t *end, cpio_entry_t *out, const uint8_t **next) {
    if (!p || !end || p + CPIO_HDR_LEN > end) return -1;
    if (!(p[0] == '0' && p[1] == '7' && p[2] == '0' && p[3] == '7' &&
          p[4] == '0' && (p[5] == '1' || p[5] == '2'))) {
        return -1;
    }
    const char *hdr = (const char *)p;
    uint32_t mode = 0;
    uint32_t size = 0;
    uint32_t namesize = 0;
    if (parse_hex8(hdr + 14, &mode) != 0) return -1;
    if (parse_hex8(hdr + 54, &size) != 0) return -1;
    if (parse_hex8(hdr + 94, &namesize) != 0) return -1;
    if (namesize == 0) return -1;
    const uint8_t *name_ptr = p + CPIO_HDR_LEN;
    const uint8_t *name_end = name_ptr + namesize;
    if (name_end > end) return -1;
    if (name_ptr[namesize - 1] != 0) return -1;
    const uint8_t *data = align4(name_end);
    const uint8_t *data_end = data + size;
    if (data_end > end) return -1;
    if (out) {
        out->name = (const char *)name_ptr;
        out->data = data;
        out->size = size;
        out->mode = mode;
    }
    if (next) *next = align4(data_end);
    return 0;
}

static int cpio_find_entry(const char *name, cpio_entry_t *out) {
    if (!cpio_start || cpio_size == 0 || !name || name[0] == 0) return -1;
    const uint8_t *p = cpio_start;
    const uint8_t *end = cpio_start + cpio_size;
    while (p < end) {
        cpio_entry_t e;
        const uint8_t *next = 0;
        if (cpio_next(p, end, &e, &next) != 0) return -1;
        if (str_eq(e.name, "TRAILER!!!")) return -1;
        if (str_eq(e.name, name)) {
            if (out) *out = e;
            return 0;
        }
        if (!next || next <= p) return -1;
        p = next;
    }
    return -1;
}

static int cpio_list_root(vfs_dir_cb cb, void *ctx) {
    if (!cpio_start || cpio_size == 0) return -1;
    const uint8_t *p = cpio_start;
    const uint8_t *end = cpio_start + cpio_size;
    while (p < end) {
        cpio_entry_t e;
        const uint8_t *next = 0;
        if (cpio_next(p, end, &e, &next) != 0) return -1;
        if (str_eq(e.name, "TRAILER!!!")) return 0;
        const char *name = e.name;
        if (name[0] != 0) {
            int has_slash = 0;
            for (const char *c = name; *c; ++c) {
                if (*c == '/') { has_slash = 1; break; }
            }
            if (!has_slash) {
                uint8_t type = (e.mode & CPIO_MODE_MASK) == CPIO_MODE_DIR ? 2 : 1;
                cb(name, type, ctx);
            }
        }
        if (!next || next <= p) return -1;
        p = next;
    }
    return 0;
}

static const ramfs_entry_t *find_entry(const char *path) {
    if (!path) return 0;
    const char *name = strip_slash(path);
    if (!name || name[0] == 0) return 0;
    for (uint32_t i = 0; i < builtin_count; ++i) {
        if (str_eq(builtin_entries[i].name, name)) return &builtin_entries[i];
    }
    return 0;
}

static uint32_t entry_size(const ramfs_entry_t *e) {
    if (!e || !e->data || !e->end) return 0;
    return (uint32_t)(e->end - e->data);
}

int ramfs_init(void) {
    use_cpio = 0;
    if (cpio_start && cpio_size > 0) {
        cpio_entry_t e;
        const uint8_t *next = 0;
        const uint8_t *end = cpio_start + cpio_size;
        if (cpio_next(cpio_start, end, &e, &next) == 0) {
            use_cpio = 1;
            ready = 1;
            return 0;
        }
    }
    ready = (builtin_count > 0) ? 1 : 0;
    return ready ? 0 : -1;
}

int ramfs_is_ready(void) { return ready; }

int ramfs_list_root(vfs_dir_cb cb, void *ctx) {
    if (!ready || !cb) return -1;
    if (use_cpio) {
        int rc = cpio_list_root(cb, ctx);
        /* Also emit dynamic entries */
        for (int i = 0; i < RAMFS_DYN_MAX; ++i) {
            if (dyn_entries[i].used) cb(dyn_entries[i].name, dyn_entries[i].type, ctx);
        }
        return rc;
    }
    for (uint32_t i = 0; i < builtin_count; ++i) {
        cb(builtin_entries[i].name, builtin_entries[i].type, ctx);
    }
    for (int i = 0; i < RAMFS_DYN_MAX; ++i) {
        if (dyn_entries[i].used) cb(dyn_entries[i].name, dyn_entries[i].type, ctx);
    }
    return 0;
}

int ramfs_list_dir(const char *path, vfs_dir_cb cb, void *ctx) {
    if (!ready) return -1;
    if (is_root_path(path)) return ramfs_list_root(cb, ctx);
    return -1;
}

int ramfs_read_file_at(const char *path, uint32_t offset, uint8_t *out,
                       uint32_t out_sz, uint32_t *out_len) {
    if (!ready || !out) return -1;
    const char *name = strip_slash(path);
    if (use_cpio) {
        cpio_entry_t e;
        if (cpio_find_entry(name, &e) == 0) {
            if ((e.mode & CPIO_MODE_MASK) == CPIO_MODE_DIR) return -1;
            const uint8_t *start = cpio_start;
            const uint8_t *end = cpio_start + cpio_size;
            if (e.data < start || (e.data + e.size) > end) return -1;
            if (offset >= e.size) {
                if (out_len) *out_len = 0;
                return 0;
            }
            uint32_t n = e.size - offset;
            if (n > out_sz) n = out_sz;
            for (uint32_t i = 0; i < n; ++i) out[i] = e.data[offset + i];
            if (out_len) *out_len = n;
            return 0;
        }
        /* Fall through to dynamic entries */
    } else {
        const ramfs_entry_t *e = find_entry(path);
        if (e && e->type == 1) {
            uint32_t size = entry_size(e);
            if (offset >= size) {
                if (out_len) *out_len = 0;
                return 0;
            }
            uint32_t n = size - offset;
            if (n > out_sz) n = out_sz;
            for (uint32_t i = 0; i < n; ++i) out[i] = e->data[offset + i];
            if (out_len) *out_len = n;
            return 0;
        }
    }
    /* Check dynamic entries */
    ramfs_dyn_entry_t *de = dyn_find(name);
    if (!de || de->type != 1) return -1;
    if (offset >= de->size) {
        if (out_len) *out_len = 0;
        return 0;
    }
    uint32_t n = de->size - offset;
    if (n > out_sz) n = out_sz;
    for (uint32_t i = 0; i < n; ++i) out[i] = de->data[offset + i];
    if (out_len) *out_len = n;
    return 0;
}

int ramfs_read_file(const char *path, uint8_t *out, uint32_t out_sz, uint32_t *out_len) {
    return ramfs_read_file_at(path, 0, out, out_sz, out_len);
}

int ramfs_stat(const char *path, vfs_stat_t *out) {
    if (!ready || !out) return -1;
    if (is_root_path(path)) {
        out->type = 2;
        out->size = 0;
        return 0;
    }
    if (use_cpio) {
        const char *name = strip_slash(path);
        cpio_entry_t e;
        if (cpio_find_entry(name, &e) != 0) {
            /* Fall through to dynamic entries */
            goto check_dyn;
        }
        out->type = ((e.mode & CPIO_MODE_MASK) == CPIO_MODE_DIR) ? 2 : 1;
        out->size = e.size;
        return 0;
    }
    {
        const ramfs_entry_t *e = find_entry(path);
        if (e) {
            out->type = e->type;
            out->size = entry_size(e);
            return 0;
        }
    }
check_dyn:
    {
        const char *name = strip_slash(path);
        for (int i = 0; i < RAMFS_DYN_MAX; ++i) {
            if (dyn_entries[i].used && str_eq(dyn_entries[i].name, name)) {
                out->type = dyn_entries[i].type;
                out->size = dyn_entries[i].size;
                return 0;
            }
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Helpers shared by write ops                                         */
/* ------------------------------------------------------------------ */

static int dyn_str_copy(char *dst, const char *src, uint32_t max) {
    if (!dst || !src || max == 0) return -1;
    uint32_t i = 0;
    for (; src[i] && i < max - 1; ++i) dst[i] = src[i];
    dst[i] = 0;
    return 0;
}

static ramfs_dyn_entry_t *dyn_find(const char *name) {
    if (!name) return 0;
    for (int i = 0; i < RAMFS_DYN_MAX; ++i) {
        if (dyn_entries[i].used && str_eq(dyn_entries[i].name, name))
            return &dyn_entries[i];
    }
    return 0;
}

static ramfs_dyn_entry_t *dyn_alloc_slot(void) {
    for (int i = 0; i < RAMFS_DYN_MAX; ++i) {
        if (!dyn_entries[i].used) return &dyn_entries[i];
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Write operations                                                    */
/* ------------------------------------------------------------------ */

int ramfs_mkdir(const char *path) {
    if (!ready || !path) return -1;
    const char *name = strip_slash(path);
    if (!name || name[0] == 0) return -1;
    /* Reject if name already exists in builtins, cpio, or dyn table */
    vfs_stat_t st;
    if (ramfs_stat(path, &st) == 0) return -17; /* -EEXIST */
    ramfs_dyn_entry_t *slot = dyn_alloc_slot();
    if (!slot) return -5; /* -EIO: no free slot */
    slot->used = 1;
    slot->type = 2; /* dir */
    slot->data = 0;
    slot->size = 0;
    if (dyn_str_copy(slot->name, name, RAMFS_DYN_NAME_MAX) != 0) {
        slot->used = 0;
        return -5;
    }
    return 0;
}

int ramfs_create(const char *path, const uint8_t *data, uint32_t size) {
    if (!ready || !path) return -1;
    const char *name = strip_slash(path);
    if (!name || name[0] == 0) return -1;
    /* If entry already exists in dyn table, overwrite it */
    ramfs_dyn_entry_t *existing = dyn_find(name);
    if (existing) {
        if (existing->data) kfree(existing->data);
        existing->data = 0;
        existing->size = 0;
    } else {
        /* Check builtins/cpio: refuse overwriting read-only entries */
        if (use_cpio) {
            cpio_entry_t e;
            if (cpio_find_entry(name, &e) == 0) return -17; /* -EEXIST */
        } else {
            if (find_entry(path)) return -17;
        }
        existing = dyn_alloc_slot();
        if (!existing) return -5;
        existing->used = 1;
        existing->type = 1; /* file */
        if (dyn_str_copy(existing->name, name, RAMFS_DYN_NAME_MAX) != 0) {
            existing->used = 0;
            return -5;
        }
    }
    if (size > 0 && data) {
        uint8_t *buf = (uint8_t *)kalloc(size);
        if (!buf) { existing->used = 0; return -5; }
        for (uint32_t i = 0; i < size; ++i) buf[i] = data[i];
        existing->data = buf;
    }
    existing->size = size;
    existing->type = 1;
    return 0;
}

int ramfs_write_file(const char *path, const uint8_t *data, uint32_t size) {
    return ramfs_create(path, data, size);
}

/* Only runtime-created files are writable; initrd and built-in entries are not. */
static int dyn_file_lookup(const char *path, ramfs_dyn_entry_t **out) {
    if (!ready || !path) return -EINVAL;
    const char *name = strip_slash(path);
    if (!name || name[0] == 0) return -EISDIR;
    ramfs_dyn_entry_t *e = dyn_find(name);
    if (!e) {
        vfs_stat_t st;
        return ramfs_stat(path, &st) == 0 ? -EROFS : -ENOENT;
    }
    if (e->type != 1) return -EISDIR;
    *out = e;
    return 0;
}

/* Bytes beyond the old size read as zero. */
static int dyn_resize(ramfs_dyn_entry_t *e, uint32_t new_size) {
    if (new_size == e->size) return 0;
    uint8_t *buf = 0;
    if (new_size > 0) {
        buf = (uint8_t *)kalloc(new_size);
        if (!buf) return -ENOMEM;
        uint32_t keep = e->size < new_size ? e->size : new_size;
        for (uint32_t i = 0; i < keep; ++i) buf[i] = e->data[i];
        for (uint32_t i = keep; i < new_size; ++i) buf[i] = 0;
    }
    if (e->data) kfree(e->data);
    e->data = buf;
    e->size = new_size;
    return 0;
}

int ramfs_write_at(const char *path, uint32_t offset, const uint8_t *data, uint32_t size) {
    if (size > 0 && !data) return -EINVAL;
    ramfs_dyn_entry_t *e = 0;
    int rc = dyn_file_lookup(path, &e);
    if (rc != 0) return rc;
    if (size == 0) return 0;
    if (size > UINT32_MAX - offset) return -EFBIG;
    uint32_t end = offset + size;
    if (end > e->size) {
        rc = dyn_resize(e, end);
        if (rc != 0) return rc;
    }
    for (uint32_t i = 0; i < size; ++i) e->data[offset + i] = data[i];
    return 0;
}

int ramfs_truncate(const char *path, uint32_t size) {
    ramfs_dyn_entry_t *e = 0;
    int rc = dyn_file_lookup(path, &e);
    if (rc != 0) return rc;
    return dyn_resize(e, size);
}

int ramfs_unlink(const char *path) {
    if (!ready || !path) return -1;
    const char *name = strip_slash(path);
    if (!name || name[0] == 0) return -1;
    ramfs_dyn_entry_t *e = dyn_find(name);
    if (!e) return -2; /* -ENOENT */
    if (e->type == 2) return -EISDIR;
    if (e->data) kfree(e->data);
    e->data = 0;
    e->size = 0;
    e->used = 0;
    e->name[0] = 0;
    return 0;
}

static int dyn_has_children(const char *dir) {
    uint32_t n = 0;
    while (dir[n]) n++;
    for (int i = 0; i < RAMFS_DYN_MAX; ++i) {
        if (!dyn_entries[i].used) continue;
        const char *name = dyn_entries[i].name;
        uint32_t k = 0;
        while (k < n && name[k] == dir[k]) k++;
        if (k == n && name[n] == '/') return 1;
    }
    return 0;
}

int ramfs_rmdir(const char *path) {
    if (!ready || !path) return -EINVAL;
    const char *name = strip_slash(path);
    if (!name || name[0] == 0) return -EBUSY;
    ramfs_dyn_entry_t *e = dyn_find(name);
    if (!e) {
        vfs_stat_t st;
        return ramfs_stat(path, &st) == 0 ? -EROFS : -ENOENT;
    }
    if (e->type != 2) return -ENOTDIR;
    if (dyn_has_children(name)) return -ENOTEMPTY;
    e->used = 0;
    e->name[0] = 0;
    return 0;
}

int ramfs_rename(const char *old_path, const char *new_path) {
    if (!ready || !old_path || !new_path) return -1;
    const char *old_name = strip_slash(old_path);
    const char *new_name = strip_slash(new_path);
    if (!old_name || old_name[0] == 0 || !new_name || new_name[0] == 0) return -1;
    ramfs_dyn_entry_t *e = dyn_find(old_name);
    if (!e) return -2; /* -ENOENT */
    /* If new name already exists in dyn table, remove it first */
    ramfs_dyn_entry_t *existing = dyn_find(new_name);
    if (existing && existing != e) {
        if (existing->data) kfree(existing->data);
        existing->used = 0;
    }
    if (dyn_str_copy(e->name, new_name, RAMFS_DYN_NAME_MAX) != 0) return -5;
    return 0;
}
