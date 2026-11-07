#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sysinfo.h"
#include "sys.h"
#include "dumps.h"

SiMeasure g_si;

void si_measure(void)
{
    unsigned t0 = ticks_ms();
    volatile unsigned sink = 0;
    unsigned x = 12345, ops = 0, t = t0;
    while ((t = ticks_ms()) - t0 < 120) {
        for (int i = 0; i < 20000; i++) { x = x * 1664525u + 1013904223u; x ^= x >> 13; x += (unsigned)i; }
        ops += 20000u * 4u;
    }
    sink = x; (void)sink;
    unsigned dt = t - t0; if (!dt) dt = 1;
    g_si.mops_x10 = (unsigned)(((unsigned long long)ops * 10ull) / ((unsigned long long)dt * 1000ull));
    const unsigned sz = 2u << 20;
    unsigned char *a = malloc(sz), *b = malloc(sz);
    if (a && b) {
        memset(a, 0x5A, sz); memset(b, 0, sz);
        unsigned long long bytes = 0;
        unsigned m0 = ticks_ms(), m1 = m0;
        while ((m1 = ticks_ms()) - m0 < 120) { memcpy(b, a, sz); a[0] ^= b[sz / 2]; bytes += sz; }
        unsigned d = m1 - m0; if (!d) d = 1;
        g_si.mem_mbs = (unsigned)((bytes * 1000ull) / ((unsigned long long)d * 1048576ull));
    } else g_si.mem_mbs = 0;
    free(a); free(b);
    g_si.ms_total = ticks_ms() - t0;
    g_si.done = 1;
}

static unsigned fr[60], fr_n, fr_i, fr_last, fr_max;
void si_frame(unsigned now)
{
    if (fr_last) {
        unsigned d = now - fr_last;
        if (d < 1000) { fr[fr_i] = d; fr_i = (fr_i + 1) % 60; if (fr_n < 60) fr_n++; if (d > fr_max) fr_max = d; }
    }
    fr_last = now;
}
unsigned si_frame_ms_x10(void)
{
    if (!fr_n) return 0;
    unsigned long long s = 0;
    for (unsigned i = 0; i < fr_n; i++) s += fr[i];
    return (unsigned)(s * 10 / fr_n);
}
unsigned si_frame_max_ms(void) { return fr_max; }

SiCap g_sicap[SI_CAP_MAX];
int g_sicap_n;

static void add(const char *name, int st, const char *d)
{
    if (g_sicap_n >= SI_CAP_MAX) return;
    g_sicap[g_sicap_n].name = name; g_sicap[g_sicap_n].state = st;
    snprintf(g_sicap[g_sicap_n].detail, sizeof(g_sicap[0].detail), "%s", d ? d : "");
    g_sicap_n++;
}

void si_detect(int pads, int usb, int sysmode)
{
    g_sicap_n = 0;
    DumpEnv keep = g_denv;          
    dumps_detect();
    DumpEnv d = g_denv;
    g_denv = keep;
    char b[48];
    add("Custom Firmware / HEN", d.hen || d.cobra || d.mamba || sysmode ? 1 : 0,
        d.hen ? "PS3HEN" : d.mamba ? "Mamba" : d.cobra ? "Cobra" : sysmode ? "" : "");
    add("Lecture mémoire LV2", d.lv2_peek ? 1 : 0, "");
    add("Lecture mémoire LV1", d.lv1_peek ? 1 : 0, "");
    add("Flash interne", d.nand == 1 ? 1 : d.nand == 0 ? 1 : 2, d.nand == 1 ? "NAND" : d.nand == 0 ? "NOR" : "");
    int c = 0, r = -1;
    c = sys_temperature(0, &r) == 0; add("Température CPU lisible", c, "");
    c = sys_temperature(1, &r) == 0; add("Température RSX lisible", c, "");
    uint64_t fb;
    add("Disque dur interne", sys_free_space("/dev_hdd0", &fb) == 0, "");
    snprintf(b, sizeof(b), "%d", usb); add("Clés USB branchées", usb > 0, b);
    snprintf(b, sizeof(b), "%d", pads); add("Manettes connectées", pads > 0, b);
    add("Lecteur Blu-ray (dossier présent)", is_dir("/dev_bdvd"), "");
    add("Mémoire système lisible (/dev_flash)", is_dir("/dev_flash"), "");
}