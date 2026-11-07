#include "tools2.h"
#include "sys.h"
#include "prefs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned g_rep_tags = 0;
int g_last_hdd_w = -1, g_last_hdd_r = -1, g_last_usb_w = -1, g_last_usb_r = -1;
int g_last_opt = -1;
int g_last_st_s = -1, g_last_st_err = 0, g_last_st_rise = -1;

const char *g_rep_tag_names[REP_TAGS] = {
    "Pâte thermique refaite", "Ventilateur et radiateur nettoyés", "Disque dur remplacé",
    "Lecteur optique remplacé", "Reflow / rebillage", "Condensateurs remplacés",
    "Pile CMOS remplacée", "Alimentation remplacée", "Firmware mis à jour", "Nettoyage complet",
};

void report_capture(Report *r)
{
    memset(r, 0, sizeof(*r));
    r->valid = 1;
    r->when = wall_time();
    sys_firmware(r->fw, sizeof(r->fw));
    r->mode = sys_detect_mode();
    diag_counts(&r->d_pass, &r->d_fail, &r->d_skip);
    if (g_diag_pos < DIAG_N) { r->d_pass = r->d_fail = 0; r->d_skip = DIAG_N; } 
    ThStat a = thermal_stat(0, 0, 0), b = thermal_stat(1, 0, 0);
    ThStat c = thermal_stat(0, 0, 1), d = thermal_stat(1, 0, 1);
    ThStat e = thermal_stat(0, 0, -1), f = thermal_stat(1, 0, -1);
    r->rest_cpu = a.n ? a.av : -1;  r->rest_rsx = b.n ? b.av : -1;
    r->load_cpu = c.n ? c.av : -1;  r->load_rsx = d.n ? d.av : -1;
    r->max_cpu = e.n ? e.mx : -1;   r->max_rsx = f.n ? f.mx : -1;
    r->st_s = g_last_st_s; r->st_err = g_last_st_err; r->st_cpu_rise = g_last_st_rise;
    r->hdd_w = g_last_hdd_w; r->hdd_r = g_last_hdd_r; r->usb_w = g_last_usb_w; r->usb_r = g_last_usb_r;
    uint64_t fb;
    r->hdd_free_mb = sys_free_space("/dev_hdd0", &fb) == 0 ? (int)(fb / (1024 * 1024)) : -1;
    r->opt = g_last_opt;
    r->tags = g_rep_tags;
}

static const char *slot_name(int slot) { return slot ? "rapport_apres.txt" : "rapport_avant.txt"; }

int report_save(int slot, const Report *r)
{
    char p[300];
    snprintf(p, sizeof(p), "%s/%s", prefs_dir(), slot_name(slot));
    FILE *f = fopen(p, "w");
    if (!f) return -1;
    fprintf(f, "when=%llu\nfw=%s\nmode=%d\ndiag=%d,%d,%d\nrest=%d,%d\nload=%d,%d\nmax=%d,%d\n"
               "stress=%d,%d,%d\nhdd=%d,%d\nusb=%d,%d\nfree=%d\nopt=%d\ntags=%u\n",
            r->when, r->fw, r->mode, r->d_pass, r->d_fail, r->d_skip, r->rest_cpu, r->rest_rsx,
            r->load_cpu, r->load_rsx, r->max_cpu, r->max_rsx, r->st_s, r->st_err, r->st_cpu_rise,
            r->hdd_w, r->hdd_r, r->usb_w, r->usb_r, r->hdd_free_mb, r->opt, r->tags);
    int bad = ferror(f);
    if (fclose(f) != 0) bad = 1;
    return bad ? -1 : 0;
}

int report_load(int slot, Report *r)
{
    char p[300], line[160];
    memset(r, 0, sizeof(*r));
    snprintf(p, sizeof(p), "%s/%s", prefs_dir(), slot_name(slot));
    FILE *f = fopen(p, "r");
    if (!f) return -1;
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq++ = 0;
        eq[strcspn(eq, "\r\n")] = 0;
        if (!strcmp(line, "when")) r->when = strtoull(eq, NULL, 10);
        else if (!strcmp(line, "fw")) snprintf(r->fw, sizeof(r->fw), "%s", eq);
        else if (!strcmp(line, "mode")) r->mode = atoi(eq);
        else if (!strcmp(line, "diag")) sscanf(eq, "%d,%d,%d", &r->d_pass, &r->d_fail, &r->d_skip);
        else if (!strcmp(line, "rest")) sscanf(eq, "%d,%d", &r->rest_cpu, &r->rest_rsx);
        else if (!strcmp(line, "load")) sscanf(eq, "%d,%d", &r->load_cpu, &r->load_rsx);
        else if (!strcmp(line, "max")) sscanf(eq, "%d,%d", &r->max_cpu, &r->max_rsx);
        else if (!strcmp(line, "stress")) sscanf(eq, "%d,%d,%d", &r->st_s, &r->st_err, &r->st_cpu_rise);
        else if (!strcmp(line, "hdd")) sscanf(eq, "%d,%d", &r->hdd_w, &r->hdd_r);
        else if (!strcmp(line, "usb")) sscanf(eq, "%d,%d", &r->usb_w, &r->usb_r);
        else if (!strcmp(line, "free")) r->hdd_free_mb = atoi(eq);
        else if (!strcmp(line, "opt")) r->opt = atoi(eq);
        else if (!strcmp(line, "tags")) r->tags = (unsigned)strtoul(eq, NULL, 10);
    }
    fclose(f);
    r->valid = r->when != 0;
    return r->valid ? 0 : -1;
}

