#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include "gfx.h"
#include "pad.h"
#include "sys.h"
#include "scan.h"
#include <sys/process.h>
#include <sysutil/sysutil.h>
#include "screen.h"
#include "prefs.h"
#include "netinfo.h"
#include "saves.h"
#include "dumps.h"
#include "patch.h"

SYS_PROCESS_PARAM(1001, 0x100000);

#define APP_VERSION "2.0"
#define PAD_X 56
#define CONTENT_W (SCREEN_W - 2 * PAD_X)
#define FOOT_LINE 646
#define FOOT_Y 664

enum { S_SPLASH, S_MENU, S_SYSTEM, S_INFO, S_FILES, S_PAD, S_SCREEN, S_CHOICE, S_RUN, S_RESULT, S_NET, S_SAVES, S_SETTINGS, S_DUMPS_WARN, S_DUMPS,
       S_DIAG, S_STRESS, S_STORAGE, S_PORTS, S_OPTICAL, S_AUDIO, S_THERMAL, S_REPORT, S_DEV, S_DEVV,
       S_KIT, S_KITV, S_KITRSX, S_KITPAD, S_KITSCN, S_KITPROF, S_KITSES };

static int screen = S_SPLASH;
static int running = 1;
static unsigned frame = 0;
static float phase = 0;

static char fw[32] = "Inconnu";
static int sysmode = MODE_UNKNOWN;
static char hdd_free[32] = "N/D";
static int temp_cpu = -1, temp_rsx = -1;
static unsigned temp_last = 0;
static char res_text[32] = "1280 x 720";

static char toast_msg[256];
static unsigned toast_until = 0;

static void toast(const char *m)
{
    snprintf(toast_msg, sizeof(toast_msg), "%s", m);
    toast_until = ticks_ms() + 2600;
}

static void refresh_temps(void)
{
    int c;
    temp_cpu = sys_temperature(0, &c) == 0 ? c : -1;
    temp_rsx = sys_temperature(1, &c) == 0 ? c : -1;
    temp_last = ticks_ms();
}

static void refresh_space(void)
{
    uint64_t b;
    if (sys_free_space("/dev_hdd0", &b) == 0) fmt_size(b, hdd_free, sizeof(hdd_free));
    else snprintf(hdd_free, sizeof(hdd_free), "N/D");
}

static void fmt_temp(int t, char *out, int n)
{
    if (t < 0) snprintf(out, n, "N/D");
    else snprintf(out, n, "%d °C", t);
}

static void section_label(const char *s, int x, int y)
{
    s = T(s);
    unsigned char up[160]; int i = 0;
    for (; s[i] && i < 159; i++) {
        unsigned char ch = (unsigned char)s[i];
        unsigned char prev = i > 0 ? (unsigned char)s[i - 1] : 0;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        else if (prev == 0xC3 && ch >= 0xA0 && ch <= 0xBE && ch != 0xB7) ch -= 0x20;           
        else if (prev == 0xD0 && ch >= 0xB0 && ch <= 0xBF) ch -= 0x20;                         
        else if (ch == 0xD1 && i + 1 < 159 && (unsigned char)s[i + 1] >= 0x80 && (unsigned char)s[i + 1] <= 0x8F) {
            up[i++] = 0xD0; up[i] = (unsigned char)s[i] + 0x20; continue;                     
        } else if (ch == 0xD1 && (unsigned char)s[i + 1] == 0x91) { up[i++] = 0xD0; up[i] = 0x81; continue; } 
        up[i] = ch;
    }
    up[i] = 0;
    text(F_SMALL, (const char *)up, x, y, C_MUTED, AL_LEFT);
}

static void header(int ic, const char *title, const char *right)
{
    icon(ic, PAD_X, 42, 30, C_ACCENT, C_BG);
    text(F_H1, title, PAD_X + 44, 57 - text_h(F_H1) / 2, C_TEXT, AL_LEFT);
    if (right) text(F_SMALL, right, SCREEN_W - PAD_X, 57 - text_h(F_SMALL) / 2, C_MUTED, AL_RIGHT);
}

static void footer_line(void)
{
    fill_rect(PAD_X, FOOT_LINE, CONTENT_W, 1, C_LINE);
}

static void kv_row(int x, int y, int w, const char *k, const char *v, Col vc)
{
    text(F_BODY, k, x, y, C_MUTED, AL_LEFT);
    text(F_BODYB, v, x + w, y, vc, AL_RIGHT);
}

static void draw_toast(void)
{
    if (ticks_ms() >= toast_until) return;
    int w = text_w(F_BODY, toast_msg) + 48;
    int x = (SCREEN_W - w) / 2, y = FOOT_LINE - 64;
    card(x, y, w, 44, 22, 1, C_ACCENT, C_PANEL_SEL);
    text(F_BODY, toast_msg, SCREEN_W / 2, y + 22 - text_h(F_BODY) / 2, C_TEXT, AL_CENTER);
}

static void logo(int x, int y, int s)
{
    card(x, y, s, s, s / 4, 2, C_ACCENT, C_BG);
    icon(I_BOX, x + s / 5, y + s / 5, s * 3 / 5, C_ACCENT, C_BG);
}

static void do_splash(void)
{
    if (frame == 2) {          
        sys_firmware(fw, sizeof(fw));
        sysmode = sys_detect_mode();
        refresh_space();
        refresh_temps();
    }
    float p = frame / 80.0f;
    if (p > 1) p = 1;
    int ready = p >= 1;
    if (ready && (pad.pressed[B_CROSS] || pad.pressed[B_START])) screen = S_MENU;

    waves(phase, 450);
    logo(SCREEN_W / 2 - 52, 170, 104);
    text(F_BIG, "PS3 Tools Kit", SCREEN_W / 2, 300, C_TEXT, AL_CENTER);
    text(F_H3, "La boîte à outils de ta PlayStation 3", SCREEN_W / 2, 360, C_TEXT2, AL_CENTER);
    bar(SCREEN_W / 2 - 180, 430, 360, 6, p, C_ACCENT);
    text(F_SMALL, ready ? "Prêt" : "Chargement des modules...", SCREEN_W / 2, 448, C_MUTED, AL_CENTER);
    if (ready && (frame / 30) % 2 == 0) {
        const char *l = "Appuie sur Croix pour continuer";
        int w = hint_w(l);
        hint(G_CROSS, l, SCREEN_W / 2 - w / 2, 630);
    }
    text(F_SMALL, "v" APP_VERSION, SCREEN_W - 40, 668, C_MUTED, AL_RIGHT);
}

typedef struct { int ic; const char *title, *desc; int target; } Tile;
#define N_TOOLS 15
static const Tile tools[N_TOOLS] = {
    { I_CHIP,     "Infos système",   "Firmware, processeur, températures et stockage", S_INFO },
    { I_FOLDER,   "Fichiers",        "Parcourir les dossiers (lecture seule)",         S_FILES },
    { I_PAD,      "Test manette",    "Boutons, sticks, gâchettes, Sixaxis",            S_PAD },
    { I_SAVE,     "Sauvegardes",     "Disque dur et clé USB, dans les deux sens",      S_SAVES },
    { I_NET,      "Réseau",          "Adresse IP, état et test de connexion",          S_NET },
    { I_SCREEN,   "Test d'écran",    "Couleurs, pixels morts, résolution",             S_SCREEN },
    { I_SCAN,     "Analyse système", "Détecter les fichiers manquants ou abîmés",      S_CHOICE },
    { I_CHECK,    "Diagnostic",      "Vérifie chaque composant : réussi, échec ou non testé", S_DIAG },
    { I_PULSE,    "Stress test",     "Charge CPU, mémoire et RSX, avec arrêt de sécurité", S_STRESS },
    { I_HDD,      "Stockage",        "Espace occupé, gros dossiers, vitesse et vérification", S_STORAGE },
    { I_USB,      "Périphériques",   "Clés USB en direct, contenu reconnu, test de transfert", S_PORTS },
    { I_DISC,     "Lecteur optique", "Type de disque, test de lecture, erreurs",           S_OPTICAL },
    { I_SPEAKER,  "Audio",           "Gauche, droite, tonalités et synchronisation",        S_AUDIO },
    { I_THERMO,   "Suivi thermique", "Courbes CPU et RSX, repos contre charge, export",     S_THERMAL },
    { I_DOC,      "Rapport",         "Fiche de la console, notes, comparaison avant/après", S_REPORT },
};

static const Tile sections[5] = {
    { I_CHIP,     "System",      "Les outils de base : infos, fichiers, manette, sauvegardes, réseau, écran et analyse.", S_SYSTEM },
    { I_DUMP,     "Dumps Patch", "Dumps en lecture seule, et tout ce qu'il faut savoir pour ne pas bricker ta console.",  S_DUMPS_WARN },
    { I_SCAN,     "Développeur", "Inspecter, tester et déboguer les homebrews : ELF/SELF, SFO, logs, API, performances.", S_DEV },
    { I_PULSE,    "Dev Kit",     "Laboratoire technique : RSX, Cell, mémoire, fichiers, manettes, scénarios et sessions.", S_KIT },
    { I_SETTINGS, "Settings",    "Journal des tests, langue et à propos.",                                                S_SETTINGS },
};
static const char *section_badge[5] = { "15 outils", "19 outils", "9 outils", "10 outils", "3 onglets" };

static int menu_sel = 0;     
static int tool_sel = 0;     
static int choice_sel = 0;

static void open_files_root(void);
static void enter_screen(int s);

static void brand_header(void)
{
    logo(PAD_X, 40, 48);
    text(F_H2, "PS3 Tools Kit", PAD_X + 64, 42, C_TEXT, AL_LEFT);
    char ver[96];
    snprintf(ver, sizeof(ver), T("Version %s · Homebrew"), APP_VERSION);
    text(F_SMALL, ver, PAD_X + 64, 70, C_MUTED, AL_LEFT);

    char chip[160];
    snprintf(chip, sizeof(chip), T("Firmware %s · %s"), fw, T(mode_name(sysmode)));
    int cw = text_w(F_SMALL, chip) + 32;
    card(SCREEN_W - PAD_X - cw, 46, cw, 36, 18, 1, C_LINE, C_BG);
    text(F_SMALL, chip, SCREEN_W - PAD_X - cw / 2, 64 - text_h(F_SMALL) / 2, C_TEXT2, AL_CENTER);
}

static void status_footer(const char *back_label)
{
    footer_line();
    char tc[16], tr[16];
    fmt_temp(temp_cpu, tc, sizeof(tc));
    fmt_temp(temp_rsx, tr, sizeof(tr));
    int x = PAD_X, y = FOOT_Y + 13 - text_h(F_SMALL) / 2;
    x += text(F_SMALL, "CPU ", x, y, C_MUTED, AL_LEFT);
    x += text(F_SMALL, tc, x, y, C_TEXT, AL_LEFT) + 28;
    x += text(F_SMALL, "RSX ", x, y, C_MUTED, AL_LEFT);
    x += text(F_SMALL, tr, x, y, temp_rsx >= 70 ? C_WARN : C_TEXT, AL_LEFT) + 28;
    x += text(F_SMALL, "HDD ", x, y, C_MUTED, AL_LEFT);
    char hf[48];
    snprintf(hf, sizeof(hf), T("%s libres"), hdd_free);
    text(F_SMALL, hf, x, y, C_TEXT, AL_LEFT);

    int gl[] = { G_CROSS, G_CIRCLE };
    const char *lb[] = { "Ouvrir", back_label };
    hints_right(FOOT_Y, SCREEN_W - PAD_X, gl, lb, 2);
}

static void do_menu(void)
{
    if (pad.repeat[B_RIGHT] && menu_sel < 4) menu_sel++;
    if (pad.repeat[B_LEFT] && menu_sel > 0) menu_sel--;
    if (pad.pressed[B_CIRCLE]) { running = 0; return; }
    if (pad.pressed[B_CROSS]) {
        if (sections[menu_sel].target < 0) toast("Bientôt disponible");
        else enter_screen(sections[menu_sel].target);
    }

    waves(phase, 600);
    brand_header();
    section_label("Sections", PAD_X, 118);

    int gap = 14, tw = (CONTENT_W - 4 * gap) / 5, th = 470, y = 146;
    for (int i = 0; i < 5; i++) {
        int x = PAD_X + i * (tw + gap);
        int sel = i == menu_sel, soon = sections[i].target < 0;
        Col bg = sel ? C_PANEL_SEL : C_PANEL;
        if (sel) card(x, y, tw, th, 22, 2, C_ACCENT, bg);
        else card(x, y, tw, th, 22, 1, C_LINE, bg);
        icon(sections[i].ic, x + 26, y + 36, 56, sel ? C_ACCENT : C_TEXT2, bg);
        text(F_H2, sections[i].title, x + 26, y + 124, soon && !sel ? C_TEXT2 : C_TEXT, AL_LEFT);
        text_wrap(F_SMALL, sections[i].desc, x + 26, y + 172, tw - 44, 22, sel ? C_TEXT2 : C_MUTED);
        const char *b = section_badge[i];
        int bw = text_w(F_SMALL, b) + 28;
        if (soon) card(x + 26, y + th - 70, bw, 32, 16, 1, C_WARN, bg);
        else card(x + 26, y + th - 70, bw, 32, 16, 1, sel ? C_ACCENT : C_LINE2, bg);
        text(F_SMALL, b, x + 26 + bw / 2, y + th - 54 - text_h(F_SMALL) / 2, soon ? C_WARN : sel ? C_ACCENT2 : C_TEXT2, AL_CENTER);
    }

    status_footer("Quitter vers XMB");
}

static void do_system(void)
{
    const int cols = 5;
    if (pad.repeat[B_RIGHT] && tool_sel % cols < cols - 1 && tool_sel + 1 < N_TOOLS) tool_sel++;
    if (pad.repeat[B_LEFT] && tool_sel % cols > 0) tool_sel--;
    if (pad.repeat[B_DOWN] && tool_sel + cols < N_TOOLS) tool_sel += cols;
    if (pad.repeat[B_UP] && tool_sel >= cols) tool_sel -= cols;
    if (pad.pressed[B_CIRCLE]) { screen = S_MENU; return; }
    if (pad.pressed[B_CROSS]) { enter_screen(tools[tool_sel].target); return; }

    waves(phase, 600);
    header(I_CHIP, "System", "15 outils");

    int gap = 14, tw = (CONTENT_W - (cols - 1) * gap) / cols, th = 168;
    for (int i = 0; i < N_TOOLS; i++) {
        int x = PAD_X + (i % cols) * (tw + gap), y = 104 + (i / cols) * (th + gap);
        int sel = i == tool_sel;
        if (sel) card(x, y, tw, th, 16, 2, C_ACCENT, C_PANEL_SEL);
        else card(x, y, tw, th, 16, 1, C_LINE, C_PANEL);
        icon(tools[i].ic, x + 18, y + 16, 34, sel ? C_ACCENT : C_TEXT2, sel ? C_PANEL_SEL : C_PANEL);
        text(F_H3, tools[i].title, x + 18, y + 60, C_TEXT, AL_LEFT);
        text_wrap(F_SMALL, tools[i].desc, x + 18, y + 90, tw - 32, 19, sel ? C_TEXT2 : C_MUTED);
    }

    status_footer("Retour");
}

static int info_tab = 0;
static void draw_info_measured(void);

