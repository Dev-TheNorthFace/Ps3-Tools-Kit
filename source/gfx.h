#ifndef GFX_H
#define GFX_H

#include <stdint.h>

#define SCREEN_W 1280
#define SCREEN_H 720

typedef struct { uint8_t r, g, b, a; } Col;

#define HEX(h)      ((Col){ ((h) >> 16) & 0xFF, ((h) >> 8) & 0xFF, (h) & 0xFF, 255 })
#define HEXA(h, a)  ((Col){ ((h) >> 16) & 0xFF, ((h) >> 8) & 0xFF, (h) & 0xFF, (a) })

#define C_BG        HEX(0x0A0E17)
#define C_PANEL     HEX(0x121826)
#define C_PANEL_SEL HEX(0x13213A)
#define C_LINE      HEX(0x1F2A3D)
#define C_LINE2     HEX(0x2A3650)
#define C_LINE3     HEX(0x3A4865)
#define C_TEXT      HEX(0xE8EEF7)
#define C_TEXT2     HEX(0xB7C2D4)
#define C_MUTED     HEX(0x8A97AD)
#define C_ACCENT    HEX(0x3D9BFF)
#define C_ACCENT2   HEX(0x7FB2FF)
#define C_WARN      HEX(0xFFB547)
#define C_ERR       HEX(0xFF8A98)
#define C_OK        HEX(0x5FD6A8)
#define C_PINK      HEX(0xF49BD6)
#define C_ERR_BG    HEX(0x1E1620)

enum { F_SMALL, F_BODY, F_BODYB, F_H3, F_H2, F_H1, F_BIG, F_HUGE, F_COUNT };
enum { AL_LEFT, AL_CENTER, AL_RIGHT };
enum { G_CROSS, G_CIRCLE, G_TRIANGLE, G_SQUARE };

int  gfx_init(const char *usrdir);
void gfx_quit(void);
void gfx_begin(void);
void gfx_end(void);
int  gfx_has_font(void);

void fill_rect(int x, int y, int w, int h, Col c);
void fill_rrect(int x, int y, int w, int h, int r, Col c);
void card(int x, int y, int w, int h, int r, int border, Col bc, Col fill);
void fill_circle(int cx, int cy, int r, Col c);
void ring(int cx, int cy, int r, int t, Col c, Col inside);
void fill_poly(const float *xy, int n, Col c);               
void fill_vgrad(int x, int y, int w, int h, Col a, Col b);   
void fill_rgrad(int cx, int cy, int r, int hx, int hy, Col a, Col b);   
void thick_line(int x1, int y1, int x2, int y2, int t, Col c);
void bar(int x, int y, int w, int h, float frac, Col c);
void waves(float phase, int base_y);

int  text(int font, const char *s, int x, int y, Col c, int align);
int  text_w(int font, const char *s);
int  text_h(int font);
int  text_wrap(int font, const char *s, int x, int y, int maxw, int lineh, Col c);
void text_fit_left(int font, const char *s, int x, int y, int maxw, Col c);
int  text_wrap_lines(int font, const char *s, int maxw);   
void clip_push(int x, int y, int w, int h);
void clip_pop(void);

void glyph(int g, int cx, int cy);
int  hint_w(const char *label);
void hint(int g, const char *label, int x, int y);
void hints_right(int y, int right, const int *gl, const char **labels, int n);

enum { I_CHIP, I_FOLDER, I_PAD, I_SAVE, I_NET, I_SCREEN, I_SCAN, I_SETTINGS, I_BOX, I_SHIELD, I_CHECK, I_WARN, I_CROSS, I_DUMP, I_USB, I_HDD, I_DISC, I_SPEAKER, I_THERMO, I_PULSE, I_DOC };
void icon(int id, int x, int y, int s, Col c, Col bg);

#endif