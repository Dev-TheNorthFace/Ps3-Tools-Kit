#include "lab.h"
#include <math.h>

enum { KT_RSX, KT_CELL, KT_MEM, KT_FILE, KT_AUDIO, KT_PAD, KT_SCN, KT_SES, KT_CON, KT_PROF, KT_N };

static const Tile kittiles[KT_N] = {
    { I_SCREEN,  "Laboratoire RSX",     "Scènes de test : textures, éclairage, transparence, profondeur, effets", 0 },
    { I_CHIP,    "Laboratoire Cell",    "Charges de calcul sur plusieurs fils, synchronisation, vérification", 0 },
    { I_BOX,     "Tests mémoire",       "Motifs écrits puis relus, différences et débit", 0 },
    { I_FOLDER,  "Banc d'essai fichiers", "Séquentiel, aléatoire, petits fichiers, données vérifiées", 0 },
    { I_SPEAKER, "Laboratoire audio",   "Tonalités, canaux, lecture en boucle, mire de synchronisation", 0 },
    { I_PAD,     "Laboratoire manettes", "Valeurs brutes, pressions, capteurs, vibrations, plusieurs manettes", 0 },
    { I_PULSE,   "Scénarios automatiques", "Enchaîner des tests, durée, répétitions, échecs", 0 },
    { I_CHECK,   "Comparateur de sessions", "Comparer avant/après réparation ou deux configurations", 0 },
    { I_DOC,     "Console technique",   "Opérations, codes d'erreur, durées et détails", 0 },
    { I_SETTINGS, "Profils de laboratoire", "Test rapide, stabilité, stockage, graphique, et les tiens", 0 },
};

static int kit_sel = 0, kit_tool = 0, kit_scroll = 0, kit_conf = 0, kit_last_n = -1, kit_start_scroll = 1;

static void kit_lines_from_res(int t)
{
    dv_clear();
    LabRes *r = &g_labres[t];
    if (!r->done) return;
    dv_add(r->fail ? DC_ERR : DC_OK, "%s · %d ms", T(r->summary), r->ms);
    for (int i = 0; i < r->nlines; i++) dv_add(r->fail && strstr(r->lines[i], "diff") ? DC_ERR : DC_NORMAL, "%s", T(r->lines[i]));
    for (int i = 0; i < r->nmet; i++) {
        char b[48];
        dv_add(DC_MUTED, "%s = %s", r->met[i].key, lab_metric_fmt(r->met[i].key, r->met[i].val, b, sizeof(b)));
    }
}

static void kit_open_tool(int t)
{
    dv_init();
    lab_prof_init(); lab_sess_load();
    kit_tool = t; kit_scroll = 0; kit_last_n = -1;
    switch (t) {
    case KT_RSX: screen = S_KITRSX; break;
    case KT_CELL: case KT_MEM: case KT_FILE: dv_clear(); if (g_labres[t - KT_CELL].done) kit_lines_from_res(t - KT_CELL); screen = S_KITV; break;
    case KT_AUDIO: enter_screen(S_AUDIO); audio_from_kit = 1; break;
    case KT_PAD: screen = S_KITPAD; break;
    case KT_SCN: screen = S_KITSCN; break;
    case KT_SES: screen = S_KITSES; break;
    case KT_CON: screen = S_KITV; break;
    case KT_PROF: screen = S_KITPROF; break;
    }
}

static void do_kit(void)
{
    const int cols = 5;
    if (pad.repeat[B_RIGHT] && kit_sel % cols < cols - 1 && kit_sel + 1 < KT_N) kit_sel++;
    if (pad.repeat[B_LEFT] && kit_sel % cols > 0) kit_sel--;
    if (pad.repeat[B_DOWN] && kit_sel + cols < KT_N) kit_sel += cols;
    if (pad.repeat[B_UP] && kit_sel >= cols) kit_sel -= cols;
    if (pad.pressed[B_CIRCLE]) { screen = S_MENU; return; }
    if (pad.pressed[B_CROSS]) { kit_open_tool(kit_sel); return; }

    waves(phase, 600);
    header(I_PULSE, "Dev Kit", "10 outils");
    int gap = 14, tw = (CONTENT_W - (cols - 1) * gap) / cols, th = 168;
    for (int i = 0; i < KT_N; i++) {
        int x = PAD_X + (i % cols) * (tw + gap), y = 104 + (i / cols) * (th + gap);
        int sel = i == kit_sel;
        if (sel) card(x, y, tw, th, 16, 2, C_ACCENT, C_PANEL_SEL);
        else card(x, y, tw, th, 16, 1, C_LINE, C_PANEL);
        icon(kittiles[i].ic, x + 18, y + 16, 34, sel ? C_ACCENT : C_TEXT2, sel ? C_PANEL_SEL : C_PANEL);
        text_wrap(F_BODYB, kittiles[i].title, x + 18, y + 56, tw - 28, 20, C_TEXT);
        text_wrap(F_SMALL, kittiles[i].desc, x + 18, y + 98, tw - 32, 18, sel ? C_TEXT2 : C_MUTED);
    }
    status_footer("Retour");
}

static Col kit_col(int c) { return dv_col(c); }

static void kit_draw_lines(int y, int h, int lh)
{
    int rows = h / lh;
    if (kit_scroll > g_dvn - rows) kit_scroll = g_dvn - rows;
    if (kit_scroll < 0) kit_scroll = 0;
    for (int i = 0; i < rows && kit_scroll + i < g_dvn; i++)
        text_fit_left(F_SMALL, g_dvl[kit_scroll + i].t, PAD_X + 24, y + i * lh, CONTENT_W - 60, kit_col(g_dvl[kit_scroll + i].c));
    if (g_dvn > rows) {
        int track = h - 8, th2 = track * rows / g_dvn;
        if (th2 < 24) th2 = 24;
        fill_rrect(PAD_X + CONTENT_W - 12, y + (track - th2) * kit_scroll / (g_dvn - rows), 4, th2, 2, C_LINE2);
    }
}

static void kit_scroll_keys(void)
{
    if (pad.repeat[B_DOWN]) kit_scroll++;
    if (pad.repeat[B_UP]) kit_scroll--;
    if (pad.repeat[B_R1] || pad.repeat[B_RIGHT]) kit_scroll += 12;
    if (pad.repeat[B_L1] || pad.repeat[B_LEFT]) kit_scroll -= 12;
}