static void do_info(void)
{
    if (pad.pressed[B_CIRCLE]) { screen = S_SYSTEM; return; }
    if (pad.pressed[B_LEFT] || pad.pressed[B_RIGHT]) info_tab = !info_tab;
    if (ticks_ms() - temp_last > 2000) refresh_temps();
    if (info_tab) { draw_info_measured(); return; }

    header(I_CHIP, "Infos système", "Spécifications du fabricant (non mesurées)");
    int gap = 20, cw = (CONTENT_W - 2 * gap) / 3, y = 108, h = 226;
    const char *titles[3] = { "Console", "Processeur Cell · spécification", "Graphique RSX · spécification" };
    for (int i = 0; i < 3; i++) {
        int x = PAD_X + i * (cw + gap);
        card(x, y, cw, h, 18, 1, C_LINE, C_PANEL);
        section_label(titles[i], x + 24, y + 24);
    }
    int ix = PAD_X + 24, iw = cw - 48;
    kv_row(ix, y + 66, iw, "Firmware", fw, C_TEXT);
    kv_row(ix, y + 104, iw, "Système", mode_name(sysmode), sysmode ? C_OK : C_TEXT);
    kv_row(ix, y + 142, iw, "Disque libre", hdd_free, C_TEXT);
    kv_row(ix, y + 180, iw, "Appli", "v" APP_VERSION, C_TEXT);
    ix += cw + gap;
    kv_row(ix, y + 66, iw, "Fréquence", "3,2 GHz", C_TEXT);
    kv_row(ix, y + 104, iw, "PPU", "1 cœur · 2 threads", C_TEXT);
    kv_row(ix, y + 142, iw, "SPU pour les jeux", "6", C_TEXT);
    kv_row(ix, y + 180, iw, "Mémoire", "256 Mo XDR", C_TEXT);
    ix += cw + gap;
    kv_row(ix, y + 66, iw, "Fréquence", "550 MHz", C_TEXT);
    kv_row(ix, y + 104, iw, "Mémoire vidéo", "256 Mo GDDR3", C_TEXT);
    kv_row(ix, y + 142, iw, "Résolution", res_text, C_TEXT);
    kv_row(ix, y + 180, iw, "Rendu", "Cairo + RSX", C_TEXT);

    int y2 = y + h + gap, h2 = FOOT_LINE - 24 - y2;
    int w2 = cw * 2 + gap;
    card(PAD_X, y2, w2, h2, 18, 1, C_LINE, C_PANEL);
    section_label("Températures en direct", PAD_X + 24, y2 + 24);
    const char *names[2] = { "CPU", "RSX" };
    int temps[2] = { temp_cpu, temp_rsx };
    for (int i = 0; i < 2; i++) {
        int ry = y2 + 76 + i * 56;
        text(F_BODY, names[i], PAD_X + 24, ry - 2, C_TEXT2, AL_LEFT);
        Col c = temps[i] >= 75 ? C_ERR : temps[i] >= 65 ? C_WARN : C_ACCENT;
        bar(PAD_X + 140, ry + 4, w2 - 140 - 140, 10, temps[i] < 0 ? 0 : temps[i] / 100.0f, c);
        char t[16];
        fmt_temp(temps[i], t, sizeof(t));
        text(F_H2, t, PAD_X + w2 - 24, ry - 6, temps[i] < 0 ? C_MUTED : c, AL_RIGHT);
    }
    if (temp_cpu < 0 && temp_rsx < 0)
        text(F_SMALL, "Non disponible : normal sur RPCS3 ou si le firmware bloque cette lecture.",
             PAD_X + 24, y2 + h2 - 40, C_MUTED, AL_LEFT);

    int x3 = PAD_X + w2 + gap;
    card(x3, y2, cw, h2, 18, 1, C_LINE, C_PANEL);
    section_label("Disque dur", x3 + 24, y2 + 24);
    text(F_BIG, hdd_free, x3 + 24, y2 + 64, C_TEXT, AL_LEFT);
    text(F_BODY, "d'espace libre sur /dev_hdd0", x3 + 24, y2 + 124, C_MUTED, AL_LEFT);

    footer_line();
    text(F_SMALL, "Gauche/Droite : page « Mesuré et détecté »", PAD_X, FOOT_Y + 13 - text_h(F_SMALL) / 2, C_MUTED, AL_LEFT);
    static const int gl[] = { G_CIRCLE };
    static const char *lb[] = { "Retour" };
    hints_right(FOOT_Y, SCREEN_W - PAD_X, gl, lb, 1);
}

#define MAX_ENTRIES 1024
typedef struct { char name[128]; int isdir; long long size; } Entry;
static Entry *entries;
static int n_entries, f_sel, f_scroll;
static char cwd[256] = "";

static const char *roots[][2] = {
    { "/dev_hdd0", "Disque dur" }, { "/dev_usb000", "Clé USB (port 1)" },
    { "/dev_usb001", "Clé USB (port 2)" }, { "/dev_usb006", "Clé USB (port 3)" },
    { "/dev_flash", "Mémoire système" }, { "/dev_bdvd", "Disque Blu-ray" },
};

static int entry_cmp(const void *a, const void *b)
{
    const Entry *x = a, *y = b;
    if (x->isdir != y->isdir) return y->isdir - x->isdir;
    return strcasecmp(x->name, y->name);
}

static int files_cb(const char *name, int isdir, void *user)
{
    (void)user;
    if (n_entries >= MAX_ENTRIES) return 1;
    Entry *e = &entries[n_entries++];
    snprintf(e->name, sizeof(e->name), "%s", name);
    e->isdir = isdir;
    e->size = -1;
    if (!isdir) {
        char full[400];
        snprintf(full, sizeof(full), "%s/%s", cwd, name);
        e->size = file_size(full);
    }
    return 0;
}

static void load_dir(void)
{
    if (!entries) entries = malloc(sizeof(Entry) * MAX_ENTRIES);
    n_entries = 0; f_sel = 0; f_scroll = 0;
    if (!entries) return;
    if (!cwd[0]) {
        for (unsigned i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
            if (!is_dir(roots[i][0])) continue;
            Entry *e = &entries[n_entries++];
            snprintf(e->name, sizeof(e->name), "%s", roots[i][0]);
            e->isdir = 1; e->size = -1;
        }
        return;
    }
    if (list_dir(cwd, files_cb, NULL) != 0) toast("Impossible de lire ce dossier");
    qsort(entries, n_entries, sizeof(Entry), entry_cmp);
}

static int fmode_reset(void);
static void open_files_root(void) { fmode_reset(); cwd[0] = 0; load_dir(); }

static const char *root_label(const char *p)
{
    for (unsigned i = 0; i < sizeof(roots) / sizeof(roots[0]); i++)
        if (!strcmp(roots[i][0], p)) return roots[i][1];
    return "";
}

#include "ui_files.inc"

static int fmode_reset(void) { ft_back_to_list(); return 0; }

static void do_files(void)
{
    if (files_tools()) return;
    const int rows = 9, rh = 46;
    if (pad.pressed[B_SQUARE] && !files_pick) { menu_open_list(); return; }
    if (pad.repeat[B_DOWN] && f_sel < n_entries - 1) f_sel++;
    if (pad.repeat[B_UP] && f_sel > 0) f_sel--;
    if (pad.repeat[B_RIGHT]) { f_sel += rows; if (f_sel > n_entries - 1) f_sel = n_entries - 1; }
    if (pad.repeat[B_LEFT]) { f_sel -= rows; if (f_sel < 0) f_sel = 0; }
    if (f_sel < 0) f_sel = 0;
    if (f_sel < f_scroll) f_scroll = f_sel;
    if (f_sel >= f_scroll + rows) f_scroll = f_sel - rows + 1;

    if (pad.pressed[B_CROSS] && files_pick && n_entries > 0 && cwd[0] && !entries[f_sel].isdir) {
        char pp[400];
        snprintf(pp, sizeof(pp), "%s/%s", cwd, entries[f_sel].name);
        dev_pick_done(pp);
        return;
    }
    if (pad.pressed[B_CROSS] && n_entries > 0 && entries[f_sel].isdir) {
        char next[400];
        if (!cwd[0]) snprintf(next, sizeof(next), "%s", entries[f_sel].name);
        else snprintf(next, sizeof(next), "%s/%s", cwd, entries[f_sel].name);
        snprintf(cwd, sizeof(cwd), "%s", next);
        load_dir();
    } else if (pad.pressed[B_CIRCLE]) {
        if (!cwd[0]) { screen = files_from_dev ? S_DEV : S_SYSTEM; files_pick = 0; files_from_dev = 0; return; }
        char *slash = strrchr(cwd, '/');
        if (!slash || slash == cwd) cwd[0] = 0;   
        else *slash = 0;                          
        load_dir();
    }

    char sub[160];
    snprintf(sub, sizeof(sub), T("Éléments : %d · lecture seule"), n_entries);
    header(I_FOLDER, "Fichiers", sub);

    int y = 108, h = FOOT_LINE - 24 - y;
    card(PAD_X, y, CONTENT_W, h, 18, 1, C_LINE, C_PANEL);
    text_fit_left(F_H3, cwd[0] ? cwd : "Périphériques", PAD_X + 28, y + 20, CONTENT_W - 300, C_TEXT);
    if (cwd[0] && strchr(cwd + 1, '/') == NULL)
        text(F_SMALL, root_label(cwd), PAD_X + CONTENT_W - 28, y + 24, C_MUTED, AL_RIGHT);
    fill_rect(PAD_X + 20, y + 56, CONTENT_W - 40, 1, C_LINE);

    if (n_entries == 0)
        text(F_BODY, "Dossier vide", PAD_X + CONTENT_W / 2, y + 200, C_MUTED, AL_CENTER);
    for (int i = 0; i < rows && f_scroll + i < n_entries; i++) {
        Entry *e = &entries[f_scroll + i];
        int ry = y + 66 + i * rh;
        int sel = f_scroll + i == f_sel;
        if (sel) fill_rrect(PAD_X + 14, ry, CONTENT_W - 28, rh - 4, 10, C_PANEL_SEL);
        Col bg = sel ? C_PANEL_SEL : C_PANEL;
        if (e->isdir) icon(I_FOLDER, PAD_X + 30, ry + 9, 24, C_ACCENT2, bg);
        else card(PAD_X + 34, ry + 9, 18, 24, 3, 2, C_TEXT2, bg);
        const char *label = e->name;
        char lab2[192];
        if (!cwd[0]) { snprintf(lab2, sizeof(lab2), "%s   %s", e->name, T(root_label(e->name))); label = lab2; }
        text_fit_left(F_BODY, label, PAD_X + 70, ry + 21 - text_h(F_BODY) / 2, CONTENT_W - 260, C_TEXT);
        if (e->size >= 0) {
            char sz[32];
            fmt_size((uint64_t)e->size, sz, sizeof(sz));
            text(F_SMALL, sz, PAD_X + CONTENT_W - 32, ry + 21 - text_h(F_SMALL) / 2, C_MUTED, AL_RIGHT);
        }
    }
    if (n_entries > rows) {
        int track = rows * rh - 8, th = track * rows / n_entries;
        if (th < 24) th = 24;
        int ty = y + 70 + (track - th) * f_scroll / (n_entries - rows);
        fill_rrect(PAD_X + CONTENT_W - 10, ty, 4, th, 2, C_LINE2);
    }

    footer_line();
    text(F_SMALL, "Aucune modification possible ici : tes fichiers sont en sécurité.",
         PAD_X, FOOT_Y + 13 - text_h(F_SMALL) / 2, C_MUTED, AL_LEFT);
    static const int gl[] = { G_CROSS, G_SQUARE, G_CIRCLE };
    static const char *lb[] = { "Ouvrir", "Outils", "Retour" };
    if (files_pick) { static const int g2[] = { G_CROSS, G_CIRCLE }; static const char *l2[] = { "Choisir ce fichier", "Retour" }; hints_right(FOOT_Y, SCREEN_W - PAD_X, g2, l2, 2); }
    else hints_right(FOOT_Y, SCREEN_W - PAD_X, gl, lb, 3);
}

static int exit_hold = 0;
static unsigned pad_seen = 0;
#define C_BODY HEX(0x0E1420)

static void pad_button(int cx, int cy, int r, int lit, int g)
{
    if (lit) {
        fill_circle(cx, cy, r + 7, HEXA(0x3D9BFF, 70));
        fill_circle(cx, cy, r, C_ACCENT);
    } else ring(cx, cy, r, 2, C_LINE3, C_BODY);
    Col bg = lit ? C_ACCENT : C_BODY;
    Col fg;
    switch (g) {
    case G_CROSS:
        fg = lit ? C_BG : C_ACCENT2;
        thick_line(cx - 7, cy - 7, cx + 7, cy + 7, 3, fg);
        thick_line(cx + 7, cy - 7, cx - 7, cy + 7, 3, fg);
        break;
    case G_CIRCLE:
        ring(cx, cy, 8, 2, lit ? C_BG : C_ERR, bg);
        break;
    case G_TRIANGLE:
        fg = lit ? C_BG : C_OK;
        thick_line(cx, cy - 8, cx + 8, cy + 6, 2, fg);
        thick_line(cx + 8, cy + 6, cx - 8, cy + 6, 2, fg);
        thick_line(cx - 8, cy + 6, cx, cy - 8, 2, fg);
        break;
    case G_SQUARE:
        card(cx - 7, cy - 7, 15, 15, 1, 2, lit ? C_BG : C_PINK, bg);
        break;
    }
}

static void pad_rect(int x, int y, int w, int h, int r, int lit, const char *label)
{
    if (lit) fill_rrect(x, y, w, h, r, C_ACCENT);
    else card(x, y, w, h, r, 2, C_LINE3, C_PANEL);
    if (label) text(F_SMALL, label, x + w / 2, y + h / 2 - text_h(F_SMALL) / 2, lit ? C_BG : C_MUTED, AL_CENTER);
}

static void stick_view(int cx, int cy, int r, int ax, int ay, int lit)
{
    ring(cx, cy, r, lit ? 3 : 2, lit ? C_ACCENT : C_LINE3, C_BODY);
    int dx = (ax - 128) * (r - 14) / 128, dy = (ay - 128) * (r - 14) / 128;
    fill_circle(cx + dx, cy + dy, 13, HEX(0x1F2A3D));
    ring(cx + dx, cy + dy, 13, 2, (abs(ax - 128) > 20 || abs(ay - 128) > 20) ? C_ACCENT : C_LINE3, HEX(0x1F2A3D));
}

