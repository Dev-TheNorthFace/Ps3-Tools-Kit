#include "tools2.h"
#include "sys.h"
#include "prefs.h"
#include "pad.h"
#include "netinfo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

int export_dir(char *out, int n)
{
    char usb[16];
    for (int i = 0; i < 8; i++) {
        snprintf(usb, sizeof(usb), "/dev_usb%03d", i);
        if (is_dir(usb)) {
            snprintf(out, n, "%s/PS3TK_EXPORT", usb);
            if (make_dir(out) == 0) return 0;
        }
    }
    snprintf(out, n, "/dev_hdd0/PS3TK_EXPORT");
    return make_dir(out);
}

static ThSample ring[TH_MAX];
static int ring_head = 0, ring_n = 0;
static unsigned th_last = 0;
static int th_started = 0;

void thermal_tick(int load)
{
    unsigned now = ticks_ms();
    if (th_started && now - th_last < 1000) return;
    th_started = 1;
    th_last = now;
    ThSample s;
    int c;
    s.cpu = sys_temperature(0, &c) == 0 ? c : -1;
    s.rsx = sys_temperature(1, &c) == 0 ? c : -1;
    s.load = load ? 1 : 0;
    ring[ring_head] = s;
    ring_head = (ring_head + 1) % TH_MAX;
    if (ring_n < TH_MAX) ring_n++;
}

int thermal_count(void) { return ring_n; }

ThSample thermal_at(int i)
{
    int start = (ring_head - ring_n + TH_MAX) % TH_MAX;
    return ring[(start + i) % TH_MAX];
}

ThStat thermal_stat(int sensor, int window_s, int sel)
{
    ThStat st = { 0, 0, 0, 0 };
    int from = (window_s > 0 && window_s < ring_n) ? ring_n - window_s : 0;
    long sum = 0;
    for (int i = from; i < ring_n; i++) {
        ThSample s = thermal_at(i);
        int v = sensor ? s.rsx : s.cpu;
        if (v < 0) continue;
        if (sel >= 0 && s.load != sel) continue;
        if (st.n == 0) st.mn = st.mx = v;
        if (v < st.mn) st.mn = v;
        if (v > st.mx) st.mx = v;
        sum += v;
        st.n++;
    }
    if (st.n) st.av = (int)((sum + st.n / 2) / st.n);
    return st;
}

int thermal_export(char *path, int n)
{
    char dir[64];
    if (export_dir(dir, sizeof(dir)) != 0) return -1;
    snprintf(path, n, "%s/temperatures_%llu.csv", dir, wall_time());
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "seconde;cpu_c;rsx_c;charge\n");
    for (int i = 0; i < ring_n; i++) {
        ThSample s = thermal_at(i);
        fprintf(f, "%d;%d;%d;%d\n", i - ring_n + 1, s.cpu, s.rsx, s.load);
    }
    int bad = ferror(f);
    if (fclose(f) != 0) bad = 1;
    return bad ? -1 : 0;
}

DiagItem g_diag[DIAG_N] = {
    { "Firmware lisible" }, { "Processeur : calcul de contrôle" }, { "Mémoire vive : motifs de test" },
    { "Sonde de température CPU" }, { "Sonde de température RSX" },
    { "Disque dur : écriture et relecture" }, { "Disque dur : espace libre" },
    { "Manette connectée" }, { "Écran (test manuel)" }, { "Réseau" },
    { "Port USB" }, { "Lecteur optique" }, { "Audio (test manuel)" },
};
int g_diag_pos = -1;
int g_manual_screen = R_UNTESTED, g_manual_audio = R_UNTESTED;

static void setd(int i, int res, const char *fmt, ...)
{
    g_diag[i].res = res;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_diag[i].detail, sizeof(g_diag[i].detail), T(fmt), ap);
    va_end(ap);
}