static int opt_idx(const int *o, int v) { for (int i = 0; i < 5; i++) if (o[i] == v) return i; return 0; }
static void cyc(int *v, const int *o) { *v = o[(opt_idx(o, *v) + 1) % 5]; }

static void kit_console_build(int errors_only)
{
    dv_clear();
    for (int i = 0; i < g_lablog_n; i++) {
        LabLogLine *l = &g_lablog[i];
        if (errors_only && l->lvl < LL_WARN) continue;
        unsigned s = l->ms / 1000;
        dv_add(l->lvl == LL_ERR ? DC_ERR : l->lvl == LL_WARN ? DC_WARN : l->lvl == LL_OK ? DC_OK : DC_NORMAL, "[%u:%02u.%u] %s", s / 60, s % 60, (l->ms % 1000) / 100, l->t);
    }
    if (!g_dvn) dv_add(DC_MUTED, "%s", T(errors_only ? "Aucune erreur ni avertissement." : "Console vide : lance un test."));
}

static void do_kitv(void)
{
    int t = kit_tool - KT_CELL;                 
    int console = kit_tool == KT_CON;
    lab_reap();
    kit_scroll_keys();
    if (pad.pressed[B_CIRCLE]) { if (!console && g_lab_state == 1) lab_cancel(); screen = S_KIT; return; }
    int running = !console && g_lab_state == 1 && g_lab_test == t;
    if (console) {
        if (pad.pressed[B_SQUARE]) { kit_conf = !kit_conf; kit_last_n = -1; }
        if (pad.pressed[B_TRIANGLE]) { lab_log_clear(); kit_last_n = -1; }
        if (kit_last_n != g_lablog_n) {
            int stick = kit_scroll >= g_dvn - 20 || kit_last_n < 0;
            kit_console_build(kit_conf);
            kit_last_n = g_lablog_n;
            if (stick) kit_scroll = g_dvn;
        }
    } else {
        if (pad.pressed[B_CROSS]) {
            if (running) lab_cancel();
            else if (g_lab_state == 0 && lab_start(t) != 0) toast("Impossible de lancer le test");
        }
        if (!running) {
            if (pad.pressed[B_SQUARE]) cyc(&g_labcfg.dur_s, lab_dur_opts);
            if (pad.pressed[B_TRIANGLE]) { if (t == LT_MEM) cyc(&g_labcfg.mem_mb, lab_mem_opts); else if (t == LT_FILE) cyc(&g_labcfg.file_mb, lab_file_opts); }
        }
        static int was_run = 0;
        if (was_run && !running && g_labres[t].done) { kit_lines_from_res(t); kit_scroll = 0; log_add("Dev Kit : test terminé", ""); }
        was_run = running;
    }

    header(I_PULSE, console ? "Console technique" : kittiles[kit_tool].title, console ? "Journal des opérations du laboratoire" : "Dev Kit");
    int y = 108, h = FOOT_LINE - 24 - y;
    card(PAD_X, y, CONTENT_W, h, 18, 1, C_LINE, C_PANEL);
    int ly = y + 16, lh = h - 32;
    if (!console) {
        char p[160];
        if (t == LT_CELL) snprintf(p, sizeof(p), T("Durée : %d s · 3 fils PPU (les SPU ne sont pas utilisés)"), g_labcfg.dur_s);
        else if (t == LT_MEM) snprintf(p, sizeof(p), T("Durée : %d s · mémoire testée : %d Mo"), g_labcfg.dur_s, g_labcfg.mem_mb);
        else snprintf(p, sizeof(p), T("Durée : %d s · fichier de test : %d Mo · dossier /dev_hdd0/tmp/PS3TK_LAB"), g_labcfg.dur_s, g_labcfg.file_mb);
        text(F_BODY, p, PAD_X + 24, y + 18, C_TEXT2, AL_LEFT);
        bar(PAD_X + 24, y + 54, CONTENT_W - 48, 12, running ? g_lab_prog / 100.0f : (g_labres[t].done ? 1.0f : 0.0f), g_labres[t].done && g_labres[t].fail && !running ? C_ERR : C_ACCENT);
        if (!running && !g_labres[t].done) text(F_BODY, "Croix : lancer le test", PAD_X + CONTENT_W / 2, y + h / 2, C_MUTED, AL_CENTER);
        ly = y + 84; lh = h - 100;
    }
    kit_draw_lines(ly, lh, 22);

    int gl[5], n = 0;
    const char *lb[5];
    if (console) {
        gl[n] = G_SQUARE; lb[n++] = kit_conf ? "Tout afficher" : "Erreurs seules";
        gl[n] = G_TRIANGLE; lb[n++] = "Effacer";
    } else {
        gl[n] = G_CROSS; lb[n++] = running ? "Arrêter" : "Lancer";
        if (!running) { gl[n] = G_SQUARE; lb[n++] = "Durée"; }
        if (!running && t != LT_CELL) { gl[n] = G_TRIANGLE; lb[n++] = "Taille"; }
    }
    gl[n] = G_CIRCLE; lb[n++] = "Retour";
    foot(console ? "Start : exporter · Haut/Bas : défiler" : "Les tests s'exécutent sur un fil de fond.", gl, lb, n);
    if (pad.pressed[B_START] && g_dvn > 0) dev_export(console ? "console" : kit_tool == KT_CELL ? "cell" : kit_tool == KT_MEM ? "memoire" : "fichiers");
}

enum { RS_TEX, RS_LIGHT, RS_ALPHA, RS_DEPTH, RS_FX, RS_FILL, RS_N };
static const char *rs_names[RS_N] = { "Textures", "Éclairage", "Transparence", "Profondeur", "Effet plein écran (calcul CPU)", "Remplissage" };
static const char *rs_tips[RS_N] = {
    "Observer : damier net, pas de lignes déchirées ni de scintillement sur les bords.",
    "Observer : dégradés lisses, sans bandes visibles ; la lumière tourne régulièrement.",
    "Observer : les couleurs se mélangent là où les formes se recouvrent, sans trous.",
    "Observer : les cubes proches cachent les lointains, sans inversion d'ordre.",
    "Observer : motif animé fluide ; ce calcul est fait par le processeur, pas par le RSX.",
    "Observer : le temps par image monte avec le nombre de formes (Haut/Bas).",
};
static int rs_scene = 0, rs_fill_n = 200;
static unsigned rs_t0 = 0;
/* statistiques de temps par image */
typedef struct { unsigned last, n, worst; unsigned long long sum; unsigned best; } RsStat;
static RsStat rs_stat;
static void rs_stat_reset(void) { memset(&rs_stat, 0, sizeof(rs_stat)); rs_stat.best = 100000; }
static void rs_stat_frame(void)
{
    unsigned now = ticks_ms();
    if (rs_stat.last) {
        unsigned d = now - rs_stat.last;
        if (d < 2000) { rs_stat.n++; rs_stat.sum += d; if (d > rs_stat.worst) rs_stat.worst = d; if (d < rs_stat.best) rs_stat.best = d; }
    }
    rs_stat.last = now;
}