static void do_pad(void)
{
    for (int i = 0; i < B_COUNT; i++) if (pad.held[i]) pad_seen |= 1u << i;
    if (pad.held[B_START] && pad.held[B_SELECT]) {
        if (++exit_hold > 45) {
            exit_hold = 0;
            int n = 0;
            for (int i = 0; i < B_COUNT; i++) if (pad_seen & (1u << i)) n++;
            char a[16];
            snprintf(a, sizeof(a), "%d", n);
            log_add("Test manette : %s boutons sur 16 testés", a);
            screen = S_SYSTEM;
            return;
        }
    } else exit_hold = 0;

    header(I_PAD, "Test manette", NULL);
    const char *st = pad.connected ? "Manette 1 connectée" : "Aucune manette détectée";
    int sw = text_w(F_SMALL, st) + 52;
    card(SCREEN_W - PAD_X - sw, 40, sw, 36, 18, 1, C_LINE, C_BG);
    fill_circle(SCREEN_W - PAD_X - sw + 22, 58, 4, pad.connected ? C_OK : C_ERR);
    text(F_SMALL, st, SCREEN_W - PAD_X - sw + 36, 58 - text_h(F_SMALL) / 2, C_TEXT2, AL_LEFT);

    
    int px = PAD_X, py = 108, pw = 720, ph = FOOT_LINE - 24 - py;
    card(px, py, pw, ph, 18, 1, C_LINE, C_PANEL);
    section_label("Boutons", px + 24, py + 24);
    int cx = px + pw / 2, cy = py + 270;
    int *h = pad.held;

    pad_rect(cx - 250, cy - 178, 110, 26, 12, h[B_L2], "L2");
    pad_rect(cx + 140, cy - 178, 110, 26, 12, h[B_R2], "R2");
    pad_rect(cx - 250, cy - 144, 110, 26, 12, h[B_L1], "L1");
    pad_rect(cx + 140, cy - 144, 110, 26, 12, h[B_R1], "R1");

    fill_rrect(cx - 272, cy - 112, 544, 200, 90, C_LINE3);
    fill_circle(cx - 196, cy + 96, 72, C_LINE3);
    fill_circle(cx + 196, cy + 96, 72, C_LINE3);
    fill_rrect(cx - 270, cy - 110, 540, 196, 88, C_BODY);
    fill_circle(cx - 196, cy + 96, 70, C_BODY);
    fill_circle(cx + 196, cy + 96, 70, C_BODY);

    int dx = cx - 180, dy = cy - 20;
    pad_rect(dx - 15, dy - 52, 30, 38, 6, h[B_UP], NULL);
    pad_rect(dx - 15, dy + 14, 30, 38, 6, h[B_DOWN], NULL);
    pad_rect(dx - 52, dy - 15, 38, 30, 6, h[B_LEFT], NULL);
    pad_rect(dx + 14, dy - 15, 38, 30, 6, h[B_RIGHT], NULL);

    int fx = cx + 180, fy = cy - 20;
    pad_button(fx, fy - 42, 20, h[B_TRIANGLE], G_TRIANGLE);
    pad_button(fx + 42, fy, 20, h[B_CIRCLE], G_CIRCLE);
    pad_button(fx, fy + 42, 20, h[B_CROSS], G_CROSS);
    pad_button(fx - 42, fy, 20, h[B_SQUARE], G_SQUARE);

    pad_rect(cx - 82, cy - 42, 50, 18, 9, h[B_SELECT], NULL);
    pad_rect(cx + 32, cy - 42, 50, 18, 9, h[B_START], NULL);
    text(F_SMALL, "SELECT", cx - 57, cy - 70, C_MUTED, AL_CENTER);
    text(F_SMALL, "START", cx + 57, cy - 70, C_MUTED, AL_CENTER);
    ring(cx, cy + 6, 18, 2, C_LINE3, C_BODY);
    text(F_SMALL, "PS", cx, cy + 6 - text_h(F_SMALL) / 2, C_MUTED, AL_CENTER);

    stick_view(cx - 82, cy + 70, 40, pad.lx, pad.ly, h[B_L3]);
    stick_view(cx + 82, cy + 70, 40, pad.rx, pad.ry, h[B_R3]);

    char last[64];
    snprintf(last, sizeof(last), "%s", pad.last_button >= 0 ? button_names[pad.last_button] : "aucun");
    int lx = px + 24, ly = py + ph - 44;
    lx += text(F_BODY, "Dernier bouton : ", lx, ly, C_TEXT2, AL_LEFT);
    text(F_BODYB, last, lx, ly, C_ACCENT2, AL_LEFT);

    int rx = px + pw + 20, rw = SCREEN_W - PAD_X - rx;
    card(rx, py, rw, 236, 18, 1, C_LINE, C_PANEL);
    section_label("Sticks analogiques", rx + 24, py + 24);
    for (int i = 0; i < 2; i++) {
        int scx = rx + rw / 4 + i * rw / 2, scy = py + 120;
        int ax = i ? pad.rx : pad.lx, ay = i ? pad.ry : pad.ly;
        ring(scx, scy, 48, 1, C_LINE2, C_PANEL);
        fill_rect(scx - 47, scy, 94, 1, C_LINE);
        fill_rect(scx, scy - 47, 1, 94, C_LINE);
        fill_circle(scx + (ax - 128) * 40 / 128, scy + (ay - 128) * 40 / 128, 7,
                    (abs(ax - 128) > 20 || abs(ay - 128) > 20) ? C_ACCENT : C_TEXT2);
        char v[96];
        snprintf(v, sizeof(v), "%s · X %d · Y %d", T(i ? "Droit" : "Gauche"), ax, ay);
        text(F_SMALL, v, scx, scy + 62, C_MUTED, AL_CENTER);
    }

    int ty = py + 256, thh = ph - 256;
    card(rx, ty, rw, thh, 18, 1, C_LINE, C_PANEL);
    section_label("Gâchettes et Sixaxis", rx + 24, ty + 24);
    int vals[2] = { pad.pl2, pad.pr2 };
    for (int i = 0; i < 2; i++) {
        int by = ty + 66 + i * 40;
        text(F_BODY, i ? "R2" : "L2", rx + 24, by - 4, C_TEXT2, AL_LEFT);
        bar(rx + 70, by + 2, rw - 70 - 84, 10, vals[i] / 255.0f, C_ACCENT);
        char pc[16];
        snprintf(pc, sizeof(pc), "%d %%", vals[i] * 100 / 255);
        text(F_SMALL, pc, rx + rw - 24, by - 2, C_TEXT, AL_RIGHT);
    }
    const char *ax_names[3] = { "Capteur X", "Capteur Y", "Capteur Z" };
    int sv[3] = { pad.sx, pad.sy, pad.sz };
    int bw = (rw - 48 - 20) / 3;
    for (int i = 0; i < 3; i++) {
        int bx = rx + 24 + i * (bw + 10), byy = ty + 156;
        card(bx, byy, bw, 64, 10, 1, C_LINE, C_PANEL);
        text(F_SMALL, ax_names[i], bx + 12, byy + 10, C_MUTED, AL_LEFT);
        char v[16];
        snprintf(v, sizeof(v), "%+d", sv[i] - 512);
        text(F_BODYB, v, bx + 12, byy + 34, C_TEXT, AL_LEFT);
    }
    if (!pad.has_pressure)
        text(F_SMALL, "Pression non disponible : mode DualShock 3 requis.", rx + 24, ty + thh - 34, C_MUTED, AL_LEFT);

    footer_line();
    text(F_SMALL, exit_hold ? "Garde Start + Select enfoncés..." : "Maintiens Start + Select pour quitter le test",
         PAD_X, FOOT_Y + 13 - text_h(F_SMALL) / 2, exit_hold ? C_ACCENT2 : C_MUTED, AL_LEFT);
}

static int color_idx = 0;
static unsigned colors_seen = 0;
static unsigned color_shown = 0;

static void do_screen(void)
{
    static const unsigned cols[] = { 0x000000, 0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF, 0x808080 };
    static const char *names[] = { "Noir", "Blanc", "Rouge", "Vert", "Bleu", "Gris 50 %" };
    const int n = 6;
    if (pad.pressed[B_CIRCLE]) {
        int n = 0;
        for (int i = 0; i < 6; i++) if (colors_seen & (1u << i)) n++;
        char a[16];
        snprintf(a, sizeof(a), "%d", n);
        log_add("Test d'écran : %s couleurs sur 6 affichées", a);
        screen = S_SYSTEM; color_idx = 0; return;
    }
    if (pad.pressed[B_CROSS] || pad.pressed[B_RIGHT]) { color_idx = (color_idx + 1) % n; color_shown = ticks_ms(); }
    if (pad.pressed[B_LEFT]) { color_idx = (color_idx + n - 1) % n; color_shown = ticks_ms(); }
    if (color_shown == 0) color_shown = ticks_ms();
    colors_seen |= 1u << color_idx;

    fill_rect(0, 0, SCREEN_W, SCREEN_H, HEX(cols[color_idx]));
    if (ticks_ms() - color_shown < 2500) {
        char l[200];
        snprintf(l, sizeof(l), T("%s  (%d/%d) · Croix : suivante · Rond : quitter"), T(names[color_idx]), color_idx + 1, n);
        int w = text_w(F_BODY, l) + 48;
        card((SCREEN_W - w) / 2, SCREEN_H - 90, w, 44, 22, 1, C_LINE2, C_PANEL);
        text(F_BODY, l, SCREEN_W / 2, SCREEN_H - 68 - text_h(F_BODY) / 2, C_TEXT, AL_CENTER);
    }
}

static void choice_card(int x, int y, int w, int h, int sel, const char *title, const char *badge,
                        const char *desc, const char **items)
{
    Col bg = sel ? C_PANEL_SEL : C_PANEL;
    if (sel) card(x, y, w, h, 18, 2, C_ACCENT, bg);
    else card(x, y, w, h, 18, 1, C_LINE, bg);
    text(F_H1, title, x + 28, y + 26, C_TEXT, AL_LEFT);
    if (badge) {
        int bw = text_w(F_SMALL, badge) + 24;
        fill_rrect(x + w - 28 - bw, y + 30, bw, 28, 14, C_ACCENT);
        text(F_SMALL, badge, x + w - 28 - bw / 2, y + 44 - text_h(F_SMALL) / 2, C_BG, AL_CENTER);
    }
    text_wrap(F_BODY, desc, x + 28, y + 84, w - 56, 24, C_TEXT2);
    for (int i = 0; items[i]; i++) {
        int iy = y + 156 + i * 34;
        text(F_BODY, ">", x + 28, iy, sel ? C_ACCENT : C_MUTED, AL_LEFT);
        text(F_BODY, items[i], x + 50, iy, C_TEXT2, AL_LEFT);
    }
}

static void do_choice(void)
{
    if (pad.pressed[B_CIRCLE]) { screen = S_SYSTEM; return; }
    if (pad.pressed[B_LEFT]) choice_sel = 0;
    if (pad.pressed[B_RIGHT]) choice_sel = 1;
    if (pad.pressed[B_CROSS]) {
        scan_start(choice_sel ? MODE_HEN : MODE_CFW);
        screen = S_RUN;
        return;
    }

    header(I_SCAN, "Analyse système", "Étape 1 sur 3");
    text(F_H2, "Sur quel système tourne ta console ?", PAD_X, 110, C_TEXT, AL_LEFT);
    int x = PAD_X;
    x += text(F_BODY, "Les fichiers vérifiés changent selon ta réponse. Détecté : ", x, 146, C_MUTED, AL_LEFT);
    text(F_BODYB, mode_name(sysmode), x, 146, sysmode ? C_ACCENT2 : C_MUTED, AL_LEFT);

    static const char *cfw_items[] = { "Modules système dans dev_flash", "Plugins au démarrage (boot_plugins.txt)",
                                       "Configuration webMAN MOD", "Jeux, applis et espace disque", NULL };
    static const char *hen_items[] = { "Modules système dans dev_flash", "Fichiers HEN (dev_hdd0/hen)",
                                       "Plugins HEN", "Jeux, applis et espace disque", NULL };
    int gap = 20, w = (CONTENT_W - gap) / 2, y = 188, h = 338;
    choice_card(PAD_X, y, w, h, choice_sel == 0, "Custom Firmware",
                sysmode != MODE_HEN ? "RECOMMANDÉ" : NULL,
                "Console avec un CFW installé (Evilnat, Rebug...). Reste actif après un redémarrage.", cfw_items);
    choice_card(PAD_X + w + gap, y, w, h, choice_sel == 1, "PS3HEN",
                sysmode == MODE_HEN ? "RECOMMANDÉ" : NULL,
                "Firmware officiel avec HEN activé. HEN se désactive à chaque redémarrage.", hen_items);

    int ny = y + h + 18;
    card(PAD_X, ny, CONTENT_W, 52, 12, 1, C_LINE, C_BG);
    icon(I_SHIELD, PAD_X + 18, ny + 14, 24, C_OK, C_BG);
    text(F_BODY, "L'analyse lit les fichiers sans jamais rien modifier. Elle ne peut pas abîmer ta console.",
         PAD_X + 56, ny + 26 - text_h(F_BODY) / 2, C_TEXT2, AL_LEFT);

    footer_line();
    static const int gl[] = { G_CROSS, G_CIRCLE };
    static const char *lb[] = { "Lancer l'analyse", "Retour" };
    hints_right(FOOT_Y, SCREEN_W - PAD_X, gl, lb, 2);
}

static unsigned done_at = 0;

static void do_run(void)
{
    if (pad.pressed[B_CIRCLE]) { scan_stop(); screen = S_CHOICE; return; }
    scan_tick(12);
    if (scan.done) {
        if (!done_at) done_at = frame;
        if (frame - done_at > 40) {
            done_at = 0; screen = S_RESULT; refresh_space();
            char a[64];
            snprintf(a, sizeof(a), "%s;%d;%d", scan.mode == MODE_HEN ? "HEN" : "CFW", scan.warns, scan.errs);
            log_add("Analyse système (%s) : %s avertissement(s), %s problème(s)", a);
        }
    }

    char sub[160];
    snprintf(sub, sizeof(sub), T("Mode %s · Étape 2 sur 3"), scan.mode == MODE_HEN ? "HEN" : "CFW");
    header(I_SCAN, scan.done ? "Analyse terminée" : "Analyse en cours", sub);

    int y = 108, h = FOOT_LINE - 24 - y, rw = 460, lw = CONTENT_W - rw - 20;
    card(PAD_X, y, lw, h, 18, 1, C_LINE, C_PANEL);
    int pct = (int)(scan.progress * 100);
    char ps[8];
    snprintf(ps, sizeof(ps), "%d", pct);
    int px = PAD_X + 36, pyy = y + 100;
    int w = text(F_HUGE, ps, px, pyy, C_TEXT, AL_LEFT);
    text(F_H1, "%", px + w + 10, pyy + 44, C_MUTED, AL_LEFT);
    bar(px, pyy + 120, lw - 72, 10, scan.progress, C_ACCENT);
    text(F_SMALL, "Fichier en cours", px, pyy + 156, C_MUTED, AL_LEFT);
    text_fit_left(F_BODY, scan.done ? "Terminé" : scan.current, px, pyy + 180, lw - 72, C_TEXT2);

    char a[16], b[16], c[16];
    snprintf(a, sizeof(a), "%d", scan.files);
    snprintf(b, sizeof(b), "%d", scan.warns);
    snprintf(c, sizeof(c), "%d", scan.errs);
    int sx = px, sy = pyy + 236;
    sx += text(F_H2, a, sx, sy, C_TEXT, AL_LEFT) + 8;
    sx += text(F_SMALL, "Fichiers lus", sx, sy + 6, C_MUTED, AL_LEFT) + 30;
    sx += text(F_H2, b, sx, sy, C_WARN, AL_LEFT) + 8;
    sx += text(F_SMALL, "Avertissements", sx, sy + 6, C_MUTED, AL_LEFT) + 30;
    sx += text(F_H2, c, sx, sy, C_ERR, AL_LEFT) + 8;
    text(F_SMALL, "Problèmes", sx, sy + 6, C_MUTED, AL_LEFT);

    int rx = PAD_X + lw + 20;
    card(rx, y, rw, h, 18, 1, C_LINE, C_PANEL);
    section_label("Vérifications", rx + 24, y + 24);
    for (int i = 0; i < SCAN_STEPS; i++) {
        int ry = y + 58 + i * 64;
        int st = scan.state[i];
        Col bg = st == ST_RUN ? C_PANEL_SEL : C_PANEL;
        if (st == ST_RUN) fill_rrect(rx + 12, ry, rw - 24, 56, 10, C_PANEL_SEL);
        int icx = rx + 38, icy = ry + 28;
        if (st == ST_DONE) icon(I_CHECK, icx - 12, icy - 12, 24, C_OK, bg);
        else if (st == ST_RUN) {
            ring(icx, icy, 11, 2, HEXA(0x3D9BFF, 70), bg);
            float an = frame * 0.18f;
            fill_circle(icx + (int)(9 * cosf(an)), icy + (int)(9 * sinf(an)), 3, C_ACCENT);
        } else ring(icx, icy, 11, 2, C_LINE3, bg);
        text(F_BODY, scan.names[i], rx + 64, icy - text_h(F_BODY) / 2, st == ST_TODO ? C_MUTED : C_TEXT, AL_LEFT);
        char inf[64];
        scan_step_info(i, inf, sizeof(inf));
        text(F_SMALL, inf, rx + rw - 28, icy - text_h(F_SMALL) / 2, C_MUTED, AL_RIGHT);
    }

    footer_line();
    static const int gl[] = { G_CIRCLE };
    static const char *lb[] = { "Annuler" };
    hints_right(FOOT_Y, SCREEN_W - PAD_X, gl, lb, 1);
}

