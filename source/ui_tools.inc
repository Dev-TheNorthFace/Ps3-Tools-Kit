#include "tools2.h"
#include "sysinfo.h"

static int tabs_bar(const char **names, int n, int sel)
{
    int tx = PAD_X;
    for (int i = 0; i < n; i++) {
        int w = text_w(F_BODYB, names[i]) + 40;
        if (i == sel) fill_rrect(tx, 100, w, 40, 20, C_ACCENT);
        else card(tx, 100, w, 40, 20, 1, C_LINE, C_BG);
        text(F_BODYB, names[i], tx + w / 2, 120 - text_h(F_BODYB) / 2, i == sel ? C_BG : C_TEXT2, AL_CENTER);
        tx += w + 12;
    }
    return tx;
}

static Col res_col(int r) { return r == R_PASS ? C_OK : r == R_FAIL ? C_ERR : C_MUTED; }
static const char *res_label(int r) { return r == R_PASS ? "Réussi" : r == R_FAIL ? "Échec" : "Non testé"; }

static void pill(int rx, int y, const char *s, Col c)         
{
    int w = text_w(F_SMALL, s) + 24;
    card(rx - w, y, w, 26, 13, 1, c, C_PANEL);
    text(F_SMALL, s, rx - w / 2, y + 13 - text_h(F_SMALL) / 2, c, AL_CENTER);
}

static void foot(const char *left, const int *gl, const char **lb, int n)
{
    footer_line();
    if (left) text(F_SMALL, left, PAD_X, FOOT_Y + 13 - text_h(F_SMALL) / 2, C_MUTED, AL_LEFT);
    hints_right(FOOT_Y, SCREEN_W - PAD_X, gl, lb, n);
}

static void fmt_x10(int v, char *out, int n)
{
    if (v < 0) snprintf(out, n, "—");
    else snprintf(out, n, "%d%c%d %s", v / 10, g_lang == LANG_EN ? '.' : ',', v % 10, T("Mo/s"));
}

static void fmt_mmss(int s, char *out, int n) { snprintf(out, n, "%d:%02d", s / 60, s % 60); }

static const char *stress_mask_name(int m)
{
    return m == ST_CPU ? "Processeur (CPU)" : m == ST_MEM ? "Mémoire vive" : m == ST_RSX ? "Affichage (RSX)" : "Tout combiné";
}

static void draw_info_measured(void)
{
    static const char *tabs[2] = { "Spécifications", "Mesuré et détecté" };
    header(I_CHIP, "Infos système", "Mesures réelles et capacités détectées");
    tabs_bar(tabs, 2, 1);
    if (pad.pressed[B_SQUARE]) { toast("Mesure en cours…"); si_measure(); }

    int y = 156, h = FOOT_LINE - 24 - y, lw = 560, gap = 20, rw = CONTENT_W - lw - gap;
    card(PAD_X, y, lw, h, 18, 1, C_LINE, C_PANEL);
    section_label("Mesures réelles", PAD_X + 24, y + 22);
    int ix = PAD_X + 24, iw = lw - 48;
    char v[64];
    if (g_si.done) snprintf(v, sizeof(v), "%u%c%u %s", g_si.mops_x10 / 10, g_lang == LANG_EN ? '.' : ',', g_si.mops_x10 % 10, T("millions d'opérations/s"));
    else snprintf(v, sizeof(v), "—");
    kv_row(ix, y + 64, iw, "Calcul entier", v, g_si.done ? C_ACCENT : C_MUTED);
    if (g_si.done) snprintf(v, sizeof(v), "%u %s", g_si.mem_mbs, T("Mo/s"));
    else snprintf(v, sizeof(v), "—");
    kv_row(ix, y + 102, iw, "Copie mémoire", v, g_si.done ? C_ACCENT : C_MUTED);
    unsigned f10 = si_frame_ms_x10();
    if (f10) snprintf(v, sizeof(v), "%u%c%u ms · max %u ms", f10 / 10, g_lang == LANG_EN ? '.' : ',', f10 % 10, si_frame_max_ms());
    else snprintf(v, sizeof(v), "—");
    kv_row(ix, y + 140, iw, "Temps par image (cette appli)", v, C_ACCENT);
    char t[16];
    fmt_temp(temp_cpu, t, sizeof(t)); kv_row(ix, y + 178, iw, "Température CPU", t, temp_cpu < 0 ? C_MUTED : C_TEXT);
    fmt_temp(temp_rsx, t, sizeof(t)); kv_row(ix, y + 216, iw, "Température RSX", t, temp_rsx < 0 ? C_MUTED : C_TEXT);
    text_wrap(F_SMALL, "Ces valeurs sont des débits mesurés, pas des fréquences : la vitesse réelle en MHz du Cell et du RSX ne peut pas être lue par une application. Compare-les d'une console à l'autre ou avant/après un nettoyage.",
              ix, y + 276, iw, 20, C_MUTED);
    text_wrap(F_SMALL, "Le RPCS3 donne des valeurs sans rapport avec une vraie console.", ix, y + 376, iw, 20, C_MUTED);
    if (g_si.done) {
        snprintf(v, sizeof(v), T("Mesure faite en %u ms"), g_si.ms_total);
        text(F_SMALL, v, ix, y + h - 34, C_MUTED, AL_LEFT);
    }

    int x2 = PAD_X + lw + gap;
    card(x2, y, rw, h, 18, 1, C_LINE, C_PANEL);
    section_label("Capacités détectées", x2 + 24, y + 22);
    for (int i = 0; i < g_sicap_n; i++) {
        int ry = y + 56 + i * 34;
        text_fit_left(F_BODY, g_sicap[i].name, x2 + 24, ry + 13 - text_h(F_BODY) / 2, rw - 220, C_TEXT2);
        const char *lab = g_sicap[i].state == 1 ? "Oui" : g_sicap[i].state == 0 ? "Non" : "Inconnu";
        Col c = g_sicap[i].state == 1 ? C_OK : C_MUTED;
        char tx[64];
        if (g_sicap[i].detail[0]) snprintf(tx, sizeof(tx), "%s · %s", T(lab), g_sicap[i].detail);
        else snprintf(tx, sizeof(tx), "%s", T(lab));
        pill(x2 + rw - 24, ry, tx, c);
    }
    static const int gl[] = { G_SQUARE, G_CIRCLE };
    static const char *lb[] = { "Mesurer", "Retour" };
    foot("Gauche/Droite : page « Spécifications »", gl, lb, 2);
}

static int was_stress = 0, was_speed = 0, was_diag_run = 0;

static void tools_frame(void)
{
    si_frame(ticks_ms());
    thermal_tick(g_stress.running);
    if (g_stress.running) stress_tick();
    if (was_stress && !g_stress.running && g_stress.done) {
        char a[96];
        snprintf(a, sizeof(a), "%s;%d;%d", T(stress_mask_name(g_stress.mask)), stress_elapsed_s(), g_stress.errors);
        log_add("Stress test : %s, %s s, %s erreur(s)", a);
        was_stress = 0;
    }
    if (g_stress.running) was_stress = 1;

    if (g_speed.state == 1 || g_speed.state == 2) speed_tick();
    if (g_speed.state == 3 && was_speed == 0) {
        was_speed = 1;
        int hdd = !strcmp(g_speed.root, "/dev_hdd0");
        if (hdd) { g_last_hdd_w = g_speed.w_x10; g_last_hdd_r = g_speed.r_x10; }
        else { g_last_usb_w = g_speed.w_x10; g_last_usb_r = g_speed.r_x10; }
        char a[96], w[24], r[24];
        fmt_x10(g_speed.w_x10, w, sizeof(w));
        fmt_x10(g_speed.r_x10, r, sizeof(r));
        snprintf(a, sizeof(a), "%s;%s;%s", hdd ? T("Disque dur") : g_speed.root, w, r);
        log_add("Vitesse %s : écriture %s, lecture %s", a);
    }
    if (g_speed.state != 3) was_speed = 0;
    if (g_scan.active && screen == S_STORAGE) scan_disk_tick(10);
    if (screen == S_PORTS || screen == S_DIAG) usb_poll();
    if (screen == S_OPTICAL) optical_poll();
    if (g_opt.state == 1 || g_opt.state == 2) optical_tick();
    aud_tick();
}

static unsigned diag_t = 0;