static void proj(float x, float y, float z, float ay, float ax, float cx, float cy, float f, float *sx, float *sy, float *sz)
{
    float x1 = x * cosf(ay) + z * sinf(ay), z1 = -x * sinf(ay) + z * cosf(ay);
    float y1 = y * cosf(ax) - z1 * sinf(ax), z2 = y * sinf(ax) + z1 * cosf(ax);
    float k = f / (z2 + 4.0f);
    *sx = cx + x1 * k; *sy = cy + y1 * k; *sz = z2;
}

static Col mulc(Col c, float k, int a)
{
    if (k < 0) k = 0;
    if (k > 1) k = 1;
    return (Col){ (uint8_t)(c.r * k), (uint8_t)(c.g * k), (uint8_t)(c.b * k), (uint8_t)a };
}

typedef struct { float z; float xy[8]; Col c; } RFace;
static int rface_cmp(const void *a, const void *b) { float d = ((const RFace *)b)->z - ((const RFace *)a)->z; return d > 0 ? 1 : d < 0 ? -1 : 0; }

static int rs_cube_faces(RFace *out, float ox, float oy, float oz, float s, float ay, float ax, float cx, float cy, float f, Col base, float lx, float ly, float lz, float fog)
{
    static const int fv[6][4] = { { 0, 1, 3, 2 }, { 4, 5, 7, 6 }, { 0, 2, 6, 4 }, { 1, 3, 7, 5 }, { 0, 1, 5, 4 }, { 2, 3, 7, 6 } };
    static const float nn[6][3] = { { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 } };
    float px[8], py[8], pz[8];
    for (int i = 0; i < 8; i++) {
        float x = ((i & 1) ? 1 : -1) * s, y = ((i & 2) ? 1 : -1) * s, z = ((i & 4) ? 1 : -1) * s;
        float x1 = x * cosf(ay) + z * sinf(ay), z1 = -x * sinf(ay) + z * cosf(ay);   /* rotation propre du cube */
        float y1 = y * cosf(ax) - z1 * sinf(ax), z2 = y * sinf(ax) + z1 * cosf(ax);
        proj(x1 + ox, y1 + oy, z2 + oz, 0.0f, 0.0f, cx, cy, f, &px[i], &py[i], &pz[i]);
    }
    int n = 0;
    for (int fi = 0; fi < 6; fi++) {
        float nx = nn[fi][0] * cosf(ay) + nn[fi][2] * sinf(ay), nz1 = -nn[fi][0] * sinf(ay) + nn[fi][2] * cosf(ay);
        float ny = nn[fi][1] * cosf(ax) - nz1 * sinf(ax), nz = nn[fi][1] * sinf(ax) + nz1 * cosf(ax);
        if (nz > 0.05f) continue;                         /* face tournée à l'opposé */
        float lum = 0.25f + 0.75f * fmaxf(0, -(nx * lx + ny * ly + nz * lz));
        float zz = 0;
        for (int k = 0; k < 4; k++) { out[n].xy[2 * k] = px[fv[fi][k]]; out[n].xy[2 * k + 1] = py[fv[fi][k]]; zz += pz[fv[fi][k]]; }
        out[n].z = zz / 4;
        out[n].c = mulc(base, lum * (1.0f - fog * (out[n].z + 1.5f) / 6.0f), 255);
        n++;
    }
    return n;
}

