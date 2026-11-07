#include "filetools.h"
#include "sha256.h"
#include "sys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static uint8_t buf1[64 * 1024], buf2[64 * 1024];

static int contains_ci(const char *h, const char *n)
{
    size_t ln = strlen(n);
    for (; *h; h++)
        if (strncasecmp(h, n, ln) == 0) return 1;
    return ln == 0;
}

static FILE *hf;
static Sha256 hs;

void fthash_cancel(void)
{
    if (hf) { fclose(hf); hf = NULL; }
    g_fthash.active = 0;
}

void fthash_start(const char *path)
{
    fthash_cancel();
    memset(&g_fthash, 0, sizeof(g_fthash));
    snprintf(g_fthash.path, sizeof(g_fthash.path), "%s", path);
    long long sz = file_size(path);
    hf = fopen(path, "rb");
    if (!hf || sz < 0) { if (hf) { fclose(hf); hf = NULL; } g_fthash.err = 1; g_fthash.done = 1; return; }
    g_fthash.total = (unsigned long long)sz;
    sha256_init(&hs);
    g_fthash.active = 1;
}

void fthash_tick(unsigned budget_ms)
{
    if (!g_fthash.active) return;
    unsigned t0 = ticks_ms();
    while (ticks_ms() - t0 < budget_ms) {
        size_t r = fread(buf1, 1, sizeof(buf1), hf);
        if (r > 0) { sha256_update(&hs, buf1, r); g_fthash.done_b += r; }
        if (r < sizeof(buf1)) {
            int bad = ferror(hf);
            fclose(hf); hf = NULL;
            g_fthash.active = 0; g_fthash.done = 1;
            if (bad) { g_fthash.err = 1; return; }
            uint8_t d[32];
            sha256_final(&hs, d);
            sha256_hex(d, g_fthash.hex);
            return;
        }
    }
}

typedef struct { char **q; int n, cap, head; } Queue;
static int q_push(Queue *q, const char *p)
{
    if (q->n >= 20000) return -1;
    if (q->n >= q->cap) {
        int nc = q->cap ? q->cap * 2 : 64;
        char **nq = realloc(q->q, sizeof(char *) * nc);
        if (!nq) return -1;
        q->q = nq; q->cap = nc;
    }
    char *s = strdup(p);
    if (!s) return -1;
    q->q[q->n++] = s;
    return 0;
}
static void q_free(Queue *q)
{
    for (int i = 0; i < q->n; i++) free(q->q[i]);
    free(q->q);
    memset(q, 0, sizeof(*q));
}

static Queue sq, fq;
static const char *cur_dir;

static int size_cb(const char *name, int isdir, void *u)
{
    (void)u;
    char p[400];
    snprintf(p, sizeof(p), "%s/%s", cur_dir, name);
    if (isdir) { g_ftsize.dirs++; if (q_push(&sq, p) != 0) g_ftsize.incomplete = 1; }
    else { long long s = file_size(p); if (s > 0) g_ftsize.bytes += (unsigned long long)s; g_ftsize.files++; }
    return 0;
}

void ftsize_cancel(void) { q_free(&sq); g_ftsize.active = 0; }

void ftsize_start(const char *dir)
{
    ftsize_cancel();
    memset(&g_ftsize, 0, sizeof(g_ftsize));
    snprintf(g_ftsize.root, sizeof(g_ftsize.root), "%s", dir);
    if (q_push(&sq, dir) != 0) { g_ftsize.done = 1; g_ftsize.incomplete = 1; return; }
    g_ftsize.active = 1;
}

void ftsize_tick(unsigned budget_ms)
{
    if (!g_ftsize.active) return;
    unsigned t0 = ticks_ms();
    while (sq.head < sq.n && ticks_ms() - t0 < budget_ms) {
        cur_dir = sq.q[sq.head];
        if (list_dir(cur_dir, size_cb, NULL) != 0) g_ftsize.incomplete = 1;
        free(sq.q[sq.head]); sq.q[sq.head] = NULL;
        sq.head++;
    }
    if (sq.head >= sq.n) { q_free(&sq); g_ftsize.active = 0; g_ftsize.done = 1; }
}

