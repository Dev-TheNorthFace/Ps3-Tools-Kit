#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>
#include <malloc.h>
#include "devtools.h"
#include "sys.h"
#include "prefs.h"
#include "tools2.h"
#include "sysinfo.h"
#include "sha256.h"
#include "pad.h"
#include "screen.h"
#include "netinfo.h"

DvLine *g_dvl;
int g_dvn;
int g_dv_api_done, g_dv_api_fail;
const char *g_dv_ver = "2.0";

int dv_init(void)
{
    if (!g_dvl) g_dvl = malloc(sizeof(DvLine) * DV_LINES);
    return g_dvl ? 0 : -1;
}
void dv_clear(void) { g_dvn = 0; }
void dv_add(int col, const char *fmt, ...)
{
    if (!g_dvl || g_dvn >= DV_LINES) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_dvl[g_dvn].t, sizeof(g_dvl[0].t), fmt, ap);
    va_end(ap);
    g_dvl[g_dvn].c = (unsigned char)col;
    g_dvn++;
}

int dv_write_lines(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    for (int i = 0; i < g_dvn; i++) fprintf(f, "%s\n", g_dvl[i].t);
    return fclose(f) == 0 ? 0 : -1;
}

static int rd(FILE *f, unsigned long long off, void *b, int n)
{
    if (fseek(f, (long)off, SEEK_SET) != 0) return -1;
    return fread(b, 1, n, f) == (size_t)n ? 0 : -1;
}
static unsigned long long be(const unsigned char *p, int n, int big)
{
    unsigned long long v = 0;
    for (int i = 0; i < n; i++) v = (v << 8) | p[big ? i : n - 1 - i];
    return v;
}

static const char *ptype(unsigned t)
{
    switch (t) {
    case 0: return "NULL"; case 1: return "LOAD"; case 2: return "DYNAMIC"; case 3: return "INTERP";
    case 4: return "NOTE"; case 6: return "PHDR"; case 7: return "TLS";
    case 0x60000001u: return "PROC_PARAM"; case 0x60000002u: return "PRX_PARAM";
    case 0x700000a4u: return "SCE_PPURELA"; case 0x700000a8u: return "SCE_SPURELA";
    default: return "?";
    }
}
static const char *shtype(unsigned t)
{
    switch (t) {
    case 0: return "NULL"; case 1: return "PROGBITS"; case 2: return "SYMTAB"; case 3: return "STRTAB";
    case 4: return "RELA"; case 8: return "NOBITS"; case 9: return "REL"; default: return "?";
    }
}

