#include "saves.h"
#include "sys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ppu-types.h>
#include <lv2/sysfs.h>

SaveJob g_savejob;

static int is_temp_name(const char *n)
{
    size_t l = strlen(n);
    return (l > 8 && !strcmp(n + l - 8, ".partial")) || (l > 4 && !strcmp(n + l - 4, ".old"));
}

static int count_cb(const char *name, int isdir, void *user)
{
    if (isdir && name[0] != '.' && !is_temp_name(name)) (*(int *)user)++;
    return 0;
}

static int count_dirs(const char *path)
{
    int n = 0;
    if (list_dir(path, count_cb, &n) != 0) return -1;
    return n;
}

typedef struct { char names[16][16]; int n; } Users;
static int user_cb(const char *name, int isdir, void *u)
{
    Users *us = u;
    if (isdir && strlen(name) == 8 && us->n < 16) snprintf(us->names[us->n++], 16, "%s", name);
    return 0;
}

static int cmpstr(const void *a, const void *b) { return strcmp(a, b); }

void saves_info(SaveInfo *si, int user_index)
{
    memset(si, 0, sizeof(*si));
    Users us = { .n = 0 };
    list_dir("/dev_hdd0/home", user_cb, &us);
    qsort(us.names, us.n, sizeof(us.names[0]), cmpstr);
    si->n_users = us.n;
    if (us.n > 0) {
        snprintf(si->user, sizeof(si->user), "%s", us.names[((user_index % us.n) + us.n) % us.n]);
        char p[96];
        snprintf(p, sizeof(p), "/dev_hdd0/home/%s/savedata", si->user);
        si->hdd_count = count_dirs(p);
        if (si->hdd_count < 0) si->hdd_count = 0;
    }
    si->usb_present = is_dir("/dev_usb000");
    if (si->usb_present) {
        si->usb_count = count_dirs(USB_SAVE_DIR);
        if (si->usb_count < 0) si->usb_count = 0;
    }
}

typedef struct { char *src, *rel; int unit; } Pair;
typedef struct { char name[72]; int first, count, ok; } Unit;
static Pair *pairs;
static Unit *units;
static int n_pairs, cap_pairs, n_units, cap_units, alloc_fail;
static char g_src_root[96], g_dst_root[96];
static int cur_unit, cur_started;

static void add_pair(const char *s, const char *rel, int unit)
{
    if (alloc_fail) return;
    if (n_pairs >= cap_pairs) {
        int nc = cap_pairs ? cap_pairs * 2 : 256;
        Pair *np = realloc(pairs, sizeof(Pair) * nc);
        if (!np) { alloc_fail = 1; return; }
        pairs = np;
        cap_pairs = nc;
    }
    char *a = strdup(s), *b = strdup(rel);
    if (!a || !b) { free(a); free(b); alloc_fail = 1; return; }
    pairs[n_pairs].src = a;
    pairs[n_pairs].rel = b;
    pairs[n_pairs].unit = unit;
    n_pairs++;
}

static int add_unit(const char *name)
{
    if (alloc_fail) return -1;
    if (n_units >= cap_units) {
        int nc = cap_units ? cap_units * 2 : 64;
        Unit *nu = realloc(units, sizeof(Unit) * nc);
        if (!nu) { alloc_fail = 1; return -1; }
        units = nu;
        cap_units = nc;
    }
    Unit *u = &units[n_units];
    memset(u, 0, sizeof(*u));
    snprintf(u->name, sizeof(u->name), "%s", name);
    u->first = n_pairs;
    u->ok = 1;
    return n_units++;
}

static void free_pairs(void)
{
    for (int i = 0; i < n_pairs; i++) { free(pairs[i].src); free(pairs[i].rel); }
    free(pairs); free(units);
    pairs = NULL; units = NULL;
    n_pairs = cap_pairs = n_units = cap_units = 0;
    alloc_fail = 0;
}

static void mkdir_p(const char *path, int is_file)
{
    char tmp[300];
    snprintf(tmp, sizeof(tmp), "%s", path);
    if (is_file) { char *s = strrchr(tmp, '/'); if (s) *s = 0; }
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (strncmp(tmp, "/dev_", 5) == 0 && strchr(tmp + 1, '/')) sysFsMkdir(tmp, 0777);
            *p = '/';
        }
    }
    sysFsMkdir(tmp, 0777);
}