static int find_cb(const char *name, int isdir, void *u)
{
    (void)u;
    char p[400];
    snprintf(p, sizeof(p), "%s/%s", cur_dir, name);
    g_ftfind.scanned++;
    if (isdir && q_push(&fq, p) != 0) g_ftfind.incomplete = 1;
    if (contains_ci(name, g_ftfind.query)) {
        if (g_ftfind.n < FT_RES) {
            FtFound *f = &g_ftfind.r[g_ftfind.n++];
            snprintf(f->path, sizeof(f->path), "%s", p);
            f->isdir = isdir;
            f->size = isdir ? -1 : file_size(p);
        } else g_ftfind.incomplete = 1;
    }
    return 0;
}

void ftfind_cancel(void) { q_free(&fq); g_ftfind.active = 0; }

void ftfind_start(const char *dir, const char *query)
{
    ftfind_cancel();
    memset(&g_ftfind, 0, sizeof(g_ftfind));
    snprintf(g_ftfind.root, sizeof(g_ftfind.root), "%s", dir);
    snprintf(g_ftfind.query, sizeof(g_ftfind.query), "%s", query);
    if (!query[0] || q_push(&fq, dir) != 0) { g_ftfind.done = 1; return; }
    g_ftfind.active = 1;
}

void ftfind_tick(unsigned budget_ms)
{
    if (!g_ftfind.active) return;
    unsigned t0 = ticks_ms();
    while (fq.head < fq.n && ticks_ms() - t0 < budget_ms) {
        cur_dir = fq.q[fq.head];
        if (list_dir(cur_dir, find_cb, NULL) != 0) g_ftfind.incomplete = 1;
        free(fq.q[fq.head]); fq.q[fq.head] = NULL;
        fq.head++;
    }
    if (fq.head >= fq.n) { q_free(&fq); g_ftfind.active = 0; g_ftfind.done = 1; }
}

static FILE *xf;
static long long xsize;

int fthex_open(const char *path)
{
    fthex_close();
    long long sz = file_size(path);
    xf = fopen(path, "rb");
    if (!xf || sz < 0) { if (xf) { fclose(xf); xf = NULL; } return -1; }
    xsize = sz;
    return 0;
}
void fthex_close(void) { if (xf) { fclose(xf); xf = NULL; } xsize = 0; g_ftseek.active = 0; }
long long fthex_size(void) { return xsize; }

int fthex_read(long long off, uint8_t *buf, int n)
{
    if (!xf || off < 0 || off >= xsize) return 0;
    if (fseek(xf, (long)off, SEEK_SET) != 0) return 0;
    if (off + n > xsize) n = (int)(xsize - off);
    size_t r = fread(buf, 1, n, xf);
    return (int)r;
}

int ft_parse_hex(const char *s, uint8_t *out, int max)
{
    int n = 0, hi = -1;
    for (; *s; s++) {
        char c = *s;
        if (c == ' ' || c == ':' || c == '-') continue;
        int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (v < 0) return 0;
        if (hi < 0) hi = v;
        else { if (n >= max) return 0; out[n++] = (uint8_t)(hi << 4 | v); hi = -1; }
    }
    return hi < 0 ? n : 0;
}

FtSeek g_ftseek;
static long long seek_pos;

void fthex_find_cancel(void) { g_ftseek.active = 0; }

void fthex_find_start(long long from, const uint8_t *pat, int plen)
{
    memset(&g_ftseek, 0, sizeof(g_ftseek));
    if (!xf || plen <= 0 || plen > 32) { g_ftseek.done = 1; return; }
    memcpy(g_ftseek.pat, pat, plen);
    g_ftseek.plen = plen;
    g_ftseek.from = from < 0 ? 0 : from;
    g_ftseek.at = -1;
    seek_pos = g_ftseek.from;
    g_ftseek.active = 1;
}