static int elf_at(FILE *f, unsigned long long eo, long long fsize, int is_self, unsigned long long phoff_abs, unsigned long long shoff_abs,
                  const unsigned char *seg_enc /* NULL ou tableau d'octets (n entrées de 0x20) */, int seg_n)
{
    unsigned char h[64];
    if (rd(f, eo, h, 64) != 0 || memcmp(h, "\x7f" "ELF", 4) != 0) { dv_add(DC_ERR, T("En-tête ELF introuvable à l'offset 0x%llX"), eo); return -1; }
    int cls = h[4], big = h[5] == 2;
    dv_add(DC_HEAD, "%s", T("En-tête ELF"));
    dv_add(DC_NORMAL, T("Classe : %s · ordre des octets : %s"), cls == 2 ? "64 bits" : cls == 1 ? "32 bits" : "?", big ? "big-endian" : "little-endian");
    unsigned mach = (unsigned)be(h + 18, 2, big), type = (unsigned)be(h + 16, 2, big);
    const char *an = mach == 0x15 ? "PowerPC 64 (PPU)" : mach == 0x14 ? "PowerPC 32" : mach == 0x17 ? "SPU (Cell)" : "?";
    dv_add(DC_NORMAL, T("Architecture : %s (code %u)"), an, mach);
    dv_add(DC_NORMAL, T("Type : %s (code 0x%X)"), type == 2 ? "EXEC" : type == 3 ? "DYN" : type == 0xFFA4 ? "PRX (SCE)" : type == 1 ? "REL" : "?", type);
    if (cls != 2) { dv_add(DC_WARN, "%s", T("ELF 32 bits : seul l'en-tête général est détaillé")); return 0; }
    unsigned long long entry = be(h + 24, 8, big), phoff = be(h + 32, 8, big), shoff = be(h + 40, 8, big);
    unsigned phes = (unsigned)be(h + 54, 2, big), phn = (unsigned)be(h + 56, 2, big);
    unsigned shes = (unsigned)be(h + 58, 2, big), shn = (unsigned)be(h + 60, 2, big), shstr = (unsigned)be(h + 62, 2, big);
    dv_add(DC_NORMAL, T("Point d'entrée : 0x%llX"), entry);
    if (type == 0xFFA4 || mach == 0x15) dv_add(DC_MUTED, "%s", T("Sur PS3 le point d'entrée est un descripteur de fonction (adresse du code + TOC)."));
    dv_add(DC_NORMAL, T("Segments : %u · sections : %u"), phn, shn);
    if (is_self) { phoff = phoff_abs; shoff = shoff_abs; }
    if (phn > 64) phn = 64;
    if (phes < 56) phn = 0;
    if (phn) dv_add(DC_HEAD, "%s", T("Segments (programme)"));
    for (unsigned i = 0; i < phn; i++) {
        unsigned char p[56];
        if (rd(f, phoff + (unsigned long long)i * phes, p, 56) != 0) { dv_add(DC_ERR, T("Segment %u illisible"), i); break; }
        unsigned pt = (unsigned)be(p, 4, big), fl = (unsigned)be(p + 4, 4, big);
        unsigned long long off = be(p + 8, 8, big), va = be(p + 16, 8, big), fs = be(p + 32, 8, big), ms = be(p + 40, 8, big);
        char perm[4] = { fl & 4 ? 'R' : '-', fl & 2 ? 'W' : '-', fl & 1 ? 'X' : '-', 0 };
        dv_add(DC_NORMAL, "#%u %-11s %s vaddr %llX off %llX fs %llX ms %llX", i, ptype(pt), perm, va, off, fs, ms);
        if (is_self && seg_enc && (int)i < seg_n) {
            const unsigned char *s = seg_enc + i * 0x20;     
            unsigned comp = (unsigned)be(s + 16, 4, 1);
            unsigned long long enc = be(s + 24, 8, 1);
            dv_add(enc ? DC_WARN : DC_OK, T("    données : %s · %s (champ=%llu)"), comp == 2 ? T("compressées (zlib)") : T("non compressées"),
                   enc == 0 ? T("en clair") : enc == 1 ? T("CHIFFRÉES") : T("chiffrement demandé"), enc);
        }
    }
    if (is_self) {
        dv_add(DC_MUTED, "%s", T("Contenu chiffré : seuls les en-têtes sont lus, le code reste illisible tant que le SELF n'est pas en clair."));
        if (shn) dv_add(DC_MUTED, "%s", T("Noms de sections inaccessibles dans un SELF (les données sont dans les segments)."));
    }
    if (shn > 80) shn = 80;
    if (shn && shes >= 64) {
        dv_add(DC_HEAD, "%s", T("Sections"));
        char *names = NULL; unsigned long long nsz = 0;
        if (!is_self && shstr < shn) {                       
            unsigned char s[64];
            if (rd(f, shoff + (unsigned long long)shstr * shes, s, 64) == 0) {
                unsigned long long no = be(s + 24, 8, big); nsz = be(s + 32, 8, big);
                if (nsz > 0 && nsz < 65536 && (long long)(no + nsz) <= fsize && (names = malloc((size_t)nsz + 1))) {
                    if (rd(f, no, names, (int)nsz) != 0) { free(names); names = NULL; } else names[nsz] = 0;
                }
            }
        }
        for (unsigned i = 0; i < shn; i++) {
            unsigned char s[64];
            if (rd(f, shoff + (unsigned long long)i * shes, s, 64) != 0) { dv_add(DC_ERR, T("Section %u illisible"), i); break; }
            unsigned nm = (unsigned)be(s, 4, big), st = (unsigned)be(s + 4, 4, big);
            unsigned long long addr = be(s + 16, 8, big), off = be(s + 24, 8, big), sz = be(s + 32, 8, big);
            const char *name = (names && nm < nsz) ? names + nm : "";
            dv_add(DC_NORMAL, "#%u %-14s %-8s addr %llX off %llX size %llX", i, name, shtype(st), addr, off, sz);
        }
        free(names);
    } else if (!is_self) dv_add(DC_MUTED, "%s", T("Aucune table de sections (normal pour un exécutable strippé)."));
    return 0;
}