static NetInfo netinf;
static unsigned net_last = 0;
static int net_logged = 1;

static void status_row(int x, int y, int w, const char *k, const char *v, Col vc)
{
    text(F_BODY, k, x, y, C_MUTED, AL_LEFT);
    text(F_BODYB, v, x + w, y, vc, AL_RIGHT);
}

static void spinner(int cx, int cy, Col bg)
{
    ring(cx, cy, 11, 2, HEXA(0x3D9BFF, 70), bg);
    float an = frame * 0.18f;
    fill_circle(cx + (int)(9 * cosf(an)), cy + (int)(9 * sinf(an)), 3, C_ACCENT);
}

static void do_net(void)
{
    if (pad.pressed[B_CIRCLE]) { screen = S_SYSTEM; return; }
    if (pad.pressed[B_TRIANGLE] || ticks_ms() - net_last > 3000) { net_info(&netinf); net_last = ticks_ms(); }
    if (pad.pressed[B_CROSS] && g_nettest.status != NT_RUNNING) { net_test_start(); net_logged = 0; }
    net_test_poll();
    if (g_nettest.status == NT_DONE && !net_logged) {
        net_logged = 1;
        char a[16];
        snprintf(a, sizeof(a), "%d", g_nettest.ms);
        if (g_nettest.web_ok) log_add("Test réseau réussi (%s ms)", a);
        else if (g_nettest.dns_ok) log_add("Test réseau : Internet inaccessible", "");
        else log_add("Test réseau : DNS en échec", "");
    }

    header(I_NET, "Réseau", NULL);
    int y = 108, h = FOOT_LINE - 24 - y, gap = 20, w = (CONTENT_W - gap) / 2;
    card(PAD_X, y, w, h, 18, 1, C_LINE, C_PANEL);
    section_label("État de la connexion", PAD_X + 24, y + 24);
    const char *st = netinf.state == NETS_ON ? "Connecté" :
                     netinf.state == NETS_CONNECTING ? "Connexion en cours..." : "Non connecté";
    Col sc = netinf.state == NETS_ON ? C_OK : netinf.state == NETS_CONNECTING ? C_WARN : C_ERR;
    fill_circle(PAD_X + 34, y + 86, 7, sc);
    text(F_H1, st, PAD_X + 54, y + 86 - text_h(F_H1) / 2, C_TEXT, AL_LEFT);
    int rx = PAD_X + 24, rw = w - 48;
    status_row(rx, y + 150, rw, "Type", netinf.state == NETS_OFF ? "—" : netinf.wireless ? "Wi-Fi" : "Câble Ethernet", C_TEXT);
    status_row(rx, y + 190, rw, "Réseau Wi-Fi", netinf.ssid[0] ? netinf.ssid : "—", C_TEXT);
    status_row(rx, y + 230, rw, "Adresse IP", netinf.ip[0] ? netinf.ip : "—", netinf.ip[0] ? C_ACCENT2 : C_TEXT);
    if (netinf.state != NETS_ON)
        text_wrap(F_SMALL, "Pas de réseau sur RPCS3 ? Active-le dans Configuration > Network > Network Status : Connected.",
                  rx, y + h - 64, rw, 20, C_MUTED);
    int x2 = PAD_X + w + gap;
    card(x2, y, w, h, 18, 1, C_LINE, C_PANEL);
    section_label("Test de connexion", x2 + 24, y + 24);
    int tx = x2 + 24, tw = w - 48;
    if (g_nettest.status == NT_IDLE) {
        text_wrap(F_BODY, "Vérifie que la console accède à Internet : recherche DNS, puis connexion à un serveur web.",
                  tx, y + 70, tw, 24, C_TEXT2);
        const char *l = "Lancer le test";
        hint(G_CROSS, l, x2 + w / 2 - hint_w(l) / 2, y + 190);
    } else if (g_nettest.status == NT_RUNNING) {
        spinner(x2 + w / 2, y + 150, C_PANEL);
        text(F_BODY, "Test en cours...", x2 + w / 2, y + 180, C_TEXT2, AL_CENTER);
    } else {
        int ok = g_nettest.web_ok;
        icon(ok ? I_CHECK : I_CROSS, tx, y + 66, 40, ok ? C_OK : C_ERR, C_PANEL);
        text(F_H2, ok ? "Internet accessible" : "Pas d'accès à Internet", tx + 56, y + 74, C_TEXT, AL_LEFT);
        status_row(tx, y + 150, tw, "Recherche DNS", g_nettest.dns_ok ? "OK" : "Échec", g_nettest.dns_ok ? C_OK : C_ERR);
        status_row(tx, y + 190, tw, "Connexion au serveur web", g_nettest.web_ok ? "OK" : "Échec", g_nettest.web_ok ? C_OK : C_ERR);
        char ms[24];
        if (g_nettest.ms >= 0) snprintf(ms, sizeof(ms), T("%d ms"), g_nettest.ms); else snprintf(ms, sizeof(ms), "—");
        status_row(tx, y + 230, tw, "Temps de réponse", ms, C_TEXT);
    }

    footer_line();
    static const int gl[] = { G_CROSS, G_TRIANGLE, G_CIRCLE };
    static const char *lb[] = { "Lancer le test", "Actualiser", "Retour" };
    hints_right(FOOT_Y, SCREEN_W - PAD_X, gl, lb, 3);
}

static SaveInfo sinfo;
static int save_user = 0, save_sel = 0, save_logged = 1;
static char save_result[200] = "";

static void do_saves(void)
{
    if (g_savejob.running) {
        if (pad.pressed[B_CIRCLE]) g_savejob.cancel = 1;
        saves_tick(12);
    } else {
        if (pad.pressed[B_CIRCLE]) { screen = S_SYSTEM; return; }
        if (pad.pressed[B_LEFT]) save_sel = 0;
        if (pad.pressed[B_RIGHT]) save_sel = 1;
        if (pad.pressed[B_TRIANGLE] && sinfo.n_users > 1) { save_user++; saves_info(&sinfo, save_user); }
        if (pad.pressed[B_CROSS]) {
            if (!sinfo.n_users) toast("Aucun utilisateur trouvé sur le disque dur");
            else if (!sinfo.usb_present) toast("Aucune clé USB détectée (dev_usb000)");
            else if (save_sel == 0 && sinfo.hdd_count == 0) toast("Aucune sauvegarde sur le disque dur");
            else if (save_sel == 1 && sinfo.usb_count == 0) toast("Aucune sauvegarde sur la clé USB");
            else if (saves_start(save_sel == 0 ? SAVE_TO_USB : SAVE_TO_HDD, sinfo.user) == 0) { save_logged = 0; save_result[0] = 0; }
            else toast("Impossible de lire le dossier des sauvegardes");
        }
    }
    if (g_savejob.done && !save_logged) {
        save_logged = 1;
        char a[64];
        if (g_savejob.dir == SAVE_TO_USB) {
            snprintf(a, sizeof(a), "%d;%d", g_savejob.saves_copied, g_savejob.errors);
            log_add("Sauvegarde vers la clé USB : %s copiée(s), %s erreur(s)", a);
            char *p1 = strtok(a, ";");
            char *p2 = strtok(NULL, ";");
            snprintf(save_result, sizeof(save_result), T("Sauvegarde vers la clé USB : %s copiée(s), %s erreur(s)"), p1, p2);
        } else {
            snprintf(a, sizeof(a), "%d;%d;%d", g_savejob.saves_copied, g_savejob.saves_skipped, g_savejob.errors);
            log_add("Restauration vers le disque dur : %s copiée(s), %s ignorée(s), %s erreur(s)", a);
            char *p1 = strtok(a, ";");
            char *p2 = strtok(NULL, ";");
            char *p3 = strtok(NULL, ";");
            snprintf(save_result, sizeof(save_result), T("Restauration vers le disque dur : %s copiée(s), %s ignorée(s), %s erreur(s)"),
                     p1, p2, p3);
        }
        toast(g_savejob.cancel ? "Copie annulée" : g_savejob.errors ? "Copie terminée avec des erreurs" : "Copie terminée");
        saves_info(&sinfo, save_user);
    }

    char sub[96];
    snprintf(sub, sizeof(sub), T("Utilisateur %s"), sinfo.n_users ? sinfo.user : "—");
    header(I_SAVE, "Sauvegardes", sub);
    int y = 108, gap = 20, w = (CONTENT_W - gap) / 2;
    char v[64];
    card(PAD_X, y, w, 64, 14, 1, C_LINE, C_PANEL);
    icon(I_CHIP, PAD_X + 20, y + 18, 28, C_TEXT2, C_PANEL);
    text(F_BODY, "Disque dur", PAD_X + 62, y + 32 - text_h(F_BODY) / 2, C_TEXT2, AL_LEFT);
    snprintf(v, sizeof(v), T("Sauvegardes : %d"), sinfo.hdd_count);
    text(F_BODYB, v, PAD_X + w - 24, y + 32 - text_h(F_BODYB) / 2, C_TEXT, AL_RIGHT);
    card(PAD_X + w + gap, y, w, 64, 14, 1, C_LINE, C_PANEL);
    icon(I_SAVE, PAD_X + w + gap + 20, y + 18, 28, C_TEXT2, C_PANEL);
    text(F_BODY, "Clé USB", PAD_X + w + gap + 62, y + 32 - text_h(F_BODY) / 2, C_TEXT2, AL_LEFT);
    if (sinfo.usb_present) snprintf(v, sizeof(v), T("Sauvegardes : %d"), sinfo.usb_count);
    else snprintf(v, sizeof(v), "%s", T("Non branchée"));
    text(F_BODYB, v, PAD_X + 2 * w + gap - 24, y + 32 - text_h(F_BODYB) / 2, sinfo.usb_present ? C_TEXT : C_WARN, AL_RIGHT);
    int cy = y + 84, ch = 270;
    static const char *titles[2] = { "Disque dur → Clé USB", "Clé USB → Disque dur" };
    static const char *subs[2] = { "Sauvegarder sur la clé", "Restaurer depuis la clé" };
    static const char *descs[2] = {
        "Copie toutes tes sauvegardes dans PS3/SAVEDATA sur la clé. Les copies déjà présentes sur la clé sont mises à jour.",
        "Copie les sauvegardes de la clé vers le disque dur. Une sauvegarde déjà présente sur le disque dur n'est jamais écrasée."
    };
    for (int i = 0; i < 2; i++) {
        int x = PAD_X + i * (w + gap), sel = i == save_sel;
        Col bg = sel ? C_PANEL_SEL : C_PANEL;
        if (sel) card(x, cy, w, ch, 18, 2, C_ACCENT, bg); else card(x, cy, w, ch, 18, 1, C_LINE, bg);
        text(F_SMALL, subs[i], x + 28, cy + 26, sel ? C_ACCENT2 : C_MUTED, AL_LEFT);
        text(F_H1, titles[i], x + 28, cy + 50, C_TEXT, AL_LEFT);
        text_wrap(F_BODY, descs[i], x + 28, cy + 112, w - 56, 24, C_TEXT2);
    }

    int ny = cy + ch + 18;
    if (g_savejob.running) {
        card(PAD_X, ny, CONTENT_W, 92, 14, 1, C_ACCENT, C_PANEL_SEL);
        char pr[96];
        snprintf(pr, sizeof(pr), T("Copie en cours : %d / %d fichiers"), g_savejob.files_done, g_savejob.files_total);
        text(F_BODYB, pr, PAD_X + 24, ny + 16, C_TEXT, AL_LEFT);
        float f = g_savejob.files_total ? (float)g_savejob.files_done / g_savejob.files_total : 0;
        bar(PAD_X + 24, ny + 46, CONTENT_W - 48, 8, f, C_ACCENT);
        text_fit_left(F_SMALL, g_savejob.current, PAD_X + 24, ny + 62, CONTENT_W - 48, C_MUTED);
    } else if (save_result[0]) {
        card(PAD_X, ny, CONTENT_W, 52, 12, 1, C_LINE, C_BG);
        icon(g_savejob.errors ? I_WARN : I_CHECK, PAD_X + 18, ny + 14, 24, g_savejob.errors ? C_WARN : C_OK, C_BG);
        text(F_BODY, save_result, PAD_X + 56, ny + 26 - text_h(F_BODY) / 2, C_TEXT2, AL_LEFT);
    } else {
        card(PAD_X, ny, CONTENT_W, 52, 12, 1, C_LINE, C_BG);
        icon(I_SHIELD, PAD_X + 18, ny + 14, 24, C_OK, C_BG);
        text(F_BODY, "Les sauvegardes restaurées fonctionnent sur la console et le compte qui les ont créées.",
             PAD_X + 56, ny + 26 - text_h(F_BODY) / 2, C_TEXT2, AL_LEFT);
    }

    footer_line();
    if (g_savejob.running) {
        static const int gl[] = { G_CIRCLE };
        static const char *lb[] = { "Annuler" };
        hints_right(FOOT_Y, SCREEN_W - PAD_X, gl, lb, 1);
    } else if (sinfo.n_users > 1) {
        static const int gl[] = { G_CROSS, G_TRIANGLE, G_CIRCLE };
        static const char *lb[] = { "Copier", "Changer d'utilisateur", "Retour" };
        hints_right(FOOT_Y, SCREEN_W - PAD_X, gl, lb, 3);
    } else {
        static const int gl[] = { G_CROSS, G_CIRCLE };
        static const char *lb[] = { "Copier", "Retour" };
        hints_right(FOOT_Y, SCREEN_W - PAD_X, gl, lb, 2);
    }
}

static int set_tab = 0, lang_sel = 0, log_sel = 0;
static const char *lang_names[LANG_COUNT] = { "Français", "English", "Русский" };

