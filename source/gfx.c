#include "gfx.h"
#include "screen.h"
#include "prefs.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <cairo/cairo.h>
#include <cairo/cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H

static cairo_surface_t *canvas = NULL;
static cairo_t *cr = NULL;
static FT_Library ft = NULL;
static FT_Face faces[2];
static cairo_font_face_t *cfaces[2];
static int has_font = 0;

static const int font_size[F_COUNT] = { 14, 16, 16, 18, 22, 30, 44, 84 };
static const int font_bold[F_COUNT] = { 0, 0, 1, 1, 1, 1, 1, 1 };
static const char *sys_regular[] = {
    "/dev_flash/data/font/SCE-PS3-RD-R-LATIN.TTF",
    "/dev_flash/data/font/SCE-PS3-RD-R-LATIN2.TTF",
    "/dev_flash/data/font/SCE-PS3-NR-R-LATIN.TTF",
    "/dev_flash/data/font/SCE-PS3-VR-R-LATIN.TTF",
    NULL
};
static const char *sys_bold[] = {
    "/dev_flash/data/font/SCE-PS3-RD-B-LATIN.TTF",
    "/dev_flash/data/font/SCE-PS3-RD-B-LATIN2.TTF",
    "/dev_flash/data/font/SCE-PS3-NR-B-LATIN.TTF",
    NULL
};

static FT_Face open_face(const char *usrdir, const char *own, const char **list)
{
    char p[512];
    FT_Face f = NULL;
    if (usrdir) {
        snprintf(p, sizeof(p), "%s/%s", usrdir, own);
        if (FT_New_Face(ft, p, 0, &f) == 0) { printf("[PS3TK] police : %s\n", p); return f; }
    }
    for (int i = 0; list[i]; i++)
        if (FT_New_Face(ft, list[i], 0, &f) == 0) { printf("[PS3TK] police : %s\n", list[i]); return f; }
    return NULL;
}

int gfx_has_font(void) { return has_font; }

int gfx_init(const char *usrdir)
{
    if (screen_init() != 0) return -1;

    canvas = cairo_image_surface_create(CAIRO_FORMAT_RGB24, screen_width(), screen_height());
    if (cairo_surface_status(canvas) != CAIRO_STATUS_SUCCESS) {
        printf("[PS3TK] canvas cairo impossible\n");
        return -1;
    }
    cr = cairo_create(canvas);
    cairo_scale(cr, screen_width() / (double)SCREEN_W, screen_height() / (double)SCREEN_H);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    if (FT_Init_FreeType(&ft) == 0) {
        faces[0] = open_face(usrdir, "font.otf", sys_regular);
        faces[1] = open_face(usrdir, "font_bold.otf", sys_bold);
        if (!faces[1]) faces[1] = faces[0];
        if (faces[0]) {
            cfaces[0] = cairo_ft_font_face_create_for_ft_face(faces[0], 0);
            cfaces[1] = cairo_ft_font_face_create_for_ft_face(faces[1], 0);
            has_font = 1;
        }
    }
    if (!has_font) printf("[PS3TK] ATTENTION : aucune police trouvée\n");
    return 0;
}

void gfx_begin(void)
{
    cairo_set_source_rgb(cr, C_BG.r / 255.0, C_BG.g / 255.0, C_BG.b / 255.0);
    cairo_paint(cr);
}

void gfx_end(void)
{
    cairo_surface_flush(canvas);
    screen_present((const uint32_t *)cairo_image_surface_get_data(canvas),
                   cairo_image_surface_get_stride(canvas));
}

void gfx_quit(void)
{
    if (cr) cairo_destroy(cr);
    if (canvas) cairo_surface_destroy(canvas);
    screen_end();
}

static void src(Col c)
{
    if (c.a == 255) cairo_set_source_rgb(cr, c.r / 255.0, c.g / 255.0, c.b / 255.0);
    else cairo_set_source_rgba(cr, c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0);
}

static void rr_path(double x, double y, double w, double h, double r)
{
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 0) r = 0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