int dv_elf(const char *path)
{
    dv_clear();
    FILE *f = fopen(path, "rb");
    if (!f) { dv_add(DC_ERR, "%s", T("Impossible d'ouvrir ce fichier")); return -1; }
    fseek(f, 0, SEEK_END);
    long long sz = ftell(f);
    unsigned char h[0x70];
    int r = -1;
    dv_add(DC_MUTED, "%s", path);
    if (rd(f, 0, h, 4) != 0) { dv_add(DC_ERR, "%s", T("Fichier trop petit")); fclose(f); return -1; }
    if (!memcmp(h, "SCE\0", 4)) {
        if (sz < 0x70 || rd(f, 0, h, 0x70) != 0) { dv_add(DC_ERR, "%s", T("Fichier trop petit")); fclose(f); return -1; }
        unsigned attr = (unsigned)be(h + 8, 2, 1);
        dv_add(DC_HEAD, "%s", T("En-tête SELF"));
        dv_add(DC_NORMAL, T("Version : %u · attributs : 0x%X"), (unsigned)be(h + 4, 4, 1), attr);
        if (attr & 0x8000) dv_add(DC_WARN, "%s", T("fSELF : signature factice, généralement non chiffré (homebrew)"));
        else dv_add(DC_NORMAL, "%s", T("SELF signé (officiel ou re-signé)"));
        unsigned long long dlen = be(h + 0x18, 8, 1);
        dv_add(DC_NORMAL, T("Taille de l'en-tête : 0x%llX · données : 0x%llX"), be(h + 0x10, 8, 1), dlen);
        unsigned long long appo = be(h + 0x28, 8, 1), elfo = be(h + 0x30, 8, 1), pho = be(h + 0x38, 8, 1),
                           sho = be(h + 0x40, 8, 1), sgo = be(h + 0x48, 8, 1);
        unsigned char ai[0x20];
        if (rd(f, appo, ai, 0x20) == 0) {
            unsigned st = (unsigned)be(ai + 12, 4, 1);
            const char *sn = st == 4 ? "APP" : st == 5 ? "ISO (module SPU)" : st == 8 ? "NPDRM" : st == 3 ? "LV2" : st == 2 ? "LV1" : st == 1 ? "LV0" : st == 6 ? "LDR" : "?";
            dv_add(DC_NORMAL, T("Type : %s (code %u) · identifiant d'autorité : %016llX"), sn, st, be(ai, 8, 1));
            dv_add(DC_NORMAL, T("Vendeur : 0x%X · version SCE : %016llX"), (unsigned)be(ai + 8, 4, 1), be(ai + 16, 8, 1));
        } else dv_add(DC_WARN, "%s", T("Bloc d'identification illisible"));
        unsigned char eh[64]; int phn = 0;
        if (rd(f, elfo, eh, 64) == 0 && !memcmp(eh, "\x7f" "ELF", 4)) phn = (int)be(eh + 56, 2, eh[5] == 2);
        if (phn > 64) phn = 64;
        unsigned char *seg = phn ? malloc(phn * 0x20) : NULL;
        if (seg && rd(f, sgo, seg, phn * 0x20) != 0) { free(seg); seg = NULL; }
        r = elf_at(f, elfo, sz, 1, pho, sho, seg, seg ? phn : 0);
        free(seg);
    } else if (!memcmp(h, "\x7f" "ELF", 4)) {
        r = elf_at(f, 0, sz, 0, 0, 0, NULL, 0);
    } else dv_add(DC_ERR, "%s", T("Ni ELF ni SELF : signature inconnue"));
    fclose(f);
    return r;
}

static unsigned le(const unsigned char *p, int n) { unsigned v = 0; for (int i = n - 1; i >= 0; i--) v = (v << 8) | p[i]; return v; }