typedef struct { char dir[300]; } RmCtx;
static int rm_cb(const char *name, int isdir, void *u)
{
    RmCtx *c = u;
    char p[300];
    snprintf(p, sizeof(p), "%s/%s", c->dir, name);
    if (isdir) {
        RmCtx sub;
        snprintf(sub.dir, sizeof(sub.dir), "%s", p);
        list_dir(p, rm_cb, &sub);
        dir_remove(p);
    } else file_remove(p);
    return 0;
}
static void rm_tree(const char *path)
{
    if (!is_dir(path)) return;
    RmCtx c;
    snprintf(c.dir, sizeof(c.dir), "%s", path);
    list_dir(path, rm_cb, &c);
    dir_remove(path);
}

typedef struct { char src[300]; char rel[300]; int unit; } Walk;
static int files_cb(const char *name, int isdir, void *u);
static void collect(const char *src, const char *rel, int unit)
{
    Walk w;
    snprintf(w.src, sizeof(w.src), "%s", src);
    snprintf(w.rel, sizeof(w.rel), "%s", rel);
    w.unit = unit;
    list_dir(src, files_cb, &w);
}
static int files_cb(const char *name, int isdir, void *u)
{
    Walk *w = u;
    char s[300], r[300];
    snprintf(s, sizeof(s), "%s/%s", w->src, name);
    if (w->rel[0]) snprintf(r, sizeof(r), "%s/%s", w->rel, name);
    else snprintf(r, sizeof(r), "%s", name);
    if (isdir) collect(s, r, w->unit);
    else add_pair(s, r, w->unit);
    return alloc_fail;
}

static int save_cb(const char *name, int isdir, void *u)
{
    (void)u;
    if (!isdir || name[0] == '.' || is_temp_name(name) || strlen(name) > 60) return 0;
    char d[300], old[300], s[300];
    snprintf(d, sizeof(d), "%s/%s", g_dst_root, name);
    snprintf(old, sizeof(old), "%s.old", d);
    snprintf(s, sizeof(s), "%s/%s", g_src_root, name);
    if (!is_dir(d) && is_dir(old)) file_rename(old, d);
    if (g_savejob.dir == SAVE_TO_HDD && is_dir(d)) {  
        g_savejob.saves_skipped++;
        return 0;
    }
    int ui = add_unit(name);
    if (ui < 0) return 1;
    collect(s, "", ui);
    units[ui].count = n_pairs - units[ui].first;
    return alloc_fail;
}

int saves_start(int dir, const char *user)
{
    char hdd[96];
    snprintf(hdd, sizeof(hdd), "/dev_hdd0/home/%s/savedata", user);
    free_pairs();
    memset(&g_savejob, 0, sizeof(g_savejob));
    g_savejob.dir = dir;
    if (dir == SAVE_TO_USB) { snprintf(g_src_root, sizeof(g_src_root), "%s", hdd); snprintf(g_dst_root, sizeof(g_dst_root), "%s", USB_SAVE_DIR); }
    else { snprintf(g_src_root, sizeof(g_src_root), "%s", USB_SAVE_DIR); snprintf(g_dst_root, sizeof(g_dst_root), "%s", hdd); }
    if (!is_dir(g_src_root)) return -1;
    if (list_dir(g_src_root, save_cb, NULL) < 0 && n_units == 0) return -1;
    if (alloc_fail) { free_pairs(); return -2; }         
    g_savejob.files_total = n_pairs;
    cur_unit = 0; cur_started = 0;
    g_savejob.running = 1;
    if (n_units == 0) { g_savejob.running = 0; g_savejob.done = 1; free_pairs(); }
    return 0;
}

static char copybuf[256 * 1024];
static unsigned crc_tab[256];

static unsigned crc_upd(unsigned c, const unsigned char *b, size_t n)
{
    if (!crc_tab[1]) {
        for (unsigned i = 0; i < 256; i++) {
            unsigned x = i;
            for (int k = 0; k < 8; k++) x = (x & 1) ? (x >> 1) ^ 0xEDB88320u : x >> 1;
            crc_tab[i] = x;
        }
    }
    c = ~c;
    while (n--) c = crc_tab[(c ^ *b++) & 0xFF] ^ (c >> 8);
    return ~c;
}

