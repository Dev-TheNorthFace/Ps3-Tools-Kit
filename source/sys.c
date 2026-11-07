#include "sys.h"
#include "prefs.h"
#include <stdio.h>
#include <string.h>
#include <ppu-lv2.h>
#include <lv2/sysfs.h>
#include <lv2/systime.h>
#include <sys/systime.h>

#define FS_IFMT  0170000
#define FS_IFDIR 0040000

int path_exists(const char *p)
{
    sysFSStat st;
    return sysFsStat(p, &st) == 0;
}

int is_dir(const char *p)
{
    sysFSStat st;
    return sysFsStat(p, &st) == 0 && (st.st_mode & FS_IFMT) == FS_IFDIR;
}

long long file_size(const char *p)
{
    sysFSStat st;
    if (sysFsStat(p, &st) != 0) return -1;
    return (long long)st.st_size;
}

int list_dir(const char *path, dir_cb cb, void *user)
{
    s32 fd;
    if (sysFsOpendir(path, &fd) != 0) return -1;
    sysFSDirent ent;
    u64 nread = 0;
    while (sysFsReaddir(fd, &ent, &nread) == 0 && nread > 0) {
        if (!strcmp(ent.d_name, ".") || !strcmp(ent.d_name, "..")) continue;
        if (cb(ent.d_name, ent.d_type == 1, user)) break;
    }
    sysFsClosedir(fd);
    return 0;
}

void sys_firmware(char *out, int n)
{
    snprintf(out, n, "Inconnu");
    FILE *f = fopen("/dev_flash/vsh/etc/version.txt", "rb");
    if (!f) return;
    char buf[256];
    size_t len = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[len] = 0;
    char *p = strstr(buf, "release:");
    if (!p) return;
    p += 8;
    int major = 0, minor = 0;
    if (sscanf(p, "%d.%d", &major, &minor) == 2) {
        while (minor >= 100) minor /= 10;  
        snprintf(out, n, "%d.%02d", major, minor);
    }
}

int sys_detect_mode(void)
{
    if (is_dir("/dev_hdd0/hen") || path_exists("/dev_flash/hen/PS3HEN.BIN") ||
        path_exists("/dev_hdd0/hen/PS3HEN.BIN"))
        return MODE_HEN;
    if (path_exists("/dev_flash/vsh/module/webftp_server.sprx") ||
        path_exists("/dev_flash/rebug") ||
        path_exists("/dev_hdd0/boot_plugins.txt") ||
        path_exists("/dev_hdd0/boot_plugins_nocobra.txt"))
        return MODE_CFW;
    return MODE_UNKNOWN;
}

const char *mode_name(int mode)
{
    switch (mode) {
    case MODE_CFW: return "Custom Firmware";
    case MODE_HEN: return "PS3HEN";
    default: return "Non détecté";
    }
}

static s32 lv2_get_temperature(u32 which, u32 *temp)
{
    lv2syscall2(383, (u64)which, (u64)(uintptr_t)temp);
    return_to_user_prog(s32);
}

int sys_temperature(int which, int *celsius)
{
    u32 t = 0;
    s32 r = lv2_get_temperature((u32)which, &t);
    if (r != 0) return -1;
    int c = (int)(t >> 24);
    if (c <= 0 || c > 120) return -1;
    *celsius = c;
    return 0;
}

int sys_free_space(const char *path, uint64_t *bytes)
{
    u32 bs = 0;
    u64 blocks = 0;
    if (sysFsGetFreeSize(path, &bs, &blocks) != 0) return -1;
    *bytes = (uint64_t)bs * blocks;
    return 0;
}

void fmt_size(uint64_t b, char *out, int n)
{
    const char *u;
    double v;
    if (b >= 1024ULL * 1024 * 1024) { v = b / (1024.0 * 1024 * 1024); u = T("Go"); }
    else if (b >= 1024ULL * 1024) { v = b / (1024.0 * 1024); u = T("Mo"); }
    else if (b >= 1024) { v = b / 1024.0; u = T("Ko"); }
    else { snprintf(out, n, T("%u octets"), (unsigned)b); return; }
    if (v >= 100) snprintf(out, n, "%d %s", (int)(v + 0.5), u);
    else {
        int whole = (int)v, dec = (int)((v - whole) * 10 + 0.5);
        if (dec == 10) { whole++; dec = 0; }
        snprintf(out, n, "%d%c%d %s", whole, g_lang == LANG_EN ? '.' : ',', dec, u);
    }
}

unsigned ticks_ms(void)
{
    static s64 start = 0;
    s64 now = sysGetSystemTime();   
    if (!start) start = now;
    return (unsigned)((now - start) / 1000);
}

int file_remove(const char *p)
{
    return sysFsUnlink(p) == 0 ? 0 : -1;
}

int make_dir(const char *p)
{
    sysFsMkdir(p, 0777);
    return is_dir(p) ? 0 : -1;
}

unsigned long long wall_time(void)
{
    u64 sec = 0, nsec = 0;
    sysGetCurrentTime(&sec, &nsec);
    return sec;
}

static s32 sc_fs_rename(const char *from, const char *to)
{
    lv2syscall2(812, (u64)(uintptr_t)from, (u64)(uintptr_t)to);
    return_to_user_prog(s32);
}

int file_rename(const char *from, const char *to)
{
    sc_fs_rename(from, to);
    return (path_exists(to) && !path_exists(from)) ? 0 : -1;
}

int dir_remove(const char *p)
{
    return sysFsRmdir(p) == 0 ? 0 : -1;
}