int dv_sfo(const char *path)
{
    dv_clear();
    FILE *f = fopen(path, "rb");
    if (!f) { dv_add(DC_ERR, "%s", T("Impossible d'ouvrir ce fichier")); return -1; }
    unsigned char *b = malloc(65536);
    if (!b) { fclose(f); return -1; }
    size_t len = fread(b, 1, 65536, f);
    fclose(f);
    int ret = -1;
    dv_add(DC_MUTED, "%s", path);
    if (len < 20 || memcmp(b, "\0PSF", 4) != 0) { dv_add(DC_ERR, "%s", T("Ce n'est pas un PARAM.SFO")); free(b); return -1; }
    unsigned kt = le(b + 8, 4), dt = le(b + 12, 4), cnt = le(b + 16, 4);
    dv_add(DC_NORMAL, T("Version du format : %u.%u · %u paramètre(s)"), b[5], b[4], cnt);
    char tid[32] = "", ver[32] = "", cat[16] = "", sysver[32] = "";
    for (unsigned i = 0; i < cnt && i < 64 && 20 + (i + 1) * 16 <= len; i++) {
        const unsigned char *e = b + 20 + i * 16;
        unsigned ko = kt + le(e, 2), fmt = le(e + 2, 2), dl = le(e + 4, 4), doff = dt + le(e + 12, 4);
        if (ko >= len || doff >= len) continue;
        char key[40];
        snprintf(key, sizeof(key), "%.39s", (const char *)b + ko);
        if (fmt == 0x0404) {
            unsigned v = doff + 4 <= len ? le(b + doff, 4) : 0;
            dv_add(DC_NORMAL, "%s = %u (0x%X)", key, v, v);
        } else {
            char val[96];
            unsigned m = dl < sizeof(val) - 1 ? dl : sizeof(val) - 1;
            if (doff + m > len) m = (unsigned)(len - doff);
            memcpy(val, b + doff, m); val[m] = 0;
            dv_add(DC_NORMAL, "%s = %s", key, val);
            if (!strcmp(key, "TITLE_ID")) snprintf(tid, sizeof(tid), "%s", val);
            if (!strcmp(key, "APP_VER") || !strcmp(key, "VERSION")) snprintf(ver, sizeof(ver), "%s", val);
            if (!strcmp(key, "CATEGORY")) snprintf(cat, sizeof(cat), "%s", val);
            if (!strcmp(key, "PS3_SYSTEM_VER")) snprintf(sysver, sizeof(sysver), "%s", val);
        }
    }
    if (g_dvn < DV_LINES - 8) {
        dv_add(DC_HEAD, "%s", T("Résumé"));
        dv_add(tid[0] ? DC_OK : DC_WARN, T("Title ID : %s"), tid[0] ? tid : "—");
        dv_add(DC_NORMAL, T("Version : %s"), ver[0] ? ver : "—");
        const char *cn = !strcmp(cat, "HG") ? "HG (jeu installé sur le disque dur)" : !strcmp(cat, "DG") ? "DG (jeu disque)" :
                         !strcmp(cat, "GD") ? "GD (données de jeu)" : !strcmp(cat, "AP") ? "AP (application)" : !strcmp(cat, "HM") ? "HM (homebrew / mode maison)" :
                         !strcmp(cat, "SD") ? "SD (données de sauvegarde)" : !strcmp(cat, "AA") ? "AA (autre)" : cat;
        dv_add(DC_NORMAL, T("Catégorie : %s"), cat[0] ? cn : "—");
        if (sysver[0]) {
            unsigned a = 0, c = 0;
            sscanf(sysver, "%u.%u", &a, &c);
            dv_add(DC_NORMAL, T("Firmware minimum déclaré : %u.%02u (champ brut %s)"), a, c / 100, sysver);
        } else dv_add(DC_MUTED, "%s", T("Aucun firmware minimum déclaré (PS3_SYSTEM_VER absent)"));
    }
    ret = 0;
    free(b);
    return ret;
}

static char (*raw)[110];
static int raw_n;

int dv_is_error_line(const char *s)
{
    static const char *k[] = { "error", "erreur", "fail", "échec", "echec", "fatal", "exception", "assert", "panic", "0x8001", "0x8002", "0x8010", "crash", NULL };
    char low[110];
    int i = 0;
    for (; s[i] && i < 109; i++) low[i] = (s[i] >= 'A' && s[i] <= 'Z') ? s[i] + 32 : s[i];
    low[i] = 0;
    for (int j = 0; k[j]; j++) if (strstr(low, k[j])) return 1;
    return 0;
}

