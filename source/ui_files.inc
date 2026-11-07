#include "filetools.h"

enum { FM_LIST, FM_MENU, FM_INPUT, FM_HASH, FM_SIZE, FM_FIND, FM_HEX, FM_CMP };
enum { MI_HASH = 1, MI_HEX, MI_MARK, MI_CMP, MI_SIZE, MI_FIND, MI_GOTO, MI_HFIND, MI_TFIND, MI_ENDIAN, MI_BACK };
enum { IN_FIND, IN_GOTO, IN_HEXSEQ, IN_TEXTSEQ };

enum { P_NONE, P_ELF, P_SFO, P_HEX, P_CMPA, P_CMPB, P_LOG };
static int files_pick = 0, files_from_dev = 0;   
static void dev_pick_done(const char *p);
static int fmode = FM_LIST;
static int menu_id[8], menu_n, menu_sel, menu_from_hex;
static const char *menu_lab[8];
static char ft_mark[300] = "";
static char ft_target[400];                     
static const char *in_set;
static int in_kind, in_cols, in_cur;
static char in_buf[40];
static long long hx_cur, hx_top;
static int hx_big, hx_seeking, hx_kind_text;
static uint8_t hx_buf[16 * 16];
static int hx_n;
static uint8_t hx_pat[32];
static int hx_plen;
static int fr_sel, fr_scroll;
#define HX_ROWS 14

static void ft_back_to_list(void)
{
    fthash_cancel(); ftsize_cancel(); ftfind_cancel(); fthex_find_cancel(); ftcmp_cancel(); fthex_close();
    fmode = FM_LIST;
}

static void menu_add(int id, const char *lab) { menu_id[menu_n] = id; menu_lab[menu_n++] = lab; }

static void menu_open_list(void)
{
    menu_n = 0; menu_sel = 0; menu_from_hex = 0;
    int have = n_entries > 0 && cwd[0];
    if (have && entries[f_sel].isdir) menu_add(MI_SIZE, "Taille du dossier");
    if (have && !entries[f_sel].isdir) {
        menu_add(MI_HASH, "Empreinte SHA-256");
        menu_add(MI_HEX, "Lecteur hexadécimal");
        menu_add(MI_MARK, "Marquer pour comparer");
        if (ft_mark[0]) menu_add(MI_CMP, "Comparer avec le fichier marqué");
    }
    if (cwd[0]) menu_add(MI_FIND, "Rechercher dans ce dossier");
    if (!menu_n) { toast("Ouvre d'abord un périphérique"); return; }
    fmode = FM_MENU;
}

static void menu_open_hex(void)
{
    menu_n = 0; menu_sel = 0; menu_from_hex = 1;
    menu_add(MI_GOTO, "Aller à un offset");
    menu_add(MI_HFIND, "Chercher une séquence hexadécimale");
    menu_add(MI_TFIND, "Chercher du texte");
    menu_add(MI_ENDIAN, hx_big ? "Valeurs : passer en little-endian" : "Valeurs : passer en big-endian");
    menu_add(MI_BACK, "Retour au lecteur");
    fmode = FM_MENU;
}

static void input_open(int kind)
{
    in_kind = kind; in_cur = 0; in_buf[0] = 0;
    if (kind == IN_FIND || kind == IN_TEXTSEQ) { in_set = "abcdefghijklmnopqrstuvwxyz0123456789._-"; in_cols = 13; }
    else { in_set = "0123456789ABCDEF"; in_cols = 8; }
    fmode = FM_INPUT;
}

static void entry_path(char *out, int n)
{
    if (!cwd[0]) snprintf(out, n, "%s", entries[f_sel].name);
    else snprintf(out, n, "%s/%s", cwd, entries[f_sel].name);
}

static void hx_load(void)
{
    int rows = HX_ROWS;
    hx_n = fthex_read(hx_top, hx_buf, rows * 16);
}

static void hx_goto(long long off)
{
    long long sz = fthex_size();
    if (off >= sz) off = sz > 0 ? sz - 1 : 0;
    if (off < 0) off = 0;
    hx_cur = off;
    hx_top = (off / 16) * 16 - 4 * 16;
    if (hx_top < 0) hx_top = 0;
    hx_load();
}