static void rs_draw_scene(int sc, int x, int y, int w, int h, float t)
{
    clip_push(x, y, w, h);
    fill_rect(x, y, w, h, C_BG);
    float cx = x + w / 2.0f, cy = y + h / 2.0f;
    switch (sc) {
    case RS_TEX: {
        const int N = 14;
        for (int i = 0; i < N; i++)
            for (int j = 0; j < N; j++) {
                float u0 = -2 + 4.0f * i / N, u1 = -2 + 4.0f * (i + 1) / N, v0 = -2 + 4.0f * j / N, v1 = -2 + 4.0f * (j + 1) / N;
                float q[8], z;
                proj(u0, 0.4f, v0, t * 0.5f, 0.8f, cx, cy, 440, &q[0], &q[1], &z);
                proj(u1, 0.4f, v0, t * 0.5f, 0.8f, cx, cy, 440, &q[2], &q[3], &z);
                proj(u1, 0.4f, v1, t * 0.5f, 0.8f, cx, cy, 440, &q[4], &q[5], &z);
                proj(u0, 0.4f, v1, t * 0.5f, 0.8f, cx, cy, 440, &q[6], &q[7], &z);
                int chk = (i + j) & 1;
                Col c = chk ? HEX(0xDDE6F5) : HEX(0x2A4A7A);
                c = mulc(c, 0.55f + 0.45f * (float)j / N, 255);
                fill_poly(q, 4, c);
            }
        break; }
    case RS_LIGHT: {
        float lx = cosf(t), ly = -0.5f, lz = sinf(t);
        float ln = sqrtf(lx * lx + ly * ly + lz * lz);
        lx /= ln; ly /= ln; lz /= ln;
        RFace f[6];
        int n = rs_cube_faces(f, 0, 0, 0, 0.9f, t * 0.7f, 0.5f, cx - w / 4, cy, 520, HEX(0x3D9BFF), lx, ly, lz, 0);
        qsort(f, n, sizeof(RFace), rface_cmp);
        for (int i = 0; i < n; i++) fill_poly(f[i].xy, 4, f[i].c);
        for (int k = 0; k < 3; k++) {
            int sx = (int)(cx + w / 4 + (k - 1) * 90), sy2 = (int)cy + (k - 1) * 30, r = 55 - k * 8;
            fill_rgrad(sx, sy2, r, sx + (int)(lx * r * 0.6f), sy2 + (int)(ly * r * 0.6f), HEX(0xFFFFFF), HEX(0x101C30));
        }
        fill_vgrad(x + 20, y + h - 40, w - 40, 22, HEX(0x000000), HEX(0xFFFFFF));
        break; }
    case RS_ALPHA:
        for (int i = 0; i < 6; i++) {
            float a = t * 0.8f + i * 1.05f;
            int px = (int)(cx + cosf(a) * 170), py = (int)(cy + sinf(a * 1.3f) * 100);
            Col c = i % 3 == 0 ? HEXA(0xFF4060, 140) : i % 3 == 1 ? HEXA(0x40FF90, 140) : HEXA(0x4090FF, 140);
            fill_circle(px, py, 90, c);
            fill_rrect(px - 70, py - 40, 140, 80, 14, HEXA(0xFFFFFF, 60));
        }
        break;
    case RS_DEPTH: {
        RFace all[6 * 5];
        int n = 0;
        for (int k = 0; k < 5; k++) {
            float a = t * 0.6f + k;
            Col base = k % 2 ? HEX(0xFFB547) : HEX(0x5FD6A8);
            n += rs_cube_faces(all + n, (k - 2) * 1.1f, sinf(a) * 0.5f, cosf(a) * 1.4f, 0.5f, t * 0.4f + k, 0.4f, cx, cy, 420, base, 0.3f, -0.5f, 0.8f, 1.0f);
        }
        qsort(all, n, sizeof(RFace), rface_cmp);
        for (int i = 0; i < n; i++) fill_poly(all[i].xy, 4, all[i].c);
        break; }
    case RS_FX: {
        const int cell = 12;
        for (int j = 0; j < h / cell; j++)
            for (int i = 0; i < w / cell; i++) {
                float v = sinf(i * 0.21f + t) + sinf(j * 0.27f - t * 1.3f) + sinf((i + j) * 0.13f + t * 0.7f);
                v = (v + 3) / 6;
                fill_rect(x + i * cell, y + j * cell, cell, cell, (Col){ (uint8_t)(30 + 200 * v), (uint8_t)(60 + 120 * (1 - v)), (uint8_t)(150 + 100 * sinf(v * 3)), 255 });
            }
        break; }
    default:
        for (int i = 0; i < rs_fill_n; i++) {
            float a = i * 2.399f + t * 0.3f;
            int px = (int)(cx + cosf(a) * (i % 300) * 1.2f), py = (int)(cy + sinf(a * 1.1f) * (i % 160) * 1.3f);
            fill_circle(px, py, 8 + (i % 20), HEXA(0x3D9BFF + (i * 977 & 0xFFFF) * 0, 90));
        }
        break;
    }
    clip_pop();
}

static void rs_stat_line(char *out, int n)
{
    if (!rs_stat.n) { snprintf(out, n, "—"); return; }
    unsigned avg10 = (unsigned)(rs_stat.sum * 10 / rs_stat.n);
    unsigned fps10 = avg10 ? 10000u * 10u / avg10 : 0;
    snprintf(out, n, T("Image : moyenne %u,%u ms (%u,%u i/s) · meilleur %u ms · pire %u ms"), avg10 / 10, avg10 % 10, fps10 / 10, fps10 % 10, rs_stat.best, rs_stat.worst);
}

static void do_kitrsx(void)
{
    if (pad.pressed[B_CIRCLE]) { screen = S_KIT; return; }
    if (pad.repeat[B_RIGHT] || pad.pressed[B_R1]) { rs_scene = (rs_scene + 1) % RS_N; rs_stat_reset(); }
    if (pad.repeat[B_LEFT] || pad.pressed[B_L1]) { rs_scene = (rs_scene + RS_N - 1) % RS_N; rs_stat_reset(); }
    if (rs_scene == RS_FILL) {
        if (pad.repeat[B_UP] && rs_fill_n < 2000) rs_fill_n += 50;
        if (pad.repeat[B_DOWN] && rs_fill_n > 50) rs_fill_n -= 50;
    }
    if (pad.pressed[B_SQUARE]) rs_stat_reset();
    rs_stat_frame();

    header(I_SCREEN, "Laboratoire RSX", T(rs_names[rs_scene]));
    int y = 108, h = FOOT_LINE - 24 - y;
    card(PAD_X, y, CONTENT_W, h, 18, 1, C_LINE, C_PANEL);
    rs_draw_scene(rs_scene, PAD_X + 8, y + 8, CONTENT_W - 16, h - 90, (ticks_ms() - rs_t0) / 1000.0f);
    char l[200];
    rs_stat_line(l, sizeof(l));
    text(F_BODY, l, PAD_X + 24, y + h - 78, C_ACCENT2, AL_LEFT);
    text_fit_left(F_SMALL, rs_tips[rs_scene], PAD_X + 24, y + h - 44, CONTENT_W - 48, C_MUTED);
    if (rs_scene == RS_FILL) { snprintf(l, sizeof(l), T("%d formes"), rs_fill_n); text(F_BODY, l, PAD_X + CONTENT_W - 24, y + h - 78, C_TEXT, AL_RIGHT); }
    static const int gl[] = { G_SQUARE, G_CIRCLE };
    static const char *lb[] = { "Remettre à zéro", "Retour" };
    foot("Gauche/Droite : scène · Rendu Cairo logiciel (pas de shader RSX réel)", gl, lb, 2);
}

static int kp_sel = 0, kp_small = 0, kp_large = 0;
static const char *kp_pre[12] = { "Dr", "Ga", "Ha", "Ba", "Tr", "Ro", "Cr", "Ca", "L1", "R1", "L2", "R2" };

static void kp_stop_rumble(void) { for (int i = 0; i < PAD_PORTS; i++) if (g_pads[i].connected) pad_rumble(i, 0, 0); kp_small = kp_large = 0; }