void dv_log_filter(int errors_only)
{
    dv_clear();
    for (int i = 0; i < raw_n; i++) {
        int e = dv_is_error_line(raw[i]);
        if (errors_only && !e) continue;
        dv_add(e ? DC_ERR : DC_NORMAL, "%s", raw[i]);
    }
    if (!g_dvn) dv_add(DC_MUTED, "%s", T(errors_only ? "Aucune ligne d'erreur dans ce journal." : "Journal vide."));
}

int dv_logfile(const char *path)
{
    if (!raw) raw = malloc(sizeof(raw[0]) * DV_LINES);
    if (!raw) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    raw_n = 0;
    char line[512];
    int total = 0;
    static int first;
    (void)first;
    int head = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        snprintf(raw[head], 110, "%s", line);
        head = (head + 1) % DV_LINES;
        total++;
    }
    fclose(f);
    raw_n = total < DV_LINES ? total : DV_LINES;
    if (total > DV_LINES) {                     
        static char tmp[DV_LINES][110];
        for (int i = 0; i < DV_LINES; i++) memcpy(tmp[i], raw[(head + i) % DV_LINES], 110);
        memcpy(raw, tmp, sizeof(tmp));
    }
    return 0;
}

void dv_journal(int errors_only)
{
    if (!raw) raw = malloc(sizeof(raw[0]) * DV_LINES);
    if (!raw) return;
    raw_n = 0;
    for (int i = g_log_n - 1; i >= 0 && raw_n < DV_LINES; i--) {
        char a[40], b[80];
        log_time(&g_log[i], a, sizeof(a));
        log_format(&g_log[i], b, sizeof(b));
        snprintf(raw[raw_n++], 110, "%s  %s", a, b);
    }
    dv_log_filter(errors_only);
}

void dv_env(int pads, int usb, int sysmode, const char *fw, const char *res)
{
    dv_clear();
    dv_add(DC_HEAD, "%s", T("Versions"));
    dv_add(DC_NORMAL, T("Firmware : %s"), fw);
    dv_add(DC_NORMAL, T("Système : %s"), T(mode_name(sysmode)));
    dv_add(DC_NORMAL, T("Application : v%s"), g_dv_ver);
    dv_add(DC_NORMAL, T("Résolution : %s"), res);
    si_detect(pads, usb, sysmode);
    dv_add(DC_HEAD, "%s", T("Capacités détectées"));
    for (int i = 0; i < g_sicap_n; i++)
        dv_add(g_sicap[i].state == 1 ? DC_OK : DC_MUTED, "%-38s %s %s", T(g_sicap[i].name),
               g_sicap[i].state == 1 ? T("oui") : g_sicap[i].state == 0 ? T("non") : T("inconnu"), g_sicap[i].detail);
    dv_add(DC_HEAD, "%s", T("Ce que cela permet"));
    int lv1 = 0, lv2 = 0;
    for (int i = 0; i < g_sicap_n; i++) {
        if (!strcmp(g_sicap[i].name, "Lecture mémoire LV1")) lv1 = g_sicap[i].state == 1;
        if (!strcmp(g_sicap[i].name, "Lecture mémoire LV2")) lv2 = g_sicap[i].state == 1;
    }
    dv_add(lv2 ? DC_OK : DC_WARN, "%s", T(lv2 ? "Dumps LV2 possibles" : "Dumps LV2 indisponibles (pas de lecture LV2)"));
    dv_add(lv1 ? DC_OK : DC_WARN, "%s", T(lv1 ? "Dumps LV1 possibles" : "Dumps LV1 indisponibles (pas de lecture LV1)"));
    dv_add(sysmode == MODE_HEN ? DC_WARN : DC_NORMAL, "%s",
           T(sysmode == MODE_HEN ? "PS3HEN : certains appels système de bas niveau peuvent être refusés" : "Les appels système de bas niveau dépendent du CFW"));
    dv_add(DC_MUTED, "%s", T("Rien n'est écrit en mémoire flash depuis cet écran."));
}

static void api_row(int ok, const char *name, const char *params, int code)
{
    g_dv_api_done++;
    if (!ok) g_dv_api_fail++;
    dv_add(ok ? DC_OK : DC_ERR, "[%s] %-14s %s", ok ? "OK" : T("ÉCHEC"), name, params);
    dv_add(DC_MUTED, T("      code : %d"), code);
}