static void hx_follow(void)
{
    if (hx_cur < hx_top) hx_top = (hx_cur / 16) * 16;
    else if (hx_cur >= hx_top + HX_ROWS * 16) hx_top = (hx_cur / 16 - HX_ROWS + 1) * 16;
    if (hx_top < 0) hx_top = 0;
    hx_load();
}

static void menu_run(int id)
{
    char p[400];
    switch (id) {
    case MI_HASH: entry_path(p, sizeof(p)); fthash_start(p); fmode = FM_HASH; break;
    case MI_SIZE: entry_path(p, sizeof(p)); ftsize_start(p); fmode = FM_SIZE; break;
    case MI_HEX:
        entry_path(p, sizeof(p));
        if (fthex_open(p) != 0) { toast("Impossible d'ouvrir ce fichier"); fmode = FM_LIST; break; }
        snprintf(ft_target, sizeof(ft_target), "%s", p);
        hx_cur = hx_top = 0; hx_seeking = 0; hx_plen = 0; memset(&g_ftseek, 0, sizeof(g_ftseek)); hx_load(); fmode = FM_HEX; break;
    case MI_MARK:
        entry_path(ft_mark, sizeof(ft_mark)); toast("Fichier marqué : choisis le second et ouvre le menu"); fmode = FM_LIST; break;
    case MI_CMP:
        entry_path(p, sizeof(p));
        if (!strcmp(p, ft_mark)) { toast("C'est le même fichier"); fmode = FM_LIST; break; }
        ftcmp_start(ft_mark, p); fmode = FM_CMP; break;
    case MI_FIND: input_open(IN_FIND); break;
    case MI_GOTO: input_open(IN_GOTO); break;
    case MI_HFIND: input_open(IN_HEXSEQ); break;
    case MI_TFIND: input_open(IN_TEXTSEQ); break;
    case MI_ENDIAN: hx_big = !hx_big; fmode = FM_HEX; break;
    default: fmode = FM_HEX; break;
    }
}

static void input_done(void)
{
    if (in_kind == IN_FIND) {
        if (!in_buf[0]) { fmode = FM_LIST; return; }
        ftfind_start(cwd, in_buf); fr_sel = fr_scroll = 0; fmode = FM_FIND; return;
    }
    if (in_kind == IN_GOTO) {
        long long v = 0;
        for (const char *c = in_buf; *c; c++) v = v * 16 + (*c <= '9' ? *c - '0' : *c - 'A' + 10);
        hx_goto(v); fmode = FM_HEX; return;
    }
    int n;
    if (in_kind == IN_HEXSEQ) n = ft_parse_hex(in_buf, hx_pat, sizeof(hx_pat));
    else { n = (int)strlen(in_buf); if (n > 32) n = 32; memcpy(hx_pat, in_buf, n); }
    if (n <= 0) { toast("Séquence invalide (nombre pair de chiffres)"); fmode = FM_HEX; return; }
    hx_plen = n; hx_kind_text = in_kind == IN_TEXTSEQ;
    hx_seeking = 1; fthex_find_start(hx_cur + 1, hx_pat, n); fmode = FM_HEX;
}

static void ft_card(const char *title, const char *sub)
{
    header(I_FOLDER, title, sub);
    card(PAD_X, 108, CONTENT_W, FOOT_LINE - 24 - 108, 18, 1, C_LINE, C_PANEL);
}

static void ft_foot(const char *left, const int *gl, const char **lb, int n)
{
    footer_line();
    if (left) text(F_SMALL, left, PAD_X, FOOT_Y + 13 - text_h(F_SMALL) / 2, C_MUTED, AL_LEFT);
    hints_right(FOOT_Y, SCREEN_W - PAD_X, gl, lb, n);
}

static void draw_menu(void)
{
    ft_card("Fichiers", menu_from_hex ? "Lecteur hexadécimal" : "Actions");
    for (int i = 0; i < menu_n; i++) {
        int ry = 130 + i * 56, sel = i == menu_sel;
        if (sel) fill_rrect(PAD_X + 14, ry, CONTENT_W - 28, 50, 12, C_PANEL_SEL);
        text(F_BODY, menu_lab[i], PAD_X + 40, ry + 25 - text_h(F_BODY) / 2, sel ? C_TEXT : C_TEXT2, AL_LEFT);
    }
    static const int gl[] = { G_CROSS, G_CIRCLE };
    static const char *lb[] = { "Choisir", "Retour" };
    ft_foot("Lecture seule : aucune modification.", gl, lb, 2);
}

