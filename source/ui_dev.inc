#include "devtools.h"

enum { DT_ELF, DT_SFO, DT_HEX, DT_CMP, DT_LOG, DT_API, DT_PERF, DT_ENV, DT_REPORT, DT_N };
#define N_DEV DT_N

static const char *dv_long[N_DEV] = { "Inspecteur SELF / ELF", "Inspecteur PARAM.SFO", "Visionneuse hexadécimale", "Comparateur de fichiers",
    "Console de logs", "Tests des API", "Analyse des performances", "Informations environnement", "Rapport de débogage" };
static const Tile devtiles[N_DEV] = {
    { I_CHIP,     "Inspecteur SELF / ELF",  "En-têtes, architecture, point d'entrée, segments, parties chiffrées", 0 },
    { I_DOC,      "Inspecteur SFO",   "Title ID, version, catégorie, firmware minimum", 0 },
    { I_SCAN,     "Visionneuse hex", "Recherche, offset, valeurs big/little-endian", 0 },
    { I_CHECK,    "Comparateur", "Octet par octet, offsets, SHA-256", 0 },
    { I_WARN,     "Console de logs",        "Journaux, filtre des erreurs, export", 0 },
    { I_CROSS,    "Tests des API",          "Fichiers, réseau, manettes, audio, affichage", 0 },
    { I_PULSE,    "Performances", "Temps par image, mémoire, temps d'exécution", 0 },
    { I_SHIELD,   "Environnement", "Capacités détectées, versions, CFW / HEN", 0 },
    { I_SAVE,     "Rapport de débogage",    "Environnement, logs et tests dans un dossier", 0 },
};

static int dev_sel = 0, dv_tool = 0, dv_scroll = 0, dv_errf = 0, dv_ran = 0;
static char dv_path[300], dv_outdir[300];

static int dv_usb_count(void)
{
    usb_poll();
    int u = 0;
    for (int i = 0; i < USB_N; i++) u += g_usb[i].present;
    return u;
}

static void dev_gen(int t)
{
    dv_init();
    g_dv_ver = APP_VERSION;
    dv_scroll = 0;
    switch (t) {
    case DT_ENV: dv_env(pad.connected ? 1 : 0, dv_usb_count(), sysmode, fw, res_text); dv_ran = 1; break;
    case DT_LOG: dv_journal(dv_errf); dv_ran = 1; break;
    case DT_API: toast("Test en cours…"); dv_api(pad.connected ? 1 : 0); dv_ran = 1;
                 log_add("Tests des API exécutés", ""); break;
    case DT_PERF: dv_perf(); dv_ran = 1; break;
    default: break;
    }
}

static void dev_open_tool(int t)
{
    dv_init();
    dv_tool = t; dv_ran = 0; dv_scroll = 0; dv_path[0] = 0; dv_outdir[0] = 0;
    if (t <= DT_CMP) {
        enter_screen(S_FILES);
        files_from_dev = 1;
        files_pick = t == DT_ELF ? P_ELF : t == DT_SFO ? P_SFO : t == DT_HEX ? P_HEX : P_CMPA;
        toast(t == DT_CMP ? "Choisis le premier fichier avec Croix" : "Choisis un fichier avec Croix");
        return;
    }
    dv_clear();
    screen = S_DEVV;
    if (t == DT_ENV || t == DT_LOG || t == DT_PERF) dev_gen(t);
}

static void dev_pick_done(const char *p)
{
    int pick = files_pick;
    if (pick == P_HEX) { files_pick = 0; menu_run(MI_HEX); return; }
    if (pick == P_CMPA) { snprintf(ft_mark, sizeof(ft_mark), "%s", p); files_pick = P_CMPB; toast("Choisis le second fichier avec Croix"); return; }
    if (pick == P_CMPB) {
        files_pick = 0;
        if (!strcmp(p, ft_mark)) { toast("C'est le même fichier"); files_pick = P_CMPB; return; }
        ftcmp_start(ft_mark, p); fmode = FM_CMP; return;
    }
    files_pick = 0; files_from_dev = 0;
    dv_init();
    snprintf(dv_path, sizeof(dv_path), "%s", p);
    dv_scroll = 0; dv_ran = 1;
    screen = S_DEVV;
    if (pick == P_ELF) { dv_tool = DT_ELF; dv_elf(p); log_add("Inspecteur SELF / ELF : fichier lu", ""); }
    else if (pick == P_SFO) { dv_tool = DT_SFO; dv_sfo(p); }
    else if (pick == P_LOG) {
        dv_tool = DT_LOG;
        if (dv_logfile(p) != 0) { dv_clear(); dv_add(DC_ERR, "%s", T("Impossible d'ouvrir ce fichier")); }
        else dv_log_filter(dv_errf);
    }
}