static void do_kitpad(void)
{
    if (pad.pressed[B_CIRCLE]) { kp_stop_rumble(); screen = S_KIT; return; }
    int ports[4], np = 0;
    for (int i = 0; i < PAD_PORTS && np < 4; i++) if (g_pads[i].connected) ports[np++] = i;
    if (kp_sel >= np) kp_sel = np ? np - 1 : 0;
    if (pad.pressed[B_R1] && kp_sel < np - 1) { kp_stop_rumble(); kp_sel++; }
    if (pad.pressed[B_L1] && kp_sel > 0) { kp_stop_rumble(); kp_sel--; }
    if (np) {
        int port = ports[kp_sel];
        if (pad.pressed[B_SQUARE]) kp_small = !kp_small;
        if (pad.pressed[B_TRIANGLE]) kp_large = kp_large ? 0 : 200;
        static int sent_small = -1, sent_large = -1, sent_port = -1;
        if (sent_small != kp_small || sent_large != kp_large || sent_port != port) {
            int r = pad_rumble(port, kp_small, kp_large);
            lab_log(r == 0 ? LL_INFO : LL_WARN, "Manette %d : vibration petit moteur %d, gros moteur %d (code %d)", port + 1, kp_small, kp_large, r);
            sent_small = kp_small; sent_large = kp_large; sent_port = port;
        }
    }
    char sub[96];
    snprintf(sub, sizeof(sub), T("%d manette(s) connectée(s)"), np);
    header(I_PAD, "Laboratoire manettes", sub);
    int y = 108, h = FOOT_LINE - 24 - y, gap = 14, cw = (CONTENT_W - 3 * gap) / 4;
    if (!np) {
        card(PAD_X, y, CONTENT_W, h, 18, 1, C_LINE, C_PANEL);
        text(F_H3, "Aucune manette connectée", PAD_X + CONTENT_W / 2, y + h / 2 - 12, C_MUTED, AL_CENTER);
    }
    for (int k = 0; k < np; k++) {
        PadRaw *r = &g_pads[ports[k]];
        int x = PAD_X + k * (cw + gap), sel = k == kp_sel;
        if (sel) card(x, y, cw, h, 16, 2, C_ACCENT, C_PANEL_SEL); else card(x, y, cw, h, 16, 1, C_LINE, C_PANEL);
        char b[96];
        snprintf(b, sizeof(b), T("Manette %d"), ports[k] + 1);
        text(F_H3, b, x + 16, y + 14, C_TEXT, AL_LEFT);
        int cy = y + 50;
        text(F_SMALL, r->has_pressure ? "Pression : oui" : "Pression : non", x + 16, cy, r->has_pressure ? C_OK : C_MUTED, AL_LEFT);
        text(F_SMALL, r->has_sensor ? "Capteurs : oui" : "Capteurs : non", x + 16, cy + 20, r->has_sensor ? C_OK : C_MUTED, AL_LEFT);
        text(F_SMALL, r->has_vibrate ? "Vibration : oui" : "Vibration : non", x + 16, cy + 40, r->has_vibrate ? C_OK : C_MUTED, AL_LEFT);
        snprintf(b, sizeof(b), T("Boutons 0x%04X"), r->buttons);
        text_fit_left(F_SMALL, b, x + 16, cy + 62, cw - 28, C_TEXT2);
        snprintf(b, sizeof(b), T("len %d · %llu lectures"), r->len, r->frames);
        text_fit_left(F_SMALL, b, x + 16, cy + 80, cw - 28, C_MUTED);
        char on[120] = "";
        for (int i = 0; i < B_COUNT; i++) if (r->buttons & (1u << i)) { if (on[0]) strncat(on, " ", sizeof(on) - strlen(on) - 1); strncat(on, T(button_names[i]), sizeof(on) - strlen(on) - 1); }
        text_fit_left(F_SMALL, on[0] ? on : "—", x + 16, cy + 98, cw - 28, C_ACCENT2);
        for (int s = 0; s < 2; s++) {
            int bx = x + 16 + s * 100, by = cy + 124, sz = 84;
            card(bx, by, sz, sz, 8, 1, C_LINE2, C_BG);
            int vx = s ? r->rx : r->lx, vy = s ? r->ry : r->ly;
            fill_circle(bx + vx * (sz - 8) / 255 + 4, by + vy * (sz - 8) / 255 + 4, 5, C_ACCENT);
            snprintf(b, sizeof(b), "%d,%d", vx, vy);
            text(F_SMALL, b, bx + sz / 2, by + sz + 4, C_MUTED, AL_CENTER);
        }
        int py = cy + 232;
        text(F_SMALL, "Pressions 0-255", x + 16, py, C_MUTED, AL_LEFT);
        for (int i = 0; i < 12; i++) {
            int col = i % 3, row = i / 3;
            snprintf(b, sizeof(b), "%s %d", kp_pre[i], r->pre[i]);
            text(F_SMALL, b, x + 16 + col * 84, py + 20 + row * 20, r->pre[i] ? C_OK : C_MUTED, AL_LEFT);
        }
        snprintf(b, sizeof(b), "X %d Y %d Z %d G %d", r->sx, r->sy, r->sz, r->sg);
        text_fit_left(F_SMALL, b, x + 16, py + 110, cw - 28, C_TEXT2);
        if (sel) {
            snprintf(b, sizeof(b), T("Vibration : petit %s · gros %s"), T(kp_small ? "oui" : "non"), T(kp_large ? "oui" : "non"));
            text_fit_left(F_SMALL, b, x + 16, py + 134, cw - 28, C_WARN);
        }
    }
    static const int gl[] = { G_SQUARE, G_TRIANGLE, G_CIRCLE };
    static const char *lb[] = { "Petit moteur", "Gros moteur", "Retour" };
    foot("L1/R1 : choisir la manette", gl, lb, 3);
}

enum { SC_CFG, SC_RUN, SC_DONE };
static int sc_state = SC_CFG, sc_row = 0, sc_rep = 0, sc_step = 0, sc_started = 0, sc_fails = 0, sc_steps_done = 0, sc_abort = 0;
static unsigned sc_rsx_t0;
static int sc_order[LT_N], sc_n;
static LabSession sc_sess;
static long long sc_acc[LAB_SMET]; static int sc_acc_n[LAB_SMET]; static char sc_acc_key[LAB_SMET][16]; static int sc_nacc;
static const char *sc_rows[] = { "Cell (PPU)", "Mémoire", "Fichiers", "RSX (rendu)", "Durée par test", "Répétitions", "Mémoire testée", "Fichier de test", "S'arrêter au premier échec", "Lancer le scénario" };
#define SC_ROWS 10