static void do_diag(void)
{
    int running = g_diag_pos >= 0 && g_diag_pos < DIAG_N;
    if (pad.pressed[B_CIRCLE]) { g_diag_pos = g_diag_pos < DIAG_N ? -1 : g_diag_pos; screen = S_SYSTEM; return; }
    if (pad.pressed[B_CROSS] && !running) { diag_start(); diag_t = ticks_ms(); running = 1; was_diag_run = 1; }
    if (running && ticks_ms() - diag_t > 160) {
        diag_t = ticks_ms();
        diag_step();
        running = g_diag_pos < DIAG_N;
    }
    if (!running && was_diag_run) {
        was_diag_run = 0;
        int p, f, s;
        diag_counts(&p, &f, &s);
        char a[48];
        snprintf(a, sizeof(a), "%d;%d;%d", p, f, s);
        log_add("Diagnostic : %s réussis, %s échecs, %s non testés", a);
    }

    int p, f, s;
    diag_counts(&p, &f, &s);
    header(I_CHECK, "Diagnostic matériel", NULL);
    int lw = 780, y = 108, h = FOOT_LINE - 24 - y;
    card(PAD_X, y, lw, h, 18, 1, C_LINE, C_PANEL);
    section_label("Vérifications", PAD_X + 24, y + 22);
    const int rh = 35;
    for (int i = 0; i < DIAG_N; i++) {
        int ry = y + 52 + i * rh;
        int done = g_diag_pos >= 0 && i < g_diag_pos;
        int cur = running && i == g_diag_pos;
        if (cur) fill_rrect(PAD_X + 10, ry - 4, lw - 20, rh - 2, 8, C_PANEL_SEL);
        text(F_BODY, g_diag[i].name, PAD_X + 28, ry + 2, done || cur ? C_TEXT : C_MUTED, AL_LEFT);
        if (done) {
            text(F_SMALL, g_diag[i].detail, PAD_X + lw - 190, ry + 5, C_MUTED, AL_RIGHT);
            pill(PAD_X + lw - 24, ry - 1, res_label(g_diag[i].res), res_col(g_diag[i].res));
        } else if (cur) pill(PAD_X + lw - 24, ry - 1, "En cours...", C_ACCENT);
    }

    int rx = PAD_X + lw + 20, rw = SCREEN_W - PAD_X - rx;
    card(rx, y, rw, h, 18, 1, C_LINE, C_PANEL);
    section_label("Résultat", rx + 24, y + 22);
    int shown = g_diag_pos < 0 ? 0 : (g_diag_pos > DIAG_N ? DIAG_N : g_diag_pos);
    bar(rx + 24, y + 56, rw - 48, 10, shown / (float)DIAG_N, shown == DIAG_N ? (f ? C_ERR : C_OK) : C_ACCENT);
    const char *lab[3] = { "Réussi", "Échec", "Non testé" };
    int val[3] = { p, f, s };
    Col col[3] = { C_OK, C_ERR, C_MUTED };
    for (int i = 0; i < 3; i++) {
        int by = y + 90 + i * 84;
        char v[16];
        snprintf(v, sizeof(v), "%d", g_diag_pos < 0 ? 0 : val[i]);
        text(F_HUGE, v, rx + 24, by, g_diag_pos < 0 ? C_MUTED : col[i], AL_LEFT);
        text(F_BODY, lab[i], rx + 110, by + 22, C_TEXT2, AL_LEFT);
    }
    text_wrap(F_SMALL, "Non testé = pas de matériel à vérifier (clé USB, disque) ou test manuel pas encore fait. Ce n'est pas une panne.",
              rx + 24, y + 366, rw - 48, 20, C_MUTED);
    if (!running) {
        text_wrap(F_SMALL, g_diag_pos < 0 ? "Appuie sur Croix pour lancer la vérification." : "Terminé. Croix : recommencer.",
                  rx + 24, y + h - 70, rw - 48, 20, C_TEXT2);
    }
    int gl[] = { G_CROSS, G_CIRCLE };
    const char *lb[] = { running ? "En cours..." : (g_diag_pos < 0 ? "Lancer" : "Relancer"), "Retour" };
    foot("Aucune écriture système · un fichier de 1 Mo est créé puis supprimé sur le disque dur", gl, lb, 2);
}

static int st_sel = 3, st_dur = 1, st_row = 0;
static const int st_masks[4] = { ST_CPU, ST_MEM, ST_RSX, ST_CPU | ST_MEM | ST_RSX };
static const char *st_names[4] = { "Processeur (CPU)", "Mémoire vive", "Affichage (RSX)", "Tout combiné" };
static const int st_durs[5] = { 30, 60, 120, 300, 600 };

static void do_stress(void)
{
    if (pad.pressed[B_CIRCLE]) {
        if (g_stress.running) stress_stop(1); else screen = S_SYSTEM;
        return;
    }
    if (!g_stress.running) {
        if (pad.repeat[B_UP] && st_row > 0) st_row--;
        if (pad.repeat[B_DOWN] && st_row < 1) st_row++;
        if (st_row == 0) {
            if (pad.repeat[B_LEFT] && st_sel > 0) st_sel--;
            if (pad.repeat[B_RIGHT] && st_sel < 3) st_sel++;
        } else {
            if (pad.repeat[B_LEFT] && st_dur > 0) st_dur--;
            if (pad.repeat[B_RIGHT] && st_dur < 4) st_dur++;
        }
        if (pad.pressed[B_CROSS]) stress_start(st_masks[st_sel], st_durs[st_dur]);
    }
    /* charge d'affichage : remplissages plein écran avant l'interface */
    if (g_stress.running && (g_stress.mask & ST_RSX))
        for (int k = 0; k < 6; k++) fill_rect(0, 0, SCREEN_W, SCREEN_H, HEX(0x0A0E17 + ((frame + k) & 7) * 0x010101));

    waves(phase, 600);
    header(I_PULSE, "Stress test", g_stress.running ? "En cours" : NULL);

    int y = 108, h = FOOT_LINE - 24 - y, lw = 470;
    card(PAD_X, y, lw, h, 18, 1, C_LINE, C_PANEL);
    section_label("Test à lancer", PAD_X + 24, y + 22);
    for (int i = 0; i < 4; i++) {
        int ry = y + 54 + i * 44;
        int sel = i == st_sel;
        if (st_row == 0 && sel && !g_stress.running) fill_rrect(PAD_X + 12, ry - 8, lw - 24, 38, 10, C_PANEL_SEL);
        ring(PAD_X + 40, ry + 11, 9, 2, sel ? C_ACCENT : C_LINE3, C_PANEL);
        if (sel) fill_circle(PAD_X + 40, ry + 11, 4, C_ACCENT);
        text(F_BODY, st_names[i], PAD_X + 64, ry, sel ? C_TEXT : C_TEXT2, AL_LEFT);
    }
    section_label("Durée", PAD_X + 24, y + 250);
    int cx = PAD_X + 24;
    for (int i = 0; i < 5; i++) {
        char d[24];
        if (st_durs[i] < 60) snprintf(d, sizeof(d), T("%d s"), st_durs[i]);
        else snprintf(d, sizeof(d), T("%d min"), st_durs[i] / 60);
        int w = text_w(F_BODYB, d) + 28;
        if (i == st_dur) fill_rrect(cx, y + 282, w, 38, 19, st_row == 1 && !g_stress.running ? C_ACCENT : C_LINE3);
        else card(cx, y + 282, w, 38, 19, 1, C_LINE, C_PANEL);
        text(F_BODYB, d, cx + w / 2, y + 301 - text_h(F_BODYB) / 2, i == st_dur ? C_BG : C_TEXT2, AL_CENTER);
        cx += w + 8;
    }
    text_wrap(F_SMALL, "Arrêt automatique de sécurité à 88 °C (CPU) ou 85 °C (RSX). Garde la console bien aérée. L'affichage ne détecte pas d'erreur graphique : il sert à chauffer le RSX.",
              PAD_X + 24, y + 346, lw - 48, 20, C_MUTED);

    int rx = PAD_X + lw + 20, rw = SCREEN_W - PAD_X - rx;
    card(rx, y, rw, h, 18, 1, C_LINE, C_PANEL);
    section_label("En direct", rx + 24, y + 22);
    int el = g_stress.running || g_stress.done ? stress_elapsed_s() : 0;
    int tot = g_stress.running || g_stress.done ? g_stress.dur_s : st_durs[st_dur];
    char t1[24], t2[24], tt[64];
    fmt_mmss(el > tot ? tot : el, t1, sizeof(t1));
    fmt_mmss(tot, t2, sizeof(t2));
    snprintf(tt, sizeof(tt), "%s / %s", t1, t2);
    text(F_H1, tt, rx + 24, y + 54, g_stress.running ? C_TEXT : C_TEXT2, AL_LEFT);
    bar(rx + 24, y + 104, rw - 48, 10, tot ? (el > tot ? 1.f : el / (float)tot) : 0, g_stress.errors ? C_ERR : C_ACCENT);

    int c, tcpu = sys_temperature(0, &c) == 0 ? c : -1;
    int trsx = sys_temperature(1, &c) == 0 ? c : -1;
    const char *nm[2] = { "CPU", "RSX" };
    int now[2] = { tcpu, trsx }, st0[2] = { g_stress.cpu0, g_stress.rsx0 }, mx[2] = { g_stress.cpu_max, g_stress.rsx_max };
    int lim[2] = { STRESS_CPU_LIMIT, STRESS_RSX_LIMIT };
    for (int i = 0; i < 2; i++) {
        int by = y + 136 + i * 50;
        char a[32], b[96];
        fmt_temp(now[i], a, sizeof(a));
        text(F_BODY, nm[i], rx + 24, by, C_TEXT2, AL_LEFT);
        bar(rx + 90, by + 7, rw - 90 - 330, 10, now[i] < 0 ? 0 : now[i] / 100.f, now[i] >= lim[i] - 8 ? C_ERR : now[i] >= 70 ? C_WARN : C_ACCENT);
        text(F_H3, a, rx + rw - 180, by - 2, C_TEXT, AL_RIGHT);
        if (g_stress.running || g_stress.done) {
            snprintf(b, sizeof(b), T("début %d · max %d"), st0[i] < 0 ? 0 : st0[i], mx[i] < 0 ? 0 : mx[i]);
            text(F_SMALL, b, rx + rw - 24, by + 2, C_MUTED, AL_RIGHT);
        }
    }
    char b[160];
    snprintf(b, sizeof(b), T("CPU : %u passes · Mémoire : %u Mo vérifiés"), (unsigned)g_stress.cpu_iters, (unsigned)g_stress.mem_mb);
    text(F_SMALL, b, rx + 24, y + 248, C_TEXT2, AL_LEFT);
    snprintf(b, sizeof(b), T("Erreurs : %d (CPU %d · mémoire %d)"), g_stress.errors, g_stress.cpu_err, g_stress.mem_err);
    text(F_BODYB, b, rx + 24, y + 276, g_stress.errors ? C_ERR : C_OK, AL_LEFT);
    section_label("Journal des erreurs", rx + 24, y + 316);
    if (g_stress.nlog == 0) text(F_SMALL, "Aucune erreur.", rx + 24, y + 344, C_MUTED, AL_LEFT);
    for (int i = 0; i < g_stress.nlog; i++) text_fit_left(F_SMALL, g_stress.log[i], rx + 24, y + 344 + i * 22, rw - 48, C_ERR);
    if (g_stress.done && !g_stress.running) {
        const char *r = g_stress.aborted == 2 ? "Arrêté par sécurité thermique" : g_stress.aborted == 1 ? "Arrêté" :
                        g_stress.errors ? "Terminé avec des erreurs" : "Terminé sans erreur";
        pill(rx + rw - 24, y + 22, r, g_stress.errors || g_stress.aborted == 2 ? C_ERR : C_OK);
    }

    int gl[] = { G_CROSS, G_CIRCLE };
    const char *lb[] = { g_stress.running ? "En cours..." : "Lancer", g_stress.running ? "Arrêter" : "Retour" };
    foot(g_stress.running ? NULL : "Haut / Bas : réglage · Gauche / Droite : changer", gl, lb, 2);
}