void dv_api(int pads)
{
    dv_clear();
    g_dv_api_done = g_dv_api_fail = 0;
    const char *dir = "/dev_hdd0/tmp/ps3tk_apitest";
    char fn[160], fn2[160];
    snprintf(fn, sizeof(fn), "%s/a.bin", dir);
    snprintf(fn2, sizeof(fn2), "%s/b.bin", dir);
    dv_add(DC_HEAD, "%s", T("Fichiers"));
    errno = 0;
    int r = make_dir(dir);
    api_row(r == 0, "mkdir", dir, r == 0 ? 0 : errno);
    static unsigned char buf[4096], back[4096];
    for (int i = 0; i < 4096; i++) buf[i] = (unsigned char)(i * 7 + 3);
    errno = 0;
    FILE *f = fopen(fn, "wb");
    api_row(f != NULL, "fopen(wb)", fn, f ? 0 : errno);
    size_t w = f ? fwrite(buf, 1, 4096, f) : 0;
    char p[64]; snprintf(p, sizeof(p), "4096 o -> %u o", (unsigned)w);
    api_row(w == 4096, "fwrite", p, w == 4096 ? 0 : errno);
    if (f) fclose(f);
    f = fopen(fn, "rb");
    size_t rr = f ? fread(back, 1, 4096, f) : 0;
    if (f) fclose(f);
    snprintf(p, sizeof(p), "4096 o -> %u o", (unsigned)rr);
    api_row(rr == 4096 && !memcmp(buf, back, 4096), "fread+cmp", p, rr == 4096 ? 0 : errno);
    r = file_rename(fn, fn2);
    api_row(r == 0, "rename", "a.bin -> b.bin", r);
    r = file_remove(fn2);
    api_row(r == 0, "unlink", "b.bin", r);
    r = dir_remove(dir);
    api_row(r == 0, "rmdir", dir, r);

    dv_add(DC_HEAD, "%s", T("Réseau"));
    NetInfo ni; net_info(&ni);
    snprintf(p, sizeof(p), "%s", ni.ip[0] ? ni.ip : "—");
    api_row(ni.state == NETS_ON, "net_info", p, ni.state);

    dv_add(DC_HEAD, "%s", T("Manettes"));
    snprintf(p, sizeof(p), "pressure=%d sixaxis=%d", pad.has_pressure, pad.has_sensor);
    api_row(pads > 0, "padGetInfo", p, pads > 0 ? 0 : -1);

    dv_add(DC_HEAD, "%s", T("Audio"));
    int a = aud_open();
    snprintf(p, sizeof(p), "8ch=%d", g_aud.ch8);
    api_row(a == 0, "audioInit/Open", p, a == 0 ? 0 : g_aud.err);
    aud_close();

    dv_add(DC_HEAD, "%s", T("Affichage"));
    snprintf(p, sizeof(p), "%d x %d", screen_width(), screen_height());
    api_row(screen_width() > 0 && screen_height() > 0, "videoGetState", p, 0);

    dv_add(DC_HEAD, "%s", T("Système"));
    char fw[32]; sys_firmware(fw, sizeof(fw));
    api_row(strcmp(fw, "Inconnu") != 0, "firmware", fw, 0);
    int tmp = 0;
    r = sys_temperature(0, &tmp);
    snprintf(p, sizeof(p), "CPU %d C", tmp);
    api_row(r == 0, "syscall 383", p, r);
    uint64_t fb = 0;
    r = sys_free_space("/dev_hdd0", &fb);
    snprintf(p, sizeof(p), "/dev_hdd0 : %llu Mo", (unsigned long long)(fb >> 20));
    api_row(r == 0, "statvfs", p, r);

    dv_add(DC_HEAD, "%s", T("Bilan"));
    dv_add(g_dv_api_fail ? DC_WARN : DC_OK, T("%d test(s), %d échec(s)"), g_dv_api_done, g_dv_api_fail);
    dv_add(DC_MUTED, "%s", T("Un échec n'est pas forcément un défaut : pas de manette, de réseau ou de CFW = résultat normal."));
}