void fthex_find_tick(unsigned budget_ms)
{
    if (!g_ftseek.active) return;
    unsigned t0 = ticks_ms();
    while (ticks_ms() - t0 < budget_ms) {
        int n = fthex_read(seek_pos, buf1, (int)sizeof(buf1));
        if (n < g_ftseek.plen) { g_ftseek.active = 0; g_ftseek.done = 1; return; }
        for (int i = 0; i + g_ftseek.plen <= n; i++)
            if (buf1[i] == g_ftseek.pat[0] && memcmp(buf1 + i, g_ftseek.pat, g_ftseek.plen) == 0) {
                g_ftseek.at = seek_pos + i; g_ftseek.found = 1;
                g_ftseek.active = 0; g_ftseek.done = 1;
                return;
            }
        if (seek_pos + n >= xsize) { g_ftseek.active = 0; g_ftseek.done = 1; return; }
        seek_pos += n - (g_ftseek.plen - 1);          
    }
}

static FILE *ca, *cb;
static Sha256 sa, sb;
static int in_run;
static unsigned long long run_end;

void ftcmp_cancel(void)
{
    if (ca) { fclose(ca); ca = NULL; }
    if (cb) { fclose(cb); cb = NULL; }
    g_ftcmp.active = 0;
}

void ftcmp_start(const char *a, const char *b)
{
    ftcmp_cancel();
    memset(&g_ftcmp, 0, sizeof(g_ftcmp));
    snprintf(g_ftcmp.a, sizeof(g_ftcmp.a), "%s", a);
    snprintf(g_ftcmp.b, sizeof(g_ftcmp.b), "%s", b);
    long long x = file_size(a), y = file_size(b);
    ca = fopen(a, "rb"); cb = fopen(b, "rb");
    if (!ca || !cb || x < 0 || y < 0) { ftcmp_cancel(); g_ftcmp.err = 1; g_ftcmp.done = 1; return; }
    g_ftcmp.size_a = (unsigned long long)x; g_ftcmp.size_b = (unsigned long long)y;
    sha256_init(&sa); sha256_init(&sb);
    in_run = 0;
    g_ftcmp.active = 1;
}

void ftcmp_tick(unsigned budget_ms)
{
    if (!g_ftcmp.active) return;
    unsigned t0 = ticks_ms();
    unsigned long long maxs = g_ftcmp.size_a > g_ftcmp.size_b ? g_ftcmp.size_a : g_ftcmp.size_b;
    while (ticks_ms() - t0 < budget_ms) {
        size_t ra = fread(buf1, 1, sizeof(buf1), ca), rb = fread(buf2, 1, sizeof(buf2), cb);
        if (ferror(ca) || ferror(cb)) { ftcmp_cancel(); g_ftcmp.err = 1; g_ftcmp.done = 1; return; }
        if (ra) sha256_update(&sa, buf1, ra);
        if (rb) sha256_update(&sb, buf2, rb);
        size_t common = ra < rb ? ra : rb;
        for (size_t i = 0; i < common; i++) {
            unsigned long long at = g_ftcmp.pos + i;
            if (buf1[i] != buf2[i]) {
                g_ftcmp.ndiff++;
                if (in_run && run_end == at) { g_ftcmp.len[g_ftcmp.nr - 1]++; run_end++; }
                else if (g_ftcmp.nr < FT_DIFF_MAX) { g_ftcmp.off[g_ftcmp.nr] = at; g_ftcmp.len[g_ftcmp.nr] = 1; g_ftcmp.nr++; in_run = 1; run_end = at + 1; }
                else { g_ftcmp.over = 1; in_run = 0; }
            }
        }
        g_ftcmp.pos += common;
        if (ra == 0 && rb == 0) {
            uint8_t d[32];
            sha256_final(&sa, d); sha256_hex(d, g_ftcmp.ha);
            sha256_final(&sb, d); sha256_hex(d, g_ftcmp.hb);
            fclose(ca); fclose(cb); ca = cb = NULL;
            g_ftcmp.active = 0; g_ftcmp.done = 1;
            return;
        }
        (void)maxs;
    }
}

int ft_cmp_identical(void)
{
    return g_ftcmp.done && !g_ftcmp.err && g_ftcmp.size_a == g_ftcmp.size_b && g_ftcmp.ndiff == 0;
}

FtHash g_fthash; FtSize g_ftsize; FtFind g_ftfind; FtCmp g_ftcmp;