static int sto_tab = 0, sto_dev = 0, sto_mb = 1;
static const int sto_sizes[3] = { 16, 32, 64 };
static int sto_list(char paths[][16], const char **names)
{
    int n = 0;
    snprintf(paths[n], 16, "/dev_hdd0"); names[n++] = "Disque dur";
    for (int i = 0; i < USB_N; i++) if (g_usb[i].present) { snprintf(paths[n], 16, "%s", g_usb[i].path); names[n++] = "Clé USB"; }
    return n;
}

static void sto_restart(void)
{
    char paths[USB_N + 1][16];
    const char *nm[USB_N + 1];
    int n = sto_list(paths, nm);
    if (sto_dev >= n) sto_dev = 0;
    scan_disk_start(paths[sto_dev]);
}

static void do_storage(void)
{
    static const char *tabs[2] = { "Espace", "Vitesse" };
    char paths[USB_N + 1][16];
    const char *nm[USB_N + 1];
    usb_poll();
    int nd = sto_list(paths, nm);
    if (sto_dev >= nd) { sto_dev = 0; sto_restart(); }
    int busy = g_speed.state == 1 || g_speed.state == 2;

    if (pad.pressed[B_CIRCLE]) {
        if (busy) speed_cancel();
        else { scan_disk_cancel(); screen = S_SYSTEM; return; }
    }
    if (!busy) {
        if (pad.pressed[B_R1] || pad.pressed[B_L1]) sto_tab ^= 1;
        if (pad.pressed[B_TRIANGLE] && nd > 1) { sto_dev = (sto_dev + 1) % nd; sto_restart(); g_speed.state = 0; }
        if (sto_tab == 0 && pad.pressed[B_CROSS]) sto_restart();
        if (sto_tab == 1) {
            if (pad.repeat[B_LEFT] && sto_mb > 0) sto_mb--;
            if (pad.repeat[B_RIGHT] && sto_mb < 2) sto_mb++;
            if (pad.pressed[B_CROSS]) speed_start(paths[sto_dev], sto_sizes[sto_mb]);
        }
    }

    header(I_HDD, "Stockage", NULL);
    tabs_bar(tabs, 2, sto_tab);
    char dv[96];
    snprintf(dv, sizeof(dv), "%s · %s", T(nm[sto_dev]), paths[sto_dev]);
    int dw = text_w(F_SMALL, dv) + 32;
    card(SCREEN_W - PAD_X - dw, 102, dw, 36, 18, 1, C_LINE, C_BG);
    text(F_SMALL, dv, SCREEN_W - PAD_X - dw / 2, 120 - text_h(F_SMALL) / 2, C_TEXT2, AL_CENTER);

    int y = 160, h = FOOT_LINE - 24 - y;
    card(PAD_X, y, CONTENT_W, h, 18, 1, C_LINE, C_PANEL);

    if (sto_tab == 0) {
        char a[32], b[32], line[160];
        uint64_t total = g_scan.used + (g_scan.free_ok ? g_scan.free_b : 0);
        fmt_size(g_scan.used, a, sizeof(a));
        fmt_size(g_scan.free_b, b, sizeof(b));
        section_label("Carte de l'espace", PAD_X + 24, y + 22);
        snprintf(line, sizeof(line), T("Occupé (analysé) : %s · Libre : %s"), a, g_scan.free_ok ? b : "N/D");
        text(F_BODY, line, PAD_X + CONTENT_W - 24, y + 18, C_TEXT2, AL_RIGHT);
        int bx = PAD_X + 24, bw = CONTENT_W - 48, by = y + 54;
        fill_rrect(bx, by, bw, 22, 11, C_LINE);
        static const unsigned pal[6] = { 0x3D9BFF, 0x5FD6A8, 0xFFB547, 0xF49BD6, 0x7FB2FF, 0xFF8A98 };
        uint64_t acc = 0;
        for (int i = 0; i < g_scan.nf && total; i++) {
            int x0 = bx + (int)(bw * acc / total);
            acc += g_scan.f[i].bytes;
            int x1 = bx + (int)(bw * acc / total);
            if (x1 > x0) fill_rect(x0, by, x1 - x0 < 2 ? 2 : x1 - x0, 22, HEX(pal[i % 6]));
        }
        clip_push(bx, by, bw, 22);
        card(bx - 2, by - 2, bw + 4, 26, 13, 3, C_PANEL, HEXA(0x000000, 0));
        clip_pop();
        uint64_t mx = g_scan.nf ? g_scan.f[0].bytes : 0;
        int rows = 8;
        for (int i = 0; i < rows && i < g_scan.nf; i++) {
            int ry = y + 96 + i * 44;
            char sz[32], fi[48];
            fmt_size(g_scan.f[i].bytes, sz, sizeof(sz));
            fill_circle(PAD_X + 34, ry + 11, 6, HEX(pal[i % 6]));
            text(F_BODY, g_scan.f[i].name, PAD_X + 56, ry, C_TEXT, AL_LEFT);
            bar(PAD_X + 360, ry + 6, 460, 10, mx ? g_scan.f[i].bytes / (float)mx : 0, HEX(pal[i % 6]));
            snprintf(fi, sizeof(fi), T("%u fichier(s)"), g_scan.f[i].files);
            text(F_SMALL, fi, PAD_X + 960, ry + 3, C_MUTED, AL_RIGHT);
            text(F_BODYB, sz, PAD_X + CONTENT_W - 24, ry, C_TEXT, AL_RIGHT);
        }
        if (g_scan.active) {
            snprintf(line, sizeof(line), T("Analyse en cours... %u fichiers vus"), g_scan.files);
            text(F_BODY, line, PAD_X + 24, y + h - 40, C_ACCENT2, AL_LEFT);
        } else if (g_scan.done) {
            snprintf(line, sizeof(line), T("%u fichiers analysés"), g_scan.files);
            text(F_SMALL, line, PAD_X + 24, y + h - 36, C_MUTED, AL_LEFT);
            if (g_scan.incomplete) text(F_SMALL, "Analyse incomplète : certains dossiers trop profonds ont été ignorés.", PAD_X + CONTENT_W - 24, y + h - 36, C_WARN, AL_RIGHT);
            if (g_scan.nf == 0) text(F_BODY, "Rien à afficher sur ce support.", SCREEN_W / 2, y + 200, C_MUTED, AL_CENTER);
        }
    } else {
        section_label("Vitesse de lecture et d'écriture", PAD_X + 24, y + 22);
        char sz[32];
        snprintf(sz, sizeof(sz), "%d %s", sto_sizes[sto_mb], T("Mo"));
        text(F_BODY, "Taille du fichier de test :", PAD_X + 24, y + 62, C_TEXT2, AL_LEFT);
        int cx = PAD_X + 290;
        for (int i = 0; i < 3; i++) {
            char d[24];
            snprintf(d, sizeof(d), "%d %s", sto_sizes[i], T("Mo"));
            int w = text_w(F_BODYB, d) + 28;
            if (i == sto_mb) fill_rrect(cx, y + 54, w, 38, 19, C_ACCENT);
            else card(cx, y + 54, w, 38, 19, 1, C_LINE, C_PANEL);
            text(F_BODYB, d, cx + w / 2, y + 73 - text_h(F_BODYB) / 2, i == sto_mb ? C_BG : C_TEXT2, AL_CENTER);
            cx += w + 8;
        }
        (void)sz;
        int bw2 = (CONTENT_W - 48 - 20) / 2;
        for (int i = 0; i < 2; i++) {
            int x = PAD_X + 24 + i * (bw2 + 20), cy = y + 120;
            card(x, cy, bw2, 170, 16, 1, C_LINE2, C_PANEL);
            section_label(i ? "Lecture" : "Écriture", x + 22, cy + 20);
            int v = i ? g_speed.r_x10 : g_speed.w_x10;
            char t[32];
            snprintf(t, sizeof(t), "%d%c%d", v / 10, g_lang == LANG_EN ? '.' : ',', v % 10);
            if (v >= 0) {
                int nw = text(F_HUGE, t, x + 22, cy + 52, C_TEXT, AL_LEFT);
                text(F_H3, "Mo/s", x + 22 + nw + 10, cy + 84, C_TEXT2, AL_LEFT);
            } else text(F_HUGE, "—", x + 22, cy + 52, C_MUTED, AL_LEFT);
            int active = (g_speed.state == 1 && i == 0) || (g_speed.state == 2 && i == 1);
            if (active) bar(x + 22, cy + 142, bw2 - 44, 10, g_speed.done_mb / (float)g_speed.mb, C_ACCENT);
        }
        int vy = y + 312;
        if (g_speed.state == 3) {
            int ok = g_speed.bad == 0;
            icon(ok ? I_CHECK : I_CROSS, PAD_X + 24, vy, 30, ok ? C_OK : C_ERR, C_PANEL);
            char v[96];
            snprintf(v, sizeof(v), ok ? T("Vérification : les %d Mo relus sont identiques à ceux écrits.") : T("Vérification : %d blocs relus sont différents !"),
                     ok ? g_speed.mb : (int)g_speed.bad);
            text(F_BODYB, v, PAD_X + 68, vy + 4, ok ? C_OK : C_ERR, AL_LEFT);
        } else if (g_speed.state == 4) {
            icon(I_CROSS, PAD_X + 24, vy, 30, C_ERR, C_PANEL);
            text(F_BODYB, g_speed.err, PAD_X + 68, vy + 4, C_ERR, AL_LEFT);
        } else if (busy) {
            text(F_BODY, g_speed.state == 1 ? "Écriture du fichier de test..." : "Relecture et vérification...", PAD_X + 24, vy + 4, C_ACCENT2, AL_LEFT);
        } else text(F_BODY, "Croix : lancer le test. Triangle : changer de support.", PAD_X + 24, vy + 4, C_TEXT2, AL_LEFT);
        text_wrap(F_SMALL, "Un fichier temporaire est écrit puis relu, comparé octet par octet, et supprimé. Juste après l'écriture, la relecture peut être accélérée par le cache : les résultats servent surtout à comparer des supports ou à repérer une lenteur anormale.",
                  PAD_X + 24, y + h - 78, CONTENT_W - 48, 20, C_MUTED);
    }

    int gl[4], n = 0;
    const char *lb[4];
    gl[n] = G_CROSS; lb[n++] = sto_tab == 0 ? "Réanalyser" : (busy ? "En cours..." : "Lancer");
    if (nd > 1 && !busy) { gl[n] = G_TRIANGLE; lb[n++] = "Support"; }
    gl[n] = G_CIRCLE; lb[n++] = busy ? "Annuler" : "Retour";
    foot(busy ? NULL : "L1 / R1 : onglet", gl, lb, n);
}