static void sc_acc_add(const char *k, int v, int is_max)
{
    for (int i = 0; i < sc_nacc; i++) if (!strcmp(sc_acc_key[i], k)) { if (is_max) { if (v > sc_acc[i]) sc_acc[i] = v; } else { sc_acc[i] += v; sc_acc_n[i]++; } return; }
    if (sc_nacc >= LAB_SMET) return;
    snprintf(sc_acc_key[sc_nacc], 16, "%s", k);
    sc_acc[sc_nacc] = v; sc_acc_n[sc_nacc] = 1; sc_nacc++;
}

static void sc_start(void)
{
    sc_n = 0;
    for (int t = 0; t < LT_N; t++) if (g_labcfg.mask & LT_MASK(t)) sc_order[sc_n++] = t;
    if (!sc_n) { toast("Choisis au moins un test"); return; }
    dv_init(); dv_clear();
    sc_state = SC_RUN; sc_rep = 0; sc_step = 0; sc_started = 0; sc_fails = 0; sc_steps_done = 0; sc_abort = 0; sc_nacc = 0; kit_scroll = 0;
    memset(&sc_sess, 0, sizeof(sc_sess));
    lab_log(LL_INFO, "##### Scénario « %s » : %d test(s) x %d répétition(s), %d s chacun #####", g_labcfg.name, sc_n, g_labcfg.reps, g_labcfg.dur_s);
}

static void sc_finish(void)
{
    sc_state = SC_DONE;
    sc_sess.when = wall_time();
    snprintf(sc_sess.label, sizeof(sc_sess.label), "Session");
    snprintf(sc_sess.profile, sizeof(sc_sess.profile), "%s", g_labcfg.name);
    sc_sess.fails = sc_fails;
    for (int i = 0; i < sc_nacc && sc_sess.nmet < LAB_SMET; i++) {
        snprintf(sc_sess.met[sc_sess.nmet].key, 16, "%s", sc_acc_key[i]);
        sc_sess.met[sc_sess.nmet].val = (!strcmp(sc_acc_key[i], "rsx.worst") || !sc_acc_n[i]) ? (int)sc_acc[i] : (int)(sc_acc[i] / sc_acc_n[i]);
        sc_sess.nmet++;
    }
    snprintf(sc_sess.met[sc_sess.nmet].key, 16, "fails"); sc_sess.met[sc_sess.nmet++].val = sc_fails;
    lab_sess_add(&sc_sess);
    lab_log(sc_fails ? LL_ERR : LL_OK, "##### Scénario terminé : %d échec(s) sur %d étape(s). Session enregistrée. #####", sc_fails, sc_steps_done);
    char a[16];
    snprintf(a, sizeof(a), "%d", sc_fails);
    log_add("Scénario Dev Kit terminé : %s échec(s)", a);
}

static void sc_record(int rep, int t, int fail, const char *summary, int ms)
{
    sc_steps_done++;
    if (fail) sc_fails++;
    char name[48];
    snprintf(name, sizeof(name), "%s", T(lab_test_name(t)));
    dv_add(fail ? DC_ERR : DC_OK, T("Répétition %d · %s : %s (%d ms)"), rep + 1, name, T(summary), ms);
    if (fail && t <= LT_FILE) for (int i = 0; i < g_labres[t].nlines && i < 3; i++) dv_add(DC_ERR, "    %s", T(g_labres[t].lines[i]));
}

static void sc_tick(void)
{
    lab_reap();
    if (pad.pressed[B_CIRCLE]) { lab_cancel(); sc_abort = 1; }
    if (sc_state != SC_RUN) return;
    if (sc_abort) {
        if (g_lab_state == 0) { dv_add(DC_WARN, "%s", T("Scénario interrompu.")); lab_log(LL_WARN, "Scénario interrompu par l'utilisateur"); sc_finish(); }
        return;
    }
    int t = sc_order[sc_step];
    if (t == LT_RSX) {
        if (!sc_started) { sc_started = 1; sc_rsx_t0 = ticks_ms(); rs_stat_reset(); lab_log(LL_INFO, "=== Test RSX (rendu) : %d s, 6 scènes en alternance ===", g_labcfg.dur_s); }
        unsigned el = ticks_ms() - sc_rsx_t0;
        if (el >= (unsigned)g_labcfg.dur_s * 1000u) {
            unsigned avg10 = rs_stat.n ? (unsigned)(rs_stat.sum * 10 / rs_stat.n) : 0;
            int fps10 = avg10 ? (int)(100000u / avg10) : 0;
            int fail = rs_stat.worst > 500;
            LabRes *r = &g_labres[LT_RSX];
            memset(r, 0, sizeof(*r));
            r->fail = fail; r->ms = (int)el;
            sc_acc_add("rsx.fps", fps10, 0); sc_acc_add("rsx.worst", (int)rs_stat.worst * 10, 1);
            lab_log(fail ? LL_ERR : LL_OK, "RSX : %d,%d i/s en moyenne, pire image %u ms", fps10 / 10, fps10 % 10, rs_stat.worst);
            sc_record(sc_rep, t, fail, fail ? "ÉCHEC" : "Réussi", (int)el);
            sc_started = 0;
            sc_step++;
        }
    } else {
        if (!sc_started) { if (lab_start(t) == 0) sc_started = 1; else { sc_record(sc_rep, t, 1, "ÉCHEC", 0); sc_step++; } }
        else if (g_lab_state == 0 && g_labres[t].done) {
            LabRes *r = &g_labres[t];
            for (int i = 0; i < r->nmet; i++) sc_acc_add(r->met[i].key, r->met[i].val, 0);
            sc_record(sc_rep, t, r->fail, r->summary, r->ms);
            sc_started = 0;
            sc_step++;
        }
    }
    if (sc_state == SC_RUN) {
        if (g_labcfg.stop_fail && sc_fails) { dv_add(DC_WARN, "%s", T("Arrêt au premier échec.")); sc_finish(); return; }
        if (sc_step >= sc_n) { sc_step = 0; sc_rep++; if (sc_rep >= g_labcfg.reps) sc_finish(); }
    }
}