static void do_settings(void)
{
    if (pad.pressed[B_CIRCLE]) { screen = S_MENU; return; }
    if (pad.pressed[B_R1] || pad.pressed[B_RIGHT]) set_tab = (set_tab + 1) % 3;
    if (pad.pressed[B_L1] || pad.pressed[B_LEFT]) set_tab = (set_tab + 2) % 3;

    header(I_SETTINGS, "Settings", NULL);
    static const char *tabs[3] = { "Journal", "Langue", "À propos" };
    int tx = PAD_X;
    for (int i = 0; i < 3; i++) {
        int w = text_w(F_BODYB, tabs[i]) + 40;
        if (i == set_tab) fill_rrect(tx, 100, w, 40, 20, C_ACCENT);
        else card(tx, 100, w, 40, 20, 1, C_LINE, C_BG);
        text(F_BODYB, tabs[i], tx + w / 2, 120 - text_h(F_BODYB) / 2, i == set_tab ? C_BG : C_TEXT2, AL_CENTER);
        tx += w + 12;
    }
    text(F_SMALL, "L1 / R1 : changer d'onglet", SCREEN_W - PAD_X, 120 - text_h(F_SMALL) / 2, C_MUTED, AL_RIGHT);

    int y = 160, h = FOOT_LINE - 24 - y;
    card(PAD_X, y, CONTENT_W, h, 18, 1, C_LINE, C_PANEL);

    if (set_tab == 0) {
        if (pad.repeat[B_DOWN] && log_sel < g_log_n - 1) log_sel++;
        if (pad.repeat[B_UP] && log_sel > 0) log_sel--;
        if (pad.pressed[B_SQUARE] && g_log_n) { log_clear(); log_sel = 0; toast("Journal effacé"); }
        if (g_log_n == 0) {
            text(F_H3, "Aucun test pour l'instant", SCREEN_W / 2, y + 150, C_TEXT, AL_CENTER);
            text(F_BODY, "Utilise un outil : chaque test et chaque copie apparaîtra ici.", SCREEN_W / 2, y + 186, C_MUTED, AL_CENTER);
        }
        const int rows = 9, rh = 48;
        int top = log_sel - rows + 1;
        if (top < 0) top = 0;
        for (int i = 0; i < rows && top + i < g_log_n; i++) {
            int idx = g_log_n - 1 - (top + i);
            const LogEntry *e = &g_log[idx];
            int ry = y + 12 + i * rh;
            if (top + i == log_sel) fill_rrect(PAD_X + 12, ry, CONTENT_W - 24, rh - 4, 10, C_PANEL_SEL);
            char tm[24], line[256];
            log_time(e, tm, sizeof(tm));
            log_format(e, line, sizeof(line));
            text(F_SMALL, tm, PAD_X + 32, ry + 22 - text_h(F_SMALL) / 2, C_MUTED, AL_LEFT);
            text_fit_left(F_BODY, line, PAD_X + 160, ry + 22 - text_h(F_BODY) / 2, CONTENT_W - 200, C_TEXT);
        }
        footer_line();
        char cnt[64];
        snprintf(cnt, sizeof(cnt), T("Entrées : %d"), g_log_n);
        text(F_SMALL, cnt, PAD_X, FOOT_Y + 13 - text_h(F_SMALL) / 2, C_MUTED, AL_LEFT);
        static const int gl[] = { G_SQUARE, G_CIRCLE };
        static const char *lb[] = { "Effacer le journal", "Retour" };
        hints_right(FOOT_Y, SCREEN_W - PAD_X, gl, lb, 2);
    } else if (set_tab == 1) {
        if (pad.repeat[B_DOWN] && lang_sel < LANG_COUNT - 1) lang_sel++;
        if (pad.repeat[B_UP] && lang_sel > 0) lang_sel--;
        if (pad.pressed[B_CROSS] && lang_sel != g_lang) {
            g_lang = lang_sel;
            prefs_save();
            log_add("Langue : %s", lang_names[g_lang]);
            toast("Langue enregistrée");
        }
        text(F_BODY, "Choisis la langue de l'application :", PAD_X + 32, y + 28, C_TEXT2, AL_LEFT);
        for (int i = 0; i < LANG_COUNT; i++) {
            int ry = y + 70 + i * 72, sel = i == lang_sel;
            Col bg = sel ? C_PANEL_SEL : C_PANEL;
            if (sel) card(PAD_X + 24, ry, 520, 60, 14, 2, C_ACCENT, bg);
            else card(PAD_X + 24, ry, 520, 60, 14, 1, C_LINE, bg);
            ring(PAD_X + 56, ry + 30, 11, 2, i == g_lang ? C_ACCENT : C_LINE3, bg);
            if (i == g_lang) fill_circle(PAD_X + 56, ry + 30, 5, C_ACCENT);
            text(F_H3, lang_names[i], PAD_X + 84, ry + 30 - text_h(F_H3) / 2, C_TEXT, AL_LEFT);
            if (i == g_lang) text(F_SMALL, "Langue actuelle", PAD_X + 520, ry + 30 - text_h(F_SMALL) / 2, C_ACCENT2, AL_RIGHT);
        }
        footer_line();
        static const int gl[] = { G_CROSS, G_CIRCLE };
        static const char *lb[] = { "Choisir", "Retour" };
        hints_right(FOOT_Y, SCREEN_W - PAD_X, gl, lb, 2);
    } else {
        logo(PAD_X + 40, y + 40, 96);
        text(F_BIG, "PS3 Tools Kit", PAD_X + 168, y + 40, C_TEXT, AL_LEFT);
        char ver[64];
        snprintf(ver, sizeof(ver), T("Version %s"), APP_VERSION);
        text(F_BODY, ver, PAD_X + 168, y + 100, C_MUTED, AL_LEFT);
        int kx = PAD_X + 40, kw = CONTENT_W - 80, ky = y + 176;
        status_row(kx, ky, kw, "Créateur", "North", C_TEXT);
        status_row(kx, ky + 44, kw, "Code source", "github.com/Dev-TheNorthFace/ps3-tools-kit", C_ACCENT2);
        status_row(kx, ky + 88, kw, "Licence", "Logiciel libre et open source", C_OK);
        fill_rect(kx, ky + 130, kw, 1, C_LINE);
        text_wrap(F_SMALL, "Merci à : police Inter (SIL Open Font License 1.1), Cairo, FreeType et PSL1GHT. Le code source complet est disponible sur GitHub : tu peux le lire, le modifier et le partager.",
                  kx, ky + 150, kw, 20, C_MUTED);
        footer_line();
        static const int gl[] = { G_CIRCLE };
        static const char *lb[] = { "Retour" };
        hints_right(FOOT_Y, SCREEN_W - PAD_X, gl, lb, 1);
    }
}

static int dump_sel = 0, dump_tab = 0, info_scroll = 0;
static int dest_open = 0, dest_sel = 0;
static unsigned dest_last = 0;
static int mod_unlocked = 0, mod_sel = 0, confirm_open = 0, confirm_armed = 0, reboot_opt = 0;
static unsigned hold_t0 = 0;

#include "ui_tools.inc"
#include "ui_dev.inc"
#include "ui_kit.inc"

static void enter_screen(int s)
{
    screen = s;
    tools_enter(s);
    switch (s) {
    case S_FILES: files_pick = 0; files_from_dev = 0; open_files_root(); break;
    case S_INFO: refresh_temps(); refresh_space(); info_tab = 0; usb_poll(); { int u = 0; for (int i = 0; i < USB_N; i++) u += g_usb[i].present; si_detect(pad.connected ? 1 : 0, u, sysmode); } break;
    case S_CHOICE: choice_sel = (sysmode == MODE_HEN) ? 1 : 0; break;
    case S_PAD: pad_seen = 0; break;
    case S_SCREEN: colors_seen = 0; color_shown = 0; break;
    case S_NET: net_info(&netinf); net_last = ticks_ms(); break;
    case S_SAVES: saves_info(&sinfo, save_user); save_result[0] = 0; break;
    case S_SETTINGS: set_tab = 0; lang_sel = g_lang; log_sel = 0; break;
    case S_DUMPS_WARN:
        dumps_detect();
        dumps_reset_results();
        patch_reset();
        dump_sel = 0; dest_open = 0;
        mod_unlocked = 0; mod_sel = 0; confirm_open = 0; hold_t0 = 0;
        break;
    }
}

static void do_dumps_warn(void)
{
    if (pad.pressed[B_CIRCLE]) { screen = S_MENU; return; }
    if (pad.pressed[B_CROSS]) { screen = S_DUMPS; dump_tab = 0; info_scroll = 0; return; }

    int w = 860, h = 470, x = (SCREEN_W - w) / 2, y = 96;
    card(x, y, w, h, 22, 2, C_WARN, C_PANEL);
    icon(I_WARN, x + 40, y + 36, 52, C_WARN, C_PANEL);
    text(F_BIG, "Avertissement", x + 112, y + 36, C_TEXT, AL_LEFT);
    static const char *lines[4] = {
        "La plupart des dumps ne fonctionnent que sur une console en Custom Firmware (CFW).",
        "Le créateur de ce logiciel n'est en aucun cas responsable des dégâts causés par certaines manipulations.",
        "Les risques de brick de la console augmentent si tu fais une mauvaise manipulation.",
        "Les dumps contiennent l'identité unique de ta console (IDPS, PSID, clés) : ne les partage jamais.",
    };
    int ty = y + 124;
    for (int i = 0; i < 4; i++) {
        fill_circle(x + 50, ty + 11, 4, C_WARN);
        int n = text_wrap(F_BODY, lines[i], x + 70, ty, w - 110, 26, C_TEXT2);
        ty += n * 26 + 18;
    }
    text_wrap(F_SMALL, "L'onglet Outils ne fait que lire. Les outils qui écrivent sont dans l'onglet Modifications, verrouillé. Lis l'onglet Informations avant de commencer.",
              x + 40, y + h - 66, w - 80, 20, C_MUTED);

    footer_line();
    static const int gl[] = { G_CROSS, G_CIRCLE };
    static const char *lb[] = { "J'ai compris", "Retour" };
    hints_right(FOOT_Y, SCREEN_W - PAD_X, gl, lb, 2);
}

static Col risk_col(int risk) { return risk == RISK_RED ? C_ERR : C_WARN; }

static Col dump_col(int st)
{
    return st == DR_OK ? C_OK : st == DR_WARN ? C_WARN : st == DR_ERR ? C_ERR : C_LINE3;
}

static void dumps_tabs(void)
{
    static const char *tabs[3] = { "Outils", "Modifications", "Informations" };
    int tx = PAD_X;
    for (int i = 0; i < 3; i++) {
        int w = text_w(F_BODYB, tabs[i]) + 40;
        if (i == dump_tab) fill_rrect(tx, 100, w, 40, 20, C_ACCENT);
        else card(tx, 100, w, 40, 20, 1, C_LINE, C_BG);
        text(F_BODYB, tabs[i], tx + w / 2, 120 - text_h(F_BODYB) / 2, i == dump_tab ? C_BG : C_TEXT2, AL_CENTER);
        tx += w + 12;
    }
    const char *env = dumps_env_text();
    int cw = text_w(F_SMALL, env) + 32;
    card(SCREEN_W - PAD_X - cw, 102, cw, 36, 18, 1, C_LINE, C_BG);
    text(F_SMALL, env, SCREEN_W - PAD_X - cw / 2, 120 - text_h(F_SMALL) / 2, C_TEXT2, AL_CENTER);
}

static void dumps_tools(void)
{
    if (!g_djob.running) {
        if (pad.repeat[B_DOWN] && dump_sel < D_COUNT - 1) dump_sel++;
        if (pad.repeat[B_UP] && dump_sel > 0) dump_sel--;
        if (pad.pressed[B_CROSS]) dumps_run(dump_sel);
    } else if (pad.pressed[B_CIRCLE]) {
        g_djob.cancel = 1;
    }

    int y = 160, h = FOOT_LINE - 24 - y, lw = 430;
    card(PAD_X, y, lw, h, 18, 1, C_LINE, C_PANEL);
    const int rh = 31;
    for (int i = 0; i < D_COUNT; i++) {
        int ry = y + 10 + i * rh, sel = i == dump_sel;
        if (sel) fill_rrect(PAD_X + 10, ry, lw - 20, rh - 2, 9, C_PANEL_SEL);
        Col bg = sel ? C_PANEL_SEL : C_PANEL;
        int st = g_dump_res[i].status;
        if (st == DR_NONE) ring(PAD_X + 30, ry + rh / 2 - 1, 6, 2, C_LINE3, bg);
        else fill_circle(PAD_X + 30, ry + rh / 2 - 1, 6, dump_col(st));
        text(F_BODY, g_dumps[i].name, PAD_X + 48, ry + rh / 2 - 1 - text_h(F_BODY) / 2, sel ? C_TEXT : C_TEXT2, AL_LEFT);
        if (g_djob.running && g_djob.item == i) spinner(PAD_X + lw - 34, ry + rh / 2 - 1, bg);
    }

    int dx = PAD_X + lw + 20, dw = SCREEN_W - PAD_X - dx;
    const DumpDef *d = &g_dumps[dump_sel];
    card(dx, y, dw, h, 18, 1, C_LINE, C_PANEL);
    text(F_H2, d->name, dx + 28, y + 24, C_TEXT, AL_LEFT);
    const char *tag = "LECTURE SEULE";
    int tw = text_w(F_SMALL, tag) + 24;
    fill_rrect(dx + dw - 28 - tw, y + 26, tw, 26, 13, C_OK);
    text(F_SMALL, tag, dx + dw - 28 - tw / 2, y + 39 - text_h(F_SMALL) / 2, C_BG, AL_CENTER);
    int n = text_wrap(F_BODY, d->desc, dx + 28, y + 70, dw - 56, 24, C_TEXT2);
    int ky = y + 70 + n * 24 + 22;
    fill_rect(dx + 28, ky, dw - 56, 1, C_LINE);
    ky += 16;
    text(F_SMALL, "Nécessite", dx + 28, ky, C_MUTED, AL_LEFT);
    int nn = text_wrap(F_SMALL, d->needs, dx + 170, ky, dw - 198, 20, C_TEXT);
    ky += nn * 20 + 12;
    text(F_SMALL, "Emplacement", dx + 28, ky, C_MUTED, AL_LEFT);
    {
        int ready = dumps_dest_ready(), usb = strncmp(g_denv.root, "/dev_usb", 8) == 0;
        const DumpDest *cur = NULL;
        for (int i = 0; i < g_ndest; i++) if (strcmp(g_dests[i].root, g_denv.root) == 0) cur = &g_dests[i];
        char line[128], fs[32];
        if (!ready) snprintf(line, sizeof(line), "%s · %s", dumps_dest_name(g_denv.root), T(usb ? "non branchée" : "indisponible"));
        else if (cur && cur->free_ok) {
            fmt_size(cur->free_bytes, fs, sizeof(fs));
            char fr[48];
            snprintf(fr, sizeof(fr), T("%s libres"), fs);
            snprintf(line, sizeof(line), "%s · %s", dumps_dest_name(g_denv.root), fr);
        } else snprintf(line, sizeof(line), "%s", dumps_dest_name(g_denv.root));
        icon(usb ? I_USB : I_HDD, dx + 170, ky - 1, 20, ready ? C_ACCENT2 : C_ERR, C_PANEL);
        const char *chg = "Changer";
        int hw = hint_w(chg);
        text_fit_left(F_SMALL, line, dx + 198, ky, dw - 198 - 28 - hw - 16, ready ? C_TEXT : C_ERR);
        if (!g_djob.running) hint(G_TRIANGLE, chg, dx + dw - 28 - hw, ky + 10 - 13);
    }
    ky += 32;
    text(F_SMALL, "Fichier", dx + 28, ky, C_MUTED, AL_LEFT);
    text_fit_left(F_SMALL, dumps_last_path(dump_sel), dx + 170, ky, dw - 198, C_ACCENT2);
    ky += 32;
    text(F_SMALL, "Résultat", dx + 28, ky, C_MUTED, AL_LEFT);
    char res[256];
    dumps_result_text(dump_sel, res, sizeof(res));
    text_wrap(F_SMALL, res, dx + 170, ky, dw - 198, 20, g_dump_res[dump_sel].status == DR_NONE ? C_MUTED : dump_col(g_dump_res[dump_sel].status));

    if (g_djob.running) {
        int py = y + h - 110;
        card(dx + 20, py, dw - 40, 90, 14, 1, C_ACCENT, C_PANEL_SEL);
        char pr[160], dn[32], tt[32];
        fmt_size(g_djob.done, dn, sizeof(dn));
        fmt_size(g_djob.total, tt, sizeof(tt));
        snprintf(pr, sizeof(pr), T(g_djob.phase == 3 ? "Comparaison : %s / %s" : g_djob.phase == 2 ? "Relecture du fichier : %s / %s" : g_djob.phase ? "Vérification : %s / %s" : "Lecture : %s / %s"), dn, tt);
        text(F_BODYB, g_dumps[g_djob.item].name, dx + 44, py + 14, C_TEXT, AL_LEFT);
        text(F_SMALL, pr, dx + dw - 44, py + 16, C_TEXT2, AL_RIGHT);
        bar(dx + 44, py + 50, dw - 88, 10, g_djob.total ? (float)g_djob.done / g_djob.total : 0, C_ACCENT);
        text(F_SMALL, "N'éteins pas la console pendant la copie.", dx + 44, py + 66, C_MUTED, AL_LEFT);
    } else {
        const char *l = "Lancer";
        hint(G_CROSS, l, dx + 28, y + h - 50);
    }

    footer_line();
    text_fit_left(F_SMALL, g_denv.outdir, PAD_X, FOOT_Y + 13 - text_h(F_SMALL) / 2, 420, C_MUTED);
    if (g_djob.running) {
        static const int gl[] = { G_CIRCLE };
        static const char *lb[] = { "Annuler" };
        hints_right(FOOT_Y, SCREEN_W - PAD_X, gl, lb, 1);
    } else {
        static const int gl[] = { G_TRIANGLE, G_CROSS, G_CIRCLE };
        static const char *lb[] = { "Emplacement", "Lancer", "Retour" };
        hints_right(FOOT_Y, SCREEN_W - PAD_X, gl, lb, 3);
    }
}

