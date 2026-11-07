#include "prefs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ppu-types.h>
#include <sys/systime.h>
#include <sysutil/sysutil.h>

int g_lang = LANG_FR;
char g_dump_dest[16] = "";
LogEntry g_log[LOG_MAX];
int g_log_n = 0;
static char base[256] = "";
const char *prefs_dir(void) { return base[0] ? base : "/dev_hdd0/tmp"; }

static void path_of(const char *name, char *out, int n)
{
    snprintf(out, n, "%s/%s", base[0] ? base : "/dev_hdd0/tmp", name);
}

static void journal_save(void)
{
    char p[300];
    path_of("journal.txt", p, sizeof(p));
    FILE *f = fopen(p, "w");
    if (!f) return;
    for (int i = 0; i < g_log_n; i++)
        fprintf(f, "%llu|%s|%s\n", g_log[i].when, g_log[i].key, g_log[i].arg);
    fclose(f);
}

static void journal_load(void)
{
    char p[300], line[256];
    path_of("journal.txt", p, sizeof(p));
    FILE *f = fopen(p, "r");
    g_log_n = 0;
    if (!f) return;
    while (fgets(line, sizeof(line), f) && g_log_n < LOG_MAX) {
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
        char *a = strchr(line, '|');
        if (!a) continue;
        char *b = strchr(a + 1, '|');
        if (!b) continue;
        *a = 0; *b = 0;
        LogEntry *e = &g_log[g_log_n++];
        e->when = strtoull(line, NULL, 10);
        snprintf(e->key, sizeof(e->key), "%s", a + 1);
        snprintf(e->arg, sizeof(e->arg), "%s", b + 1);
    }
    fclose(f);
}

void prefs_load(const char *usrdir)
{
    snprintf(base, sizeof(base), "%s", usrdir ? usrdir : "");
    char p[300];
    path_of("settings.txt", p, sizeof(p));
    FILE *f = fopen(p, "r");
    int found = 0;
    if (f) {
        char line[64];
        while (fgets(line, sizeof(line), f)) {
            int v;
            char d[16];
            if (sscanf(line, "lang=%d", &v) == 1 && v >= 0 && v < LANG_COUNT) { g_lang = v; found = 1; }
            else if (sscanf(line, "dumpdest=%15s", d) == 1 && strncmp(d, "/dev_", 5) == 0)
                snprintf(g_dump_dest, sizeof(g_dump_dest), "%s", d);
        }
        fclose(f);
    }
    if (!found) {
        s32 sys = 0;
        if (sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_LANG, &sys) == 0)
            g_lang = sys == 2 ? LANG_FR : sys == 8 ? LANG_RU : LANG_EN;
    }
    journal_load();
}

void prefs_save(void)
{
    char p[300];
    path_of("settings.txt", p, sizeof(p));
    FILE *f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "lang=%d\n", g_lang);
    if (g_dump_dest[0]) fprintf(f, "dumpdest=%s\n", g_dump_dest);
    fclose(f);
}

void log_add(const char *key, const char *arg)
{
    if (g_log_n >= LOG_MAX) {
        memmove(&g_log[0], &g_log[1], sizeof(LogEntry) * (LOG_MAX - 1));
        g_log_n = LOG_MAX - 1;
    }
    LogEntry *e = &g_log[g_log_n++];
    u64 sec = 0, nsec = 0;
    sysGetCurrentTime(&sec, &nsec);
    e->when = sec;
    snprintf(e->key, sizeof(e->key), "%s", key);
    snprintf(e->arg, sizeof(e->arg), "%s", arg ? arg : "");
    for (char *c = e->arg; *c; c++) if (*c == '|' || *c == '\n') *c = ' ';
    journal_save();
}

void log_clear(void)
{
    g_log_n = 0;
    journal_save();
}

void log_format(const LogEntry *e, char *out, int n)
{
    char buf[96], *a[3] = { "", "", "" };
    snprintf(buf, sizeof(buf), "%s", e->arg);
    char *p = buf;
    for (int i = 0; i < 3 && p; i++) {
        a[i] = p;
        p = strchr(p, ';');
        if (p) *p++ = 0;
    }
    snprintf(out, n, T(e->key), a[0], a[1], a[2]);
}

void log_time(const LogEntry *e, char *out, int n)
{
    s32 tz = 0, summer = 0;
    sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_TIMEZONE, &tz);
    sysUtilGetSystemParamInt(SYSUTIL_SYSTEMPARAM_ID_SUMMERTIME, &summer);
    long long t = (long long)e->when + tz * 60LL + (summer ? 3600 : 0);
    long long days = t / 86400, rem = t % 86400;
    if (rem < 0) { rem += 86400; days--; }
    long long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp < 10 ? mp + 3 : mp - 9;
    snprintf(out, n, "%02u/%02u %02d:%02d", d, m, (int)(rem / 3600), (int)(rem % 3600 / 60));
}