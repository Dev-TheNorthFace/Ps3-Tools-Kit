#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>
#include <math.h>
#include "lab.h"
#include "sys.h"
#include "prefs.h"
#include "devtools.h"
#ifdef LAB_HOST
#include <pthread.h>
#else
#include <sys/thread.h>
#include <lv2/thread.h>
#endif

LabCfg g_labcfg = { "Test rapide", LT_MASK(LT_CELL) | LT_MASK(LT_MEM) | LT_MASK(LT_FILE), 5, 1, 8, 4, 0 };
const int lab_dur_opts[5] = { 5, 10, 20, 30, 60 };
const int lab_rep_opts[5] = { 1, 2, 3, 5, 10 };
const int lab_mem_opts[5] = { 4, 8, 16, 32, 64 };
const int lab_file_opts[5] = { 2, 4, 8, 16, 32 };
LabRes g_labres[LT_N];
volatile int g_lab_state, g_lab_prog, g_lab_test;
static volatile int cancel_flag;

const char *lab_test_name(int t)
{
    static const char *n[LT_N] = { "Cell (PPU)", "Mémoire", "Fichiers", "RSX (rendu)" };
    return t >= 0 && t < LT_N ? n[t] : "?";
}

LabLogLine g_lablog[LAB_LOG_N];
volatile int g_lablog_n;
void lab_log_clear(void) { g_lablog_n = 0; }
void lab_log(int lvl, const char *fmt, ...)
{
    int i = g_lablog_n;
    if (i >= LAB_LOG_N) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_lablog[i].t, sizeof(g_lablog[i].t), T(fmt), ap);
    va_end(ap);
    g_lablog[i].ms = ticks_ms();
    g_lablog[i].lvl = (unsigned char)lvl;
    __sync_synchronize();
    g_lablog_n = i + 1;
}

typedef struct { void (*fn)(void *); void *arg; } ThrArg;
#ifdef LAB_HOST
typedef pthread_t LabThr;
static void *thr_entry(void *p) { ThrArg a = *(ThrArg *)p; free(p); a.fn(a.arg); return NULL; }
static int thr_create(LabThr *t, void (*fn)(void *), void *arg)
{
    ThrArg *a = malloc(sizeof(*a)); a->fn = fn; a->arg = arg;
    return pthread_create(t, NULL, thr_entry, a);
}
static void thr_join(LabThr t) { pthread_join(t, NULL); }
#else
typedef sys_ppu_thread_t LabThr;
static void thr_entry(void *p)
{
    ThrArg a = *(ThrArg *)p;
    free(p);
    a.fn(a.arg);
    sysThreadExit(0);
}
static int thr_create(LabThr *t, void (*fn)(void *), void *arg)
{
    ThrArg *a = malloc(sizeof(*a));
    if (!a) return -1;
    a->fn = fn; a->arg = arg;
    return sysThreadCreate(t, thr_entry, a, 1000, 0x20000, THREAD_JOINABLE, "ps3tk_lab");
}
static void thr_join(LabThr t) { u64 r; sysThreadJoin(t, &r); }
#endif

static LabRes *R;      
static void rline(const char *fmt, ...)
{
    if (R->nlines >= LAB_NLINES) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(R->lines[R->nlines++], 100, T(fmt), ap);
    va_end(ap);
}
static void rmet(const char *key, int val)
{
    if (R->nmet >= LAB_NMET) return;
    snprintf(R->met[R->nmet].key, 16, "%s", key);
    R->met[R->nmet++].val = val;
}