void fill_rect(int x, int y, int w, int h, Col c)
{
    if (w <= 0 || h <= 0) return;
    src(c);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
}

void fill_rrect(int x, int y, int w, int h, int r, Col c)
{
    if (w <= 0 || h <= 0) return;
    src(c);
    rr_path(x, y, w, h, r);
    cairo_fill(cr);
}

void card(int x, int y, int w, int h, int r, int border, Col bc, Col fill)
{
    if (w <= 0 || h <= 0) return;
    fill_rrect(x, y, w, h, r, fill);
    if (border > 0) {
        double b = border / 2.0;
        src(bc);
        cairo_set_line_width(cr, border);
        rr_path(x + b, y + b, w - border, h - border, r - b);
        cairo_stroke(cr);
    }
}

void fill_poly(const float *xy, int n, Col c)
{
    if (n < 3) return;
    src(c);
    cairo_new_path(cr);
    cairo_move_to(cr, xy[0], xy[1]);
    for (int i = 1; i < n; i++) cairo_line_to(cr, xy[2 * i], xy[2 * i + 1]);
    cairo_close_path(cr);
    cairo_fill(cr);
}

void fill_vgrad(int x, int y, int w, int h, Col a, Col b)
{
    cairo_pattern_t *p = cairo_pattern_create_linear(0, y, 0, y + h);
    cairo_pattern_add_color_stop_rgba(p, 0, a.r / 255.0, a.g / 255.0, a.b / 255.0, a.a / 255.0);
    cairo_pattern_add_color_stop_rgba(p, 1, b.r / 255.0, b.g / 255.0, b.b / 255.0, b.a / 255.0);
    cairo_set_source(cr, p);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
}

void fill_rgrad(int cx, int cy, int r, int hx, int hy, Col a, Col b)
{
    if (r <= 0) return;
    cairo_pattern_t *p = cairo_pattern_create_radial(hx, hy, 0, cx, cy, r);
    cairo_pattern_add_color_stop_rgba(p, 0, a.r / 255.0, a.g / 255.0, a.b / 255.0, a.a / 255.0);
    cairo_pattern_add_color_stop_rgba(p, 1, b.r / 255.0, b.g / 255.0, b.b / 255.0, b.a / 255.0);
    cairo_set_source(cr, p);
    cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_pattern_destroy(p);
}

void fill_circle(int cx, int cy, int r, Col c)
{
    if (r <= 0) return;
    src(c);
    cairo_new_sub_path(cr);
    cairo_arc(cr, cx + 0.5, cy + 0.5, r, 0, 2 * M_PI);
    cairo_fill(cr);
}

void ring(int cx, int cy, int r, int t, Col c, Col inside)
{
    if (r <= 0) return;
    fill_circle(cx, cy, r, inside);
    src(c);
    cairo_set_line_width(cr, t);
    cairo_new_sub_path(cr);
    cairo_arc(cr, cx + 0.5, cy + 0.5, r - t / 2.0, 0, 2 * M_PI);
    cairo_stroke(cr);
}

void thick_line(int x1, int y1, int x2, int y2, int t, Col c)
{
    double o = (t % 2) ? 0.5 : 0.0;
    src(c);
    cairo_set_line_width(cr, t);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, x1 + o, y1 + o);
    cairo_line_to(cr, x2 + o, y2 + o);
    cairo_stroke(cr);
}

void bar(int x, int y, int w, int h, float frac, Col c)
{
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    fill_rrect(x, y, w, h, h / 2, C_LINE);
    int fw = (int)(w * frac);
    if (fw > 0) fill_rrect(x, y, fw < h ? h : fw, h, h / 2, c);
}

void waves(float phase, int base_y)
{
    static const int alpha[3] = { 70, 42, 24 };
    cairo_set_line_width(cr, 1.6);
    for (int k = 0; k < 3; k++) {
        for (int x = 0; x <= SCREEN_W; x += 8) {
            float fx = (float)x / SCREEN_W;
            float y = base_y + k * 30
                    + 34.0f * sinf(fx * 5.2f + phase + k * 0.7f)
                    + 14.0f * sinf(fx * 11.0f - phase * 1.3f + k);
            if (x == 0) cairo_move_to(cr, x, y);
            else cairo_line_to(cr, x, y);
        }
        src(HEXA(0x3D9BFF, alpha[k]));
        cairo_stroke(cr);
    }
}

