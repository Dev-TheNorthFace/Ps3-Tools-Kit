#include "tools2.h"
#include "sys.h"
#include "prefs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DiskScan g_scan;

#define STK_N 768
#define STK_LEN 256
static char (*stk)[STK_LEN] = NULL;
static unsigned char *stk_f = NULL;
static int stk_n = 0;
static int cur_folder = 0;    

static int root_cb(const char *name, int isdir, void *u)
{
    (void)u;
    if (isdir) {
        if (g_scan.nf < FS_MAXF - 1) {
            FolderSize *f = &g_scan.f[g_scan.nf++];
            snprintf(f->name, sizeof(f->name), "%s", name);
        } else g_scan.incomplete = 1;
    } else {
        char p[STK_LEN];
        snprintf(p, sizeof(p), "%s/%s", g_scan.root, name);
        long long s = file_size(p);
        if (s > 0) { g_scan.f[FS_MAXF - 1].bytes += (uint64_t)s; g_scan.f[FS_MAXF - 1].files++; }
        g_scan.files++;
    }
    return 0;
}

static int dir_cb_scan(const char *name, int isdir, void *u)
{
    const char *dir = (const char *)u;
    char p[STK_LEN];
    if (snprintf(p, sizeof(p), "%s/%s", dir, name) >= (int)sizeof(p)) { g_scan.incomplete = 1; return 0; }
    if (isdir) {
        if (stk_n < STK_N) { memcpy(stk[stk_n], p, STK_LEN); stk_f[stk_n++] = (unsigned char)cur_folder; }
        else g_scan.incomplete = 1;
    } else {
        long long s = file_size(p);
        if (s > 0) { g_scan.f[cur_folder].bytes += (uint64_t)s; g_scan.f[cur_folder].files++; }
        g_scan.files++;
    }
    return 0;
}

void scan_disk_start(const char *root)
{
    if (!stk) { stk = malloc((size_t)STK_N * STK_LEN); stk_f = malloc(STK_N); }
    memset(&g_scan, 0, sizeof(g_scan));
    snprintf(g_scan.root, sizeof(g_scan.root), "%s", root);
    g_scan.free_ok = sys_free_space(root, &g_scan.free_b) == 0;
    stk_n = 0;
    if (!stk || !stk_f) { g_scan.done = 1; g_scan.incomplete = 1; return; }
    snprintf(g_scan.f[FS_MAXF - 1].name, sizeof(g_scan.f[0].name), "(racine)");
    if (list_dir(root, root_cb, NULL) != 0) { g_scan.done = 1; g_scan.incomplete = 1; return; }
    for (int i = g_scan.nf - 1; i >= 0; i--) {
        snprintf(stk[stk_n], STK_LEN, "%s/%s", root, g_scan.f[i].name);
        stk_f[stk_n++] = (unsigned char)i;
    }
    g_scan.active = 1;
}

void scan_disk_cancel(void) { g_scan.active = 0; }

void scan_disk_tick(int budget_ms)
{
    if (!g_scan.active) return;
    unsigned end = ticks_ms() + (unsigned)budget_ms;
    while (stk_n > 0 && (int)(ticks_ms() - end) < 0) {
        stk_n--;
        char dir[STK_LEN];
        memcpy(dir, stk[stk_n], STK_LEN);
        cur_folder = stk_f[stk_n];
        list_dir(dir, dir_cb_scan, dir);
    }
    if (stk_n == 0) {
        g_scan.active = 0;
        g_scan.done = 1;
        FolderSize *r = &g_scan.f[FS_MAXF - 1];
        if (r->bytes) g_scan.f[g_scan.nf++] = *r;
        g_scan.used = 0;
        for (int i = 0; i < g_scan.nf; i++) g_scan.used += g_scan.f[i].bytes;
        for (int i = 0; i < g_scan.nf; i++)
            for (int j = i + 1; j < g_scan.nf; j++)
                if (g_scan.f[j].bytes > g_scan.f[i].bytes) { FolderSize t = g_scan.f[i]; g_scan.f[i] = g_scan.f[j]; g_scan.f[j] = t; }
    } else {
        g_scan.used = 0;
        for (int i = 0; i < FS_MAXF; i++) g_scan.used += g_scan.f[i].bytes;
    }
}