void dv_perf(void)
{
    dv_clear();
    dv_add(DC_HEAD, "%s", T("Cette application"));
    unsigned f10 = si_frame_ms_x10();
    if (f10) dv_add(DC_NORMAL, T("Temps par image : moyenne %u,%u ms · pire %u ms"), f10 / 10, f10 % 10, si_frame_max_ms());
    else dv_add(DC_MUTED, "%s", T("Temps par image : pas encore de mesure"));
    dv_add(DC_NORMAL, T("Temps depuis le lancement : %u s"), ticks_ms() / 1000);
    struct mallinfo mi = mallinfo();
    dv_add(DC_NORMAL, T("Mémoire utilisée par le tas : %u Ko (obtenue du système : %u Ko)"), (unsigned)mi.uordblks / 1024, (unsigned)mi.arena / 1024);

    dv_add(DC_HEAD, "%s", T("Temps d'exécution d'opérations"));
    unsigned t0 = ticks_ms();
    unsigned char *b = malloc(1 << 20);
    if (b) {
        memset(b, 0x42, 1 << 20);
        t0 = ticks_ms();
        Sha256 c; sha256_init(&c); sha256_update(&c, b, 1 << 20);
        unsigned char d[32]; sha256_final(&c, d);
        dv_add(DC_NORMAL, T("SHA-256 de 1 Mo : %u ms"), ticks_ms() - t0);
        free(b);
    }
    uint64_t fb;
    t0 = ticks_ms();
    int r = sys_free_space("/dev_hdd0", &fb);
    dv_add(r == 0 ? DC_NORMAL : DC_MUTED, T("Lecture de l'espace libre : %u ms"), ticks_ms() - t0);
    t0 = ticks_ms();
    int n = 0;
    struct cnt { int n; } cc = { 0 };
    (void)cc;
    n = is_dir("/dev_hdd0") ? 1 : 0;
    dv_add(DC_NORMAL, T("Test d'existence d'un dossier : %u ms"), ticks_ms() - t0);
    (void)n;

    dv_add(DC_HEAD, "%s", T("Homebrew instrumenté"));
    FILE *f = fopen("/dev_hdd0/tmp/ps3tk_perf.txt", "r");
    if (!f) {
        dv_add(DC_MUTED, "%s", T("Aucun fichier /dev_hdd0/tmp/ps3tk_perf.txt."));
        dv_add(DC_MUTED, "%s", T("Un homebrew peut y écrire des lignes « nom;valeur;unité » (ex. frame;16.7;ms)."));
    } else {
        char line[160];
        int k = 0;
        while (fgets(line, sizeof(line), f) && k < 60) {
            line[strcspn(line, "\r\n")] = 0;
            if (!line[0]) continue;
            dv_add(DC_NORMAL, "%s", line);
            k++;
        }
        fclose(f);
        dv_add(DC_MUTED, T("%d ligne(s) lue(s)"), k);
    }
}

int dv_report(char *outdir, int n, int pads, int usb, int sysmode, const char *fw, const char *res)
{
    char base[64];
    if (export_dir(base, sizeof(base)) != 0) return -1;
    snprintf(outdir, n, "%s/DEBUG_%llu", base, wall_time());
    if (make_dir(outdir) != 0) return -1;
    char p[300];
    int bad = 0;
    dv_env(pads, usb, sysmode, fw, res);
    snprintf(p, sizeof(p), "%s/environnement.txt", outdir); bad |= dv_write_lines(p);
    dv_journal(0);
    snprintf(p, sizeof(p), "%s/journal.txt", outdir); bad |= dv_write_lines(p);
    dv_journal(1);
    snprintf(p, sizeof(p), "%s/erreurs.txt", outdir); bad |= dv_write_lines(p);
    dv_api(pads);
    snprintf(p, sizeof(p), "%s/api.txt", outdir); bad |= dv_write_lines(p);
    dv_perf();
    snprintf(p, sizeof(p), "%s/performances.txt", outdir); bad |= dv_write_lines(p);
    dv_clear();
    dv_add(DC_NORMAL, "PS3 Tools Kit - rapport de debogage");
    dv_add(DC_NORMAL, "Date (secondes depuis 1970) : %llu", wall_time());
    dv_add(DC_NORMAL, "Ce dossier ne contient ni IDPS, ni PSID, ni cles, ni dump.");
    snprintf(p, sizeof(p), "%s/LISEZMOI.txt", outdir); bad |= dv_write_lines(p);
    return bad ? -1 : 0;
}