static void use_font(int font)
{
    cairo_set_font_face(cr, cfaces[font_bold[font] ? 1 : 0]);
    cairo_set_font_size(cr, font_size[font]);
}

int text_h(int font)
{
    if (!has_font) return font_size[font];
    cairo_font_extents_t fe;
    use_font(font);
    cairo_font_extents(cr, &fe);
    return (int)(fe.ascent + fe.descent + 0.5);
}

static int raw_text_w(int font, const char *s)
{
    if (!has_font || !s || !*s) return 0;
    cairo_text_extents_t te;
    use_font(font);
    cairo_text_extents(cr, s, &te);
    return (int)(te.x_advance + 0.5);
}

static int raw_text(int font, const char *s, int x, int y, Col c, int align)
{
    if (!has_font || !s || !*s) return 0;
    cairo_font_extents_t fe;
    cairo_text_extents_t te;
    use_font(font);
    cairo_font_extents(cr, &fe);
    cairo_text_extents(cr, s, &te);
    int w = (int)(te.x_advance + 0.5);
    if (align == AL_CENTER) x -= w / 2;
    else if (align == AL_RIGHT) x -= w;
    src(c);
    cairo_move_to(cr, x, y + fe.ascent);
    cairo_show_text(cr, s);
    cairo_new_path(cr);
    return w;
}

void clip_push(int x, int y, int w, int h)
{
    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, h);
    cairo_clip(cr);
}

void clip_pop(void) { cairo_restore(cr); }

int text_wrap_lines(int font, const char *s, int maxw)
{
    s = T(s);
    char line[512] = "", trial[512];
    const char *p = s;
    int lines = 0;
    while (*p) {
        char word[128]; int n = 0;
        while (*p == ' ') p++;
        while (*p && *p != ' ' && n < 127) word[n++] = *p++;
        word[n] = 0;
        if (!n) break;
        if (line[0]) snprintf(trial, sizeof(trial), "%s %s", line, word);
        else snprintf(trial, sizeof(trial), "%s", word);
        if (line[0] && raw_text_w(font, trial) > maxw) { lines++; snprintf(line, sizeof(line), "%s", word); }
        else snprintf(line, sizeof(line), "%s", trial);
    }
    if (line[0]) lines++;
    return lines;
}

int text_w(int font, const char *s) { return raw_text_w(font, T(s)); }
int text(int font, const char *s, int x, int y, Col c, int align) { return raw_text(font, T(s), x, y, c, align); }

int text_wrap(int font, const char *s, int x, int y, int maxw, int lineh, Col c)
{
    s = T(s);
    char line[512] = "", trial[512];
    const char *p = s;
    int lines = 0;
    while (*p) {
        char word[128]; int n = 0;
        while (*p == ' ') p++;
        while (*p && *p != ' ' && n < 127) word[n++] = *p++;
        word[n] = 0;
        if (!n) break;
        if (line[0]) snprintf(trial, sizeof(trial), "%s %s", line, word);
        else snprintf(trial, sizeof(trial), "%s", word);
        if (line[0] && raw_text_w(font, trial) > maxw) {
            raw_text(font, line, x, y + lines * lineh, c, AL_LEFT);
            lines++;
            snprintf(line, sizeof(line), "%s", word);
        } else {
            snprintf(line, sizeof(line), "%s", trial);
        }
    }
    if (line[0]) { raw_text(font, line, x, y + lines * lineh, c, AL_LEFT); lines++; }
    return lines;
}