void diag_start(void)
{
    for (int i = 0; i < DIAG_N; i++) { g_diag[i].res = R_UNTESTED; g_diag[i].detail[0] = 0; }
    g_diag_pos = 0;
}

static volatile int k_scale = 1;

static uint64_t cpu_kernel(int rounds)
{
    rounds *= k_scale;
    uint64_t x = 0x9E3779B97F4A7C15ULL, acc = 0;
    double f = 1.0;
    for (int i = 0; i < rounds; i++) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        f = f * 1.0000001 + (double)(x & 0xFF) * 1e-9;
        f = sqrt(f * f + 1e-12);
        acc += x * 31 + (uint64_t)(f * 1000.0);
    }
    return acc;
}

static int mem_pattern_test(size_t bytes, int *err_off)
{
    uint64_t *p = (uint64_t *)malloc(bytes);
    if (!p) return -1;
    size_t nw = bytes / 8;
    static const uint64_t pat[4] = { 0, ~0ULL, 0x5555555555555555ULL, 0xAAAAAAAAAAAAAAAAULL };
    int bad = 0;
    for (int k = 0; k < 5 && !bad; k++) {
        for (size_t i = 0; i < nw; i++) p[i] = k < 4 ? pat[k] : ((uint64_t)i * 0x9E3779B97F4A7C15ULL);
        for (size_t i = 0; i < nw; i++) {
            uint64_t want = k < 4 ? pat[k] : ((uint64_t)i * 0x9E3779B97F4A7C15ULL);
            if (p[i] != want) { bad = 1; *err_off = (int)(i * 8); break; }
        }
    }
    free(p);
    return bad;
}

static int hdd_rw_test(char *why, int n)
{
    make_dir("/dev_hdd0/tmp");
    const char *path = "/dev_hdd0/tmp/ps3tk_diag.bin";
    enum { SZ = 1024 * 1024 };
    unsigned char *b = (unsigned char *)malloc(SZ);
    if (!b) { snprintf(why, n, "mémoire insuffisante"); return -1; }
    for (int i = 0; i < SZ; i++) b[i] = (unsigned char)(i * 131 + (i >> 8) * 7);
    FILE *f = fopen(path, "wb");
    if (!f) { free(b); snprintf(why, n, "écriture impossible"); return -1; }
    size_t w = fwrite(b, 1, SZ, f);
    int c = fclose(f);
    if (w != SZ || c != 0) { file_remove(path); free(b); snprintf(why, n, "écriture incomplète"); return -1; }
    unsigned char *r = (unsigned char *)malloc(SZ);
    if (!r) { file_remove(path); free(b); snprintf(why, n, "mémoire insuffisante"); return -1; }
    f = fopen(path, "rb");
    size_t rd = f ? fread(r, 1, SZ, f) : 0;
    if (f) fclose(f);
    int ok = rd == SZ && memcmp(b, r, SZ) == 0;
    free(b); free(r);
    file_remove(path);
    if (!ok) { snprintf(why, n, rd == SZ ? "données relues différentes" : "lecture incomplète"); return -1; }
    return 0;
}