static int dev_export(const char *base)
{
    char dir[64], p[300];
    if (export_dir(dir, sizeof(dir)) != 0) { toast("Export impossible : aucun emplacement inscriptible"); return -1; }
    snprintf(p, sizeof(p), "%s/%s_%llu.txt", dir, base, wall_time());
    if (dv_write_lines(p) != 0) { toast("Export impossible : aucun emplacement inscriptible"); return -1; }
    char m[340];
    snprintf(m, sizeof(m), T("Exporté : %s"), p);
    toast(m);
    return 0;
}

static void do_dev(void)
{
    const int cols = 5;
    if (pad.repeat[B_RIGHT] && dev_sel % cols < cols - 1 && dev_sel + 1 < N_DEV) dev_sel++;
    if (pad.repeat[B_LEFT] && dev_sel % cols > 0) dev_sel--;
    if (pad.repeat[B_DOWN] && dev_sel + cols < N_DEV) dev_sel += cols;
    if (pad.repeat[B_UP] && dev_sel >= cols) dev_sel -= cols;
    if (pad.pressed[B_CIRCLE]) { screen = S_MENU; return; }
    if (pad.pressed[B_CROSS]) { dev_open_tool(dev_sel); return; }

    waves(phase, 600);
    header(I_SCAN, "Développeur", "9 outils");
    int gap = 14, tw = (CONTENT_W - (cols - 1) * gap) / cols, th = 168;
    for (int i = 0; i < N_DEV; i++) {
        int x = PAD_X + (i % cols) * (tw + gap), y = 104 + (i / cols) * (th + gap);
        int sel = i == dev_sel;
        if (sel) card(x, y, tw, th, 16, 2, C_ACCENT, C_PANEL_SEL);
        else card(x, y, tw, th, 16, 1, C_LINE, C_PANEL);
        icon(devtiles[i].ic, x + 18, y + 16, 34, sel ? C_ACCENT : C_TEXT2, sel ? C_PANEL_SEL : C_PANEL);
        text(F_H3, devtiles[i].title, x + 18, y + 60, C_TEXT, AL_LEFT);
        text_wrap(F_SMALL, devtiles[i].desc, x + 18, y + 90, tw - 32, 19, sel ? C_TEXT2 : C_MUTED);
    }
    status_footer("Retour");
}

static Col dv_col(int c)
{
    return c == DC_OK ? C_OK : c == DC_WARN ? C_WARN : c == DC_ERR ? C_ERR : c == DC_HEAD ? C_ACCENT2 : c == DC_MUTED ? C_MUTED : C_TEXT;
}