static int port_sel = 0;

static void content_text(int c, char *out, int n)
{
    out[0] = 0;
    static const struct { int bit; const char *name; } tb[] = {
        { UC_PUP, "Mise à jour PS3" }, { UC_SAVES, "Sauvegardes PS3" }, { UC_DUMPS, "Dumps PS3TK" },
        { UC_GAMES, "Jeux" }, { UC_ISO, "Images ISO" }, { UC_EXPORT, "Exports PS3TK" },
    };
    for (unsigned i = 0; i < sizeof(tb) / sizeof(tb[0]); i++)
        if (c & tb[i].bit) {
            if (out[0]) strncat(out, " · ", n - strlen(out) - 1);
            strncat(out, T(tb[i].name), n - strlen(out) - 1);
        }
    if (!out[0]) snprintf(out, n, "%s", T((c & UC_PS3) ? "Dossier PS3" : "Aucun contenu reconnu"));
}

static void do_ports(void)
{
    if (pad.pressed[B_CIRCLE]) {
        if (g_speed.state == 1 || g_speed.state == 2) speed_cancel(); else { screen = S_SYSTEM; return; }
    }
    int busy = g_speed.state == 1 || g_speed.state == 2;
    if (!busy) {
        if (pad.repeat[B_UP] && port_sel > 0) port_sel--;
        if (pad.repeat[B_DOWN] && port_sel < USB_N - 1) port_sel++;
        if (pad.pressed[B_SQUARE] && g_usb[port_sel].present) usb_analyze(port_sel);
        if (pad.pressed[B_CROSS]) {
            if (g_usb[port_sel].present) speed_start(g_usb[port_sel].path, 16);
            else toast("Aucun périphérique sur ce port");
        }
    }
    header(I_USB, "Périphériques", NULL);
    int y = 108, h = FOOT_LINE - 24 - y, lw = 700;
    card(PAD_X, y, lw, h, 18, 1, C_LINE, C_PANEL);
    section_label("Ports USB", PAD_X + 24, y + 22);
    for (int i = 0; i < USB_N; i++) {
        int ry = y + 52 + i * 56;
        UsbPort *u = &g_usb[i];
        if (i == port_sel) fill_rrect(PAD_X + 10, ry - 6, lw - 20, 52, 10, C_PANEL_SEL);
        icon(I_USB, PAD_X + 26, ry + 2, 30, u->present ? C_ACCENT : C_LINE3, i == port_sel ? C_PANEL_SEL : C_PANEL);
        char t[48];
        snprintf(t, sizeof(t), T("Port %d"), i);
        text(F_BODYB, t, PAD_X + 74, ry - 2, u->present ? C_TEXT : C_MUTED, AL_LEFT);
        if (u->present) {
            char s[32], d[64];
            if (u->free_ok) { fmt_size(u->free_b, s, sizeof(s)); snprintf(d, sizeof(d), T("%s libres"), s); }
            else snprintf(d, sizeof(d), "%s", T("Lecture impossible"));
            text(F_SMALL, d, PAD_X + 74, ry + 22, C_MUTED, AL_LEFT);
            pill(PAD_X + lw - 24, ry + 6, "Branché", C_OK);
        } else text(F_SMALL, "Libre", PAD_X + 74, ry + 22, C_LINE3, AL_LEFT);
    }

    int rx = PAD_X + lw + 20, rw = SCREEN_W - PAD_X - rx;
    card(rx, y, rw, 250, 18, 1, C_LINE, C_PANEL);
    section_label("Identification", rx + 24, y + 22);
    UsbPort *u = &g_usb[port_sel];
    if (u->present) {
        char c[200];
        content_text(u->content, c, sizeof(c));
        kv_row(rx + 24, y + 56, rw - 48, "Point de montage", u->path, C_TEXT);
        text(F_BODY, "Contenu reconnu", rx + 24, y + 94, C_MUTED, AL_LEFT);
        text_wrap(F_BODYB, c, rx + 24, y + 124, rw - 48, 24, C_ACCENT2);
        if (g_speed.state != 0 && !strcmp(g_speed.root, u->path)) {
            char w[24], r[24], l[96];
            fmt_x10(g_speed.w_x10, w, sizeof(w));
            fmt_x10(g_speed.r_x10, r, sizeof(r));
            if (g_speed.state == 3) {
                snprintf(l, sizeof(l), T("Écriture %s · Lecture %s"), w, r);
                text(F_BODYB, l, rx + 24, y + 196, g_speed.bad ? C_ERR : C_OK, AL_LEFT);
                text(F_SMALL, g_speed.bad ? "Données relues différentes !" : "Données vérifiées : identiques", rx + 24, y + 222, g_speed.bad ? C_ERR : C_MUTED, AL_LEFT);
            } else if (g_speed.state == 4) text(F_BODYB, g_speed.err, rx + 24, y + 196, C_ERR, AL_LEFT);
            else bar(rx + 24, y + 200, rw - 48, 10, g_speed.done_mb / (float)g_speed.mb, C_ACCENT);
        } else text(F_SMALL, "Croix : test de transfert (16 Mo écrits, relus, vérifiés).", rx + 24, y + 214, C_MUTED, AL_LEFT);
    } else text(F_BODY, "Aucun périphérique sur ce port.", rx + 24, y + 70, C_MUTED, AL_LEFT);

    int ey = y + 270, eh = h - 270;
    card(rx, ey, rw, eh, 18, 1, C_LINE, C_PANEL);
    section_label("Évènements en direct", rx + 24, ey + 22);
    if (g_usb_nev == 0) text_wrap(F_SMALL, "Branche ou débranche une clé : l'évènement apparaît ici tout de suite.", rx + 24, ey + 56, rw - 48, 20, C_MUTED);
    for (int i = 0; i < g_usb_nev && i < 5; i++) {
        char l[96];
        int ago = (int)((ticks_ms() - g_usb_ev[i].t) / 1000);
        snprintf(l, sizeof(l), g_usb_ev[i].up ? T("Port %d : branché") : T("Port %d : débranché"), g_usb_ev[i].port);
        text(F_BODY, l, rx + 24, ey + 54 + i * 32, g_usb_ev[i].up ? C_OK : C_WARN, AL_LEFT);
        snprintf(l, sizeof(l), T("il y a %d s"), ago);
        text(F_SMALL, l, rx + rw - 24, ey + 58 + i * 32, C_MUTED, AL_RIGHT);
    }
    char pc[64];
    snprintf(pc, sizeof(pc), "%s : %s", T("Manette 1"), T(pad.connected ? "connectée" : "absente"));
    text(F_SMALL, pc, rx + 24, ey + eh - 32, pad.connected ? C_OK : C_ERR, AL_LEFT);

    int gl[] = { G_CROSS, G_SQUARE, G_CIRCLE };
    const char *lb[] = { busy ? "En cours..." : "Test de transfert", "Relire le contenu", busy ? "Annuler" : "Retour" };
    foot(NULL, gl, lb, 3);
}

