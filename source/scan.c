#include "scan.h"
#include "sys.h"
#include "prefs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

Scan scan;

static void add_arg(int sev, const char *title, const char *arg, const char *path, const char *advice);
static void add(int sev, const char *title, const char *path, const char *advice)
{
    add_arg(sev, title, "", path, advice);
}

static void set_info(const char *fmt, const char *arg)
{
    scan.info_fmt[scan.step] = fmt;
    snprintf(scan.info[scan.step], 48, "%s", arg ? arg : "");
}

static void set_info_n(const char *fmt, int n)
{
    char b[24];
    snprintf(b, sizeof(b), "%d", n);
    set_info(fmt, b);
}

static void add_arg(int sev, const char *title, const char *arg, const char *path, const char *advice)
{
    if (sev == SEV_WARN) scan.warns++;
    if (sev == SEV_ERR) scan.errs++;
    if (scan.nf >= MAX_FIND) return;
    Finding *f = &scan.f[scan.nf++];
    f->sev = sev;
    snprintf(f->title, sizeof(f->title), "%s", title);
    snprintf(f->arg, sizeof(f->arg), "%s", arg ? arg : "");
    snprintf(f->path, sizeof(f->path), "%s", path ? path : "");
    snprintf(f->advice, sizeof(f->advice), "%s", advice ? advice : "");
}

static int ends_with_ci(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    if (b > a) return 0;
    for (size_t i = 0; i < b; i++)
        if (tolower((unsigned char)s[a - b + i]) != tolower((unsigned char)suf[i])) return 0;
    return 1;
}

#define WSTACK 256
static char wstack[WSTACK][256];
static int wsp;
typedef void (*file_fn)(const char *path, long long size);
static file_fn w_fn;
static int w_depth_limit = 1;   

static void w_reset(file_fn fn) { wsp = 0; w_fn = fn; }
static void w_push(const char *p)
{
    if (wsp < WSTACK && is_dir(p)) snprintf(wstack[wsp++], 256, "%s", p);
}

static int w_cb(const char *name, int isdir, void *user)
{
    const char *dir = (const char *)user;
    char full[256];
    snprintf(full, sizeof(full), "%s/%s", dir, name);
    if (isdir) {
        if (w_depth_limit && wsp < WSTACK) snprintf(wstack[wsp++], 256, "%s", full);
        return 0;
    }
    scan.files++;
    if (w_fn) w_fn(full, file_size(full));
    return 0;
}

static int w_step(void)
{
    if (wsp <= 0) return 1;
    char dir[256];
    snprintf(dir, sizeof(dir), "%s", wstack[--wsp]);
    snprintf(scan.current, sizeof(scan.current), "%s", dir);
    list_dir(dir, w_cb, dir);
    return wsp <= 0;
}

static int sub;          
static int idx, count;   
static float frac;       
static int step_files_start;
static int webman_in_boot;
static int zero_count;
static char zero_first[256];
static const char *critical[] = {
    "/dev_flash/vsh/module/vsh.self",
    "/dev_flash/vsh/module/explore_plugin.sprx",
    "/dev_flash/vsh/module/xmb_plugin.sprx",
    "/dev_flash/vsh/module/game_ext_plugin.sprx",
    "/dev_flash/vsh/module/sysconf_plugin.sprx",
    "/dev_flash/vsh/module/paf.sprx",
    "/dev_flash/vsh/etc/version.txt",
    NULL
};

static void flash_file(const char *path, long long size)
{
    if (size == 0 && (ends_with_ci(path, ".sprx") || ends_with_ci(path, ".self")))
        add(SEV_ERR, "Module système vide (0 octet)", path,
            "Fichier système abîmé. Ne modifie rien et réinstalle le même firmware en mode Recovery.");
}

static int step_modules(void)
{
    if (sub == 0) {
        if (!is_dir("/dev_flash/vsh/module")) {
            add(SEV_ERR, "dev_flash illisible", "/dev_flash/vsh/module",
                "Impossible de lire les fichiers système. Redémarre la console puis relance l'analyse.");
            return 1;
        }
        for (int i = 0; critical[i]; i++) {
            long long s = file_size(critical[i]);
            scan.files++;
            if (s < 0)
                add(SEV_ERR, "Fichier système manquant", critical[i],
                    "Ne touche à rien et réinstalle le même firmware en mode Recovery.");
            else if (s == 0)
                add(SEV_ERR, "Fichier système vide (0 octet)", critical[i],
                    "Ne touche à rien et réinstalle le même firmware en mode Recovery.");
        }
        step_files_start = scan.files;
        w_depth_limit = 1;
        w_reset(flash_file);
        w_push("/dev_flash/vsh/module");
        w_push("/dev_flash/sys/external");
        w_push("/dev_flash/sys/internal");
        sub = 1;
        return 0;
    }
    int finished = w_step();
    int n = scan.files - step_files_start;
    frac = n / 600.0f;
    if (frac > 0.95f) frac = 0.95f;
    set_info_n("Fichiers : %s", n);
    return finished;
}

