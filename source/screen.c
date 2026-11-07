#include "screen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <ppu-types.h>
#include <sysutil/video.h>
#include <rsx/gcm_sys.h>
#include <rsx/rsx.h>

#define CB_SIZE    0x100000
#define HOST_SIZE  (32 * 1024 * 1024)
#define LABEL_IDX  255

static gcmContextData *ctx = NULL;
static void *host_addr = NULL;
static u32 *buf[2];
static u32 buf_off[2];
static int W = 1280, H = 720, cur = 0;

int screen_width(void) { return W; }
int screen_height(void) { return H; }

static void wait_flip(void)
{
    while (gcmGetFlipStatus() != 0)
        usleep(200);
    gcmResetFlipStatus();
}

static void wait_rsx_idle(void)
{
    u32 label = 1;
    rsxSetWriteBackendLabel(ctx, LABEL_IDX, label);
    rsxSetWaitLabel(ctx, LABEL_IDX, label);
    label++;
    rsxSetWriteBackendLabel(ctx, LABEL_IDX, label);
    rsxFlushBuffer(ctx);
    while (*(vu32 *)gcmGetLabelAddress(LABEL_IDX) != label)
        usleep(30);
}

static void do_flip(int id)
{
    if (gcmSetFlip(ctx, (u8)id) == 0) {
        rsxFlushBuffer(ctx);
        gcmSetWaitFlip(ctx);
    }
}

int screen_init(void)
{
    videoState state;
    videoResolution res;
    videoConfiguration vc;

    host_addr = memalign(1024 * 1024, HOST_SIZE);
    if (!host_addr) { printf("[PS3TK] memalign host_addr a échoué\n"); return -1; }
    rsxInit(&ctx, CB_SIZE, HOST_SIZE, host_addr);
    if (!ctx) { printf("[PS3TK] rsxInit a échoué\n"); return -1; }

    if (videoGetState(0, 0, &state) != 0 || state.state != 0) { printf("[PS3TK] écran indisponible\n"); return -1; }
    if (videoGetResolution(state.displayMode.resolution, &res) != 0) { printf("[PS3TK] résolution inconnue\n"); return -1; }

    memset(&vc, 0, sizeof(vc));
    vc.resolution = state.displayMode.resolution;
    vc.format = VIDEO_BUFFER_FORMAT_XRGB;
    vc.pitch = res.width * sizeof(u32);
    vc.aspect = state.displayMode.aspect;
    wait_rsx_idle();
    if (videoConfigure(0, &vc, NULL, 0) != 0) { printf("[PS3TK] videoConfigure a échoué\n"); return -1; }
    gcmSetFlipMode(GCM_FLIP_VSYNC);

    W = res.width;
    H = res.height;
    int pitch = W * 4;
    for (int i = 0; i < 2; i++) {
        buf[i] = (u32 *)rsxMemalign(64, pitch * H);
        if (!buf[i] || rsxAddressToOffset(buf[i], &buf_off[i]) != 0 ||
            gcmSetDisplayBuffer(i, buf_off[i], pitch, W, H) != 0) {
            printf("[PS3TK] tampon d'écran %d impossible\n", i);
            return -1;
        }
        memset(buf[i], 0, pitch * H);
    }
    gcmResetFlipStatus();
    do_flip(1);   
    cur = 0;
    printf("[PS3TK] écran %d x %d prêt\n", W, H);
    return 0;
}

void screen_present(const uint32_t *pixels, int stride)
{
    if (!ctx) return;
    wait_flip();  
    const uint8_t *srcp = (const uint8_t *)pixels;
    uint8_t *dst = (uint8_t *)buf[cur];
    int row = W * 4;
    if (stride == row) memcpy(dst, srcp, (size_t)row * H);
    else for (int y = 0; y < H; y++) memcpy(dst + (size_t)y * row, srcp + (size_t)y * stride, row);
    do_flip(cur);
    cur ^= 1;
}

void screen_end(void)
{
    if (!ctx) return;
    gcmSetWaitFlip(ctx);
    rsxFinish(ctx, 1);
    for (int i = 0; i < 2; i++)
        if (buf[i]) rsxFree(buf[i]);
    free(host_addr);
    ctx = NULL;
}