static int verify_file(const char *d, unsigned long long size, unsigned crc)
{
    FILE *f = fopen(d, "rb");
    if (!f) return -1;
    unsigned long long tot = 0;
    unsigned c = 0;
    size_t r;
    while ((r = fread(copybuf, 1, sizeof(copybuf), f)) > 0) { c = crc_upd(c, (unsigned char *)copybuf, r); tot += r; }
    int bad = ferror(f) || tot != size || c != crc;
    fclose(f);
    return bad ? -1 : 0;
}

static int copy_file(const char *s, const char *d)
{
    FILE *in = fopen(s, "rb");
    if (!in) return -1;
    mkdir_p(d, 1);
    FILE *out = fopen(d, "wb");
    if (!out) { fclose(in); return -1; }
    size_t r;
    int err = 0;
    unsigned long long tot = 0;
    unsigned crc = 0;
    while ((r = fread(copybuf, 1, sizeof(copybuf), in)) > 0) {
        if (fwrite(copybuf, 1, r, out) != r) { err = 1; break; }
        crc = crc_upd(crc, (unsigned char *)copybuf, r);
        tot += r;
        g_savejob.bytes += r;
    }
    if (ferror(in)) err = 1;
    fclose(in);
    if (fflush(out) != 0) err = 1;
    if (fclose(out) != 0) err = 1;
    if (!err && file_size(d) != (long long)tot) err = 1;
    if (!err && verify_file(d, tot, crc) != 0) err = 1;
    if (err) file_remove(d);
    return err ? -1 : 0;
}

static int install_unit(const Unit *u)
{
    char fin[300], part[300], old[300];
    snprintf(fin, sizeof(fin), "%s/%s", g_dst_root, u->name);
    snprintf(part, sizeof(part), "%s.partial", fin);
    snprintf(old, sizeof(old), "%s.old", fin);
    if (g_savejob.dir == SAVE_TO_HDD && is_dir(fin)) return 1;   
    int had = is_dir(fin);
    if (had) {
        rm_tree(old);
        if (file_rename(fin, old) != 0) return -1;
    }
    if (file_rename(part, fin) != 0) {
        if (had) file_rename(old, fin);         
        return -1;
    }
    if (had) rm_tree(old);
    return 0;
}

void saves_tick(unsigned budget_ms)
{
    if (!g_savejob.running) return;
    unsigned t0 = ticks_ms();
    while (cur_unit < n_units && ticks_ms() - t0 < budget_ms) {
        Unit *u = &units[cur_unit];
        char part[300];
        snprintf(part, sizeof(part), "%s/%s.partial", g_dst_root, u->name);
        if (g_savejob.cancel) { rm_tree(part); break; }
        if (!cur_started) {
            rm_tree(part);                         
            mkdir_p(part, 0);
            if (!is_dir(part)) { u->ok = 0; g_savejob.errors++; }
            cur_started = 1;
            continue;
        }
        if (u->ok && g_savejob.files_done < u->first + u->count) {
            Pair *p = &pairs[g_savejob.files_done];
            char d[300];
            snprintf(g_savejob.current, sizeof(g_savejob.current), "%s", p->src);
            snprintf(d, sizeof(d), "%s/%s", part, p->rel);
            if (copy_file(p->src, d) != 0) { u->ok = 0; g_savejob.errors++; }
            else g_savejob.files_done++;
            continue;
        }
        g_savejob.files_done = u->first + u->count;
        if (u->ok) {
            int r = install_unit(u);
            if (r == 0) g_savejob.saves_copied++;
            else if (r > 0) { rm_tree(part); g_savejob.saves_skipped++; }
            else { g_savejob.errors++; u->ok = 0; rm_tree(part); }
        } else rm_tree(part);
        if (!u->ok) g_savejob.saves_failed++;
        cur_unit++;
        cur_started = 0;
    }
    if (g_savejob.cancel || cur_unit >= n_units) {
        g_savejob.running = 0;
        g_savejob.done = 1;
        free_pairs();
    }
}