static void do_optical(void)
{
    int busy = g_opt.state == 1 || g_opt.state == 2;
    if (pad.pressed[B_CIRCLE]) {
        if (busy) optical_test_cancel(); else { screen = S_SYSTEM; return; }
    }
    if (pad.pressed[B_CROSS] && g_opt.present && !busy) optical_test_start();

    header(I_DISC, "Lecteur optique", NULL);
    int y = 108, h = FOOT_LINE - 24 - y, lw = 560;
    card(PAD_X, y, lw, h, 18, 1, C_LINE, C_PANEL);
    section_label("Disque", PAD_X + 24, y + 22);
    if (!g_opt.present) {
        text(F_H2, "Aucun disque détecté", PAD_X + lw / 2, y + 150, C_TEXT, AL_CENTER);
        text_wrap(F_BODY, "Insère un disque : il sera reconnu automatiquement, sans rien appuyer.", PAD_X + 40, y + 200, lw - 80, 26, C_MUTED);
    } else {
        kv_row(PAD_X + 24, y + 60, lw - 48, "Type de média", T(g_opt.media), C_ACCENT2);
        if (g_opt.title[0]) {
            text(F_BODY, "Titre", PAD_X + 24, y + 108, C_MUTED, AL_LEFT);
            text_wrap(F_BODYB, g_opt.title, PAD_X + 24, y + 138, lw - 48, 24, C_TEXT);
        }
        if (g_opt.titleid[0]) kv_row(PAD_X + 24, y + 196, lw - 48, "Identifiant", g_opt.titleid, C_TEXT);
        if (g_opt.version[0]) kv_row(PAD_X + 24, y + 234, lw - 48, "Version", g_opt.version, C_TEXT);
        if (g_opt.state == 2 || g_opt.state == 3) {
            char a[32], b[32];
            snprintf(b, sizeof(b), "%u", g_opt.files);
            fmt_size(g_opt.bytes, a, sizeof(a));
            kv_row(PAD_X + 24, y + 288, lw - 48, "Fichiers", b, C_TEXT);
            kv_row(PAD_X + 24, y + 326, lw - 48, "Taille du contenu", a, C_TEXT);
        }
    }

    int rx = PAD_X + lw + 20, rw = SCREEN_W - PAD_X - rx;
    card(rx, y, rw, h, 18, 1, C_LINE, C_PANEL);
    section_label("Test de lecture", rx + 24, y + 22);
    if (g_opt.present) {
        const uint64_t cap = 96ull * 1024 * 1024;
        float frac = g_opt.state == 3 ? 1.f : g_opt.state == 2 ? (float)g_opt.read_b / (float)cap : 0.f;
        bar(rx + 24, y + 58, rw - 48, 10, frac, g_opt.errors ? C_ERR : C_ACCENT);
        char a[32], v[24];
        fmt_size(g_opt.read_b, a, sizeof(a));
        fmt_x10(g_opt.mbs_x10, v, sizeof(v));
        text(F_H1, g_opt.mbs_x10 >= 0 ? v : "—", rx + 24, y + 92, g_opt.mbs_x10 >= 0 ? C_TEXT : C_MUTED, AL_LEFT);
        char l[96];
        snprintf(l, sizeof(l), T("Lu : %s"), a);
        text(F_BODY, l, rx + 24, y + 150, C_TEXT2, AL_LEFT);
        snprintf(l, sizeof(l), T("Erreurs : %d"), g_opt.errors);
        text(F_BODYB, l, rx + 24, y + 184, g_opt.errors ? C_ERR : (g_opt.state == 3 ? C_OK : C_TEXT2), AL_LEFT);
        const char *st = g_opt.state == 1 ? "Inventaire des fichiers..." : g_opt.state == 2 ? "Lecture en cours..." :
                         g_opt.state == 3 ? (g_opt.errors ? "Terminé : des erreurs de lecture ont été trouvées." : "Terminé : lecture correcte.") :
                         "Croix : lancer le test.";
        text_wrap(F_BODY, st, rx + 24, y + 226, rw - 48, 24, g_opt.state == 3 && g_opt.errors ? C_ERR : C_TEXT2);
        section_label("Erreurs rencontrées", rx + 24, y + 290);
        if (g_opt.nlog == 0) text(F_SMALL, "Aucune.", rx + 24, y + 318, C_MUTED, AL_LEFT);
        for (int i = 0; i < g_opt.nlog; i++) text_fit_left(F_SMALL, g_opt.log[i], rx + 24, y + 318 + i * 24, rw - 48, C_ERR);
        text_wrap(F_SMALL, "Lit le début des plus gros fichiers du disque (96 Mo au plus). Rien n'est écrit.", rx + 24, y + h - 56, rw - 48, 20, C_MUTED);
    } else text(F_BODY, "En attente d'un disque.", rx + 24, y + 70, C_MUTED, AL_LEFT);

    int gl[] = { G_CROSS, G_CIRCLE };
    const char *lb[] = { busy ? "En cours..." : "Tester", busy ? "Arrêter" : "Retour" };
    foot(NULL, gl, lb, 2);
}

static int au_row = 0;
static unsigned au_t0 = 0;

static int audio_from_kit = 0;
static void aud_leave(void)
{
    if (g_aud.playing && aud_played_s() >= 1) g_manual_audio = R_PASS;
    aud_close();
    screen = audio_from_kit ? S_KIT : S_SYSTEM;
    audio_from_kit = 0;
}