static void do_devv(void)
{
    const int rows = 21, lh = 23;
    if (pad.repeat[B_DOWN] && dv_scroll < g_dvn - rows) dv_scroll++;
    if (pad.repeat[B_UP] && dv_scroll > 0) dv_scroll--;
    if (pad.repeat[B_R1] || pad.repeat[B_RIGHT]) { dv_scroll += rows - 1; }
    if (pad.repeat[B_L1] || pad.repeat[B_LEFT]) { dv_scroll -= rows - 1; }
    if (dv_scroll > g_dvn - rows) dv_scroll = g_dvn - rows;
    if (dv_scroll < 0) dv_scroll = 0;

    if (pad.pressed[B_CIRCLE]) { screen = S_DEV; return; }
    int runner = dv_tool == DT_API || dv_tool == DT_PERF || dv_tool == DT_ENV || dv_tool == DT_REPORT;
    if (pad.pressed[B_CROSS] && runner) {
        if (dv_tool == DT_REPORT) {
            dv_clear();
            if (dv_report(dv_outdir, sizeof(dv_outdir), pad.connected ? 1 : 0, dv_usb_count(), sysmode, fw, res_text) == 0) {
                dv_clear();
                dv_add(DC_OK, "%s", T("Rapport créé :"));
                dv_add(DC_NORMAL, "%s", dv_outdir);
                dv_add(DC_HEAD, "%s", T("Contenu du dossier"));
                static const char *fl[] = { "environnement.txt", "journal.txt", "erreurs.txt", "api.txt", "performances.txt", "LISEZMOI.txt" };
                for (int i = 0; i < 6; i++) dv_add(DC_NORMAL, "%s", fl[i]);
                dv_add(DC_MUTED, "%s", T("Ce dossier ne contient ni IDPS, ni PSID, ni clés, ni dump : tu peux le partager."));
                log_add("Rapport de débogage créé", "");
            } else dv_add(DC_ERR, "%s", T("Création du rapport impossible (aucun emplacement inscriptible)."));
            dv_ran = 1; dv_scroll = 0;
        } else dev_gen(dv_tool);
    }
    if (dv_tool == DT_LOG) {
        if (pad.pressed[B_SQUARE]) { dv_errf = !dv_errf; if (dv_path[0]) dv_log_filter(dv_errf); else dv_journal(dv_errf); dv_scroll = 0; }
        if (pad.pressed[B_TRIANGLE]) {
            enter_screen(S_FILES); files_from_dev = 1; files_pick = P_LOG;
            toast("Choisis un fichier de log avec Croix");
            return;
        }
    }
    if (pad.pressed[B_START] && g_dvn > 0) {
        static const char *bn[DT_N] = { "elf", "sfo", "hex", "cmp", "logs", "api", "perf", "env", "rapport" };
        dev_export(bn[dv_tool]);
    }

    char sub[200];
    if (dv_path[0]) snprintf(sub, sizeof(sub), "%s", dv_path);
    else snprintf(sub, sizeof(sub), T("%d ligne(s)"), g_dvn);
    header(I_SCAN, dv_long[dv_tool], sub);
    int y = 108, h = FOOT_LINE - 24 - y;
    card(PAD_X, y, CONTENT_W, h, 18, 1, C_LINE, C_PANEL);
    if (runner && !dv_ran) {
        const char *msg = dv_tool == DT_REPORT ? "Croix : créer le dossier de rapport" : 
                          dv_tool == DT_API ? "Croix : lancer les tests des API" : "Croix : lancer";
        text(F_H3, msg, PAD_X + CONTENT_W / 2, y + h / 2 - 20, C_MUTED, AL_CENTER);
        if (dv_tool == DT_REPORT) text(F_SMALL, "Dossier daté sur la clé USB si présente, sinon sur le disque dur.", PAD_X + CONTENT_W / 2, y + h / 2 + 20, C_MUTED, AL_CENTER);
        if (dv_tool == DT_API) text(F_SMALL, "Crée puis supprime un petit fichier dans /dev_hdd0/tmp. Rien d'autre n'est modifié.", PAD_X + CONTENT_W / 2, y + h / 2 + 20, C_MUTED, AL_CENTER);
    }
    for (int i = 0; i < rows && dv_scroll + i < g_dvn; i++) {
        DvLine *l = &g_dvl[dv_scroll + i];
        text_fit_left(F_SMALL, l->t, PAD_X + 24, y + 14 + i * lh, CONTENT_W - 60, dv_col(l->c));
    }
    if (g_dvn > rows) {
        int track = h - 28, th2 = track * rows / g_dvn;
        if (th2 < 24) th2 = 24;
        int ty = y + 14 + (track - th2) * dv_scroll / (g_dvn - rows);
        fill_rrect(PAD_X + CONTENT_W - 12, ty, 4, th2, 2, C_LINE2);
    }

    int gl[5], n = 0;
    const char *lb[5];
    if (runner) { gl[n] = G_CROSS; lb[n++] = dv_tool == DT_REPORT ? "Créer" : dv_ran ? "Relancer" : "Lancer"; }
    if (dv_tool == DT_LOG) {
        gl[n] = G_SQUARE; lb[n++] = dv_errf ? "Tout afficher" : "Erreurs seules";
        gl[n] = G_TRIANGLE; lb[n++] = "Ouvrir un log";
    }
    gl[n] = G_CIRCLE; lb[n++] = "Retour";
    foot("Start : exporter · Haut/Bas : défiler", gl, lb, n);
}