static void do_kitscn(void)
{
    sc_tick();
    if (sc_state == SC_CFG) {
        if (pad.pressed[B_CIRCLE]) { screen = S_KIT; return; }
        if (pad.repeat[B_UP] && sc_row > 0) sc_row--;
        if (pad.repeat[B_DOWN] && sc_row < SC_ROWS - 1) sc_row++;
        if (pad.pressed[B_CROSS] || pad.pressed[B_RIGHT]) {
            if (sc_row < 4) g_labcfg.mask ^= LT_MASK(sc_row);
            else if (sc_row == 4) cyc(&g_labcfg.dur_s, lab_dur_opts);
            else if (sc_row == 5) cyc(&g_labcfg.reps, lab_rep_opts);
            else if (sc_row == 6) cyc(&g_labcfg.mem_mb, lab_mem_opts);
            else if (sc_row == 7) cyc(&g_labcfg.file_mb, lab_file_opts);
            else if (sc_row == 8) g_labcfg.stop_fail = !g_labcfg.stop_fail;
            else if (pad.pressed[B_CROSS]) sc_start();
        }
    } else if (sc_state == SC_DONE) {
        kit_scroll_keys();
        if (pad.pressed[B_CIRCLE]) sc_state = SC_CFG;
        if (pad.pressed[B_CROSS]) { sc_state = SC_CFG; }
        if (pad.pressed[B_SQUARE]) { kit_tool = KT_SES; screen = S_KITSES; }
        if (pad.pressed[B_START]) dev_export("scenario");
    } else kit_scroll_keys();

    header(I_PULSE, "Scénarios automatiques", g_labcfg.name);
    int y = 108, h = FOOT_LINE - 24 - y;
    card(PAD_X, y, CONTENT_W, h, 18, 1, C_LINE, C_PANEL);
    if (sc_state == SC_CFG) {
        for (int i = 0; i < SC_ROWS; i++) {
            int ry = y + 14 + i * 47, sel = i == sc_row;
            if (sel) fill_rrect(PAD_X + 14, ry, CONTENT_W - 28, 42, 10, C_PANEL_SEL);
            text(F_BODY, sc_rows[i], PAD_X + 34, ry + 21 - text_h(F_BODY) / 2, i == 9 ? C_ACCENT : C_TEXT, AL_LEFT);
            char v[64] = "";
            if (i < 4) snprintf(v, sizeof(v), "%s", T(g_labcfg.mask & LT_MASK(i) ? "Activé" : "Désactivé"));
            else if (i == 4) snprintf(v, sizeof(v), "%d s", g_labcfg.dur_s);
            else if (i == 5) snprintf(v, sizeof(v), "%d", g_labcfg.reps);
            else if (i == 6) snprintf(v, sizeof(v), "%d %s", g_labcfg.mem_mb, T("Mo"));
            else if (i == 7) snprintf(v, sizeof(v), "%d %s", g_labcfg.file_mb, T("Mo"));
            else if (i == 8) snprintf(v, sizeof(v), "%s", T(g_labcfg.stop_fail ? "Oui" : "Non"));
            Col c = i < 4 ? (g_labcfg.mask & LT_MASK(i) ? C_OK : C_MUTED) : C_TEXT2;
            text(F_BODY, v, PAD_X + CONTENT_W - 34, ry + 21 - text_h(F_BODY) / 2, c, AL_RIGHT);
        }
    } else {
        int total = sc_n * g_labcfg.reps;
        char b[120];
        if (sc_state == SC_RUN) {
            snprintf(b, sizeof(b), T("Étape %d sur %d · répétition %d sur %d · %s"), sc_steps_done + 1, total, sc_rep + 1, g_labcfg.reps, T(lab_test_name(sc_order[sc_step < sc_n ? sc_step : 0])));
            text(F_BODY, b, PAD_X + 24, y + 16, C_TEXT2, AL_LEFT);
            int tt = sc_order[sc_step < sc_n ? sc_step : 0];
            float frac = tt == LT_RSX ? (float)(ticks_ms() - sc_rsx_t0) / (g_labcfg.dur_s * 1000.0f) : g_lab_prog / 100.0f;
            if (frac > 1) frac = 1;
            bar(PAD_X + 24, y + 46, CONTENT_W - 48, 12, ((float)sc_steps_done + frac) / (total ? total : 1), C_ACCENT);
            if (tt == LT_RSX && sc_started) {
                int sc = (int)((ticks_ms() - sc_rsx_t0) / 2500u) % RS_N;
                rs_draw_scene(sc, PAD_X + CONTENT_W - 340, y + 76, 316, 190, (ticks_ms() - sc_rsx_t0) / 1000.0f);
                rs_stat_frame();
            }
        } else {
            snprintf(b, sizeof(b), T("Terminé : %d échec(s) sur %d étape(s)"), sc_fails, sc_steps_done);
            text(F_H3, b, PAD_X + 24, y + 14, sc_fails ? C_ERR : C_OK, AL_LEFT);
            text(F_SMALL, "Session enregistrée : renomme-la dans « Comparateur de sessions ».", PAD_X + 24, y + 46, C_MUTED, AL_LEFT);
        }
        kit_draw_lines(y + 76, h - 92, 22);
    }
    int gl[4], n = 0;
    const char *lb[4];
    if (sc_state == SC_CFG) { gl[n] = G_CROSS; lb[n++] = sc_row == 9 ? "Lancer" : "Changer"; }
    else if (sc_state == SC_DONE) { gl[n] = G_CROSS; lb[n++] = "Nouvelle config"; gl[n] = G_SQUARE; lb[n++] = "Sessions"; }
    gl[n] = G_CIRCLE; lb[n++] = sc_state == SC_RUN ? "Interrompre" : "Retour";
    foot(sc_state == SC_CFG ? "Les profils (section suivante) remplissent cette configuration." : "Start : exporter les résultats", gl, lb, n);
}

static int pf_sel = 0;
static void pf_summary(const LabCfg *c, char *o, int n)
{
    char t[96] = "";
    for (int i = 0; i < LT_N; i++) if (c->mask & LT_MASK(i)) { if (t[0]) strncat(t, " + ", sizeof(t) - strlen(t) - 1); strncat(t, T(lab_test_name(i)), sizeof(t) - strlen(t) - 1); }
    snprintf(o, n, T("%s · %d s x %d"), t[0] ? t : "—", c->dur_s, c->reps);
}