typedef struct { const char *key, *unit; int scale, higher; } MetDef;
static const MetDef mdefs[] = {
    { "cell.mops", "M op/s", 10, 1 }, { "cell.rounds", "", 1, 1 }, { "cell.sync", "", 1, 1 },
    { "mem.wmbs", "Mo/s", 1, 1 }, { "mem.rmbs", "Mo/s", 1, 1 }, { "mem.err", "", 1, 0 },
    { "file.seqw", "Mo/s", 10, 1 }, { "file.seqr", "Mo/s", 10, 1 }, { "file.rand", "IOPS", 1, 1 },
    { "file.small", "fich./s", 1, 1 }, { "file.err", "", 1, 0 },
    { "rsx.fps", "i/s", 10, 1 }, { "rsx.worst", "ms", 10, 0 }, { "fails", "", 1, 0 },
};
static const MetDef *mdef(const char *k)
{
    for (unsigned i = 0; i < sizeof(mdefs) / sizeof(mdefs[0]); i++) if (!strcmp(mdefs[i].key, k)) return &mdefs[i];
    return NULL;
}
const char *lab_metric_fmt(const char *key, int val, char *out, int n)
{
    const MetDef *d = mdef(key);
    if (!d) snprintf(out, n, "%d", val);
    else if (d->scale == 10) snprintf(out, n, "%d,%d %s", val / 10, val % 10, T(d->unit));
    else snprintf(out, n, "%d %s", val, T(d->unit));
    return out;
}

static int elapsed_prog(unsigned t0, unsigned dur) { unsigned e = ticks_ms() - t0; return e >= dur ? 99 : (int)(e * 100 / dur); }

static uint64_t work(uint64_t seed, unsigned iters)
{
    uint64_t x = seed;
    for (unsigned i = 0; i < iters; i++) { x = x * 6364136223846793005ULL + 1442695040888963407ULL; x ^= x >> 29; x += i; }
    return x;
}
typedef struct { uint64_t seed, out; unsigned iters; volatile int done; } WArg;
static void work_thr(void *p) { WArg *a = p; a->out = work(a->seed, a->iters); __sync_synchronize(); a->done = 1; }

typedef struct { volatile int lock; volatile long plain; volatile long atom; unsigned n; volatile int ready; } SArg;
static void sync_thr(void *p)
{
    SArg *s = p;
    for (unsigned i = 0; i < s->n; i++) {
        while (__sync_lock_test_and_set(&s->lock, 1)) { }
        s->plain = s->plain + 1;
        __sync_synchronize();
        __sync_lock_release(&s->lock);
        __sync_fetch_and_add(&s->atom, 1);
    }
}
typedef struct { volatile int turn; volatile int bad; unsigned n; } Ping;
typedef struct { Ping *p; int me; } PArg;
static void ping_thr(void *x)
{
    PArg *a = x;
    for (unsigned i = 0; i < a->p->n; i++) {
        unsigned spin = 0;
        while (a->p->turn != a->me) { if (++spin > 100000000u) { a->p->bad = 1; return; } }
        __sync_synchronize();
        a->p->turn = 1 - a->me;
    }
}

static int wait_thr(LabThr t1, LabThr t2, volatile int *d1, volatile int *d2)
{
    (void)d1; (void)d2;
    thr_join(t1); thr_join(t2);
    return 0;
}