int diag_step(void)
{
    if (g_diag_pos < 0 || g_diag_pos >= DIAG_N) return 0;
    int i = g_diag_pos++;
    char b[64];
    switch (i) {
    case 0: {
        char fw[32];
        sys_firmware(fw, sizeof(fw));
        if (strcmp(fw, "Inconnu")) setd(i, R_PASS, "%s", fw);
        else setd(i, R_FAIL, "version.txt illisible");
        break;
    }
    case 1: {
        uint64_t a = cpu_kernel(300000), c = cpu_kernel(300000), d = cpu_kernel(300000);
        if (a == c && c == d) setd(i, R_PASS, "3 passes identiques");
        else setd(i, R_FAIL, "résultats différents");
        break;
    }
    case 2: {
        int off = 0;
        int r = mem_pattern_test(16u * 1024 * 1024, &off);
        if (r == 0) setd(i, R_PASS, "16 Mo, 5 motifs");
        else if (r < 0) setd(i, R_FAIL, "allocation impossible");
        else setd(i, R_FAIL, "erreur à l'octet %d", off);
        break;
    }
    case 3: case 4: {
        int c, ok = sys_temperature(i - 3, &c) == 0;
        if (!ok) setd(i, R_FAIL, "sonde illisible");
        else if (c >= 95) setd(i, R_FAIL, "%d °C : trop chaud", c);
        else setd(i, R_PASS, "%d °C", c);
        break;
    }
    case 5:
        if (hdd_rw_test(b, sizeof(b)) == 0) setd(i, R_PASS, "1 Mo écrit et relu");
        else setd(i, R_FAIL, "%s", b);
        break;
    case 6: {
        uint64_t fb;
        if (sys_free_space("/dev_hdd0", &fb) == 0) {
            char s[24];
            fmt_size(fb, s, sizeof(s));
            setd(i, R_PASS, "%s", s);
        } else setd(i, R_FAIL, "lecture impossible");
        break;
    }
    case 7:
        if (pad.connected) setd(i, R_PASS, "manette 1");
        else setd(i, R_UNTESTED, "aucune manette");
        break;
    case 8:
        if (g_manual_screen == R_PASS) setd(i, R_PASS, "6 couleurs vues");
        else setd(i, R_UNTESTED, "lance le test d'écran");
        break;
    case 9: {
        NetInfo ni;
        net_info(&ni);
        if (ni.state == NETS_ON) setd(i, R_PASS, "%s", ni.ip);
        else setd(i, R_UNTESTED, "non connecté");
        break;
    }
    case 10: {
        int found = 0, okc = 0;
        usb_poll();
        for (int k = 0; k < USB_N; k++) if (g_usb[k].present) { found++; if (g_usb[k].free_ok) okc++; }
        if (!found) setd(i, R_UNTESTED, "aucune clé branchée");
        else if (okc == found) setd(i, R_PASS, "%d périphérique(s)", found);
        else setd(i, R_FAIL, "lecture impossible");
        break;
    }
    case 11:
        optical_poll();
        if (!g_opt.present) setd(i, R_UNTESTED, "aucun disque");
        else setd(i, R_PASS, "disque détecté");
        break;
    case 12:
        if (g_manual_audio == R_PASS) setd(i, R_PASS, "son émis");
        else if (g_manual_audio == R_FAIL) setd(i, R_FAIL, "port audio refusé");
        else setd(i, R_UNTESTED, "lance le test audio");
        break;
    }
    return g_diag_pos < DIAG_N;
}

void diag_counts(int *pass, int *fail, int *skip)
{
    *pass = *fail = *skip = 0;
    for (int i = 0; i < DIAG_N; i++) {
        if (g_diag[i].res == R_PASS) (*pass)++;
        else if (g_diag[i].res == R_FAIL) (*fail)++;
        else (*skip)++;
    }
}

StressState g_stress;
static uint64_t cpu_ref;
static uint64_t *mem_buf = NULL;
#define MEM_WORDS ((32u * 1024 * 1024) / 8)
#define MEM_SLICE (1u * 1024 * 1024 / 8)
static size_t mem_pos = 0;
static unsigned mem_pass = 0;

static void st_log(const char *fmt, ...)
{
    if (g_stress.nlog >= ST_LOG_N) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_stress.log[g_stress.nlog++], 64, T(fmt), ap);
    va_end(ap);
}

