#include "tools2.h"
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <ppu-types.h>
#include <audio/audio.h>

AudioState g_aud = { 0, 0, 0, -1, AC_BOTH, AT_440, 20, 0 };

#define RATE 48000
static int inited = 0;
static u32 port = 0;
static audioPortConfig cfg;
static float *base;
static volatile u64 *rd_ptr;
static u32 nblocks, ri0, last_ri;
static uint64_t rd_abs, wr_abs;       
static double phase = 0;

int aud_open(void)
{
    if (g_aud.open) return 0;
    int r = audioInit();
    if (r != 0) { g_aud.err = r; return r; }
    inited = 1;
    audioPortParam p8;
    memset(&p8, 0, sizeof(p8));
    p8.numChannels = AUDIO_PORT_8CH;
    p8.numBlocks = AUDIO_BLOCK_8;
    u32 pn8 = 0;
    if (audioPortOpen(&p8, &pn8) == 0) { g_aud.ch8 = 1; audioPortClose(pn8); }
    else g_aud.ch8 = 0;

    audioPortParam prm;
    memset(&prm, 0, sizeof(prm));
    prm.numChannels = AUDIO_PORT_2CH;
    prm.numBlocks = AUDIO_BLOCK_32;
    prm.attrib = 0;
    prm.level = 1.0f;
    r = audioPortOpen(&prm, &port);
    if (r != 0) { g_aud.err = r; audioQuit(); inited = 0; return r; }
    if (audioGetPortConfig(port, &cfg) != 0) { g_aud.err = -1; audioPortClose(port); audioQuit(); inited = 0; return -1; }
    base = (float *)(uintptr_t)cfg.audioDataStart;
    rd_ptr = (volatile u64 *)(uintptr_t)cfg.readIndex;
    nblocks = (u32)cfg.numBlocks;
    memset(base, 0, (size_t)nblocks * AUDIO_BLOCK_SAMPLES * 2 * sizeof(float));
    ri0 = last_ri = (u32)(*rd_ptr % nblocks);
    rd_abs = 0; wr_abs = 4;
    phase = 0;
    g_aud.err = 0;
    g_aud.open = 1;
    return 0;
}

void aud_close(void)
{
    if (g_aud.open) {
        if (g_aud.playing) audioPortStop(port);
        audioPortClose(port);
    }
    if (inited) audioQuit();
    inited = 0;
    g_aud.open = g_aud.playing = 0;
}

void aud_play(int on)
{
    if (!g_aud.open) return;
    if (on && !g_aud.playing) {
        if (audioPortStart(port) == 0) {
            g_aud.playing = 1;
            last_ri = (u32)(*rd_ptr % nblocks);
        }
    } else if (!on && g_aud.playing) {
        audioPortStop(port);
        g_aud.playing = 0;
        memset(base, 0, (size_t)nblocks * AUDIO_BLOCK_SAMPLES * 2 * sizeof(float));
        rd_abs = 0; wr_abs = 4;
        ri0 = last_ri = (u32)(*rd_ptr % nblocks);
    }
}

static float tone_freq(uint64_t s)
{
    switch (g_aud.tone) {
    case AT_1000: return 1000.f;
    case AT_220: return 220.f;
    case AT_SWEEP: {                   
        double t = fmod((double)s / RATE, 6.0) / 6.0;
        return (float)(100.0 * pow(40.0, t));
    }
    default: return 440.f;
    }
}

static void fill_block(uint64_t k)
{
    float *b = base + (size_t)((ri0 + k) % nblocks) * AUDIO_BLOCK_SAMPLES * 2;
    float amp = g_aud.vol / 100.0f;
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        uint64_t s = k * AUDIO_BLOCK_SAMPLES + i;
        phase += 2.0 * M_PI * tone_freq(s) / RATE;
        if (phase > 2.0 * M_PI * 1000) phase = fmod(phase, 2.0 * M_PI);
        float v = (float)sin(phase) * amp;
        if (g_aud.pulse) {
            uint64_t ph = s % RATE;                          
            float env = ph < 9600 ? 1.f : 0.f;
            if (ph < 240) env = ph / 240.f;                  
            else if (ph >= 9360 && ph < 9600) env = (9600 - ph) / 240.f;
            v *= env;
        }
        float l = v, r = v;
        int side = g_aud.chan;
        if (side == AC_ALT) side = ((s / RATE) & 1) ? AC_RIGHT : AC_LEFT;
        if (side == AC_LEFT) r = 0;
        else if (side == AC_RIGHT) l = 0;
        b[i * 2] = l;
        b[i * 2 + 1] = r;
    }
}

void aud_tick(void)
{
    if (!g_aud.open || !g_aud.playing) return;
    u32 ri = (u32)(*rd_ptr % nblocks);
    rd_abs += (ri + nblocks - last_ri) % nblocks;
    last_ri = ri;
    while (wr_abs < rd_abs + nblocks / 2) fill_block(wr_abs++);
}

int aud_pulse_on(void)
{
    if (!g_aud.playing || !g_aud.pulse) return 0;
    uint64_t s = rd_abs * AUDIO_BLOCK_SAMPLES;
    return (s % RATE) < 9600;
}

int aud_played_s(void) { return (int)(rd_abs * AUDIO_BLOCK_SAMPLES / RATE); }