static void test_cell(const LabCfg *c)
{
    unsigned dur = (unsigned)c->dur_s * 1000u, t0 = ticks_ms();
    unsigned half = dur / 2 ? dur / 2 : 1;
    int rounds = 0, bad = 0;
    unsigned long long ops = 0;
    const unsigned iters = 400000;
    lab_log(LL_INFO, "Cell : charge entière sur 3 fils (%u iterations par tour)", iters);
    unsigned tw = ticks_ms();
    while ((ticks_ms() - t0) < half && !cancel_flag) {
        WArg a = { 0x1234567ULL + rounds, 0, iters, 0 }, b = a;
        LabThr th1, th2;
        int r1 = thr_create(&th1, work_thr, &a), r2 = thr_create(&th2, work_thr, &b);
        if (r1 || r2) {
            lab_log(LL_ERR, "Cell : création de fil impossible (codes %d, %d)", r1, r2);
            rline("Création de fil impossible (codes %d / %d)", r1, r2);
            if (!r1) thr_join(th1);
            if (!r2) thr_join(th2);
            R->fail = 1;
            break;
        }
        uint64_t m = work(a.seed, iters);
        wait_thr(th1, th2, &a.done, &b.done);
        if (a.out != m || b.out != m) {
            bad++;
            lab_log(LL_ERR, "Cell : résultats différents tour %d (%llx %llx %llx)", rounds, (unsigned long long)a.out, (unsigned long long)b.out, (unsigned long long)m);
            if (R->nlines < 6) rline("Tour %d : résultats différents", rounds);
        }
        rounds++;
        ops += 3ull * iters * 4ull;
        g_lab_prog = elapsed_prog(t0, dur) / 2 * 1;
    }
    unsigned d = ticks_ms() - tw; if (!d) d = 1;
    int mops10 = (int)(ops * 10ull / ((unsigned long long)d * 1000ull));
    lab_log(bad ? LL_ERR : LL_OK, "Cell : %d tours, %d différence(s), %d,%d millions d'opérations/s", rounds, bad, mops10 / 10, mops10 % 10);
    rline("Charge entière : %d tours, %d différence(s)", rounds, bad);
    rline("Débit : %d,%d millions d'opérations/s (3 fils)", mops10 / 10, mops10 % 10);
    rmet("cell.mops", mops10); rmet("cell.rounds", rounds);
    if (bad) R->fail = 1;
    double s = 0; const unsigned N = 1000000;
    for (unsigned i = 0; i < N; i++) s += i * 0.25;
    double expect = 0.25 * ((double)N * (N - 1) / 2);
    int fpok = s == expect;
    lab_log(fpok ? LL_OK : LL_ERR, "Cell : somme flottante %s", fpok ? "exacte" : "FAUSSE");
    rline("Flottants (somme de 10^6 termes) : %s", fpok ? "exact" : "FAUX");
    if (!fpok) R->fail = 1;
    g_lab_prog = 60;
    SArg sa; memset(&sa, 0, sizeof(sa)); sa.n = 200000;
    LabThr t1, t2;
    int e1 = thr_create(&t1, sync_thr, &sa), e2 = thr_create(&t2, sync_thr, &sa);
    if (!e1) thr_join(t1);
    if (!e2) thr_join(t2);
    int want = (!e1 + !e2) * (int)sa.n;
    int sok = !e1 && !e2 && sa.plain == want && sa.atom == want;
    lab_log(sok ? LL_OK : LL_ERR, "Cell : verrou %ld / atomique %ld (attendu %d)", sa.plain, sa.atom, want);
    rline("Verrou tournant : %ld, compteur atomique : %ld, attendu : %d", sa.plain, sa.atom, want);
    if (!sok) R->fail = 1;
    g_lab_prog = 85;
    Ping pg = { 0, 0, 20000 };
    PArg p0 = { &pg, 0 }, p1 = { &pg, 1 };
    LabThr q1, q2;
    int f1 = thr_create(&q1, ping_thr, &p0), f2 = thr_create(&q2, ping_thr, &p1);
    if (!f1) thr_join(q1);
    if (!f2) thr_join(q2);
    int pok = !f1 && !f2 && !pg.bad && pg.turn == 0;
    lab_log(pok ? LL_OK : LL_ERR, "Cell : alternance stricte de 2 fils (20000 échanges) %s", pok ? "réussie" : "ÉCHEC");
    rline("Alternance stricte entre 2 fils : %s", pok ? "réussie" : "ÉCHEC");
    rmet("cell.sync", sok && pok);
    if (!(sok && pok)) R->fail = 1;
    rline("Remarque : seuls les fils du PPU sont testés, aucun programme SPU n'est lancé.");
}

