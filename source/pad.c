#include "pad.h"
#include <string.h>
#include <io/pad.h>

PadState pad;

const char *button_names[B_COUNT] = {
    "Haut", "Bas", "Gauche", "Droite", "Croix", "Rond", "Carré", "Triangle",
    "Start", "Select", "L1", "R1", "L2", "R2", "L3", "R3"
};

static padData last;
static int have_last = 0;
static int hold_frames[B_COUNT];
static int modes_set = 0;
PadRaw g_pads[PAD_PORTS];
static int port_modes[PAD_PORTS];

static void raw_store(PadRaw *r, const padData *d)
{
    r->buttons = (d->BTN_UP << B_UP) | (d->BTN_DOWN << B_DOWN) | (d->BTN_LEFT << B_LEFT) | (d->BTN_RIGHT << B_RIGHT) |
                 (d->BTN_CROSS << B_CROSS) | (d->BTN_CIRCLE << B_CIRCLE) | (d->BTN_SQUARE << B_SQUARE) | (d->BTN_TRIANGLE << B_TRIANGLE) |
                 (d->BTN_START << B_START) | (d->BTN_SELECT << B_SELECT) | (d->BTN_L1 << B_L1) | (d->BTN_R1 << B_R1) |
                 (d->BTN_L2 << B_L2) | (d->BTN_R2 << B_R2) | (d->BTN_L3 << B_L3) | (d->BTN_R3 << B_R3);
    r->lx = d->ANA_L_H; r->ly = d->ANA_L_V; r->rx = d->ANA_R_H; r->ry = d->ANA_R_V;
    r->pre[0] = d->PRE_RIGHT; r->pre[1] = d->PRE_LEFT; r->pre[2] = d->PRE_UP; r->pre[3] = d->PRE_DOWN;
    r->pre[4] = d->PRE_TRIANGLE; r->pre[5] = d->PRE_CIRCLE; r->pre[6] = d->PRE_CROSS; r->pre[7] = d->PRE_SQUARE;
    r->pre[8] = d->PRE_L1; r->pre[9] = d->PRE_R1; r->pre[10] = d->PRE_L2; r->pre[11] = d->PRE_R2;
    r->sx = d->SENSOR_X; r->sy = d->SENSOR_Y; r->sz = d->SENSOR_Z; r->sg = d->SENSOR_G;
    r->len = d->len;
    r->frames++;
}

int pad_rumble(int port, int small_on, int large_speed)
{
    if (port < 0 || port >= PAD_PORTS) return -1;
    padActParam a;
    memset(&a, 0, sizeof(a));
    a.small_motor = small_on ? 1 : 0;
    a.large_motor = (u8)(large_speed < 0 ? 0 : large_speed > 255 ? 255 : large_speed);
    return (int)ioPadSetActDirect(port, &a);
}

static void poll_others(const padInfo *info, int have_info)
{
    for (int p = 1; p < PAD_PORTS; p++) {
        PadRaw *r = &g_pads[p];
        if (!have_info || !info->status[p]) { memset(r, 0, sizeof(*r)); port_modes[p] = 0; continue; }
        r->connected = 1;
        if (!port_modes[p]) {
            r->has_pressure = ioPadSetPressMode(p, PAD_PRESS_MODE_ON) == 0;
            r->has_sensor = ioPadSetSensorMode(p, PAD_SENSOR_MODE_ON) == 0;
            padCapabilityInfo cap;
            memset(&cap, 0, sizeof(cap));
            r->has_vibrate = ioPadGetCapabilityInfo(p, &cap) == 0 && cap.has_vibrate;
            port_modes[p] = 1;
        }
        padData d;
        memset(&d, 0, sizeof(d));
        if (ioPadGetData(p, &d) == 0 && d.len > 0) raw_store(r, &d);
    }
}

void pad_init(void)
{
    memset(&pad, 0, sizeof(pad));
    memset(&last, 0, sizeof(last));
    pad.last_button = -1;
    ioPadInit(7);
}

void pad_end(void)
{
    ioPadEnd();
}