void text_fit_left(int font, const char *s, int x, int y, int maxw, Col c)
{
    s = T(s);
    if (raw_text_w(font, s) <= maxw) { raw_text(font, s, x, y, c, AL_LEFT); return; }
    char buf[512];
    const char *p = s;
    while (*p) {
        snprintf(buf, sizeof(buf), "...%s", p);
        if (raw_text_w(font, buf) <= maxw) break;
        p++;
        while ((*p & 0xC0) == 0x80) p++; 
    }
    raw_text(font, buf, x, y, c, AL_LEFT);
}

void glyph(int g, int cx, int cy)
{
    ring(cx, cy, 13, 1, C_LINE2, C_BG);
    switch (g) {
    case G_CROSS:
        thick_line(cx - 5, cy - 5, cx + 5, cy + 5, 2, C_ACCENT2);
        thick_line(cx + 5, cy - 5, cx - 5, cy + 5, 2, C_ACCENT2);
        break;
    case G_CIRCLE:
        ring(cx, cy, 6, 2, C_ERR, C_BG);
        break;
    case G_TRIANGLE:
        thick_line(cx, cy - 6, cx + 6, cy + 5, 2, C_OK);
        thick_line(cx + 6, cy + 5, cx - 6, cy + 5, 2, C_OK);
        thick_line(cx - 6, cy + 5, cx, cy - 6, 2, C_OK);
        break;
    case G_SQUARE:
        card(cx - 5, cy - 5, 11, 11, 1, 2, C_PINK, C_BG);
        break;
    }
}

int hint_w(const char *label) { return 26 + 8 + text_w(F_SMALL, label); }

void hint(int g, const char *label, int x, int y)
{
    glyph(g, x + 13, y + 13);
    text(F_SMALL, label, x + 34, y + 13 - text_h(F_SMALL) / 2, C_TEXT2, AL_LEFT);
}

void hints_right(int y, int right, const int *gl, const char **labels, int n)
{
    int total = 0;
    for (int i = 0; i < n; i++) total += hint_w(labels[i]) + (i ? 22 : 0);
    int x = right - total;
    for (int i = 0; i < n; i++) {
        hint(gl[i], labels[i], x, y);
        x += hint_w(labels[i]) + 22;
    }
}