static void test_mem(const LabCfg *c)
{
    size_t mb = (size_t)c->mem_mb, bytes = mb << 20;
    uint64_t *buf = malloc(bytes);
    lab_log(LL_INFO, "Mémoire : allocation de %d Mo", c->mem_mb);
    if (!buf) {
        lab_log(LL_ERR, "Mémoire : allocation de %d Mo refusée", c->mem_mb);
        rline("Allocation de %d Mo refusée : essaie une taille plus petite.", c->mem_mb);
        R->fail = 1;
        return;
    }
    size_t n = bytes / 8;
    unsigned dur = (unsigned)c->dur_s * 1000u, t0 = ticks_ms();
    static const char *names[] = { "0x00", "0xFF", "0x55", "0xAA", "1 mobile", "adresse" };
    unsigned long long wb = 0, rb = 0, wms = 0, rms = 0, errs = 0;
    int pass = 0, shown = 0;
    do {
        for (int p = 0; p < 6 && !cancel_flag; p++) {
            unsigned a = ticks_ms();
            switch (p) {
            case 0: for (size_t i = 0; i < n; i++) buf[i] = 0; break;
            case 1: for (size_t i = 0; i < n; i++) buf[i] = ~0ULL; break;
            case 2: for (size_t i = 0; i < n; i++) buf[i] = 0x5555555555555555ULL; break;
            case 3: for (size_t i = 0; i < n; i++) buf[i] = 0xAAAAAAAAAAAAAAAAULL; break;
            case 4: for (size_t i = 0; i < n; i++) buf[i] = 1ULL << (i & 63); break;
            default: for (size_t i = 0; i < n; i++) buf[i] = (uint64_t)(uintptr_t)(buf + i) ^ 0x9E3779B97F4A7C15ULL; break;
            }
            unsigned b = ticks_ms();
            unsigned long long e = 0;
            for (size_t i = 0; i < n; i++) {
                uint64_t want = p == 0 ? 0 : p == 1 ? ~0ULL : p == 2 ? 0x5555555555555555ULL : p == 3 ? 0xAAAAAAAAAAAAAAAAULL :
                                p == 4 ? (1ULL << (i & 63)) : ((uint64_t)(uintptr_t)(buf + i) ^ 0x9E3779B97F4A7C15ULL);
                if (buf[i] != want) {
                    e++;
                    if (shown < 4) { shown++; lab_log(LL_ERR, "Mémoire : différence motif %s à l'offset 0x%llX (lu %llX)", names[p], (unsigned long long)(i * 8), (unsigned long long)buf[i]); }
                }
            }
            unsigned d = ticks_ms();
            wms += b - a; rms += d - b; wb += bytes; rb += bytes; errs += e;
            lab_log(e ? LL_ERR : LL_INFO, "Mémoire : motif %s passe %d : %llu différence(s), écriture %u ms, relecture %u ms", names[p], pass + 1, e, b - a, d - b);
            g_lab_prog = elapsed_prog(t0, dur);
        }
        pass++;
    } while ((ticks_ms() - t0) < dur && !cancel_flag);
    free(buf);
    if (!wms) wms = 1;
    if (!rms) rms = 1;
    int w = (int)(wb / 1048576ull * 1000ull / wms), r = (int)(rb / 1048576ull * 1000ull / rms);
    rline("%d passe(s) de 6 motifs sur %d Mo : %llu différence(s)", pass, c->mem_mb, errs);
    rline("Débit d'écriture : %d Mo/s · de relecture : %d Mo/s", w, r);
    if (errs) rline("Des différences en mémoire vive indiquent un défaut matériel (ou un bug de l'outil).");
    rmet("mem.wmbs", w); rmet("mem.rmbs", r); rmet("mem.err", (int)(errs > 1000000 ? 1000000 : errs));
    if (errs) R->fail = 1;
    lab_log(errs ? LL_ERR : LL_OK, "Mémoire : écriture %d Mo/s, relecture %d Mo/s, %llu différence(s)", w, r, errs);
}

#define LDIR "/dev_hdd0/tmp/PS3TK_LAB"
static void fill_block(unsigned char *b, size_t n, unsigned long long seed)
{
    uint64_t x = seed * 0x9E3779B97F4A7C15ULL + 1;
    for (size_t i = 0; i + 8 <= n; i += 8) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; memcpy(b + i, &x, 8); }
}