void stress_start(int mask, int dur_s)
{
    memset(&g_stress, 0, sizeof(g_stress));
    g_stress.mask = mask;
    g_stress.dur_s = dur_s;
    g_stress.t0 = ticks_ms();
    g_stress.t_end = g_stress.t0 + (unsigned)dur_s * 1000u;
    int c;
    g_stress.cpu0 = sys_temperature(0, &c) == 0 ? c : -1;
    g_stress.rsx0 = sys_temperature(1, &c) == 0 ? c : -1;
    g_stress.cpu_max = g_stress.cpu0;
    g_stress.rsx_max = g_stress.rsx0;
    cpu_ref = cpu_kernel(20000);
    mem_pos = 0; mem_pass = 0;
    if (mask & ST_MEM) {
        mem_buf = (uint64_t *)malloc(MEM_WORDS * 8);
        if (!mem_buf) { st_log("Mémoire : allocation impossible"); g_stress.mask &= ~ST_MEM; g_stress.errors++; }
    }
    g_stress.running = 1;
}

void stress_stop(int why)
{
    if (!g_stress.running) return;
    g_stress.running = 0;
    g_stress.done = 1;
    g_stress.aborted = why;
    g_stress.t_end = ticks_ms();
    if (mem_buf) { free(mem_buf); mem_buf = NULL; }
    g_last_st_s = (int)((g_stress.t_end - g_stress.t0) / 1000);
    g_last_st_err = g_stress.errors;
    g_last_st_rise = (g_stress.cpu0 >= 0 && g_stress.cpu_max >= 0) ? g_stress.cpu_max - g_stress.cpu0 : -1;
}

int stress_elapsed_s(void)
{
    unsigned now = g_stress.running ? ticks_ms() : g_stress.t_end;
    return (int)((now - g_stress.t0) / 1000);
}

static uint64_t mem_pat(unsigned pass, size_t i)
{
    switch (pass & 3) {
    case 0: return 0x5555555555555555ULL;
    case 1: return 0xAAAAAAAAAAAAAAAAULL;
    case 2: return (uint64_t)i * 0x9E3779B97F4A7C15ULL;
    default: return ~((uint64_t)i * 0x9E3779B97F4A7C15ULL);
    }
}

void stress_tick(void)
{
    if (!g_stress.running) return;
    unsigned now = ticks_ms();
    if (ring_n) {
        ThSample s = thermal_at(ring_n - 1);
        if (s.cpu > g_stress.cpu_max) g_stress.cpu_max = s.cpu;
        if (s.rsx > g_stress.rsx_max) g_stress.rsx_max = s.rsx;
        if (s.cpu >= STRESS_CPU_LIMIT || s.rsx >= STRESS_RSX_LIMIT) {
            st_log("Arrêt de sécurité : %d °C (CPU) / %d °C (RSX)", s.cpu, s.rsx);
            stress_stop(2);
            return;
        }
    }
    if ((int)(now - g_stress.t_end) >= 0) { stress_stop(0); return; }

    unsigned budget_end = now + 14;
    if (g_stress.mask & ST_CPU) {
        do {
            uint64_t r = cpu_kernel(20000);
            g_stress.cpu_iters++;
            if (r != cpu_ref) {
                g_stress.cpu_err++; g_stress.errors++;
                st_log("Processeur : résultat différent (passe %u)", (unsigned)g_stress.cpu_iters);
                cpu_ref = r;
            }
        } while (ticks_ms() < budget_end);
    }
    if ((g_stress.mask & ST_MEM) && mem_buf) {
        size_t end = mem_pos + MEM_SLICE;
        if (end > MEM_WORDS) end = MEM_WORDS;
        for (size_t i = mem_pos; i < end; i++) mem_buf[i] = mem_pat(mem_pass, i);
        for (size_t i = mem_pos; i < end; i++) {
            if (mem_buf[i] != mem_pat(mem_pass, i)) {
                g_stress.mem_err++; g_stress.errors++;
                st_log("Mémoire : erreur à l'octet %u", (unsigned)(i * 8));
                break;
            }
        }
        g_stress.mem_mb += (end - mem_pos) * 8 / (1024 * 1024);
        mem_pos = end;
        if (mem_pos >= MEM_WORDS) { mem_pos = 0; mem_pass++; }
    }
    g_stress.frames++;
}