static void draw_input(void)
{
    const char *tt = in_kind == IN_FIND ? "Nom à rechercher" : in_kind == IN_GOTO ? "Offset (hexadécimal)"
                   : in_kind == IN_HEXSEQ ? "Séquence hexadécimale" : "Texte à chercher";
    ft_card("Fichiers", tt);
    char b[64];
    snprintf(b, sizeof(b), "%s_", in_buf);
    text(F_BIG, b, PAD_X + 40, 130, C_ACCENT, AL_LEFT);
    int n = (int)strlen(in_set);
    for (int i = 0; i < n; i++) {
        int cx = PAD_X + 40 + (i % in_cols) * 78, cy = 250 + (i / in_cols) * 70, sel = i == in_cur;
        if (sel) fill_rrect(cx, cy, 68, 60, 12, C_ACCENT);
        else card(cx, cy, 68, 60, 12, 1, C_LINE, C_BG);
        char ch[2] = { in_set[i], 0 };
        text(F_H3, ch, cx + 34, cy + 30 - text_h(F_H3) / 2, sel ? C_BG : C_TEXT, AL_CENTER);
    }
    static const int gl[] = { G_CROSS, G_SQUARE, G_CIRCLE };
    static const char *lb[] = { "Ajouter", "Effacer", "Annuler" };
    ft_foot("Start : valider", gl, lb, 3);
}

static void draw_hash(void)
{
    ft_card("Fichiers", "Empreinte SHA-256");
    text_fit_left(F_BODY, g_fthash.path, PAD_X + 40, 130, CONTENT_W - 80, C_TEXT2);
    if (g_fthash.err) {
        text(F_H3, "Lecture impossible", PAD_X + 40, 220, C_ERR, AL_LEFT);
    } else if (!g_fthash.done) {
        float f = g_fthash.total ? (float)g_fthash.done_b / (float)g_fthash.total : 0;
        bar(PAD_X + 40, 220, CONTENT_W - 80, 14, f, C_ACCENT);
        char a[40], t[40], l[96];
        fmt_size(g_fthash.done_b, a, sizeof(a)); fmt_size(g_fthash.total, t, sizeof(t));
        snprintf(l, sizeof(l), "%s / %s", a, t);
        text(F_BODY, l, PAD_X + 40, 250, C_MUTED, AL_LEFT);
    } else {
        text(F_SMALL, "Empreinte (ne dépend que du contenu du fichier)", PAD_X + 40, 200, C_MUTED, AL_LEFT);
        char l1[40], l2[40];
        snprintf(l1, sizeof(l1), "%.32s", g_fthash.hex);
        snprintf(l2, sizeof(l2), "%.32s", g_fthash.hex + 32);
        text(F_H3, l1, PAD_X + 40, 240, C_OK, AL_LEFT);
        text(F_H3, l2, PAD_X + 40, 290, C_OK, AL_LEFT);
    }
    static const int gl[] = { G_CIRCLE };
    static const char *lb[] = { "Retour" };
    ft_foot(NULL, gl, lb, 1);
}

static void draw_size(void)
{
    ft_card("Fichiers", "Taille du dossier");
    text_fit_left(F_BODY, g_ftsize.root, PAD_X + 40, 130, CONTENT_W - 80, C_TEXT2);
    char s[40], l[96];
    fmt_size(g_ftsize.bytes, s, sizeof(s));
    text(F_HUGE, s, PAD_X + 40, 190, g_ftsize.done ? C_OK : C_ACCENT, AL_LEFT);
    snprintf(l, sizeof(l), T("%u fichier(s) · %u dossier(s)"), g_ftsize.files, g_ftsize.dirs);
    text(F_BODY, l, PAD_X + 40, 300, C_TEXT2, AL_LEFT);
    if (!g_ftsize.done) text(F_BODY, "Calcul en cours…", PAD_X + 40, 340, C_MUTED, AL_LEFT);
    else if (g_ftsize.incomplete) text(F_BODY, "Résultat partiel : certains éléments sont illisibles.", PAD_X + 40, 340, C_WARN, AL_LEFT);
    static const int gl[] = { G_CIRCLE };
    static const char *lb[] = { "Retour" };
    ft_foot(NULL, gl, lb, 1);
}