static void test_file(const LabCfg *c)
{
    const size_t BS = 65536;
    int mb = c->file_mb;
    unsigned blocks = (unsigned)mb * 16u;
    unsigned char *buf = malloc(BS), *chk = malloc(BS);
    char big[96], small[128];
    int errs = 0;
    snprintf(big, sizeof(big), "%s/seq.bin", LDIR);
    if (!buf || !chk) { free(buf); free(chk); rline("Mémoire insuffisante"); R->fail = 1; return; }
    errno = 0;
    if (make_dir("/dev_hdd0/tmp") != 0 || make_dir(LDIR) != 0) {
        lab_log(LL_ERR, "Fichiers : dossier %s impossible (errno %d)", LDIR, errno);
        rline("Impossible de créer %s (errno %d)", LDIR, errno);
        R->fail = 1; free(buf); free(chk); return;
    }
    unsigned dur = (unsigned)c->dur_s * 1000u, t0 = ticks_ms();
    lab_log(LL_INFO, "Fichiers : écriture séquentielle de %d Mo", mb);
    unsigned a = ticks_ms();
    errno = 0;
    FILE *f = fopen(big, "wb");
    int wok = f != NULL;
    if (!f) { lab_log(LL_ERR, "Fichiers : fopen(wb) échoué, errno %d", errno); errs++; }
    for (unsigned i = 0; f && i < blocks && !cancel_flag; i++) {
        fill_block(buf, BS, i);
        if (fwrite(buf, 1, BS, f) != BS) { lab_log(LL_ERR, "Fichiers : fwrite bloc %u échoué, errno %d", i, errno); errs++; wok = 0; break; }
        g_lab_prog = (int)(i * 30 / blocks);
    }
    if (f) { if (fclose(f) != 0) { errs++; lab_log(LL_ERR, "Fichiers : fclose échoué, errno %d", errno); } }
    unsigned dw = ticks_ms() - a; if (!dw) dw = 1;
    int seqw = wok ? (int)((unsigned long long)mb * 10000ull / dw) : 0;
    a = ticks_ms();
    int bad = 0, seqr = 0;
    f = fopen(big, "rb");
    if (!f) { errs++; lab_log(LL_ERR, "Fichiers : fopen(rb) échoué, errno %d", errno); }
    else {
        for (unsigned i = 0; i < blocks && !cancel_flag; i++) {
            fill_block(chk, BS, i);
            if (fread(buf, 1, BS, f) != BS) { errs++; lab_log(LL_ERR, "Fichiers : lecture courte au bloc %u", i); break; }
            if (memcmp(buf, chk, BS)) { bad++; if (bad < 4) lab_log(LL_ERR, "Fichiers : données différentes, bloc %u", i); }
            g_lab_prog = 30 + (int)(i * 30 / blocks);
        }
        fclose(f);
        unsigned dr = ticks_ms() - a; if (!dr) dr = 1;
        seqr = (int)((unsigned long long)mb * 10000ull / dr);
    }
    errs += bad;
    rline("Séquentiel %d Mo : écriture %d,%d Mo/s · lecture %d,%d Mo/s · %d bloc(s) différent(s)", mb, seqw / 10, seqw % 10, seqr / 10, seqr % 10, bad);
    lab_log(bad ? LL_ERR : LL_OK, "Fichiers : séquentiel écriture %d,%d Mo/s, lecture %d,%d Mo/s", seqw / 10, seqw % 10, seqr / 10, seqr % 10);
    unsigned long long rnd = 12345;
    unsigned ops = 0, rbad = 0;
    a = ticks_ms();
    unsigned rend = a + (dur / 2 > 500 ? dur / 2 : 500);
    f = fopen(big, "rb");
    if (f) {
        unsigned total4k = (unsigned)mb * 256u;
        while ((ticks_ms() < rend || ops < 64) && ops < 100000 && !cancel_flag) {
            rnd = rnd * 6364136223846793005ULL + 1442695040888963407ULL;
            unsigned k = (unsigned)((rnd >> 33) % total4k);
            unsigned blk = k / 16, sub = k % 16;
            fill_block(chk, BS, blk);
            if (fseek(f, (long)((unsigned long long)k * 4096ull), SEEK_SET) != 0 || fread(buf, 1, 4096, f) != 4096) { errs++; lab_log(LL_ERR, "Fichiers : lecture aléatoire échouée au secteur %u", k); break; }
            if (memcmp(buf, chk + sub * 4096, 4096)) { rbad++; if (rbad < 4) lab_log(LL_ERR, "Fichiers : données aléatoires différentes, secteur %u", k); }
            ops++;
            if ((ops & 15) == 0) g_lab_prog = 60 + elapsed_prog(a, rend - a) / 5;
        }
        fclose(f);
    } else { errs++; lab_log(LL_ERR, "Fichiers : réouverture impossible, errno %d", errno); }
    unsigned dr2 = ticks_ms() - a; if (!dr2) dr2 = 1;
    int iops = (int)((unsigned long long)ops * 1000ull / dr2);
    errs += rbad;
    rline("Lectures aléatoires 4 Ko : %u en %u ms (%d IOPS), %u différence(s)", ops, dr2, iops, rbad);
    lab_log(rbad ? LL_ERR : LL_OK, "Fichiers : %d IOPS en lecture aléatoire 4 Ko", iops);
    unsigned nf = 0, sbad = 0;
    a = ticks_ms();
    unsigned send = a + (dur / 2 > 500 ? dur / 2 : 500);
    unsigned char sb[1024], sc[1024];
    while ((ticks_ms() < send || nf < 40) && nf < 2000 && !cancel_flag) {
        snprintf(small, sizeof(small), "%s/s%u.bin", LDIR, nf % 100);
        fill_block(sb, 1024, 1000 + nf);
        FILE *g = fopen(small, "wb");
        if (!g) { errs++; lab_log(LL_ERR, "Fichiers : création de %s échouée, errno %d", small, errno); break; }
        size_t w = fwrite(sb, 1, 1024, g);
        fclose(g);
        g = fopen(small, "rb");
        size_t r = g ? fread(sc, 1, 1024, g) : 0;
        if (g) fclose(g);
        if (w != 1024 || r != 1024 || memcmp(sb, sc, 1024)) { sbad++; if (sbad < 4) lab_log(LL_ERR, "Fichiers : petit fichier %u faux", nf); }
        nf++;
        if ((nf & 7) == 0) g_lab_prog = 80 + elapsed_prog(a, send - a) / 6;
    }
    for (int i = 0; i < 100; i++) { snprintf(small, sizeof(small), "%s/s%d.bin", LDIR, i); file_remove(small); }
    unsigned ds = ticks_ms() - a; if (!ds) ds = 1;
    int fps = (int)((unsigned long long)nf * 1000ull / ds);
    errs += sbad;
    rline("Petits fichiers (1 Ko, création+relecture) : %u en %u ms (%d/s), %u faux", nf, ds, fps, sbad);
    lab_log(sbad ? LL_ERR : LL_OK, "Fichiers : %d petits fichiers par seconde", fps);
    int r1 = file_remove(big), r2 = dir_remove(LDIR);
    if (r1 != 0 || r2 != 0) { lab_log(LL_WARN, "Fichiers : nettoyage incomplet (codes %d, %d)", r1, r2); rline("Nettoyage incomplet : supprime /dev_hdd0/tmp/PS3TK_LAB à la main."); }
    else lab_log(LL_INFO, "Fichiers : fichiers temporaires supprimés");
    rmet("file.seqw", seqw); rmet("file.seqr", seqr); rmet("file.rand", iops); rmet("file.small", fps); rmet("file.err", errs);
    if (errs) R->fail = 1;
    free(buf); free(chk);
}