static const char *boot_cfw[] = {
    "/dev_hdd0/boot_plugins.txt",
    "/dev_hdd0/boot_plugins_nocobra.txt",
    "/dev_hdd0/boot_plugins_nocobra_dex.txt",
    "/dev_hdd0/boot_plugins_kernel.txt",
    NULL
};
static const char *boot_hen[] = {
    "/dev_hdd0/boot_plugins_hen.txt",
    "/dev_hdd0/boot_plugins.txt",
    NULL
};

static int check_boot_list(const char *list)
{
    FILE *f = fopen(list, "rb");
    if (!f) return 0;
    snprintf(scan.current, sizeof(scan.current), "%s", list);
    char line[512];
    int plugins = 0;
    while (fgets(line, sizeof(line), f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        size_t l = strlen(s);
        while (l && (s[l - 1] == '\n' || s[l - 1] == '\r' || s[l - 1] == ' ' || s[l - 1] == '\t')) s[--l] = 0;
        if (!l || s[0] == '#' || s[0] == ';' || (s[0] == '/' && s[1] == '/')) continue;
        plugins++;
        scan.files++;
        if (strstr(s, "webftp_server")) webman_in_boot = 1;
        char where[256];
        snprintf(where, sizeof(where), "%s -> %s", list, s);
        long long sz = file_size(s);
        if (sz < 0)
            add(SEV_ERR, "Plugin introuvable au démarrage", where,
                "Le fichier listé n'existe plus. Retire cette ligne pour éviter un blocage au démarrage.");
        else if (sz == 0)
            add(SEV_ERR, "Plugin vide (0 octet)", where,
                "Ce plugin est abîmé. Remplace-le ou retire sa ligne du fichier.");
    }
    fclose(f);
    return plugins;
}

static int step_boot(const char **lists)
{
    int total = 0, found = 0;
    for (int i = 0; lists[i]; i++) {
        if (!path_exists(lists[i])) continue;
        found++;
        total += check_boot_list(lists[i]);
    }
    if (!found) set_info("Aucune liste", NULL);
    else set_info_n("Plugins : %s", total);
    return 1;
}

static int step_webman(void)
{
    int installed = webman_in_boot ||
                    path_exists("/dev_flash/vsh/module/webftp_server.sprx") ||
                    path_exists("/dev_hdd0/plugins/webftp_server.sprx");
    scan.files++;
    if (!installed) {
        set_info("Non installé", NULL);
        return 1;
    }
    if (!is_dir("/dev_hdd0/tmp/wm_res"))
        add(SEV_WARN, "Ressources webMAN MOD manquantes", "/dev_hdd0/tmp/wm_res",
            "Certaines fonctions de webMAN MOD ne marcheront pas. Réinstalle son PKG officiel.");
    set_info("Installé", NULL);
    return 1;
}

static void hen_file(const char *path, long long size)
{
    if (size == 0)
        add(SEV_WARN, "Fichier HEN vide (0 octet)", path,
            "Réinstalle le PKG de PS3HEN depuis la source officielle du projet.");
}

static int step_hen(void)
{
    if (sub == 0) {
        if (!is_dir("/dev_hdd0/hen")) {
            add(SEV_WARN, "Dossier HEN introuvable", "/dev_hdd0/hen",
                "HEN ne semble pas installé. Normal sur RPCS3 ; sur une console, réinstalle le PKG de PS3HEN.");
            set_info("Absent", NULL);
            return 1;
        }
        step_files_start = scan.files;
        w_depth_limit = 1;
        w_reset(hen_file);
        w_push("/dev_hdd0/hen");
        sub = 1;
        return 0;
    }
    int finished = w_step();
    set_info_n("Fichiers : %s", scan.files - step_files_start);
    frac = 0.5f;
    return finished;
}

#define MAX_GAMES 2048
static char (*games)[48];

static int game_cb(const char *name, int isdir, void *user)
{
    (void)user;
    if (!isdir || name[0] == '_' || name[0] == '.') return 0;
    if (count >= MAX_GAMES) return 1;
    snprintf(games[count++], 48, "%s", name);
    return 0;
}

static unsigned rd32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
static unsigned rd16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static int sfo_get(const unsigned char *b, long n, const char *key, char *out, int outn)
{
    out[0] = 0;
    if (n < 20 || memcmp(b, "\0PSF", 4) != 0) return 0;
    unsigned kt = rd32(b + 8), dt = rd32(b + 12), cnt = rd32(b + 16);
    if (kt >= (unsigned)n || dt >= (unsigned)n || cnt > 256 || 20 + cnt * 16 > (unsigned)n) return 0;
    for (unsigned i = 0; i < cnt; i++) {
        const unsigned char *e = b + 20 + i * 16;
        unsigned ko = rd16(e), len = rd32(e + 4), off = rd32(e + 12);
        if (kt + ko >= (unsigned)n) return 0;
        if (strcmp((const char *)b + kt + ko, key) == 0) {
            if (dt + off + len > (unsigned)n) return 0;
            int l = len < (unsigned)outn - 1 ? (int)len : outn - 1;
            memcpy(out, b + dt + off, l);
            out[l] = 0;
        }
    }
    return 1;
}

static void check_game(const char *id)
{
    char dir[128], sfo[160], eboot[192];
    snprintf(dir, sizeof(dir), "/dev_hdd0/game/%s", id);
    snprintf(sfo, sizeof(sfo), "%s/PARAM.SFO", dir);
    snprintf(scan.current, sizeof(scan.current), "%s", sfo);
    scan.files++;

    long long sz = file_size(sfo);
    if (sz < 0) {
        add(SEV_WARN, "Dossier sans PARAM.SFO", dir,
            "La console n'affichera pas ce contenu. Réinstalle le jeu ou l'appli concernée.");
        return;
    }
    if (sz == 0 || sz > 65536) {
        add(SEV_ERR, "PARAM.SFO abîmé", sfo, "Réinstalle le jeu ou l'appli concernée.");
        return;
    }
    unsigned char *buf = malloc((size_t)sz);
    if (!buf) return;
    FILE *f = fopen(sfo, "rb");
    long got = f ? (long)fread(buf, 1, (size_t)sz, f) : 0;
    if (f) fclose(f);

    char cat[8], title[64];
    int ok = sfo_get(buf, got, "CATEGORY", cat, sizeof(cat));
    sfo_get(buf, got, "TITLE", title, sizeof(title));
    free(buf);
    if (!ok) {
        add(SEV_ERR, "PARAM.SFO abîmé", sfo, "Réinstalle le jeu ou l'appli concernée.");
        return;
    }
    if (strcmp(cat, "HG") == 0) {
        snprintf(eboot, sizeof(eboot), "%s/USRDIR/EBOOT.BIN", dir);
        scan.files++;
        long long es = file_size(eboot);
        if (es <= 0) {
            add_arg(SEV_WARN, es < 0 ? "%s : EBOOT.BIN manquant" : "%s : EBOOT.BIN vide",
                    title[0] ? title : id, eboot, "Ce jeu ne pourra pas se lancer. Réinstalle son PKG.");
        }
    }
}

static int step_games(void)
{
    if (sub == 0) {
        if (!games) games = malloc(sizeof(*games) * MAX_GAMES);
        count = 0; idx = 0;
        if (!games || list_dir("/dev_hdd0/game", game_cb, NULL) != 0) {
            add(SEV_ERR, "Dossier des jeux illisible", "/dev_hdd0/game",
                "Le disque dur ne répond pas correctement. Redémarre la console.");
            return 1;
        }
        sub = 1;
        return count == 0;
    }
    if (idx < count) check_game(games[idx++]);
    frac = count ? (float)idx / count : 1.0f;
    {
        char b[32];
        snprintf(b, sizeof(b), "%d / %d", idx, count);
        set_info("%s", b);
    }
    return idx >= count;
}

static void zero_file(const char *path, long long size)
{
    if (size != 0) return;
    if (ends_with_ci(path, ".sprx") || ends_with_ci(path, ".self")) {
        add(SEV_ERR, "Plugin ou module vide (0 octet)", path,
            "Ce fichier est abîmé. S'il est listé au démarrage, retire sa ligne puis remplace-le.");
        return;
    }
    if (zero_count++ == 0) snprintf(zero_first, sizeof(zero_first), "%s", path);
}

static int step_zero(void)
{
    if (sub == 0) {
        zero_count = 0; zero_first[0] = 0;
        step_files_start = scan.files;
        w_depth_limit = 1;
        w_reset(zero_file);
        w_push("/dev_hdd0/tmp");
        w_push("/dev_hdd0/plugins");
        sub = 1;
        return 0;
    }
    int finished = w_step();
    frac = 0.5f;
    set_info_n("Fichiers : %s", scan.files - step_files_start);
    if (finished && zero_count > 0) {
        char cnt[16];
        snprintf(cnt, sizeof(cnt), "%d", zero_count);
        add_arg(SEV_WARN, "Fichiers vides (0 octet) : %s", cnt, zero_first,
            "Sans danger, souvent des restes de téléchargement. Tu peux les supprimer.");
    }
    return finished;
}

static int step_space(void)
{
    uint64_t freeb = 0;
    scan.files++;
    if (sys_free_space("/dev_hdd0", &freeb) != 0) {
        set_info("N/D", NULL);
        return 1;
    }
    char s[32];
    fmt_size(freeb, s, sizeof(s));
    set_info("%s libres", s);
    const uint64_t GB = 1024ULL * 1024 * 1024;
    if (freeb < GB)
        add(SEV_ERR, "Disque dur presque plein", "/dev_hdd0",
            "Moins de 1 Go libre : les mises à jour et sauvegardes peuvent échouer. Libère de la place.");
    else if (freeb < 5 * GB)
        add(SEV_WARN, "Peu d'espace libre", "/dev_hdd0",
            "Moins de 5 Go libres. Pense à faire un peu de ménage.");
    return 1;
}

static int run_step(int i)
{
    switch (i) {
    case 0: return step_modules();
    case 1: return step_boot(scan.mode == MODE_HEN ? boot_hen : boot_cfw);
    case 2: return scan.mode == MODE_HEN ? step_hen() : step_webman();
    case 3: return step_games();
    case 4: return step_zero();
    case 5: return step_space();
    }
    return 1;
}

void scan_start(int mode)
{
    memset(&scan, 0, sizeof(scan));
    scan.mode = mode;
    scan.names[0] = "Modules système (dev_flash)";
    scan.names[1] = mode == MODE_HEN ? "Plugins HEN" : "Plugins au démarrage";
    scan.names[2] = mode == MODE_HEN ? "Fichiers HEN" : "Configuration webMAN MOD";
    scan.names[3] = "Jeux et applications";
    scan.names[4] = "Fichiers vides ou abîmés";
    scan.names[5] = "Espace disque";
    scan.running = 1;
    scan.state[0] = ST_RUN;
    scan.start_ms = ticks_ms();
    sub = 0; idx = 0; count = 0; frac = 0; webman_in_boot = 0;
}

void scan_stop(void)
{
    scan.running = 0;
}

void scan_tick(unsigned budget_ms)
{
    if (!scan.running) return;
    unsigned t0 = ticks_ms();
    do {
        if (run_step(scan.step)) {
            scan.state[scan.step] = ST_DONE;
            if (!scan.info_fmt[scan.step]) set_info("OK", NULL);
            scan.step++;
            sub = 0; frac = 0;
            if (scan.step >= SCAN_STEPS) {
                scan.running = 0;
                scan.done = 1;
                scan.progress = 1.0f;
                scan.end_ms = ticks_ms();
                return;
            }
            scan.state[scan.step] = ST_RUN;
        }
    } while (ticks_ms() - t0 < budget_ms);
    scan.progress = (scan.step + frac) / SCAN_STEPS;
}

void scan_finding_title(const Finding *f, char *out, int n)
{
    snprintf(out, n, T(f->title), f->arg);
}

void scan_step_info(int i, char *out, int n)
{
    if (!scan.info_fmt[i]) { out[0] = 0; return; }
    snprintf(out, n, T(scan.info_fmt[i]), scan.info[i]);
}

int scan_export(char *out_path, int n)
{
    const char *p = is_dir("/dev_usb000") ? "/dev_usb000/PS3ToolsKit_rapport.txt"
                                          : "/dev_hdd0/tmp/PS3ToolsKit_rapport.txt";
    snprintf(out_path, n, "%s", p);
    FILE *f = fopen(p, "w");
    if (!f) return -1;
    char fw[32], line[256];
    sys_firmware(fw, sizeof(fw));
    fprintf(f, "\xEF\xBB\xBF");
    fprintf(f, "PS3 Tools Kit - %s\r\n", T("Rapport d'analyse"));
    fprintf(f, "%s : %s | Firmware : %s\r\n", T("Mode"), T(mode_name(scan.mode)), fw);
    fprintf(f, "%s : %d | %s : %d | %s : %d\r\n\r\n",
            T("Fichiers lus"), scan.files, T("Avertissements"), scan.warns, T("Problèmes"), scan.errs);
    for (int i = 0; i < SCAN_STEPS; i++) {
        scan_step_info(i, line, sizeof(line));
        fprintf(f, "- %s : %s\r\n", T(scan.names[i]), line);
    }
    fprintf(f, "\r\n");
    for (int i = 0; i < scan.nf; i++) {
        Finding *x = &scan.f[i];
        scan_finding_title(x, line, sizeof(line));
        fprintf(f, "[%s] %s\r\n    %s\r\n    %s\r\n\r\n",
                T(x->sev == SEV_ERR ? "PROBLÈME" : x->sev == SEV_WARN ? "ATTENTION" : "INFO"),
                line, x->path, T(x->advice));
    }
    if (!scan.nf) fprintf(f, "%s\r\n", T("Aucun problème trouvé"));
    fclose(f);
    return 0;
}