static void draw_find(void)
{
    char sub[96];
    snprintf(sub, sizeof(sub), T("Recherche « %s » : %d résultat(s)"), g_ftfind.query, g_ftfind.n);
    ft_card("Fichiers", sub);
    const int rows = 9, rh = 46;
    if (g_ftfind.n == 0)
        text(F_BODY, g_ftfind.done ? "Aucun résultat" : "Recherche en cours…", PAD_X + CONTENT_W / 2, 280, C_MUTED, AL_CENTER);
    for (int i = 0; i < rows && fr_scroll + i < g_ftfind.n; i++) {
        FtFound *f = &g_ftfind.r[fr_scroll + i];
        int ry = 124 + i * rh, sel = fr_scroll + i == fr_sel;
        if (sel) fill_rrect(PAD_X + 14, ry, CONTENT_W - 28, rh - 4, 10, C_PANEL_SEL);
        text_fit_left(F_BODY, f->path, PAD_X + 30, ry + 21 - text_h(F_BODY) / 2, CONTENT_W - 220, C_TEXT);
        if (!f->isdir && f->size >= 0) {
            char sz[32];
            fmt_size((uint64_t)f->size, sz, sizeof(sz));
            text(F_SMALL, sz, PAD_X + CONTENT_W - 32, ry + 21 - text_h(F_SMALL) / 2, C_MUTED, AL_RIGHT);
        }
    }
    static const int gl[] = { G_CROSS, G_CIRCLE };
    static const char *lb[] = { "Ouvrir", "Retour" };
    ft_foot(g_ftfind.incomplete ? "Résultats limités ou partiels." : NULL, gl, lb, 2);
}

static void draw_hex(void)
{
    char sub[160];
    char sz[32];
    fmt_size((uint64_t)fthex_size(), sz, sizeof(sz));
    snprintf(sub, sizeof(sub), "%s · %s", sz, hx_big ? "big-endian" : "little-endian");
    ft_card("Fichiers", sub);
    text_fit_left(F_SMALL, ft_target, PAD_X + 30, 120, CONTENT_W - 60, C_MUTED);
    for (int r = 0; r < HX_ROWS; r++) {
        int ry = 146 + r * 25;
        long long ro = hx_top + (long long)r * 16;
        if (r * 16 >= hx_n) break;
        char l[24];
        snprintf(l, sizeof(l), "%08llX", (unsigned long long)ro);
        text(F_SMALL, l, PAD_X + 30, ry, C_MUTED, AL_LEFT);
        for (int c = 0; c < 16 && r * 16 + c < hx_n; c++) {
            int cur = ro + c == hx_cur;
            int cx = PAD_X + 150 + c * 36 + (c >= 8 ? 14 : 0);
            if (cur) fill_rrect(cx - 4, ry - 3, 34, 24, 5, C_ACCENT);
            char b[4];
            snprintf(b, sizeof(b), "%02X", hx_buf[r * 16 + c]);
            text(F_SMALL, b, cx, ry, cur ? C_BG : C_TEXT, AL_LEFT);
            char a[2] = { hx_buf[r * 16 + c] >= 32 && hx_buf[r * 16 + c] < 127 ? (char)hx_buf[r * 16 + c] : '.', 0 };
            text(F_SMALL, a, PAD_X + 150 + 16 * 36 + 40 + c * 12, ry, cur ? C_ACCENT : C_TEXT2, AL_LEFT);
        }
    }
    uint8_t v[8];
    int k = fthex_read(hx_cur, v, 8);
    unsigned long long u8v = 0, u16 = 0, u32 = 0;
    if (k >= 1) u8v = v[0];
    if (k >= 2) u16 = hx_big ? (v[0] << 8 | v[1]) : (v[1] << 8 | v[0]);
    if (k >= 4) u32 = hx_big ? ((unsigned)v[0] << 24 | v[1] << 16 | v[2] << 8 | v[3])
                             : ((unsigned)v[3] << 24 | v[2] << 16 | v[1] << 8 | v[0]);
    char l[200];
    snprintf(l, sizeof(l), T("Offset 0x%llX · 8 bits : %llu · 16 bits : %llu · 32 bits : %llu"),
             (unsigned long long)hx_cur, u8v, u16, u32);
    text(F_SMALL, l, PAD_X + 30, 146 + HX_ROWS * 25 + 6, C_ACCENT2, AL_LEFT);
    if (hx_seeking) text(F_SMALL, "Recherche en cours…", PAD_X + CONTENT_W - 30, 120, C_WARN, AL_RIGHT);
    else if (hx_plen && g_ftseek.done && !g_ftseek.found) text(F_SMALL, "Séquence non trouvée après ce point", PAD_X + CONTENT_W - 30, 120, C_WARN, AL_RIGHT);
    static const int gl[] = { G_TRIANGLE, G_SQUARE, G_CIRCLE };
    static const char *lb[] = { "Menu", "Endian", "Retour" };
    ft_foot("Start : suivant · L1/R1 : page", gl, lb, 3);
}