static LabThr worker;
static int worker_valid;
static LabCfg run_cfg;

static void worker_main(void *arg)
{
    int t = (int)(intptr_t)arg;
    LabRes *res = &g_labres[t];
    memset(res, 0, sizeof(*res));
    R = res;
    unsigned t0 = ticks_ms();
    lab_log(LL_INFO, "=== Test %s : durée %d s, mémoire %d Mo, fichier %d Mo ===", lab_test_name(t), run_cfg.dur_s, run_cfg.mem_mb, run_cfg.file_mb);
    if (t == LT_CELL) test_cell(&run_cfg);
    else if (t == LT_MEM) test_mem(&run_cfg);
    else if (t == LT_FILE) test_file(&run_cfg);
    res->ms = (int)(ticks_ms() - t0);
    if (cancel_flag) { res->fail = res->fail; snprintf(res->summary, sizeof(res->summary), "Interrompu"); }
    else snprintf(res->summary, sizeof(res->summary), res->fail ? "ÉCHEC" : "Réussi");
    lab_log(res->fail ? LL_ERR : LL_OK, "=== Test %s terminé en %d ms : %s ===", lab_test_name(t), res->ms, res->summary);
    g_lab_prog = 100;
    __sync_synchronize();
    g_lab_state = 2;
}

int lab_start(int t)
{
    if (g_lab_state != 0 || t < 0 || t > LT_FILE) return -1;
    run_cfg = g_labcfg;
    cancel_flag = 0;
    g_lab_prog = 0;
    g_lab_test = t;
    g_labres[t].done = 0;
    g_lab_state = 1;
    if (thr_create(&worker, worker_main, (void *)(intptr_t)t) != 0) {
        g_lab_state = 0;
        lab_log(LL_ERR, "Création du fil de test impossible");
        return -1;
    }
    worker_valid = 1;
    return 0;
}