void report_notes_load(void)
{
    char p[300], line[64];
    snprintf(p, sizeof(p), "%s/rapport_notes.txt", prefs_dir());
    FILE *f = fopen(p, "r");
    if (!f) return;
    if (fgets(line, sizeof(line), f)) g_rep_tags = (unsigned)strtoul(line, NULL, 10) & ((1u << REP_TAGS) - 1);
    fclose(f);
}

void report_notes_save(void)
{
    char p[300];
    snprintf(p, sizeof(p), "%s/rapport_notes.txt", prefs_dir());
    FILE *f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "%u\n", g_rep_tags);
    fclose(f);
}

static void put_x10(FILE *f, const char *label, int v)
{
    if (v < 0) fprintf(f, "  %-26s : non mesuré\n", label);
    else fprintf(f, "  %-26s : %d.%d Mo/s\n", label, v / 10, v % 10);
}

static void put_temp(FILE *f, const char *label, int v)
{
    if (v < 0) fprintf(f, "  %-26s : n/d\n", label);
    else fprintf(f, "  %-26s : %d C\n", label, v);
}

int report_export(char *path, int n)
{
    char dir[64];
    if (export_dir(dir, sizeof(dir)) != 0) return -1;
    snprintf(path, n, "%s/rapport_controle_%llu.txt", dir, wall_time());
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    Report r;
    report_capture(&r);
    fprintf(f, "PS3 TOOLS KIT - RAPPORT DE CONTROLE\n===================================\n");
    fprintf(f, "Firmware : %s (%s)\n", r.fw, mode_name(r.mode));
    fprintf(f, "Date (secondes depuis 1970) : %llu\n\n", r.when);
    fprintf(f, "DIAGNOSTIC\n");
    fprintf(f, "  reussi %d, echec %d, non teste %d\n", r.d_pass, r.d_fail, r.d_skip);
    for (int i = 0; i < DIAG_N; i++)
        fprintf(f, "  [%s] %s %s\n", g_diag[i].res == R_PASS ? "OK    " : g_diag[i].res == R_FAIL ? "ECHEC " : "n/teste", g_diag[i].name, g_diag[i].detail);
    fprintf(f, "\nTEMPERATURES (depuis le lancement de l'appli)\n");
    put_temp(f, "CPU au repos (moyenne)", r.rest_cpu);
    put_temp(f, "RSX au repos (moyenne)", r.rest_rsx);
    put_temp(f, "CPU en charge (moyenne)", r.load_cpu);
    put_temp(f, "RSX en charge (moyenne)", r.load_rsx);
    put_temp(f, "CPU maximum", r.max_cpu);
    put_temp(f, "RSX maximum", r.max_rsx);
    fprintf(f, "\nSTRESS TEST\n");
    if (r.st_s < 0) fprintf(f, "  jamais lance\n");
    else fprintf(f, "  duree %d s, erreurs %d, montee CPU %d C\n", r.st_s, r.st_err, r.st_cpu_rise);
    fprintf(f, "\nSTOCKAGE\n");
    put_x10(f, "Disque dur : ecriture", r.hdd_w);
    put_x10(f, "Disque dur : lecture", r.hdd_r);
    put_x10(f, "USB : ecriture", r.usb_w);
    put_x10(f, "USB : lecture", r.usb_r);
    if (r.hdd_free_mb >= 0) fprintf(f, "  Disque dur libre           : %d Mo\n", r.hdd_free_mb);
    fprintf(f, "\nLECTEUR OPTIQUE\n");
    if (r.opt < 0) fprintf(f, "  non teste\n");
    else fprintf(f, "  %s (%d erreur(s))\n", r.opt ? "problemes de lecture" : "lecture correcte", r.opt);
    fprintf(f, "\nNOTES DU REPARATEUR\n");
    int any = 0;
    for (int i = 0; i < REP_TAGS; i++)
        if (r.tags & (1u << i)) { fprintf(f, "  - %s\n", g_rep_tag_names[i]); any = 1; }
    if (!any) fprintf(f, "  (aucune)\n");
    fprintf(f, "\nCe rapport ne contient aucun identifiant de la console (IDPS, PSID, numero de serie).\n");
    int bad = ferror(f);
    if (fclose(f) != 0) bad = 1;
    return bad ? -1 : 0;
}