static void do_audio(void)
{
    if (pad.pressed[B_CIRCLE]) { aud_leave(); return; }
    static const char *chn[4] = { "Les deux côtés", "Gauche", "Droite", "Alterné (1 s chacun)" };
    static const char *tn[4] = { "440 Hz (La)", "1000 Hz", "Balayage 100 Hz – 4 kHz", "220 Hz (grave)" };
    if (pad.repeat[B_UP] && au_row > 0) au_row--;
    if (pad.repeat[B_DOWN] && au_row < 3) au_row++;
    int d = (pad.repeat[B_RIGHT] ? 1 : 0) - (pad.repeat[B_LEFT] ? 1 : 0);
    if (d) {
        if (au_row == 0) g_aud.chan = (g_aud.chan + d + 4) % 4;
        else if (au_row == 1) g_aud.tone = (g_aud.tone + d + 4) % 4;
        else if (au_row == 2) { g_aud.vol += d * 5; if (g_aud.vol < 5) g_aud.vol = 5; if (g_aud.vol > 50) g_aud.vol = 50; }
        else g_aud.pulse ^= 1;
    }
    if (pad.pressed[B_CROSS] && g_aud.open) {
        aud_play(!g_aud.playing);
        au_t0 = ticks_ms();
    }
    if (g_aud.playing && aud_played_s() >= 1) g_manual_audio = R_PASS;

    header(I_SPEAKER, "Audio", NULL);
    int y = 108, h = FOOT_LINE - 24 - y, lw = 560;
    card(PAD_X, y, lw, h, 18, 1, C_LINE, C_PANEL);
    section_label("Réglages du test", PAD_X + 24, y + 22);
    char vol[16];
    snprintf(vol, sizeof(vol), "%d %%", g_aud.vol);
    const char *lab[4] = { "Canal", "Tonalité", "Volume", "Bips de synchro" };
    const char *val[4] = { T(chn[g_aud.chan]), T(tn[g_aud.tone]), vol, T(g_aud.pulse ? "Activés" : "Désactivés") };
    for (int i = 0; i < 4; i++) {
        int ry = y + 54 + i * 70;
        if (i == au_row) fill_rrect(PAD_X + 10, ry - 8, lw - 20, 60, 10, C_PANEL_SEL);
        text(F_SMALL, lab[i], PAD_X + 28, ry, C_MUTED, AL_LEFT);
        text(F_BODYB, val[i], PAD_X + 28, ry + 22, C_TEXT, AL_LEFT);
        if (i == au_row) { text(F_H3, "<", PAD_X + lw - 70, ry + 12, C_ACCENT, AL_CENTER); text(F_H3, ">", PAD_X + lw - 34, ry + 12, C_ACCENT, AL_CENTER); }
    }
    char ch[96];
    if (g_aud.ch8 >= 0) snprintf(ch, sizeof(ch), T("Sortie testée : stéréo 2 canaux · port 8 canaux %s"), T(g_aud.ch8 ? "accepté" : "refusé"));
    else snprintf(ch, sizeof(ch), "%s", T("Sortie testée : stéréo 2 canaux"));
    text_wrap(F_SMALL, ch, PAD_X + 24, y + 346, lw - 48, 20, C_TEXT2);
    text_wrap(F_SMALL, "Baisse le volume de la télé avant de lancer. Le volume démarre bas (20 %).", PAD_X + 24, y + 392, lw - 48, 20, C_WARN);

    int rx = PAD_X + lw + 20, rw = SCREEN_W - PAD_X - rx;
    card(rx, y, rw, h, 18, 1, C_LINE, C_PANEL);
    section_label("Contrôle visuel", rx + 24, y + 22);
    if (!g_aud.open) {
        char e[96];
        snprintf(e, sizeof(e), T("Impossible d'ouvrir la sortie audio (code %d)."), g_aud.err);
        text_wrap(F_BODYB, e, rx + 24, y + 90, rw - 48, 26, C_ERR);
    } else {
        int side = g_aud.chan;
        if (side == AC_ALT) side = (aud_played_s() & 1) ? AC_RIGHT : AC_LEFT;
        int pulse_off = g_aud.pulse && !aud_pulse_on();
        for (int i = 0; i < 2; i++) {
            int on = g_aud.playing && !pulse_off && (side == AC_BOTH || (i == 0 ? side == AC_LEFT : side == AC_RIGHT));
            int sx = rx + rw / 4 + i * rw / 2, sy = y + 150;
            fill_circle(sx, sy, 62, on ? C_ACCENT : C_LINE);
            ring(sx, sy, 62, 3, on ? C_ACCENT2 : C_LINE3, on ? C_ACCENT : C_LINE);
            fill_circle(sx, sy, 22, on ? C_BG : C_LINE3);
            text(F_H2, i ? "R" : "L", sx, sy + 90, on ? C_TEXT : C_MUTED, AL_CENTER);
        }
        const char *st = g_aud.playing ? "Lecture en cours" : "En pause";
        pill(rx + rw - 24, y + 18, st, g_aud.playing ? C_OK : C_MUTED);
        if (g_aud.pulse) {
            int lit = g_aud.playing && aud_pulse_on();
            card(rx + 24, y + 310, rw - 48, 64, 14, 2, lit ? C_WARN : C_LINE2, lit ? HEXA(0xFFB547, 90) : C_PANEL);
            text(F_BODYB, lit ? "BIP" : "...", rx + rw / 2, y + 342 - text_h(F_BODYB) / 2, lit ? C_WARN : C_MUTED, AL_CENTER);
        }
        text_wrap(F_SMALL, "Synchro : le cadre s'allume pendant que le bip est lu. Un écart avec le son entendu indique le retard de ta télé ou de ton ampli, pas un défaut de la console.",
                  rx + 24, y + h - 84, rw - 48, 20, C_MUTED);
    }
    int gl[] = { G_CROSS, G_CIRCLE };
    const char *lb[] = { g_aud.playing ? "Pause" : "Lecture", "Retour" };
    foot("Haut / Bas : réglage · Gauche / Droite : changer", gl, lb, 2);
}

static int th_win = 1;
static const int th_wins[4] = { 60, 300, 600, 1800 };

static void do_thermal(void)
{
    if (pad.pressed[B_CIRCLE]) { screen = S_SYSTEM; return; }
    if (pad.repeat[B_LEFT] && th_win > 0) th_win--;
    if (pad.repeat[B_RIGHT] && th_win < 3) th_win++;
    if (pad.pressed[B_SQUARE]) {
        char p[96], m[200];
        if (thermal_export(p, sizeof(p)) == 0) { snprintf(m, sizeof(m), T("Exporté : %s"), p); toast(m); }
        else toast("Export impossible");
    }
    header(I_THERMO, "Suivi thermique", NULL);
    int win = th_wins[th_win];
    int y = 108, h = FOOT_LINE - 24 - y, gw = 800;
    card(PAD_X, y, gw, h, 18, 1, C_LINE, C_PANEL);
    section_label("Courbes", PAD_X + 24, y + 22);
    int cx = PAD_X + gw - 24;
    for (int i = 3; i >= 0; i--) {
        char d[24];
        snprintf(d, sizeof(d), T("%d min"), th_wins[i] / 60);
        int w = text_w(F_SMALL, d) + 22;
        cx -= w;
        if (i == th_win) fill_rrect(cx, y + 14, w, 28, 14, C_ACCENT);
        else card(cx, y + 14, w, 28, 14, 1, C_LINE, C_PANEL);
        text(F_SMALL, d, cx + w / 2, y + 28 - text_h(F_SMALL) / 2, i == th_win ? C_BG : C_TEXT2, AL_CENTER);
        cx -= 6;
    }
    int gx = PAD_X + 70, gy = y + 66, gh = h - 130, gww = gw - 100;
    const int tmin = 20, tmax = 100;
    for (int t = tmin; t <= tmax; t += 20) {
        int ly = gy + gh - (t - tmin) * gh / (tmax - tmin);
        fill_rect(gx, ly, gww, 1, C_LINE);
        char l[16];
        snprintf(l, sizeof(l), "%d °C", t);
        text(F_SMALL, l, gx - 10, ly - text_h(F_SMALL) / 2, C_MUTED, AL_RIGHT);
    }
    int n = thermal_count();
    if (n < 2) text(F_BODY, "Les mesures arrivent (une par seconde)...", gx + gww / 2, gy + gh / 2, C_MUTED, AL_CENTER);
    else {
        int span = win < n ? win : n;
        int from = n - span;
        for (int px = 0; px < gww; px++) {
            int i = from + (int)((long)px * (span - 1) / (gww - 1));
            if (thermal_at(i).load) fill_rect(gx + px, gy, 1, gh, HEXA(0xFFB547, 34));
        }
        for (int s = 0; s < 2; s++) {
            Col c = s ? C_PINK : C_ACCENT;
            int px0 = -1, py0 = 0;
            for (int px = 0; px < gww; px += 3) {
                int i = from + (int)((long)px * (span - 1) / (gww - 1));
                ThSample sm = thermal_at(i);
                int v = s ? sm.rsx : sm.cpu;
                if (v < 0) { px0 = -1; continue; }
                if (v < tmin) v = tmin;
                if (v > tmax) v = tmax;
                int py = gy + gh - (v - tmin) * gh / (tmax - tmin);
                if (px0 >= 0) thick_line(gx + px0, py0, gx + px, py, 3, c);
                px0 = px; py0 = py;
            }
        }
        char a[24];
        if (span >= 60) snprintf(a, sizeof(a), T("-%d min"), span / 60);
        else snprintf(a, sizeof(a), T("-%d s"), span);
        text(F_SMALL, a, gx, gy + gh + 8, C_MUTED, AL_LEFT);
        text(F_SMALL, "maintenant", gx + gww, gy + gh + 8, C_MUTED, AL_RIGHT);
    }
    int ly = y + h - 38, lx = PAD_X + 24;
    fill_rrect(lx, ly + 7, 22, 6, 3, C_ACCENT); text(F_SMALL, "CPU", lx + 30, ly, C_TEXT2, AL_LEFT);
    fill_rrect(lx + 100, ly + 7, 22, 6, 3, C_PINK); text(F_SMALL, "RSX", lx + 130, ly, C_TEXT2, AL_LEFT);
    fill_rect(lx + 210, ly + 2, 22, 14, HEXA(0xFFB547, 90)); text(F_SMALL, "Charge (stress test)", lx + 240, ly, C_TEXT2, AL_LEFT);

    int rx = PAD_X + gw + 20, rw = SCREEN_W - PAD_X - rx;
    int hh = 236;
    card(rx, y, rw, hh, 18, 1, C_LINE, C_PANEL);
    section_label("Sur la période affichée", rx + 24, y + 22);
    text(F_SMALL, "min", rx + rw - 200, y + 54, C_MUTED, AL_RIGHT);
    text(F_SMALL, "moy", rx + rw - 120, y + 54, C_MUTED, AL_RIGHT);
    text(F_SMALL, "max", rx + rw - 30, y + 54, C_MUTED, AL_RIGHT);
    for (int s = 0; s < 2; s++) {
        ThStat st = thermal_stat(s, win, -1);
        int ry = y + 84 + s * 66;
        text(F_BODYB, s ? "RSX" : "CPU", rx + 24, ry, s ? C_PINK : C_ACCENT2, AL_LEFT);
        char a[16];
        int v[3] = { st.mn, st.av, st.mx };
        for (int k = 0; k < 3; k++) {
            if (st.n) snprintf(a, sizeof(a), "%d", v[k]); else snprintf(a, sizeof(a), "—");
            text(F_H3, a, rx + rw - 200 + k * 85 + (k == 2 ? 0 : 0), ry - 2, C_TEXT, AL_RIGHT);
        }
        char nn[48];
        snprintf(nn, sizeof(nn), T("%d mesures"), st.n);
        text(F_SMALL, nn, rx + 24, ry + 28, C_MUTED, AL_LEFT);
    }
    int y2 = y + hh + 20, h2 = h - hh - 20;
    card(rx, y2, rw, h2, 18, 1, C_LINE, C_PANEL);
    section_label("Repos / charge (depuis le lancement)", rx + 24, y2 + 22);
    text(F_SMALL, "repos", rx + rw - 110, y2 + 54, C_MUTED, AL_RIGHT);
    text(F_SMALL, "charge", rx + rw - 30, y2 + 54, C_MUTED, AL_RIGHT);
    for (int s = 0; s < 2; s++) {
        ThStat r0 = thermal_stat(s, 0, 0), r1 = thermal_stat(s, 0, 1);
        int ry = y2 + 84 + s * 56;
        text(F_BODYB, s ? "RSX" : "CPU", rx + 24, ry, s ? C_PINK : C_ACCENT2, AL_LEFT);
        char a[32];
        if (r0.n) snprintf(a, sizeof(a), "%d / %d", r0.av, r0.mx); else snprintf(a, sizeof(a), "—");
        text(F_BODY, a, rx + rw - 110, ry, C_TEXT, AL_RIGHT);
        if (r1.n) snprintf(a, sizeof(a), "%d / %d", r1.av, r1.mx); else snprintf(a, sizeof(a), "—");
        text(F_BODY, a, rx + rw - 30, ry, C_TEXT, AL_RIGHT);
        if (r0.n && r1.n) {
            char dl[48];
            snprintf(dl, sizeof(dl), T("écart de moyenne : +%d °C"), r1.av - r0.av);
            text(F_SMALL, dl, rx + 24, ry + 26, r1.av - r0.av > 25 ? C_WARN : C_MUTED, AL_LEFT);
        }
    }
    text_wrap(F_SMALL, "moyenne / maximum en °C. Lance un stress test pour remplir la colonne « charge ».", rx + 24, y2 + h2 - 52, rw - 48, 18, C_MUTED);

    int gl[] = { G_SQUARE, G_CIRCLE };
    const char *lb[] = { "Exporter (CSV)", "Retour" };
    foot("Gauche / Droite : durée affichée", gl, lb, 2);
}