void lab_reap(void)
{
    if (g_lab_state == 2 && worker_valid) {
        thr_join(worker);
        worker_valid = 0;
        g_labres[g_lab_test].done = 1;
        g_lab_state = 0;
    }
}
void lab_cancel(void) { if (g_lab_state == 1) cancel_flag = 1; }

static LabCfg profs[LAB_PROF_N];
static int prof_used[LAB_PROF_N];

static void prof_path(char *o, int n) { snprintf(o, n, "%s/lab_profiles.txt", prefs_dir()); }
static void prof_write(void)
{
    char p[300];
    prof_path(p, sizeof(p));
    FILE *f = fopen(p, "w");
    if (!f) return;
    for (int i = 4; i < LAB_PROF_N; i++)
        if (prof_used[i]) fprintf(f, "%d|%u|%d|%d|%d|%d|%d\n", i, profs[i].mask, profs[i].dur_s, profs[i].reps, profs[i].mem_mb, profs[i].file_mb, profs[i].stop_fail);
    fclose(f);
}

void lab_prof_init(void)
{
    static const LabCfg pre[4] = {
        { "Test rapide", LT_MASK(LT_CELL) | LT_MASK(LT_MEM) | LT_MASK(LT_FILE), 5, 1, 8, 4, 0 },
        { "Stabilité prolongée", LT_MASK(LT_CELL) | LT_MASK(LT_MEM) | LT_MASK(LT_FILE) | LT_MASK(LT_RSX), 60, 5, 32, 16, 0 },
        { "Stockage", LT_MASK(LT_FILE), 20, 3, 8, 32, 0 },
        { "Graphique", LT_MASK(LT_RSX), 30, 3, 8, 4, 0 },
    };
    for (int i = 0; i < 4; i++) { profs[i] = pre[i]; prof_used[i] = 1; }
    for (int i = 4; i < LAB_PROF_N; i++) { memset(&profs[i], 0, sizeof(profs[i])); snprintf(profs[i].name, sizeof(profs[i].name), "Perso %d", i - 3); prof_used[i] = 0; }
    char p[300], line[160];
    prof_path(p, sizeof(p));
    FILE *f = fopen(p, "r");
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        int i, d, r, m, fl, st; unsigned mask;
        if (sscanf(line, "%d|%u|%d|%d|%d|%d|%d", &i, &mask, &d, &r, &m, &fl, &st) == 7 && i >= 4 && i < LAB_PROF_N) {
            profs[i].mask = mask & 15u; profs[i].dur_s = d > 0 && d <= 600 ? d : 5; profs[i].reps = r > 0 && r <= 50 ? r : 1;
            profs[i].mem_mb = m > 0 && m <= 256 ? m : 8; profs[i].file_mb = fl > 0 && fl <= 256 ? fl : 4; profs[i].stop_fail = st != 0;
            prof_used[i] = 1;
        }
    }
    fclose(f);
}
const LabCfg *lab_prof_get(int i) { return &profs[i]; }
int lab_prof_is_user(int i) { return i >= 4; }
int lab_prof_used(int i) { return prof_used[i]; }
int lab_prof_save(int i, const LabCfg *c)
{
    if (i < 4 || i >= LAB_PROF_N) return -1;
    char nm[24];
    snprintf(nm, sizeof(nm), "Perso %d", i - 3);
    profs[i] = *c;
    snprintf(profs[i].name, sizeof(profs[i].name), "%s", nm);
    prof_used[i] = 1;
    prof_write();
    return 0;
}
int lab_prof_clear(int i)
{
    if (i < 4 || i >= LAB_PROF_N) return -1;
    prof_used[i] = 0;
    prof_write();
    return 0;
}