SpeedTest g_speed;
#define SP_CHUNK (1024 * 1024)
static unsigned char *sp_buf = NULL;
static FILE *sp_f = NULL;
static char sp_path[96];
static unsigned sp_ms_w, sp_ms_r;
static int sp_mb_i;

static void fill_block(unsigned char *b, int idx)
{
    uint64_t *w = (uint64_t *)b;
    for (int j = 0; j < SP_CHUNK / 8; j++)
        w[j] = ((uint64_t)(idx + 1) * 0x9E3779B97F4A7C15ULL) ^ ((uint64_t)j * 0xD1B54A32D192ED03ULL);
}

static void sp_fail(const char *msg)
{
    g_speed.state = 4;
    snprintf(g_speed.err, sizeof(g_speed.err), "%s", msg);
    if (sp_f) { fclose(sp_f); sp_f = NULL; }
    file_remove(sp_path);
}

void speed_start(const char *root, int mb)
{
    memset(&g_speed, 0, sizeof(g_speed));
    g_speed.w_x10 = g_speed.r_x10 = -1;
    g_speed.mb = mb;
    snprintf(g_speed.root, sizeof(g_speed.root), "%s", root);
    if (!strcmp(root, "/dev_hdd0")) { make_dir("/dev_hdd0/tmp"); snprintf(sp_path, sizeof(sp_path), "/dev_hdd0/tmp/ps3tk_speed.bin"); }
    else snprintf(sp_path, sizeof(sp_path), "%s/ps3tk_speed.bin", root);
    uint64_t fb;
    if (sys_free_space(root, &fb) == 0 && fb < (uint64_t)(mb + 16) * 1024 * 1024) { sp_fail("espace libre insuffisant"); return; }
    if (!sp_buf) sp_buf = malloc(SP_CHUNK);
    if (!sp_buf) { sp_fail("mémoire insuffisante"); return; }
    sp_f = fopen(sp_path, "wb");
    if (!sp_f) { sp_fail("création du fichier impossible"); return; }
    sp_ms_w = sp_ms_r = 0;
    sp_mb_i = 0;
    g_speed.state = 1;
}

void speed_cancel(void)
{
    if (g_speed.state == 1 || g_speed.state == 2) {
        if (sp_f) { fclose(sp_f); sp_f = NULL; }
        file_remove(sp_path);
        g_speed.state = 0;
    }
}

static int x10(int mb, unsigned ms) { return ms ? (int)((uint64_t)mb * 10000 / ms) : -1; }

void speed_tick(void)
{
    if (g_speed.state == 1) {
        fill_block(sp_buf, sp_mb_i);
        unsigned t = ticks_ms();
        size_t w = fwrite(sp_buf, 1, SP_CHUNK, sp_f);
        sp_ms_w += ticks_ms() - t;
        if (w != SP_CHUNK) { sp_fail("écriture interrompue (disque plein ?)"); return; }
        g_speed.done_mb = ++sp_mb_i;
        if (sp_mb_i >= g_speed.mb) {
            t = ticks_ms();
            int c = fclose(sp_f);
            sp_f = NULL;
            sp_ms_w += ticks_ms() - t;
            if (c != 0) { sp_fail("fermeture du fichier en erreur"); return; }
            g_speed.w_x10 = x10(g_speed.mb, sp_ms_w);
            sp_f = fopen(sp_path, "rb");
            if (!sp_f) { sp_fail("relecture impossible"); return; }
            sp_mb_i = 0; g_speed.done_mb = 0;
            g_speed.state = 2;
        }
    } else if (g_speed.state == 2) {
        unsigned t = ticks_ms();
        size_t r = fread(sp_buf, 1, SP_CHUNK, sp_f);
        sp_ms_r += ticks_ms() - t;
        if (r != SP_CHUNK) { sp_fail("lecture incomplète"); return; }
        uint64_t *w = (uint64_t *)sp_buf;
        int bad = 0;
        for (int j = 0; j < SP_CHUNK / 8; j++)
            if (w[j] != (((uint64_t)(sp_mb_i + 1) * 0x9E3779B97F4A7C15ULL) ^ ((uint64_t)j * 0xD1B54A32D192ED03ULL))) { bad = 1; break; }
        if (bad) g_speed.bad++;
        g_speed.done_mb = ++sp_mb_i;
        if (sp_mb_i >= g_speed.mb) {
            fclose(sp_f); sp_f = NULL;
            file_remove(sp_path);
            g_speed.r_x10 = x10(g_speed.mb, sp_ms_r);
            g_speed.state = 3;
        }
    }
}