static void draw_cmp(void)
{
    ft_card("Fichiers", "Comparaison de deux fichiers");
    text_fit_left(F_SMALL, g_ftcmp.a, PAD_X + 30, 124, CONTENT_W - 60, C_TEXT2);
    text_fit_left(F_SMALL, g_ftcmp.b, PAD_X + 30, 148, CONTENT_W - 60, C_TEXT2);
    char l[160];
    if (g_ftcmp.err) { text(F_H3, "Lecture impossible", PAD_X + 30, 200, C_ERR, AL_LEFT); }
    else if (!g_ftcmp.done) {
        float f = g_ftcmp.size_a ? (float)g_ftcmp.pos / (float)g_ftcmp.size_a : 0;
        bar(PAD_X + 30, 200, CONTENT_W - 60, 14, f, C_ACCENT);
        text(F_BODY, "Comparaison en cours…", PAD_X + 30, 230, C_MUTED, AL_LEFT);
    } else {
        int same = ft_cmp_identical();
        text(F_H2, same ? "Fichiers identiques" : "Fichiers différents", PAD_X + 30, 180, same ? C_OK : C_WARN, AL_LEFT);
        char a[32], b[32];
        fmt_size(g_ftcmp.size_a, a, sizeof(a)); fmt_size(g_ftcmp.size_b, b, sizeof(b));
        snprintf(l, sizeof(l), T("Tailles : %s et %s · octets différents : %llu"), a, b, (unsigned long long)g_ftcmp.ndiff);
        text(F_SMALL, l, PAD_X + 30, 226, C_TEXT2, AL_LEFT);
        for (int i = 0; i < g_ftcmp.nr && i < 8; i++) {
            snprintf(l, sizeof(l), T("Zone %d : offset 0x%llX, %llu octet(s)"), i + 1,
                     (unsigned long long)g_ftcmp.off[i], (unsigned long long)g_ftcmp.len[i]);
            text(F_SMALL, l, PAD_X + 30, 256 + i * 24, C_TEXT, AL_LEFT);
        }
        if (g_ftcmp.nr > 8 || g_ftcmp.over) text(F_SMALL, "… autres zones non listées", PAD_X + 30, 256 + 8 * 24, C_MUTED, AL_LEFT);
        char h1[40], h2[40];
        snprintf(h1, sizeof(h1), "A : %.32s", g_ftcmp.ha);
        snprintf(h2, sizeof(h2), "B : %.32s", g_ftcmp.hb);
        text(F_SMALL, h1, PAD_X + 30, 480, C_MUTED, AL_LEFT);
        text(F_SMALL, h2, PAD_X + 30, 504, C_MUTED, AL_LEFT);
        text(F_SMALL, "SHA-256 (32 premiers caractères)", PAD_X + 30, 528, C_MUTED, AL_LEFT);
    }
    static const int gl[] = { G_CIRCLE };
    static const char *lb[] = { "Retour" };
    ft_foot(NULL, gl, lb, 1);
}