typedef struct { int kind; const char *text; int tool; } InfoItem;
static const InfoItem infos[] = {
    { 0, "Avant tout", 0 },
    { 1, "L'onglet Outils lit des zones sensibles de la console sans rien modifier. Chaque dump a son propre dossier daté dans PS3TK_DUMPS (clé USB conseillée, ou disque dur) : un ancien dump n'est jamais écrasé. Les fichiers sont écrits en .partial, relus en entier, puis validés avec une empreinte SHA-256 (SHA256SUMS.txt). Triangle change l'emplacement.", 0 },
    { 1, "L'onglet Modifications contient les outils qui écrivent dans la console. Il est verrouillé : maintiens L2 + R2 pendant 3 secondes pour l'ouvrir.", 0 },
    { 1, "Ces outils ne fonctionnent pas sur RPCS3 : l'émulateur n'a ni vraie flash, ni hyperviseur, ni Syscon. Ils se testent uniquement sur une vraie console en CFW.", 0 },
    { 0, "Ce que fait chaque outil", 0 },
    { 3, NULL, D_ERRLOG }, { 3, NULL, D_AIM }, { 3, NULL, D_VTRM }, { 3, NULL, D_LV1 }, { 3, NULL, D_LV2 },
    { 3, NULL, D_PTAB }, { 3, NULL, D_EEID }, { 3, NULL, D_FLASH }, { 3, NULL, D_SRK }, { 3, NULL, D_SYSROM },
    { 3, NULL, D_APPVER }, { 3, NULL, D_TOKEN }, { 3, NULL, D_TEMP },
    { 0, "Onglet Modifications", 0 },
    { 1, "Chaque outil suit trois étapes : vérifier (rien n'est écrit), confirmer, puis exécuter. Les outils rouges demandent de maintenir L2 + R2 pendant 3 secondes.", 0 },
    { 1, "Flash EEID et Flash NOR lisent le fichier dans le dossier PS3TK_DUMPS/A_FLASHER de l'emplacement choisi. Le fichier doit venir de cette console. Avant d'écrire, l'appli sauvegarde la zone actuelle dans un dossier daté, la relit en entier et la compare à la flash : au moindre écart, rien n'est écrit. Elle n'écrit que les blocs qui changent, puis relit toute la zone ; si la vérification finale échoue, l'état d'origine est remis et revérifié, et RECUPERATION.txt explique quoi faire.", 0 },
    { 4, NULL, P_REBOOT }, { 4, NULL, P_RECOVER }, { 4, NULL, P_RSOD }, { 4, NULL, P_EEID }, { 4, NULL, P_NOR },
    { 0, "Comment ne pas bricker ta console", 0 },
    { 2, "Fais d'abord un Dump NAND/NOR, puis vérifie-le sur un PC avec PyPS3checker. Sans dump valide, ne touche à rien.", 0 },
    { 2, "Garde plusieurs copies de tes dumps (PC et clé USB), jamais uniquement sur la console.", 0 },
    { 2, "N'écris jamais dans la flash (Flash NOR, Flash EEID, patchs) sans dump vérifié, sans flasher matériel de secours et sans savoir exactement ce que tu fais.", 0 },
    { 2, "Pendant une écriture ou une installation de firmware : n'éteins pas la console, ne débranche rien et évite les coupures de courant.", 0 },
    { 2, "N'installe que des CFW et des PKG venant de sources reconnues, et vérifie leur MD5.", 0 },
    { 2, "Ne mélange jamais les fichiers de deux consoles : l'EEID, la VTRM et les clés sont uniques à chaque PS3.", 0 },
    { 2, "Un écran rouge (RSOD) ou une lumière jaune (YLOD) qui reviennent indiquent souvent un problème matériel : un logiciel ne peut pas tout réparer.", 0 },
    { 2, "Ne partage jamais tes dumps en ligne : ils contiennent l'identité de ta console (IDPS, PSID).", 0 },
    { 2, "Avant un outil de l'onglet Modifications, prépare une clé USB avec PS3/UPDATE/PS3UPDAT.PUP de ton CFW : en cas de souci, tu pourras le réinstaller depuis le menu Recovery.", 0 },
    { 2, "Pendant une écriture, n'appuie jamais sur le bouton PS pour quitter l'appli.", 0 },
    { 2, "En cas de doute, demande conseil (psdevwiki, PSX-Place) avant d'agir.", 0 },
    { 0, "Crédits", 0 },
    { 1, "Méthodes basées sur la documentation de psdevwiki, le PS3 Advanced Toolset et webMAN MOD. Contrairement à l'outil d'origine, PS3 Tools Kit n'applique aucun correctif à la mémoire de LV1 : une demande refusée par la console reste refusée. Les outils d'écriture ajoutent des contrôles que l'outil d'origine n'a pas (taille, IDPS, sauvegarde, relecture, restauration).", 0 },
};
#define N_INFOS (int)(sizeof(infos) / sizeof(infos[0]))

static void dumps_infos(void)
{
    int y = 160, h = FOOT_LINE - 24 - y, x = PAD_X, w = CONTENT_W;
    int tx = x + 32, tw = w - 64;
    card(x, y, w, h, 18, 1, C_LINE, C_PANEL);
    int total = 0;
    for (int i = 0; i < N_INFOS; i++) {
        const InfoItem *it = &infos[i];
        if (it->kind == 0) total += 50;
        else if (it->kind == 3) total += 28 + text_wrap_lines(F_SMALL, g_dumps[it->tool].desc, tw - 20) * 20 + 14;
        else if (it->kind == 4) total += 28 + text_wrap_lines(F_SMALL, g_patches[it->tool].desc, tw - 20) * 20 + 14;
        else total += text_wrap_lines(it->kind == 2 ? F_BODY : F_BODY, it->text, it->kind == 2 ? tw - 24 : tw) * 24 + 14;
    }
    int view = h - 40, maxs = total - view;
    if (maxs < 0) maxs = 0;
    if (pad.repeat[B_DOWN]) info_scroll += 48;
    if (pad.repeat[B_UP]) info_scroll -= 48;
    if (info_scroll > maxs) info_scroll = maxs;
    if (info_scroll < 0) info_scroll = 0;

    clip_push(x + 2, y + 2, w - 4, h - 4);
    int cy = y + 20 - info_scroll;
    for (int i = 0; i < N_INFOS; i++) {
        const InfoItem *it = &infos[i];
        if (it->kind == 0) {
            text(F_H3, it->text, tx, cy + 14, C_ACCENT2, AL_LEFT);
            cy += 50;
        } else if (it->kind == 4) {
            const PatchDef *pd = &g_patches[it->tool];
            fill_circle(tx + 5, cy + 11, 5, risk_col(pd->risk));
            text(F_BODYB, pd->name, tx + 20, cy, C_TEXT, AL_LEFT);
            int n = text_wrap(F_SMALL, pd->desc, tx + 20, cy + 26, tw - 20, 20, C_TEXT2);
            cy += 28 + n * 20 + 14;
        } else if (it->kind == 3) {
            text(F_BODYB, g_dumps[it->tool].name, tx, cy, C_TEXT, AL_LEFT);
            int n = text_wrap(F_SMALL, g_dumps[it->tool].desc, tx + 20, cy + 26, tw - 20, 20, C_TEXT2);
            cy += 28 + n * 20 + 14;
        } else if (it->kind == 2) {
            fill_circle(tx + 6, cy + 11, 3, C_WARN);
            int n = text_wrap(F_BODY, it->text, tx + 24, cy, tw - 24, 24, C_TEXT2);
            cy += n * 24 + 14;
        } else {
            int n = text_wrap(F_BODY, it->text, tx, cy, tw, 24, C_TEXT2);
            cy += n * 24 + 14;
        }
    }
    clip_pop();

    if (maxs > 0) {  
        int track = h - 40, th = track * view / total;
        if (th < 30) th = 30;
        int ty = y + 20 + (track - th) * info_scroll / maxs;
        fill_rrect(x + w - 12, ty, 4, th, 2, C_LINE2);
    }

    footer_line();
    text(F_SMALL, "Haut / Bas : faire défiler", PAD_X, FOOT_Y + 13 - text_h(F_SMALL) / 2, C_MUTED, AL_LEFT);
    static const int gl[] = { G_CIRCLE };
    static const char *lb[] = { "Retour" };
    hints_right(FOOT_Y, SCREEN_W - PAD_X, gl, lb, 1);
}

static void dest_open_picker(void)
{
    dumps_scan_dests();
    dest_sel = g_ndest - 1;                
    for (int i = 0; i < g_ndest; i++) if (strcmp(g_dests[i].root, g_denv.root) == 0) dest_sel = i;
    if (strncmp(g_denv.root, "/dev_usb", 8) == 0 && !dumps_dest_ready())  
        for (int i = 0; i < g_ndest; i++) if (g_dests[i].usb) { dest_sel = i; break; }
    dest_open = 1;
    dest_last = ticks_ms();
}

static void dest_picker(void)
{
    if (ticks_ms() - dest_last > 1000) {
        dest_last = ticks_ms();
        if (dumps_dests_changed()) {
            char keep[16];
            snprintf(keep, sizeof(keep), "%s", g_dests[dest_sel].root);
            dumps_scan_dests();
            dest_sel = g_ndest - 1;
            for (int i = 0; i < g_ndest; i++) if (strcmp(g_dests[i].root, keep) == 0) dest_sel = i;
        }
    }
    if (pad.repeat[B_DOWN] && dest_sel < g_ndest - 1) dest_sel++;
    if (pad.repeat[B_UP] && dest_sel > 0) dest_sel--;
    if (pad.pressed[B_CIRCLE] || pad.pressed[B_TRIANGLE]) { dest_open = 0; return; }
    if (pad.pressed[B_CROSS]) {
        dumps_set_dest(g_dests[dest_sel].root);
        dest_open = 0;
        char m[200];
        snprintf(m, sizeof(m), T("Les dumps seront enregistrés dans %s"), g_denv.outdir);
        toast(m);
        return;
    }

    int has_usb = 0;
    for (int i = 0; i < g_ndest; i++) if (g_dests[i].usb) has_usb = 1;
    int rows = g_ndest + (has_usb ? 0 : 1);
    const int rh = 78;
    int w = 760, h = 150 + rows * rh + 96, x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2 - 10;

    fill_rect(0, 0, SCREEN_W, SCREEN_H, HEXA(0x05080F, 190));
    card(x, y, w, h, 22, 1, C_LINE2, C_PANEL);
    icon(I_SAVE, x + 36, y + 32, 30, C_ACCENT, C_PANEL);
    text(F_H2, "Où enregistrer les dumps ?", x + 82, y + 30, C_TEXT, AL_LEFT);
    text_wrap(F_SMALL, "Un dossier PS3TK_DUMPS est créé à cet endroit. Ton choix est retenu pour les prochaines fois.",
              x + 82, y + 70, w - 82 - 36, 20, C_MUTED);

    int ry = y + 128;
    if (!has_usb) {   
        card(x + 24, ry, w - 48, rh - 10, 14, 1, C_LINE, C_PANEL);
        icon(I_USB, x + 48, ry + 17, 34, C_LINE3, C_PANEL);
        text(F_BODYB, "Clé USB", x + 100, ry + 12, C_MUTED, AL_LEFT);
        text_fit_left(F_SMALL, "Aucune clé détectée : branche-la, la liste se met à jour toute seule.", x + 100, ry + 40, w - 100 - 48, C_MUTED);
        ry += rh;
    }
    for (int i = 0; i < g_ndest; i++, ry += rh) {
        const DumpDest *d = &g_dests[i];
        int sel = i == dest_sel, cur = strcmp(d->root, g_denv.root) == 0;
        Col bg = sel ? C_PANEL_SEL : C_PANEL;
        card(x + 24, ry, w - 48, rh - 10, 14, 1, sel ? C_ACCENT : C_LINE, bg);
        icon(d->usb ? I_USB : I_HDD, x + 48, ry + 17, 34, sel ? C_ACCENT : C_TEXT2, bg);
        text(F_BODYB, dumps_dest_name(d->root), x + 100, ry + 12, C_TEXT, AL_LEFT);
        int nx = x + 100 + text_w(F_BODYB, dumps_dest_name(d->root)) + 12;
        if (d->usb) {   
            const char *tg = "Conseillé";
            int tw = text_w(F_SMALL, tg) + 20;
            fill_rrect(nx, ry + 13, tw, 22, 11, C_OK);
            text(F_SMALL, tg, nx + tw / 2, ry + 24 - text_h(F_SMALL) / 2, C_BG, AL_CENTER);
        }
        char sub[96];
        snprintf(sub, sizeof(sub), "%s/PS3TK_DUMPS", d->root);
        text_fit_left(F_SMALL, sub, x + 100, ry + 40, w - 100 - 260, C_MUTED);
        char fs[32], fr[48];
        if (d->free_ok) { fmt_size(d->free_bytes, fs, sizeof(fs)); snprintf(fr, sizeof(fr), T("%s libres"), fs); }
        else snprintf(fr, sizeof(fr), "%s", T("Espace libre inconnu"));
        text(F_SMALL, fr, x + w - 48, ry + 40, C_TEXT2, AL_RIGHT);
        if (cur) {
            const char *ac = "Actuel";
            text(F_SMALL, ac, x + w - 48, ry + 14, C_ACCENT2, AL_RIGHT);
        }
    }

    const char *tip = g_dests[dest_sel].usb
        ? "Conseillé : tes dumps quittent la console et se copient directement sur ton PC."
        : "Sur le disque dur, récupère ensuite tes dumps par FTP (webMAN MOD) ou avec un gestionnaire de fichiers.";
    text_wrap(F_SMALL, tip, x + 36, ry + 8, w - 72, 20, C_TEXT2);

    static const int gl[] = { G_CROSS, G_CIRCLE };
    static const char *lb[] = { "Choisir", "Annuler" };
    hints_right(y + h - 46, x + w - 32, gl, lb, 2);
}