UsbPort g_usb[USB_N];
UsbEvent g_usb_ev[USB_EV_N];
int g_usb_nev = 0;
static unsigned usb_last = 0, usb_free_last = 0;
static int usb_init = 0;

int usb_count(void)
{
    int n = 0;
    for (int i = 0; i < USB_N; i++) if (g_usb[i].present) n++;
    return n;
}

static int has(const char *dir, const char *name)
{
    char p[160];
    snprintf(p, sizeof(p), "%s/%s", dir, name);
    return path_exists(p);
}

void usb_analyze(int i)
{
    UsbPort *u = &g_usb[i];
    u->content = 0;
    if (!u->present) return;
    u->free_ok = sys_free_space(u->path, &u->free_b) == 0;
    if (has(u->path, "PS3")) u->content |= UC_PS3;
    if (has(u->path, "PS3/UPDATE/PS3UPDAT.PUP")) u->content |= UC_PUP;
    if (has(u->path, "PS3/SAVEDATA") || has(u->path, "PS3/EXPORT")) u->content |= UC_SAVES;
    if (has(u->path, "PS3TK_DUMPS")) u->content |= UC_DUMPS;
    if (has(u->path, "GAMES") || has(u->path, "GAMEZ")) u->content |= UC_GAMES;
    if (has(u->path, "PS3ISO")) u->content |= UC_ISO;
    if (has(u->path, "PS3TK_EXPORT")) u->content |= UC_EXPORT;
}

static void usb_event(int port, int up)
{
    memmove(&g_usb_ev[1], &g_usb_ev[0], sizeof(UsbEvent) * (USB_EV_N - 1));
    g_usb_ev[0].t = ticks_ms();
    g_usb_ev[0].port = port;
    g_usb_ev[0].up = up;
    if (g_usb_nev < USB_EV_N) g_usb_nev++;
}

void usb_poll(void)
{
    unsigned now = ticks_ms();
    if (usb_init && now - usb_last < 500) return;
    usb_last = now;
    for (int i = 0; i < USB_N; i++) {
        UsbPort *u = &g_usb[i];
        snprintf(u->path, sizeof(u->path), "/dev_usb%03d", i);
        int p = is_dir(u->path);
        if (p != u->present) {
            u->present = p;
            if (usb_init) usb_event(i, p);
            usb_analyze(i);
        }
    }
    if (now - usb_free_last > 5000) {
        usb_free_last = now;
        for (int i = 0; i < USB_N; i++)
            if (g_usb[i].present) g_usb[i].free_ok = sys_free_space(g_usb[i].path, &g_usb[i].free_b) == 0;
    }
    usb_init = 1;
}

Optical g_opt;
#define OPT_ROOT "/dev_bdvd"
#define OPT_FILES 256
static char (*opt_files)[160] = NULL;
static long long *opt_sizes = NULL;
static int opt_nf = 0, opt_cur = 0;
static long long opt_cur_off = 0;
static FILE *opt_f = NULL;
static unsigned opt_ms = 0, opt_last = 0;
static char (*opt_stk)[160] = NULL;
static int opt_stk_n = 0;
#define OPT_STK 128
#define OPT_READ_CAP (96ull * 1024 * 1024)

static uint32_t rd32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