LabSession g_sess[LAB_SESS_MAX];
int g_nsess;
static void sess_path(char *o, int n) { snprintf(o, n, "%s/lab_sessions.txt", prefs_dir()); }
static void sess_write(void)
{
    char p[300];
    sess_path(p, sizeof(p));
    FILE *f = fopen(p, "w");
    if (!f) return;
    for (int i = 0; i < g_nsess; i++) {
        LabSession *s = &g_sess[i];
        fprintf(f, "%llu|%s|%s|%d|", s->when, s->label, s->profile, s->fails);
        for (int k = 0; k < s->nmet; k++) fprintf(f, "%s=%d;", s->met[k].key, s->met[k].val);
        fprintf(f, "\n");
    }
    fclose(f);
}
void lab_sess_load(void)
{
    char p[300], line[700];
    g_nsess = 0;
    sess_path(p, sizeof(p));
    FILE *f = fopen(p, "r");
    if (!f) return;
    while (g_nsess < LAB_SESS_MAX && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        LabSession *s = &g_sess[g_nsess];
        memset(s, 0, sizeof(*s));
        char *tok[5]; int nt = 0;
        char *q = line;
        while (nt < 4) { tok[nt++] = q; q = strchr(q, '|'); if (!q) break; *q++ = 0; }
        if (nt < 4 || !q) continue;
        s->when = strtoull(tok[0], NULL, 10);
        snprintf(s->label, sizeof(s->label), "%s", tok[1]);
        snprintf(s->profile, sizeof(s->profile), "%s", tok[2]);
        s->fails = atoi(tok[3]);
        char *m = strtok(q, ";");
        while (m && s->nmet < LAB_SMET) {
            char *eq = strchr(m, '=');
            if (eq) { *eq++ = 0; snprintf(s->met[s->nmet].key, 16, "%s", m); s->met[s->nmet].val = atoi(eq); s->nmet++; }
            m = strtok(NULL, ";");
        }
        g_nsess++;
    }
    fclose(f);
}
int lab_sess_add(const LabSession *s)
{
    if (g_nsess < LAB_SESS_MAX) g_nsess++;
    for (int i = g_nsess - 1; i > 0; i--) g_sess[i] = g_sess[i - 1];
    g_sess[0] = *s;
    sess_write();
    return 0;
}
int lab_sess_set_label(int i, const char *label)
{
    if (i < 0 || i >= g_nsess) return -1;
    snprintf(g_sess[i].label, sizeof(g_sess[i].label), "%s", label);
    sess_write();
    return 0;
}

static int sess_get(const LabSession *s, const char *k, int *v)
{
    for (int i = 0; i < s->nmet; i++) if (!strcmp(s->met[i].key, k)) { *v = s->met[i].val; return 1; }
    return 0;
}
void lab_sess_compare(int a, int b)
{
    dv_clear();
    if (a < 0 || b < 0 || a >= g_nsess || b >= g_nsess) { dv_add(DC_ERR, "%s", T("Session introuvable")); return; }
    LabSession *A = &g_sess[a], *B = &g_sess[b];
    dv_add(DC_HEAD, "A : %s · %s · %llu", A->label, A->profile, A->when);
    dv_add(DC_HEAD, "B : %s · %s · %llu", B->label, B->profile, B->when);
    dv_add(DC_NORMAL, T("Échecs : A %d · B %d"), A->fails, B->fails);
    int shown = 0;
    for (int i = 0; i < A->nmet; i++) {
        int vb;
        const char *k = A->met[i].key;
        if (!sess_get(B, k, &vb)) continue;
        int va = A->met[i].val;
        const MetDef *d = mdef(k);
        char sa[40], sb2[40];
        lab_metric_fmt(k, va, sa, sizeof(sa));
        lab_metric_fmt(k, vb, sb2, sizeof(sb2));
        int pct = va ? (int)((long long)(vb - va) * 100 / va) : (vb ? 100 : 0);
        int better = d ? (d->higher ? vb > va : vb < va) : 0;
        int worse = d ? (d->higher ? vb < va : vb > va) : 0;
        int col = pct > -3 && pct < 3 ? DC_NORMAL : better ? DC_OK : worse ? DC_WARN : DC_NORMAL;
        dv_add(col, "%s : %s -> %s (%+d %%)", k, sa, sb2, pct);
        shown++;
    }
    if (!shown) dv_add(DC_MUTED, "%s", T("Aucune mesure en commun : les deux sessions n'ont pas lancé les mêmes tests."));
    dv_add(DC_MUTED, "%s", T("Vert : B meilleur que A. Orange : B moins bon. Écart de moins de 3 % : bruit normal."));
}