static void do_kitprof(void)
{
    if (pad.pressed[B_CIRCLE]) { screen = S_KIT; return; }
    if (pad.repeat[B_UP] && pf_sel > 0) pf_sel--;
    if (pad.repeat[B_DOWN] && pf_sel < LAB_PROF_N - 1) pf_sel++;
    if (pad.pressed[B_CROSS]) {
        if (lab_prof_used(pf_sel)) { g_labcfg = *lab_prof_get(pf_sel); sc_state = SC_CFG; sc_row = 9; screen = S_KITSCN; lab_log(LL_INFO, "Profil chargé : %s", g_labcfg.name); }
        else toast("Emplacement vide : enregistre d'abord la configuration courante");
    }
    if (pad.pressed[B_SQUARE]) {
        if (!lab_prof_is_user(pf_sel)) toast("Les quatre modèles ne peuvent pas être modifiés");
        else if (lab_prof_save(pf_sel, &g_labcfg) == 0) toast("Configuration courante enregistrée");
    }
    if (pad.pressed[B_TRIANGLE]) {
        if (lab_prof_is_user(pf_sel) && lab_prof_used(pf_sel)) { lab_prof_clear(pf_sel); lab_prof_init(); toast("Profil effacé"); }
    }
    header(I_SETTINGS, "Profils de laboratoire", "Dev Kit");
    int y = 108, h = FOOT_LINE - 24 - y;
    card(PAD_X, y, CONTENT_W, h, 18, 1, C_LINE, C_PANEL);
    for (int i = 0; i < LAB_PROF_N; i++) {
        int ry = y + 14 + i * 56, sel = i == pf_sel;
        if (sel) fill_rrect(PAD_X + 14, ry, CONTENT_W - 28, 50, 10, C_PANEL_SEL);
        const LabCfg *c = lab_prof_get(i);
        char s[160];
        text(F_BODYB, c->name, PAD_X + 34, ry + 8, lab_prof_used(i) ? C_TEXT : C_MUTED, AL_LEFT);
        if (lab_prof_used(i)) { pf_summary(c, s, sizeof(s)); text_fit_left(F_SMALL, s, PAD_X + 34, ry + 30, CONTENT_W - 220, C_TEXT2); }
        else text(F_SMALL, "Vide", PAD_X + 34, ry + 30, C_MUTED, AL_LEFT);
        text(F_SMALL, lab_prof_is_user(i) ? "Personnel" : "Modèle", PAD_X + CONTENT_W - 34, ry + 18, C_MUTED, AL_RIGHT);
    }
    static const int gl[] = { G_CROSS, G_SQUARE, G_TRIANGLE, G_CIRCLE };
    static const char *lb[] = { "Charger", "Enregistrer ici", "Effacer", "Retour" };
    foot("Enregistrer = copie la configuration des scénarios dans l'emplacement choisi", gl, lb, 4);
}

static int ss_sel = 0, ss_a = -1, ss_mode = 0;
static const char *ss_labels[] = { "Session", "Avant", "Après", "Config A", "Config B" };

static void do_kitses(void)
{
    if (ss_mode == 1) {
        kit_scroll_keys();
        if (pad.pressed[B_CIRCLE]) { ss_mode = 0; ss_a = -1; }
        if (pad.pressed[B_START]) dev_export("comparaison");
    } else {
        if (pad.pressed[B_CIRCLE]) { screen = S_KIT; return; }
        if (pad.repeat[B_UP] && ss_sel > 0) ss_sel--;
        if (pad.repeat[B_DOWN] && ss_sel < g_nsess - 1) ss_sel++;
        if (pad.pressed[B_SQUARE] && g_nsess) {
            int k = 0;
            for (int i = 0; i < 5; i++) if (!strcmp(g_sess[ss_sel].label, ss_labels[i])) k = i;
            lab_sess_set_label(ss_sel, ss_labels[(k + 1) % 5]);
        }
        if (pad.pressed[B_CROSS] && g_nsess) {
            if (ss_a < 0) { ss_a = ss_sel; toast("Choisis la seconde session"); }
            else if (ss_a == ss_sel) ss_a = -1;
            else { dv_init(); lab_sess_compare(ss_a, ss_sel); ss_mode = 1; kit_scroll = 0; }
        }
    }
    header(I_CHECK, "Comparateur de sessions", ss_mode ? "Comparaison" : "Dev Kit");
    int y = 108, h = FOOT_LINE - 24 - y;
    card(PAD_X, y, CONTENT_W, h, 18, 1, C_LINE, C_PANEL);
    if (ss_mode == 1) kit_draw_lines(y + 16, h - 32, 24);
    else {
        if (!g_nsess) text(F_BODY, "Aucune session : lance un scénario, il en enregistre une.", PAD_X + CONTENT_W / 2, y + h / 2, C_MUTED, AL_CENTER);
        int st = ss_sel > 8 ? ss_sel - 8 : 0;
        for (int i = st; i < g_nsess && i < st + 9; i++) {
            int ry = y + 14 + (i - st) * 50, sel = i == ss_sel;
            if (sel) fill_rrect(PAD_X + 14, ry, CONTENT_W - 28, 44, 10, C_PANEL_SEL);
            LogEntry fake; memset(&fake, 0, sizeof(fake)); fake.when = g_sess[i].when;
            char tm[40], s[160];
            log_time(&fake, tm, sizeof(tm));
            snprintf(s, sizeof(s), "%s · %s", g_sess[i].label, g_sess[i].profile);
            text(F_BODYB, i == ss_a ? "A" : "", PAD_X + 28, ry + 12, C_ACCENT, AL_LEFT);
            text_fit_left(F_BODY, s, PAD_X + 58, ry + 22 - text_h(F_BODY) / 2, CONTENT_W - 400, C_TEXT);
            text(F_SMALL, tm, PAD_X + CONTENT_W - 190, ry + 22 - text_h(F_SMALL) / 2, C_MUTED, AL_RIGHT);
            char f[40];
            snprintf(f, sizeof(f), T("%d échec(s)"), g_sess[i].fails);
            text(F_SMALL, f, PAD_X + CONTENT_W - 34, ry + 22 - text_h(F_SMALL) / 2, g_sess[i].fails ? C_ERR : C_OK, AL_RIGHT);
        }
    }
    int gl[3], n = 0;
    const char *lb[3];
    if (ss_mode == 0) { gl[n] = G_CROSS; lb[n++] = ss_a < 0 ? "Choisir A" : "Comparer à A"; gl[n] = G_SQUARE; lb[n++] = "Étiquette"; }
    gl[n] = G_CIRCLE; lb[n++] = "Retour";
    foot(ss_mode ? "Start : exporter · Haut/Bas : défiler" : "Étiquettes : Avant, Après, Config A, Config B", gl, lb, n);
}