int sfo_read_str(const char *path, const char *key, char *out, int n)
{
    out[0] = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    unsigned char *b = malloc(16384);
    if (!b) { fclose(f); return -1; }
    size_t len = fread(b, 1, 16384, f);
    fclose(f);
    int ret = -1;
    if (len >= 20 && b[0] == 0 && b[1] == 'P' && b[2] == 'S' && b[3] == 'F') {
        uint32_t kt = rd32(b + 8), dt = rd32(b + 12), cnt = rd32(b + 16);
        for (uint32_t i = 0; i < cnt && 20 + (i + 1) * 16 <= len; i++) {
            const unsigned char *e = b + 20 + i * 16;
            uint32_t ko = kt + rd16(e), dlen = rd32(e + 4), doff = dt + rd32(e + 12);
            if (ko >= len || doff >= len) continue;
            if (strncmp((const char *)b + ko, key, len - ko) == 0 && strlen(key) == strnlen((const char *)b + ko, len - ko)) {
                uint32_t m = dlen < (uint32_t)(n - 1) ? dlen : (uint32_t)(n - 1);
                if (doff + m > len) m = (uint32_t)(len - doff);
                memcpy(out, b + doff, m);
                out[m] = 0;
                ret = 0;
                break;
            }
        }
    }
    free(b);
    return ret;
}

static void optical_identify(void)
{
    g_opt.media[0] = g_opt.title[0] = g_opt.titleid[0] = g_opt.version[0] = 0;
    g_opt.ps3disc = 0;
    if (path_exists(OPT_ROOT "/PS3_GAME") || path_exists(OPT_ROOT "/PS3_DISC.SFB")) {
        g_opt.ps3disc = 1;
        snprintf(g_opt.media, sizeof(g_opt.media), "Disque de jeu PS3");
        sfo_read_str(OPT_ROOT "/PS3_GAME/PARAM.SFO", "TITLE", g_opt.title, sizeof(g_opt.title));
        sfo_read_str(OPT_ROOT "/PS3_GAME/PARAM.SFO", "TITLE_ID", g_opt.titleid, sizeof(g_opt.titleid));
        sfo_read_str(OPT_ROOT "/PS3_GAME/PARAM.SFO", "APP_VER", g_opt.version, sizeof(g_opt.version));
    } else if (path_exists(OPT_ROOT "/BDMV")) snprintf(g_opt.media, sizeof(g_opt.media), "Film Blu-ray");
    else if (path_exists(OPT_ROOT "/VIDEO_TS")) snprintf(g_opt.media, sizeof(g_opt.media), "Film DVD");
    else if (path_exists(OPT_ROOT "/PSP_GAME")) snprintf(g_opt.media, sizeof(g_opt.media), "Disque PSP");
    else snprintf(g_opt.media, sizeof(g_opt.media), "Disque de données");
}

void optical_poll(void)
{
    unsigned now = ticks_ms();
    if (opt_last && now - opt_last < 700) return;
    opt_last = now;
    int p = is_dir(OPT_ROOT);
    if (p && !g_opt.present) {
        g_opt.present = 1;
        optical_identify();
    } else if (!p && g_opt.present) {
        optical_test_cancel();
        memset(&g_opt, 0, sizeof(g_opt));
    }
}

static void opt_log(const char *fmt, const char *a)
{
    if (g_opt.nlog >= 4) return;
    snprintf(g_opt.log[g_opt.nlog++], 64, T(fmt), a);
}

static int opt_enum_cb(const char *name, int isdir, void *u)
{
    const char *dir = (const char *)u;
    char p[160];
    if (snprintf(p, sizeof(p), "%s/%s", dir, name) >= (int)sizeof(p)) return 0;
    if (isdir) {
        if (opt_stk_n < OPT_STK) strcpy(opt_stk[opt_stk_n++], p);
    } else {
        long long s = file_size(p);
        g_opt.files++;
        if (s > 0) {
            g_opt.bytes += (uint64_t)s;
            int slot = -1;
            if (opt_nf < OPT_FILES) slot = opt_nf++;
            else {
                int mi = 0;
                for (int i = 1; i < OPT_FILES; i++) if (opt_sizes[i] < opt_sizes[mi]) mi = i;
                if (opt_sizes[mi] < s) slot = mi;
            }
            if (slot >= 0) { strcpy(opt_files[slot], p); opt_sizes[slot] = s; }
        }
    }
    return 0;
}