static float hold_l2r2(unsigned ms)
{
    if (pad.held[B_L2] && pad.held[B_R2]) {
        if (!hold_t0) hold_t0 = ticks_ms();
        float p = (float)(ticks_ms() - hold_t0) / ms;
        return p > 1 ? 1 : p;
    }
    hold_t0 = 0;
    return 0;
}

static void hold_bar(int x, int y, int w, float p, Col c, const char *label)
{
    text(F_SMALL, label, x, y, C_TEXT2, AL_LEFT);
    fill_rrect(x, y + 28, w, 10, 5, C_LINE);
    if (p > 0) fill_rrect(x, y + 28, (int)(w * p) < 10 ? 10 : (int)(w * p), 10, 5, c);
}

static void mods_locked(void)
{
    float p = hold_l2r2(3000);
    if (p >= 1) {
        mod_unlocked = 1; hold_t0 = 0;
        log_add("Dumps Patch : zone de modification déverrouillée", "");
        return;
    }
    int y = 160, h = FOOT_LINE - 24 - y, x = PAD_X, w = CONTENT_W;
    card(x, y, w, h, 18, 1, HEXA(0xFF8A98, 90), C_PANEL);
    icon(I_WARN, x + 36, y + 32, 40, C_ERR, C_PANEL);
    text(F_H2, "Zone de modification", x + 96, y + 30, C_TEXT, AL_LEFT);
    text(F_BODY, "Ces outils écrivent dans ta console.", x + 96, y + 66, C_ERR, AL_LEFT);
    static const char *lines[] = {
        "Une erreur, un mauvais fichier ou une coupure de courant pendant une écriture peut bricker la console.",
        "Avant tout : fais un Dump NAND/NOR dans l'onglet Outils, puis vérifie-le sur PC avec PyPS3checker.",
        "Prépare une clé USB avec PS3/UPDATE/PS3UPDAT.PUP de ton CFW, pour pouvoir réinstaller en Recovery.",
        "Chaque outil vérifie d'abord tout sans rien écrire, puis te demande une confirmation.",
    };
    int ty = y + 116;
    for (int i = 0; i < 4; i++) {
        fill_circle(x + 50, ty + 11, 4, i == 0 ? C_ERR : C_WARN);
        int n = text_wrap(F_BODY, lines[i], x + 70, ty, w - 110, 26, C_TEXT2);
        ty += n * 26 + 14;
    }
    hold_bar(x + 40, y + h - 84, w - 80, p, C_ERR, "Maintiens L2 + R2 pendant 3 secondes pour déverrouiller");

    footer_line();
    text(F_SMALL, "Rien n'est écrit tant que tu n'as pas confirmé.", PAD_X, FOOT_Y + 13 - text_h(F_SMALL) / 2, C_MUTED, AL_LEFT);
    static const int gl[] = { G_CIRCLE };
    static const char *lb[] = { "Retour" };
    hints_right(FOOT_Y, SCREEN_W - PAD_X, gl, lb, 1);
}

static void check_icon(int st, int cx, int cy, Col bg)
{
    Col c = st == CK_OK ? C_OK : st == CK_WARN ? C_WARN : st == CK_FAIL ? C_ERR : C_ACCENT2;
    ring(cx, cy, 8, 2, c, bg);
    if (st == CK_OK) {
        thick_line(cx - 4, cy, cx - 1, cy + 3, 2, c);
        thick_line(cx - 1, cy + 3, cx + 4, cy - 3, 2, c);
    } else if (st == CK_FAIL) {
        thick_line(cx - 3, cy - 3, cx + 3, cy + 3, 2, c);
        thick_line(cx + 3, cy - 3, cx - 3, cy + 3, 2, c);
    } else if (st == CK_WARN) {
        thick_line(cx, cy - 4, cx, cy + 1, 2, c);
        fill_circle(cx, cy + 4, 1, c);
    } else {
        fill_circle(cx, cy - 4, 1, c);
        thick_line(cx, cy - 1, cx, cy + 4, 2, c);
    }
}

static void mods_list(int y, int h, int lw)
{
    card(PAD_X, y, lw, h, 18, 1, C_LINE, C_PANEL);
    int ry = y + 14;
    for (int i = 0; i < P_COUNT; i++) {
        if (i == 0 || g_patches[i].risk != g_patches[i - 1].risk) {
            section_label(g_patches[i].risk == RISK_RED ? "Risque de brick" : "Modifie un réglage", PAD_X + 24, ry + 6);
            fill_circle(PAD_X + lw - 30, ry + 14, 5, risk_col(g_patches[i].risk));
            ry += 36;
        }
        const int rh = 42;
        int sel = i == mod_sel;
        if (sel) fill_rrect(PAD_X + 10, ry, lw - 20, rh - 4, 10, C_PANEL_SEL);
        Col bg = sel ? C_PANEL_SEL : C_PANEL;
        fill_rrect(PAD_X + 10, ry + 8, 3, rh - 20, 1, risk_col(g_patches[i].risk));
        int st = g_patch_res[i].status;
        if (st == DR_NONE) ring(PAD_X + 34, ry + rh / 2 - 2, 6, 2, C_LINE3, bg);
        else fill_circle(PAD_X + 34, ry + rh / 2 - 2, 6, dump_col(st));
        text(F_BODY, g_patches[i].name, PAD_X + 52, ry + rh / 2 - 2 - text_h(F_BODY) / 2, sel ? C_TEXT : C_TEXT2, AL_LEFT);
        if ((g_pcheck.running && g_pcheck.item == i) || (g_pjob.running && g_pjob.item == i))
            spinner(PAD_X + lw - 34, ry + rh / 2 - 2, bg);
        ry += rh;
    }
}

static void mods_detail(int dx, int y, int dw, int h)
{
    const PatchDef *d = &g_patches[mod_sel];
    Col rc = risk_col(d->risk);
    card(dx, y, dw, h, 18, 1, C_LINE, C_PANEL);
    text(F_H2, d->name, dx + 28, y + 24, C_TEXT, AL_LEFT);
    const char *tag = d->risk == RISK_RED ? "RISQUE DE BRICK" : "MODIFIE UN RÉGLAGE";
    int tw = text_w(F_SMALL, tag) + 24;
    fill_rrect(dx + dw - 28 - tw, y + 26, tw, 26, 13, rc);
    text(F_SMALL, tag, dx + dw - 28 - tw / 2, y + 39 - text_h(F_SMALL) / 2, C_BG, AL_CENTER);

    int showing = g_pcheck.item == mod_sel && (g_pcheck.done || g_pcheck.running);
    int ky = y + 70;
    if (!showing) {
        int n = text_wrap(F_BODY, d->desc, dx + 28, ky, dw - 56, 24, C_TEXT2);
        ky += n * 24 + 18;
        fill_rect(dx + 28, ky, dw - 56, 1, C_LINE);
        ky += 14;
        text(F_SMALL, "Écrit", dx + 28, ky, C_MUTED, AL_LEFT);
        n = text_wrap(F_SMALL, d->writes, dx + 170, ky, dw - 198, 20, C_TEXT);
        ky += n * 20 + 10;
        text(F_SMALL, "Nécessite", dx + 28, ky, C_MUTED, AL_LEFT);
        n = text_wrap(F_SMALL, d->needs, dx + 170, ky, dw - 198, 20, C_TEXT);
        ky += n * 20 + 10;
        if (mod_sel == P_EEID || mod_sel == P_NOR) {
            text(F_SMALL, "Fichier", dx + 28, ky, C_MUTED, AL_LEFT);
            text_fit_left(F_SMALL, patch_src_path(mod_sel), dx + 170, ky, dw - 198, C_ACCENT2);
            ky += 30;
        }
        text(F_SMALL, "Résultat", dx + 28, ky, C_MUTED, AL_LEFT);
        char res[300];
        patch_result_text(mod_sel, res, sizeof(res));
        text_wrap(F_SMALL, res, dx + 170, ky, dw - 198, 20, g_patch_res[mod_sel].status == DR_NONE ? C_MUTED : dump_col(g_patch_res[mod_sel].status));
        if (!g_pjob.running) hint(G_CROSS, "Vérifier (rien n'est écrit)", dx + 28, y + h - 50);
    } else {
        text(F_SMALL, "Vérifications", dx + 28, ky, C_MUTED, AL_LEFT);
        ky += 28;
        for (int i = 0; i < g_pcheck.n; i++) {
            char t[300];
            patch_line_text(&g_pcheck.l[i], t, sizeof(t));
            check_icon(g_pcheck.l[i].st, dx + 38, ky + 10, C_PANEL);
            Col tc = g_pcheck.l[i].st == CK_FAIL ? C_ERR : g_pcheck.l[i].st == CK_WARN ? C_WARN : C_TEXT2;
            int n = text_wrap(F_SMALL, t, dx + 58, ky, dw - 86, 20, tc);
            ky += n * 20 + 8;
        }
        if (g_pcheck.running) {
            char pr[160], dn[32], tt[32];
            fmt_size(g_pcheck.done_b, dn, sizeof(dn));
            fmt_size(g_pcheck.total_b, tt, sizeof(tt));
            snprintf(pr, sizeof(pr), T("Lecture : %s / %s"), dn, tt);
            text(F_SMALL, pr, dx + 28, ky + 4, C_TEXT2, AL_LEFT);
            bar(dx + 28, ky + 30, dw - 56, 10, g_pcheck.total_b ? (float)g_pcheck.done_b / g_pcheck.total_b : 0, C_ACCENT);
        } else {
            int py = y + h - 104;
            if (g_pcheck.ready && !g_pcheck.nothing) {
                char pl[240];
                patch_plan_text(pl, sizeof(pl));
                card(dx + 20, py - 6, dw - 40, 44, 12, 1, rc, C_PANEL_SEL);
                text_fit_left(F_SMALL, pl, dx + 36, py + 6, dw - 72, C_TEXT);
                hint(G_CROSS, "Continuer", dx + 28, y + h - 50);
            } else if (g_pcheck.nothing) {
                text(F_BODYB, "Rien à faire : la flash est déjà identique.", dx + 28, py + 6, C_OK, AL_LEFT);
            } else {
                text(F_BODYB, "Conditions non remplies : rien ne sera écrit.", dx + 28, py + 6, C_ERR, AL_LEFT);
            }
        }
    }

    if (g_pjob.running) {  
        int py = y + h - 150;
        card(dx + 20, py, dw - 40, 130, 14, 2, C_ERR, C_PANEL_SEL);
        static const char *ph[] = { "Sauvegarde de la flash actuelle", "Écriture", "Vérification finale", "Restauration de l'état d'origine", "Vérification de la sauvegarde", "Vérification de la restauration" };
        char pr[160], dn[32], tt[32];
        fmt_size(g_pjob.done, dn, sizeof(dn));
        fmt_size(g_pjob.total, tt, sizeof(tt));
        snprintf(pr, sizeof(pr), "%s / %s", dn, tt);
        text(F_BODYB, ph[g_pjob.phase], dx + 44, py + 16, C_TEXT, AL_LEFT);
        text(F_SMALL, pr, dx + dw - 44, py + 18, C_TEXT2, AL_RIGHT);
        bar(dx + 44, py + 52, dw - 88, 10, g_pjob.total ? (float)g_pjob.done / g_pjob.total : 0, g_pjob.phase == PH_ROLLBACK ? C_WARN : C_ERR);
        text(F_BODYB, "N'éteins pas la console. N'appuie pas sur le bouton PS.", dx + 44, py + 78, C_ERR, AL_LEFT);
        if (g_pjob.exit_pending) text(F_SMALL, "Fermeture demandée : l'appli se fermera à la fin.", dx + 44, py + 104, C_WARN, AL_LEFT);
    }
}

static void mods_confirm(void)
{
    const PatchDef *d = &g_patches[mod_sel];
    int red = d->risk == RISK_RED;
    float p = 0;
    if (pad.pressed[B_CIRCLE]) { confirm_open = 0; hold_t0 = 0; return; }
    if (mod_sel == P_REBOOT && (pad.pressed[B_LEFT] || pad.pressed[B_RIGHT])) reboot_opt ^= 1;
    if (red) {
        if (!pad.held[B_L2] && !pad.held[B_R2]) confirm_armed = 1;
        p = confirm_armed ? hold_l2r2(3000) : 0;
        if (p >= 1) { confirm_open = 0; hold_t0 = 0; patch_execute(mod_sel, 0); return; }
    } else if (pad.pressed[B_CROSS]) {
        confirm_open = 0;
        patch_execute(mod_sel, reboot_opt);
        return;
    }

    const char *b[3] = { NULL, NULL, NULL };
    if (mod_sel == P_RECOVER) {
        b[0] = "Au prochain démarrage, la console ira directement dans le menu Recovery.";
        b[1] = "Si elle y revient à chaque fois, utilise l'option System Update avec la clé préparée.";
    } else if (mod_sel == P_RSOD) {
        b[0] = "Tes anciens trophées, sauvegardes et dumps peuvent ne plus être valables.";
        b[1] = "Après : redémarre, puis refais un Dump NAND/NOR.";
        b[2] = "N'éteins pas la console et n'appuie pas sur le bouton PS.";
    } else if (mod_sel != P_REBOOT) {
        b[0] = "La zone actuelle est d'abord sauvegardée sur l'emplacement choisi.";
        b[1] = "Chaque bloc écrit est relu ; en cas d'échec, l'état d'origine est remis.";
        b[2] = "N'éteins pas la console et n'appuie pas sur le bouton PS.";
    }
    int nb = 0;
    for (int i = 0; i < 3; i++) if (b[i]) nb++;
    int w = 780, h = mod_sel == P_REBOOT ? 300 : 92 + 66 + nb * 36 + (red ? 104 : 76);
    int x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2 - 10;
    Col rc = risk_col(d->risk);
    fill_rect(0, 0, SCREEN_W, SCREEN_H, HEXA(0x05080F, 200));
    card(x, y, w, h, 22, 2, rc, C_PANEL);
    icon(I_WARN, x + 36, y + 32, 34, rc, C_PANEL);
    char ttl[96];
    snprintf(ttl, sizeof(ttl), T("Confirmer : %s"), d->name);
    text(F_H2, ttl, x + 86, y + 32, C_TEXT, AL_LEFT);

    int ky = y + 92;
    if (mod_sel == P_REBOOT) {
        static const char *opt[2] = { "Redémarrage complet", "Redémarrage rapide" };
        static const char *sub[2] = { "Comme éteindre puis rallumer", "Ne relance que le système de jeu" };
        int bw = (w - 72 - 16) / 2;
        for (int i = 0; i < 2; i++) {
            int bx = x + 36 + i * (bw + 16), sel = i == reboot_opt;
            card(bx, ky, bw, 84, 14, sel ? 2 : 1, sel ? C_ACCENT : C_LINE, sel ? C_PANEL_SEL : C_PANEL);
            text(F_BODYB, opt[i], bx + 20, ky + 16, C_TEXT, AL_LEFT);
            text(F_SMALL, sub[i], bx + 20, ky + 48, C_MUTED, AL_LEFT);
        }
        ky += 110;
        text(F_SMALL, "Gauche / Droite : choisir", x + 36, ky, C_MUTED, AL_LEFT);
    } else {
        char pl[240];
        patch_plan_text(pl, sizeof(pl));
        card(x + 36, ky, w - 72, 46, 12, 1, rc, C_PANEL_SEL);
        text_fit_left(F_SMALL, pl, x + 52, ky + 13, w - 104, C_TEXT);
        ky += 66;
        for (int i = 0; i < 3 && b[i]; i++) {
            fill_circle(x + 46, ky + 11, 4, i == 2 ? C_ERR : C_WARN);
            int n = text_wrap(F_BODY, b[i], x + 64, ky, w - 100, 24, C_TEXT2);
            ky += n * 24 + 10;
        }
    }
    if (red) {
        hold_bar(x + 36, y + h - 76, w - 72, p, C_ERR, "Maintiens L2 + R2 pendant 3 secondes pour lancer");
        static const int gl[] = { G_CIRCLE };
        static const char *lb[] = { "Annuler" };
        hints_right(y + 26, x + w - 32, gl, lb, 1);
    } else {
        static const int gl[] = { G_CROSS, G_CIRCLE };
        static const char *lb[] = { "Confirmer", "Annuler" };
        hints_right(y + h - 52, x + w - 32, gl, lb, 2);
    }
}