static int rep_tab = 0, rep_sel = 0;
static Report rep_now, rep_before, rep_after;
static unsigned rep_t = 0;

static void rep_refresh(void)
{
    report_capture(&rep_now);
    report_load(0, &rep_before);
    report_load(1, &rep_after);
    rep_t = ticks_ms();
}

static void cmp_row(int x, int w, int y, const char *label, int vb, int va, int kind, int lower_better)
{
    char b[32], a[32], d[32];
    Col dc = C_MUTED;
    if (kind == 2) { fmt_x10(vb, b, sizeof(b)); fmt_x10(va, a, sizeof(a)); }
    else {
        if (vb < 0) snprintf(b, sizeof(b), "—"); else snprintf(b, sizeof(b), kind == 1 ? "%d °C" : "%d", vb);
        if (va < 0) snprintf(a, sizeof(a), "—"); else snprintf(a, sizeof(a), kind == 1 ? "%d °C" : "%d", va);
    }
    d[0] = 0;
    if (vb >= 0 && va >= 0 && vb != va) {
        int diff = va - vb;
        if (kind == 2) snprintf(d, sizeof(d), "%+d,%d", diff / 10, abs(diff) % 10);
        else snprintf(d, sizeof(d), "%+d", diff);
        dc = ((diff < 0) == (lower_better != 0)) ? C_OK : C_ERR;
    } else if (vb >= 0 && va >= 0) snprintf(d, sizeof(d), "=");
    text(F_BODY, label, x, y, C_TEXT2, AL_LEFT);
    text(F_BODY, b, x + w - 330, y, C_TEXT, AL_RIGHT);
    text(F_BODY, a, x + w - 170, y, C_TEXT, AL_RIGHT);
    text(F_BODYB, d, x + w, y, dc, AL_RIGHT);
}

static void fmt_when(unsigned long long t, char *out, int n)
{
    LogEntry e;
    e.when = t;
    log_time(&e, out, n);
}