void optical_test_start(void)
{
    if (!g_opt.present) return;
    if (!opt_files) { opt_files = malloc(OPT_FILES * 160); opt_sizes = malloc(OPT_FILES * sizeof(long long)); opt_stk = malloc(OPT_STK * 160); }
    if (!opt_files || !opt_sizes || !opt_stk) return;
    g_opt.files = 0; g_opt.bytes = 0; g_opt.read_b = 0; g_opt.mbs_x10 = -1; g_opt.errors = 0; g_opt.nlog = 0;
    opt_nf = 0; opt_cur = 0; opt_cur_off = 0; opt_ms = 0; opt_stk_n = 0;
    if (opt_f) { fclose(opt_f); opt_f = NULL; }
    strcpy(opt_stk[opt_stk_n++], OPT_ROOT);
    g_opt.state = 1;
}

void optical_test_cancel(void)
{
    if (opt_f) { fclose(opt_f); opt_f = NULL; }
    if (g_opt.state == 1 || g_opt.state == 2) g_opt.state = 0;
}

void optical_tick(void)
{
    if (g_opt.state == 1) {
        unsigned end = ticks_ms() + 14;
        while (opt_stk_n > 0 && (int)(ticks_ms() - end) < 0) {
            char d[160];
            strcpy(d, opt_stk[--opt_stk_n]);
            if (list_dir(d, opt_enum_cb, d) != 0) { g_opt.errors++; opt_log("Dossier illisible : %s", d); }
        }
        if (opt_stk_n == 0) {
            if (opt_nf == 0) { g_opt.state = 3; g_last_opt = 1; opt_log("%s", "Aucun fichier à lire sur ce disque"); return; }
            for (int i = 0; i < opt_nf; i++)
                for (int j = i + 1; j < opt_nf; j++)
                    if (opt_sizes[j] > opt_sizes[i]) {
                        long long ts = opt_sizes[i]; opt_sizes[i] = opt_sizes[j]; opt_sizes[j] = ts;
                        char tp[160]; strcpy(tp, opt_files[i]); strcpy(opt_files[i], opt_files[j]); strcpy(opt_files[j], tp);
                    }
            g_opt.state = 2;
        }
        return;
    }
    if (g_opt.state != 2) return;
    static unsigned char buf[256 * 1024];
    if (!opt_f) {
        if (opt_cur >= opt_nf || g_opt.read_b >= OPT_READ_CAP) goto finish;
        opt_f = fopen(opt_files[opt_cur], "rb");
        opt_cur_off = 0;
        if (!opt_f) { g_opt.errors++; opt_log("Ouverture impossible : %s", opt_files[opt_cur]); opt_cur++; return; }
    }
    unsigned t = ticks_ms();
    size_t r = fread(buf, 1, sizeof(buf), opt_f);
    opt_ms += ticks_ms() - t;
    g_opt.read_b += r;
    opt_cur_off += (long long)r;
    if (r < sizeof(buf) && ferror(opt_f)) {
        g_opt.errors++;
        opt_log("Erreur de lecture : %s", opt_files[opt_cur]);
        fclose(opt_f); opt_f = NULL; opt_cur++;
    } else if (r == 0 || opt_cur_off >= 12ll * 1024 * 1024 || opt_cur_off >= opt_sizes[opt_cur]) {
        if (r < sizeof(buf) && opt_cur_off < opt_sizes[opt_cur] && opt_cur_off < 12ll * 1024 * 1024) {
            g_opt.errors++;
            opt_log("Lecture trop courte : %s", opt_files[opt_cur]);
        }
        fclose(opt_f); opt_f = NULL; opt_cur++;
    }
    if (opt_ms) g_opt.mbs_x10 = (int)(g_opt.read_b * 10000ull / ((uint64_t)opt_ms * 1024 * 1024));
    return;
finish:
    g_opt.state = 3;
    g_last_opt = g_opt.errors;
}