static void dumps_mods(void)
{
    if (!mod_unlocked) { mods_locked(); return; }

    int busy = g_pcheck.running || g_pjob.running;
    if (!busy && !confirm_open) {
        if (pad.repeat[B_DOWN] && mod_sel < P_COUNT - 1) { mod_sel++; }
        if (pad.repeat[B_UP] && mod_sel > 0) { mod_sel--; }
        int showing = g_pcheck.item == mod_sel && g_pcheck.done;
        if (pad.pressed[B_CROSS]) {
            if (showing && g_pcheck.ready && !g_pcheck.nothing) {
                confirm_open = 1; confirm_armed = 0; hold_t0 = 0; reboot_opt = 0;
                memset(pad.pressed, 0, sizeof(pad.pressed));
                memset(pad.repeat, 0, sizeof(pad.repeat));
            }
            else patch_check(mod_sel);
        }
    } else if (g_pcheck.running && pad.pressed[B_CIRCLE]) {
        patch_check_cancel();
    } else if (g_pjob.running && (g_pjob.phase == PH_BACKUP || g_pjob.phase == PH_BACKUPCHK) && pad.pressed[B_CIRCLE]) {
        g_pjob.cancel = 1;          
    }

    int y = 160, h = FOOT_LINE - 24 - y, lw = 430;
    mods_list(y, h, lw);
    mods_detail(PAD_X + lw + 20, y, SCREEN_W - PAD_X - (PAD_X + lw + 20), h);

    footer_line();
    text(F_SMALL, g_denv.outdir, PAD_X, FOOT_Y + 13 - text_h(F_SMALL) / 2, C_MUTED, AL_LEFT);
    if (g_pjob.running && g_pjob.phase != PH_BACKUP && g_pjob.phase != PH_BACKUPCHK) {
        text(F_SMALL, "Écriture en cours", SCREEN_W - PAD_X, FOOT_Y + 13 - text_h(F_SMALL) / 2, C_ERR, AL_RIGHT);
    } else if (busy) {
        static const int gl[] = { G_CIRCLE };
        static const char *lb[] = { "Annuler" };
        hints_right(FOOT_Y, SCREEN_W - PAD_X, gl, lb, 1);
    } else {
        int showing = g_pcheck.item == mod_sel && g_pcheck.done;
        static const int gl[] = { G_CROSS, G_CIRCLE };
        const char *lb[] = { showing && g_pcheck.ready && !g_pcheck.nothing ? "Continuer" : "Vérifier", showing ? "Fermer" : "Retour" };
        hints_right(FOOT_Y, SCREEN_W - PAD_X, gl, lb, 2);
    }
    if (confirm_open) mods_confirm();
}

static void do_dumps(void)
{
    if (g_djob.running) dumps_tick(14);
    if (g_pcheck.running || g_pjob.running) patch_tick(g_pjob.exit_pending ? 60 : 14);
    int busy = g_djob.running || g_pcheck.running || g_pjob.running;
    if (!busy && !dest_open && !confirm_open) {
        int showing = dump_tab == 1 && mod_unlocked && g_pcheck.item == mod_sel && g_pcheck.done;
        if (pad.pressed[B_CIRCLE]) {
            if (showing) { patch_check_close(); pad.pressed[B_CIRCLE] = 0; }
            else { screen = S_MENU; return; }
        }
        if (pad.pressed[B_R1]) { dump_tab = (dump_tab + 1) % 3; hold_t0 = 0; }
        if (pad.pressed[B_L1]) { dump_tab = (dump_tab + 2) % 3; hold_t0 = 0; }
        if (dump_tab == 0 && pad.pressed[B_TRIANGLE]) { dest_open_picker(); pad.pressed[B_TRIANGLE] = 0; }
    }
    PadState saved;
    if (dest_open) { saved = pad; memset(pad.pressed, 0, sizeof(pad.pressed)); memset(pad.repeat, 0, sizeof(pad.repeat)); }
    header(I_DUMP, "Dumps Patch", NULL);
    dumps_tabs();
    if (dump_tab == 0) dumps_tools();
    else if (dump_tab == 1) dumps_mods();
    else dumps_infos();
    if (dest_open) { pad = saved; dest_picker(); }
}

static int r_sel = 0;

static void do_result(void)
{
    if (pad.pressed[B_CIRCLE]) { screen = S_SYSTEM; r_sel = 0; return; }
    if (pad.pressed[B_TRIANGLE]) { screen = S_CHOICE; r_sel = 0; return; }
    if (pad.pressed[B_SQUARE]) {
        char path[128], m[200];
        if (scan_export(path, sizeof(path)) == 0) {
            snprintf(m, sizeof(m), T("Rapport enregistré : %s"), path);
            log_add("Rapport d'analyse exporté : %s", path);
        }
        else snprintf(m, sizeof(m), "Impossible d'écrire le rapport");
        toast(m);
    }
    if (pad.repeat[B_DOWN] && r_sel < scan.nf - 1) r_sel++;
    if (pad.repeat[B_UP] && r_sel > 0) r_sel--;

    char sub[160];
    unsigned secs = (scan.end_ms - scan.start_ms) / 1000;
    snprintf(sub, sizeof(sub), T("Mode %s · %u min %02u s · Étape 3 sur 3"),
             scan.mode == MODE_HEN ? "HEN" : "CFW", secs / 60, secs % 60);
    header(I_SCAN, "Résultats de l'analyse", sub);

    int gap = 20, cw = (CONTENT_W - 2 * gap) / 3, y = 108, ch = 84;
    int ok_files = scan.files - scan.warns - scan.errs;
    if (ok_files < 0) ok_files = 0;
    char v[3][16];
    snprintf(v[0], 16, "%d", ok_files);
    snprintf(v[1], 16, "%d", scan.warns);
    snprintf(v[2], 16, "%d", scan.errs);
    const char *lab[3] = { "En bon état", "Avertissements", "Problèmes à corriger" };
    int ics[3] = { I_CHECK, I_WARN, I_CROSS };
    Col cs[3] = { C_OK, C_WARN, C_ERR };
    for (int i = 0; i < 3; i++) {
        int x = PAD_X + i * (cw + gap);
        card(x, y, cw, ch, 16, 1, C_LINE, C_PANEL);
        icon(ics[i], x + 24, y + 25, 34, cs[i], C_PANEL);
        text(F_H1, v[i], x + 76, y + 14, i ? cs[i] : C_TEXT, AL_LEFT);
        text(F_SMALL, lab[i], x + 76, y + 52, C_MUTED, AL_LEFT);
    }

    int ly = y + ch + 20, lh = FOOT_LINE - 24 - ly;
    card(PAD_X, ly, CONTENT_W, lh, 18, 1, C_LINE, C_PANEL);
    if (scan.nf == 0) {
        icon(I_CHECK, SCREEN_W / 2 - 28, ly + 80, 56, C_OK, C_PANEL);
        text(F_H2, "Aucun problème trouvé", SCREEN_W / 2, ly + 160, C_TEXT, AL_CENTER);
        text(F_BODY, "Ta console a l'air en pleine forme.", SCREEN_W / 2, ly + 198, C_MUTED, AL_CENTER);
    } else {
        const int rows = 3, rh = 102;
        int top = r_sel - rows + 1;
        if (top < 0) top = 0;
        for (int i = 0; i < rows && top + i < scan.nf; i++) {
            Finding *f = &scan.f[top + i];
            int ry = ly + 12 + i * rh;
            int sel = top + i == r_sel;
            Col bg = f->sev == SEV_ERR ? C_ERR_BG : C_PANEL;
            if (sel) bg = C_PANEL_SEL;
            fill_rrect(PAD_X + 12, ry, CONTENT_W - 24, rh - 6, 12, bg);
            if (sel) fill_rrect(PAD_X + 12, ry + 12, 3, rh - 30, 1, C_ACCENT);
            const char *tag = f->sev == SEV_ERR ? "PROBLÈME" : f->sev == SEV_WARN ? "ATTENTION" : "INFO";
            Col tc = f->sev == SEV_ERR ? C_ERR : f->sev == SEV_WARN ? C_WARN : C_ACCENT2;
            fill_rrect(PAD_X + 32, ry + 18, 112, 26, 13, tc);
            text(F_SMALL, tag, PAD_X + 88, ry + 31 - text_h(F_SMALL) / 2, C_BG, AL_CENTER);
            char ft[192];
            scan_finding_title(f, ft, sizeof(ft));
            text(F_H3, ft, PAD_X + 164, ry + 14, C_TEXT, AL_LEFT);
            text_fit_left(F_SMALL, f->path, PAD_X + 164, ry + 42, CONTENT_W - 200, C_MUTED);
            text_fit_left(F_SMALL, f->advice, PAD_X + 164, ry + 64, CONTENT_W - 200, C_TEXT2);
        }
        if (scan.nf > rows) {
            char more[48];
            snprintf(more, sizeof(more), "%d / %d", r_sel + 1, scan.nf);
            text(F_SMALL, more, PAD_X + CONTENT_W - 24, ly + lh - 28, C_MUTED, AL_RIGHT);
        }
    }

    footer_line();
    text(F_SMALL, "Rien n'a été modifié sur ta console.", PAD_X, FOOT_Y + 13 - text_h(F_SMALL) / 2, C_MUTED, AL_LEFT);
    static const int gl[] = { G_SQUARE, G_TRIANGLE, G_CIRCLE };
    static const char *lb[] = { "Exporter le rapport", "Nouvelle analyse", "Retour" };
    hints_right(FOOT_Y, SCREEN_W - PAD_X, gl, lb, 3);
}

static void on_sysutil(u64 status, u64 param, void *user)
{
    (void)param; (void)user;
    if (status == SYSUTIL_EXIT_GAME) {
        if (patch_busy()) g_pjob.exit_pending = 1;  
        else running = 0;
    }
}

int main(int argc, char *argv[])
{
    char usrdir[256] = "";
    if (argc > 0 && argv && argv[0]) {
        snprintf(usrdir, sizeof(usrdir), "%s", argv[0]);
        char *s = strrchr(usrdir, '/');
        if (s) *s = 0; else usrdir[0] = 0;
    }
    printf("[PS3TK] PS3 Tools Kit %s - USRDIR=%s\n", APP_VERSION, usrdir);

    prefs_load(usrdir[0] ? usrdir : NULL);
    if (gfx_init(usrdir[0] ? usrdir : NULL) != 0) return 1;
    if (!gfx_has_font()) printf("[PS3TK] ATTENTION : aucune police trouvée, le texte sera invisible\n");
    snprintf(res_text, sizeof(res_text), "%d x %d", screen_width(), screen_height());
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, on_sysutil, NULL);
    pad_init();

    while (running) {
        sysUtilCheckCallback();   
        if (g_pjob.exit_pending && !patch_busy()) running = 0;
        if (patch_busy() && screen != S_DUMPS) patch_tick(14);   
        pad_update();
        net_test_poll();

        unsigned t_frame = ticks_ms();
        tools_frame();
        gfx_begin();
        static int last_screen = -1;
        if (screen != last_screen) {
            printf("[PS3TK] ouverture écran %d (image %u)\n", screen, frame);
            last_screen = screen;
        }
        switch (screen) {
        case S_SPLASH: do_splash(); break;
        case S_MENU:   do_menu(); break;
        case S_SYSTEM: do_system(); break;
        case S_INFO:   do_info(); break;
        case S_FILES:  do_files(); break;
        case S_PAD:    do_pad(); break;
        case S_SCREEN: do_screen(); break;
        case S_CHOICE: do_choice(); break;
        case S_RUN:    do_run(); break;
        case S_RESULT: do_result(); break;
        case S_NET:    do_net(); break;
        case S_SAVES:  do_saves(); break;
        case S_SETTINGS: do_settings(); break;
        case S_DUMPS_WARN: do_dumps_warn(); break;
        case S_DUMPS:  do_dumps(); break;
        case S_DIAG:    do_diag(); break;
        case S_STRESS:  do_stress(); break;
        case S_STORAGE: do_storage(); break;
        case S_PORTS:   do_ports(); break;
        case S_OPTICAL: do_optical(); break;
        case S_AUDIO:   do_audio(); break;
        case S_THERMAL: do_thermal(); break;
        case S_REPORT:  do_report(); break;
        case S_DEV:     do_dev(); break;
        case S_DEVV:    do_devv(); break;
        case S_KIT:     do_kit(); break;
        case S_KITV:    do_kitv(); break;
        case S_KITRSX:  do_kitrsx(); break;
        case S_KITPAD:  do_kitpad(); break;
        case S_KITSCN:  do_kitscn(); break;
        case S_KITPROF: do_kitprof(); break;
        case S_KITSES:  do_kitses(); break;
        }
        if (!gfx_has_font()) fill_rect(0, 0, SCREEN_W, 6, C_ERR); 
        draw_toast();
        gfx_end();

        static unsigned acc = 0, worst = 0;
        unsigned dt = ticks_ms() - t_frame;
        acc += dt;
        if (dt > worst) worst = dt;
        if (frame % 60 == 0) {
            printf("[PS3TK] image %u, écran %d, moyenne %u ms, pire %u ms\n",
                   frame, screen, acc / 60, worst);
            acc = 0; worst = 0;
        }

        frame++;
        phase += 0.012f;
    }

    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
    pad_end();
    gfx_quit();
    return 0;
}