static int files_tools(void)
{
    if (fmode == FM_LIST) return 0;
    switch (fmode) {
    case FM_MENU:
        if (pad.repeat[B_DOWN] && menu_sel < menu_n - 1) menu_sel++;
        if (pad.repeat[B_UP] && menu_sel > 0) menu_sel--;
        if (pad.pressed[B_CIRCLE]) fmode = menu_from_hex ? FM_HEX : FM_LIST;
        else if (pad.pressed[B_CROSS]) menu_run(menu_id[menu_sel]);
        draw_menu(); break;
    case FM_INPUT: {
        int n = (int)strlen(in_set);
        if (pad.repeat[B_RIGHT] && in_cur < n - 1) in_cur++;
        if (pad.repeat[B_LEFT] && in_cur > 0) in_cur--;
        if (pad.repeat[B_DOWN] && in_cur + in_cols < n) in_cur += in_cols;
        if (pad.repeat[B_UP] && in_cur - in_cols >= 0) in_cur -= in_cols;
        int l = (int)strlen(in_buf);
        int maxl = in_kind == IN_FIND ? 24 : in_kind == IN_GOTO ? 8 : 32;
        if (pad.pressed[B_CROSS] && l < maxl) { in_buf[l] = in_set[in_cur]; in_buf[l + 1] = 0; }
        if (pad.pressed[B_SQUARE] && l > 0) in_buf[l - 1] = 0;
        if (pad.pressed[B_CIRCLE]) fmode = in_kind == IN_FIND ? FM_LIST : FM_HEX;
        else if (pad.pressed[B_START]) input_done();
        draw_input(); break;
    }
    case FM_HASH:
        fthash_tick(12);
        if (pad.pressed[B_CIRCLE]) { ft_back_to_list(); return 1; }
        draw_hash(); break;
    case FM_SIZE:
        ftsize_tick(12);
        if (pad.pressed[B_CIRCLE]) { ft_back_to_list(); return 1; }
        draw_size(); break;
    case FM_FIND:
        ftfind_tick(12);
        if (pad.repeat[B_DOWN] && fr_sel < g_ftfind.n - 1) fr_sel++;
        if (pad.repeat[B_UP] && fr_sel > 0) fr_sel--;
        if (fr_sel < fr_scroll) fr_scroll = fr_sel;
        if (fr_sel >= fr_scroll + 9) fr_scroll = fr_sel - 8;
        if (pad.pressed[B_CIRCLE]) { ft_back_to_list(); return 1; }
        if (pad.pressed[B_CROSS] && g_ftfind.n > 0) {
            FtFound *f = &g_ftfind.r[fr_sel];
            char p[300];
            snprintf(p, sizeof(p), "%s", f->path);
            ft_back_to_list();
            if (f->isdir) { snprintf(cwd, sizeof(cwd), "%s", p); load_dir(); }
            else {
                char *s = strrchr(p, '/');
                if (s && s != p) { *s = 0; snprintf(cwd, sizeof(cwd), "%s", p); load_dir(); *s = '/'; }
                for (int i = 0; i < n_entries; i++) if (!strcmp(entries[i].name, s ? s + 1 : p)) { f_sel = i; break; }
            }
        }
        draw_find(); break;
    case FM_HEX:
        if (hx_seeking) {
            fthex_find_tick(12);
            if (g_ftseek.done) { hx_seeking = 0; if (g_ftseek.found) hx_goto(g_ftseek.at); else toast("Séquence non trouvée"); }
        }
        if (pad.repeat[B_RIGHT] && hx_cur + 1 < fthex_size()) { hx_cur++; hx_follow(); }
        if (pad.repeat[B_LEFT] && hx_cur > 0) { hx_cur--; hx_follow(); }
        if (pad.repeat[B_DOWN] && hx_cur + 16 < fthex_size()) { hx_cur += 16; hx_follow(); }
        if (pad.repeat[B_UP] && hx_cur >= 16) { hx_cur -= 16; hx_follow(); }
        if (pad.repeat[B_R1]) { hx_cur += HX_ROWS * 16; if (hx_cur >= fthex_size()) hx_cur = fthex_size() - 1; if (hx_cur < 0) hx_cur = 0; hx_follow(); }
        if (pad.repeat[B_L1]) { hx_cur -= HX_ROWS * 16; if (hx_cur < 0) hx_cur = 0; hx_follow(); }
        if (pad.pressed[B_SQUARE]) hx_big = !hx_big;
        if (pad.pressed[B_TRIANGLE]) menu_open_hex();
        if (pad.pressed[B_START] && hx_plen && !hx_seeking) { hx_seeking = 1; fthex_find_start(hx_cur + 1, hx_pat, hx_plen); }
        if (pad.pressed[B_CIRCLE]) { ft_back_to_list(); return 1; }
        draw_hex(); break;
    case FM_CMP:
        ftcmp_tick(12);
        if (pad.pressed[B_CIRCLE]) { ft_back_to_list(); return 1; }
        draw_cmp(); break;
    }
    return 1;
}