void pad_update(void)
{
    padInfo info;
    int prev[B_COUNT];
    memcpy(prev, pad.held, sizeof(prev));

    pad.connected = 0;
    int have_info = ioPadGetInfo(&info) == 0;
    poll_others(&info, have_info);
    if (have_info && info.status[0]) {
        pad.connected = 1;
        if (!modes_set) {
            pad.has_pressure = ioPadSetPressMode(0, PAD_PRESS_MODE_ON) == 0;
            pad.has_sensor = ioPadSetSensorMode(0, PAD_SENSOR_MODE_ON) == 0;
            { padCapabilityInfo cap; memset(&cap, 0, sizeof(cap)); g_pads[0].has_vibrate = ioPadGetCapabilityInfo(0, &cap) == 0 && cap.has_vibrate; }
            modes_set = 1;
        }
        padData d;
        memset(&d, 0, sizeof(d));
        if (ioPadGetData(0, &d) == 0 && d.len > 0) {
            last = d;
            have_last = 1;
            raw_store(&g_pads[0], &d);
        }
        g_pads[0].connected = 1;
        g_pads[0].has_pressure = pad.has_pressure;
        g_pads[0].has_sensor = pad.has_sensor;
    } else {
        modes_set = 0;
        have_last = 0;
        memset(&g_pads[0], 0, sizeof(g_pads[0]));
    }

    if (!have_last) {
        memset(pad.held, 0, sizeof(pad.held));
        pad.lx = pad.ly = pad.rx = pad.ry = 128;
        pad.pl2 = pad.pr2 = 0;
        pad.sx = pad.sy = pad.sz = 512;
    } else {
        padData *d = &last;
        pad.held[B_UP] = d->BTN_UP;
        pad.held[B_DOWN] = d->BTN_DOWN;
        pad.held[B_LEFT] = d->BTN_LEFT;
        pad.held[B_RIGHT] = d->BTN_RIGHT;
        pad.held[B_CROSS] = d->BTN_CROSS;
        pad.held[B_CIRCLE] = d->BTN_CIRCLE;
        pad.held[B_SQUARE] = d->BTN_SQUARE;
        pad.held[B_TRIANGLE] = d->BTN_TRIANGLE;
        pad.held[B_START] = d->BTN_START;
        pad.held[B_SELECT] = d->BTN_SELECT;
        pad.held[B_L1] = d->BTN_L1;
        pad.held[B_R1] = d->BTN_R1;
        pad.held[B_L2] = d->BTN_L2;
        pad.held[B_R2] = d->BTN_R2;
        pad.held[B_L3] = d->BTN_L3;
        pad.held[B_R3] = d->BTN_R3;
        pad.lx = d->ANA_L_H; pad.ly = d->ANA_L_V;
        pad.rx = d->ANA_R_H; pad.ry = d->ANA_R_V;
        pad.pl2 = d->PRE_L2; pad.pr2 = d->PRE_R2;
        if (!pad.has_pressure) {
            pad.pl2 = d->BTN_L2 ? 255 : 0;
            pad.pr2 = d->BTN_R2 ? 255 : 0;
        }
        pad.sx = d->SENSOR_X; pad.sy = d->SENSOR_Y; pad.sz = d->SENSOR_Z;
    }

    int nav[B_COUNT];
    memcpy(nav, pad.held, sizeof(nav));
    if (have_last) {
        if (pad.ly < 50) nav[B_UP] = 1;
        if (pad.ly > 205) nav[B_DOWN] = 1;
        if (pad.lx < 50) nav[B_LEFT] = 1;
        if (pad.lx > 205) nav[B_RIGHT] = 1;
    }

    for (int i = 0; i < B_COUNT; i++) {
        pad.pressed[i] = pad.held[i] && !prev[i];
        if (pad.pressed[i]) pad.last_button = i;
        if (nav[i]) hold_frames[i]++;
        else hold_frames[i] = 0;
        int h = hold_frames[i];
        pad.repeat[i] = (h == 1) || (h > 20 && (h - 20) % 5 == 0);
    }
}