void icon(int id, int x, int y, int s, Col c, Col bg)
{
    int t = s >= 34 ? 3 : 2;
    int cx = x + s / 2, cy = y + s / 2;
    switch (id) {
    case I_CHIP: {
        int a = s / 5;
        card(x + a, y + a, s - 2 * a, s - 2 * a, 3, t, c, bg);
        card(cx - s / 8, cy - s / 8, s / 4, s / 4, 1, t, c, bg);
        for (int i = 0; i < 2; i++) {
            int o = (i ? s * 3 / 5 : s * 2 / 5);
            thick_line(x + o, y + 2, x + o, y + a, t, c);
            thick_line(x + o, y + s - a, x + o, y + s - 2, t, c);
            thick_line(x + 2, y + o, x + a, y + o, t, c);
            thick_line(x + s - a, y + o, x + s - 2, y + o, t, c);
        }
        break;
    }
    case I_FOLDER:
        card(x + 2, y + s / 4, s * 2 / 5, s / 5, 3, t, c, bg);
        card(x + 2, y + s / 3, s - 4, s / 2, 4, t, c, bg);
        break;
    case I_PAD:
        card(x + 1, y + s / 3, s - 2, s * 2 / 5, s / 6, t, c, bg);
        thick_line(x + s / 4, cy - s / 10, x + s / 4, cy + s / 7, t, c);
        thick_line(x + s / 4 - s / 8, cy + s / 50, x + s / 4 + s / 8, cy + s / 50, t, c);
        fill_circle(x + s * 3 / 4 - s / 12, cy - s / 20, t, c);
        fill_circle(x + s * 3 / 4 + s / 12, cy + s / 12, t, c);
        break;
    case I_SAVE:
        thick_line(cx, y + 3, cx, y + s * 3 / 5, t, c);
        thick_line(cx - s / 4, y + s * 2 / 5, cx, y + s * 3 / 5 + 1, t, c);
        thick_line(cx + s / 4, y + s * 2 / 5, cx, y + s * 3 / 5 + 1, t, c);
        thick_line(x + 4, y + s - 6, x + s - 4, y + s - 6, t, c);
        break;
    case I_NET:
        for (int i = 0; i < 4; i++) {
            int bh = s / 5 + i * s / 6;
            fill_rrect(x + 4 + i * (s / 4 - 1), y + s - 4 - bh, s / 6, bh, 2, c);
        }
        break;
    case I_SCREEN:
        card(x + 2, y + s / 6, s - 4, s / 2 + 2, 4, t, c, bg);
        thick_line(cx, y + s * 2 / 3 + 2, cx, y + s - 6, t, c);
        thick_line(cx - s / 5, y + s - 5, cx + s / 5, y + s - 5, t, c);
        break;
    case I_SCAN:
        ring(x + s * 9 / 20, y + s * 9 / 20, s * 3 / 10, t, c, bg);
        thick_line(x + s * 13 / 20, y + s * 13 / 20, x + s - 3, y + s - 3, t + 1, c);
        thick_line(x + s * 3 / 10, y + s * 9 / 20, x + s * 5 / 12, y + s * 11 / 20, t, c);
        thick_line(x + s * 5 / 12, y + s * 11 / 20, x + s * 3 / 5, y + s * 1 / 3, t, c);
        break;
    case I_SETTINGS:
        for (int i = 0; i < 3; i++) {
            int ly = y + s / 4 + i * s / 4;
            thick_line(x + 3, ly, x + s - 3, ly, t, c);
            int kx = x + (i == 0 ? s * 2 / 3 : i == 1 ? s / 3 : s * 3 / 4);
            ring(kx, ly, s / 9 + 1, t, c, bg);
        }
        break;
    case I_BOX:
        card(x + 2, y + s * 3 / 8, s - 4, s / 2, 3, t, c, bg);
        card(x + s / 3, y + s / 5, s / 3, s / 5 + t, 2, t, c, bg);
        thick_line(x + 2, y + s * 9 / 16, x + s - 3, y + s * 9 / 16, t, c);
        fill_rect(cx - s / 10, y + s / 2, s / 5, s / 8, c);
        break;
    case I_SHIELD:
        thick_line(x + 3, y + s / 5, cx, y + 2, t, c);
        thick_line(cx, y + 2, x + s - 3, y + s / 5, t, c);
        thick_line(x + 3, y + s / 5, x + 3, y + s / 2, t, c);
        thick_line(x + s - 3, y + s / 5, x + s - 3, y + s / 2, t, c);
        thick_line(x + 3, y + s / 2, cx, y + s - 2, t, c);
        thick_line(x + s - 3, y + s / 2, cx, y + s - 2, t, c);
        thick_line(cx - s / 6, cy, cx - s / 30, cy + s / 7, t, c);
        thick_line(cx - s / 30, cy + s / 7, cx + s / 5, cy - s / 8, t, c);
        break;
    case I_CHECK:
        ring(cx, cy, s / 2 - 1, t, c, bg);
        thick_line(cx - s / 5, cy, cx - s / 20, cy + s / 6, t, c);
        thick_line(cx - s / 20, cy + s / 6, cx + s / 5, cy - s / 6, t, c);
        break;
    case I_WARN:
        thick_line(cx, y + 2, x + s - 2, y + s - 3, t, c);
        thick_line(x + s - 2, y + s - 3, x + 2, y + s - 3, t, c);
        thick_line(x + 2, y + s - 3, cx, y + 2, t, c);
        thick_line(cx, y + s * 2 / 5, cx, y + s * 2 / 3, t, c);
        fill_circle(cx, y + s * 4 / 5, t / 2 + 1, c);
        break;
    case I_DUMP: {
        int a = s / 6;
        card(x + a, y + 2, s - 2 * a, s * 9 / 20, 3, t, c, bg);
        for (int i = 1; i <= 3; i++) {
            int px = x + a + i * (s - 2 * a) / 4;
            thick_line(px, y + s * 9 / 20 + 2, px, y + s * 9 / 20 + s / 10, t, c);
        }
        thick_line(cx, y + s * 6 / 10, cx, y + s - 4, t, c);
        thick_line(cx - s / 6, y + s - 4 - s / 6, cx, y + s - 4, t, c);
        thick_line(cx + s / 6, y + s - 4 - s / 6, cx, y + s - 4, t, c);
        break;
    }
    case I_USB: {
        int bw = s * 9 / 20, cw = s * 3 / 10;
        card(cx - cw / 2, y + 2, cw, s / 4 + 2, 2, t, c, bg);
        fill_rect(cx - cw / 4 - 1, y + s / 10 + 1, 3, 3, c);
        fill_rect(cx + cw / 4 - 2, y + s / 10 + 1, 3, 3, c);
        card(cx - bw / 2, y + s / 4 + 2, bw, s * 3 / 4 - 4, 4, t, c, bg);
        fill_circle(cx, y + s * 3 / 4, t / 2 + 1, c);
        break;
    }
    case I_HDD:
        card(x + 2, y + s / 6, s - 4, s * 2 / 3, 4, t, c, bg);
        thick_line(x + 3, y + s / 2 + s / 12, x + s - 3, y + s / 2 + s / 12, t, c);
        fill_circle(x + s - s / 4, y + s * 2 / 3 + s / 24, t / 2 + 1, c);
        fill_circle(x + s / 4, y + s * 2 / 3 + s / 24, t / 2 + 1, c);
        break;
    case I_DISC:
        ring(cx, cy, s / 2 - 1, t, c, bg);
        ring(cx, cy, s / 6, t, c, bg);
        thick_line(cx + s / 4, cy - s / 3, cx + s / 3, cy - s / 5, t, c);
        break;
    case I_SPEAKER:
        card(x + 3, y + s * 3 / 8, s / 5, s / 4, 2, t, c, bg);
        thick_line(x + 3 + s / 5, y + s * 3 / 8, x + s * 3 / 5, y + s / 5, t, c);
        thick_line(x + 3 + s / 5, y + s * 5 / 8, x + s * 3 / 5, y + s * 4 / 5, t, c);
        thick_line(x + s * 3 / 5, y + s / 5, x + s * 3 / 5, y + s * 4 / 5, t, c);
        thick_line(x + s * 3 / 4, cy - s / 8, x + s * 3 / 4 + s / 12, cy, t, c);
        thick_line(x + s * 3 / 4 + s / 12, cy, x + s * 3 / 4, cy + s / 8, t, c);
        break;
    case I_THERMO:
        card(cx - s / 10, y + 2, s / 5, s * 3 / 5, s / 10, t, c, bg);
        ring(cx, y + s - s / 5 - 1, s / 5, t, c, bg);
        fill_circle(cx, y + s - s / 5 - 1, s / 9, c);
        thick_line(cx, y + s / 3, cx, y + s * 4 / 5, t, c);
        break;
    case I_PULSE:
        thick_line(x + 2, cy, x + s / 4, cy, t, c);
        thick_line(x + s / 4, cy, x + s * 3 / 8, y + s / 6, t, c);
        thick_line(x + s * 3 / 8, y + s / 6, x + s * 5 / 8, y + s * 5 / 6, t, c);
        thick_line(x + s * 5 / 8, y + s * 5 / 6, x + s * 3 / 4, cy, t, c);
        thick_line(x + s * 3 / 4, cy, x + s - 2, cy, t, c);
        break;
    case I_DOC:
        card(x + s / 5, y + 2, s * 3 / 5, s - 4, 4, t, c, bg);
        for (int i = 0; i < 3; i++)
            thick_line(x + s * 3 / 10, y + s * 3 / 10 + i * s / 5, x + s * 7 / 10, y + s * 3 / 10 + i * s / 5, t, c);
        break;
    case I_CROSS:
        ring(cx, cy, s / 2 - 1, t, c, bg);
        thick_line(cx - s / 6, cy - s / 6, cx + s / 6, cy + s / 6, t, c);
        thick_line(cx + s / 6, cy - s / 6, cx - s / 6, cy + s / 6, t, c);
        break;
    }
}