static void do_report(void)
{
    static const char *tabs[3] = { "Fiche", "Notes", "Avant / Après" };
    if (pad.pressed[B_CIRCLE]) { screen = S_SYSTEM; return; }
    if (pad.pressed[B_R1] || pad.pressed[B_RIGHT]) { rep_tab = (rep_tab + 1) % 3; }
    if (pad.pressed[B_L1] || pad.pressed[B_LEFT]) { rep_tab = (rep_tab + 2) % 3; }
    if (ticks_ms() - rep_t > 1500) rep_refresh();
    if (rep_tab == 1) {
        if (pad.repeat[B_UP] && rep_sel > 0) rep_sel--;
        if (pad.repeat[B_DOWN] && rep_sel < REP_TAGS - 1) rep_sel++;
        if (pad.pressed[B_CROSS]) { g_rep_tags ^= 1u << rep_sel; report_notes_save(); rep_refresh(); }
    }
    if (pad.pressed[B_SQUARE] || pad.pressed[B_TRIANGLE]) {
        int slot = pad.pressed[B_TRIANGLE] ? 1 : 0;
        rep_refresh();
        if (report_save(slot, &rep_now) == 0) {
            rep_refresh();
            toast(slot ? "Enregistré comme APRÈS" : "Enregistré comme AVANT");
            log_add(slot ? "Rapport : état APRÈS enregistré" : "Rapport : état AVANT enregistré", "");
        } else toast("Enregistrement impossible");
    }
    if (pad.pressed[B_START]) {
        char p[96], m[200];
        if (report_export(p, sizeof(p)) == 0) { snprintf(m, sizeof(m), T("Exporté : %s"), p); toast(m); }
        else toast("Export impossible");
    }

    header(I_DOC, "Rapport de contrôle", NULL);
    tabs_bar(tabs, 3, rep_tab);
    text(F_SMALL, "L1 / R1 : changer d'onglet", SCREEN_W - PAD_X, 120 - text_h(F_SMALL) / 2, C_MUTED, AL_RIGHT);
    int y = 160, h = FOOT_LINE - 24 - y;
    card(PAD_X, y, CONTENT_W, h, 18, 1, C_LINE, C_PANEL);
    const Report *r = &rep_now;

    if (rep_tab == 0) {
        int cw = (CONTENT_W - 48 - 40) / 2, x1 = PAD_X + 24, x2 = x1 + cw + 40, ry = y + 20;
        char v[64];
        section_label("Console", x1, ry);
        kv_row(x1, ry + 30, cw, "Firmware", r->fw, C_TEXT);
        kv_row(x1, ry + 64, cw, "Système", mode_name(r->mode), C_TEXT);
        if (r->hdd_free_mb >= 0) { fmt_size((uint64_t)r->hdd_free_mb * 1024 * 1024, v, sizeof(v)); kv_row(x1, ry + 98, cw, "Disque dur libre", v, C_TEXT); }
        section_label("Diagnostic", x1, ry + 150);
        if (g_diag_pos < DIAG_N) kv_row(x1, ry + 180, cw, "Résultat", T("pas encore lancé"), C_MUTED);
        else {
            snprintf(v, sizeof(v), T("%d réussis · %d échecs · %d non testés"), r->d_pass, r->d_fail, r->d_skip);
            text(F_BODYB, v, x1, ry + 180, r->d_fail ? C_ERR : C_OK, AL_LEFT);
        }
        section_label("Stress test", x1, ry + 232);
        if (r->st_s < 0) kv_row(x1, ry + 262, cw, "Dernier test", T("jamais lancé"), C_MUTED);
        else {
            snprintf(v, sizeof(v), T("%d s · %d erreur(s)"), r->st_s, r->st_err);
            kv_row(x1, ry + 262, cw, "Dernier test", v, r->st_err ? C_ERR : C_OK);
        }
        section_label("Températures", x2, ry);
        const char *tl[6] = { "CPU repos (moyenne)", "RSX repos (moyenne)", "CPU charge (moyenne)", "RSX charge (moyenne)", "CPU maximum", "RSX maximum" };
        int tv[6] = { r->rest_cpu, r->rest_rsx, r->load_cpu, r->load_rsx, r->max_cpu, r->max_rsx };
        for (int i = 0; i < 6; i++) { fmt_temp(tv[i], v, sizeof(v)); kv_row(x2, ry + 30 + i * 34, cw, tl[i], v, tv[i] < 0 ? C_MUTED : C_TEXT); }
        section_label("Stockage et optique", x2, ry + 250);
        fmt_x10(r->hdd_w, v, sizeof(v)); char v2[32]; fmt_x10(r->hdd_r, v2, sizeof(v2));
        char both[80]; snprintf(both, sizeof(both), "%s · %s", v, v2);
        kv_row(x2, ry + 280, cw, "Disque dur (écr. · lect.)", both, r->hdd_w < 0 ? C_MUTED : C_TEXT);
        fmt_x10(r->usb_w, v, sizeof(v)); fmt_x10(r->usb_r, v2, sizeof(v2));
        snprintf(both, sizeof(both), "%s · %s", v, v2);
        kv_row(x2, ry + 314, cw, "USB (écr. · lect.)", both, r->usb_w < 0 ? C_MUTED : C_TEXT);
        kv_row(x2, ry + 348, cw, "Lecteur optique", r->opt < 0 ? T("non testé") : (r->opt ? T("erreurs de lecture") : T("lecture correcte")),
               r->opt < 0 ? C_MUTED : (r->opt ? C_ERR : C_OK));
        text(F_SMALL, "Aucun identifiant de la console (IDPS, PSID, série) n'est inclus.", x1, y + h - 36, C_MUTED, AL_LEFT);
    } else if (rep_tab == 1) {
        section_label("Interventions effectuées (notes du réparateur)", PAD_X + 24, y + 20);
        for (int i = 0; i < REP_TAGS; i++) {
            int col = i / 5, row = i % 5;
            int x = PAD_X + 24 + col * ((CONTENT_W - 48) / 2), ry = y + 62 + row * 62, w = (CONTENT_W - 48) / 2 - 16;
            int on = (g_rep_tags >> i) & 1;
            if (i == rep_sel) card(x, ry - 8, w, 52, 12, 2, C_ACCENT, C_PANEL_SEL);
            else card(x, ry - 8, w, 52, 12, 1, C_LINE, C_PANEL);
            card(x + 16, ry + 4, 24, 24, 6, 2, on ? C_OK : C_LINE3, on ? C_OK : C_PANEL);
            if (on) { thick_line(x + 21, ry + 17, x + 26, ry + 22, 3, C_BG); thick_line(x + 26, ry + 22, x + 35, ry + 10, 3, C_BG); }
            text(F_BODY, g_rep_tag_names[i], x + 56, ry + 4, on ? C_TEXT : C_TEXT2, AL_LEFT);
        }
        text_wrap(F_SMALL, "Coche ce qui a été fait. Les notes sont enregistrées dans la fiche « Avant / Après » quand tu appuies sur Carré ou Triangle, et dans l'export.",
                  PAD_X + 24, y + h - 62, CONTENT_W - 48, 20, C_MUTED);
    } else {
        int x = PAD_X + 24, w = CONTENT_W - 48, ry = y + 18;
        char da[32], db[32];
        if (rep_before.valid) fmt_when(rep_before.when, db, sizeof(db)); else snprintf(db, sizeof(db), "—");
        if (rep_after.valid) fmt_when(rep_after.when, da, sizeof(da)); else snprintf(da, sizeof(da), "—");
        text(F_SMALL, "AVANT", x + w - 330, ry, C_MUTED, AL_RIGHT);
        text(F_SMALL, db, x + w - 330, ry + 18, C_MUTED, AL_RIGHT);
        text(F_SMALL, "APRÈS", x + w - 170, ry, C_MUTED, AL_RIGHT);
        text(F_SMALL, da, x + w - 170, ry + 18, C_MUTED, AL_RIGHT);
        text(F_SMALL, "écart", x + w, ry, C_MUTED, AL_RIGHT);
        const Report *b = &rep_before, *a = &rep_after;
        int vb[12], va[12];
        vb[0] = b->valid && b->d_pass + b->d_fail > 0 ? b->d_pass : -1;  va[0] = a->valid && a->d_pass + a->d_fail > 0 ? a->d_pass : -1;
        vb[1] = b->rest_cpu;                       va[1] = a->rest_cpu;
        vb[2] = b->rest_rsx;                       va[2] = a->rest_rsx;
        vb[3] = b->load_cpu;                       va[3] = a->load_cpu;
        vb[4] = b->load_rsx;                       va[4] = a->load_rsx;
        vb[5] = b->max_cpu;                        va[5] = a->max_cpu;
        vb[6] = b->st_err < 0 || b->st_s < 0 ? -1 : b->st_err;  va[6] = a->st_err < 0 || a->st_s < 0 ? -1 : a->st_err;
        vb[7] = b->hdd_w;                          va[7] = a->hdd_w;
        vb[8] = b->hdd_r;                          va[8] = a->hdd_r;
        vb[9] = b->usb_w;                          va[9] = a->usb_w;
        vb[10] = b->opt;                           va[10] = a->opt;
        vb[11] = 0; va[11] = 0;
        if (!b->valid) for (int i = 0; i < 11; i++) vb[i] = -1;
        if (!a->valid) for (int i = 0; i < 11; i++) va[i] = -1;
        struct { const char *l; int kind, lower; } rows[11] = {
            { "Tests réussis (sur 13)", 0, 0 }, { "CPU au repos", 1, 1 }, { "RSX au repos", 1, 1 }, { "CPU en charge", 1, 1 },
            { "RSX en charge", 1, 1 }, { "CPU maximum", 1, 1 }, { "Erreurs au stress test", 0, 1 },
            { "Disque dur : écriture", 2, 0 }, { "Disque dur : lecture", 2, 0 }, { "USB : écriture", 2, 0 }, { "Erreurs lecteur optique", 0, 1 },
        };
        for (int i = 0; i < 11; i++) cmp_row(x, w, ry + 46 + i * 31, rows[i].l, vb[i], va[i], rows[i].kind, rows[i].lower);
        if (!b->valid || !a->valid) {
            text_wrap(F_SMALL, !b->valid ? "Pas d'état AVANT : fais tes tests, puis appuie sur Carré. Après l'intervention, refais les tests et appuie sur Triangle."
                                         : "Pas d'état APRÈS : après l'intervention, refais les tests puis appuie sur Triangle.",
                      x, y + h - 56, w, 20, C_WARN);
        } else if (a->tags & ~b->tags) {
            char l[256];
            snprintf(l, sizeof(l), "%s", T("Interventions ajoutées : "));
            for (int i = 0; i < REP_TAGS; i++) if ((a->tags & ~b->tags) & (1u << i)) {
                if (l[strlen(l) - 1] != ' ') strncat(l, " · ", sizeof(l) - strlen(l) - 1);
                strncat(l, T(g_rep_tag_names[i]), sizeof(l) - strlen(l) - 1);
            }
            text_fit_left(F_SMALL, l, x, y + h - 36, w, C_TEXT2);
        }
    }
    int gl[4], n = 0;
    const char *lb[4];
    gl[n] = G_SQUARE; lb[n++] = "Enregistrer AVANT";
    gl[n] = G_TRIANGLE; lb[n++] = "Enregistrer APRÈS";
    if (rep_tab == 1) { gl[n] = G_CROSS; lb[n++] = "Cocher"; }
    gl[n] = G_CIRCLE; lb[n++] = "Retour";
    foot("Start : exporter le rapport (.txt)", gl, lb, n);
}

static void tools_enter(int s)
{
    switch (s) {
    case S_DIAG: usb_poll(); optical_poll(); break;
    case S_STRESS: break;
    case S_STORAGE: sto_tab = 0; usb_poll(); g_speed.state = 0; sto_restart(); break;
    case S_PORTS: usb_poll(); port_sel = 0; for (int i = 0; i < USB_N; i++) if (g_usb[i].present) { port_sel = i; break; } g_speed.state = 0; break;
    case S_OPTICAL: optical_poll(); break;
    case S_AUDIO: if (aud_open() != 0) g_manual_audio = R_FAIL; else g_manual_audio = g_manual_audio == R_FAIL ? R_UNTESTED : g_manual_audio; au_row = 0; break;
    case S_THERMAL: break;
    case S_REPORT: report_notes_load(); rep_refresh(); rep_tab = 0; rep_sel = 0; break;
    }
}