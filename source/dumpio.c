#include "dumpio.h"
#include "sys.h"
#include <string.h>

static uint8_t rbuf[64 * 1024];

static void stamp(char *out, int n)
{
    long long t = (long long)wall_time();
    long long days = t / 86400, rem = t % 86400;
    long long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long y = (long long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) y++;
    snprintf(out, n, "%04lld-%02u-%02u_%02d%02d%02d", y, m, d, (int)(rem / 3600), (int)(rem % 3600 / 60), (int)(rem % 60));
}

int dio_new_dir(const char *base, const char *tag, char *out, int n)
{
    char st[32], p[300];
    stamp(st, sizeof(st));
    if (make_dir(base) != 0) return -1;
    for (int i = 1; i < 100; i++) {
        if (i == 1) snprintf(p, sizeof(p), "%s/%s_%s", base, st, tag);
        else snprintf(p, sizeof(p), "%s/%s_%s_%d", base, st, tag, i);
        if (path_exists(p)) continue;         
        if (make_dir(p) != 0) return -1;
        snprintf(out, n, "%s", p);
        return 0;
    }
    return -1;
}

int dio_open(OutFile *o, const char *dir, const char *name)
{
    memset(o, 0, sizeof(*o));
    snprintf(o->dir, sizeof(o->dir), "%s", dir);
    snprintf(o->name, sizeof(o->name), "%s", name);
    snprintf(o->fin, sizeof(o->fin), "%s/%s", dir, name);
    snprintf(o->part, sizeof(o->part), "%s/%s.partial", dir, name);
    if (path_exists(o->fin)) return DIO_E_EXISTS;
    if (path_exists(o->part)) file_remove(o->part);
    o->f = fopen(o->part, "wb");
    if (!o->f) return DIO_E_OPEN;
    sha256_init(&o->sha);
    return DIO_OK;
}

int dio_write(OutFile *o, const void *b, size_t n)
{
    if (!o->f) return DIO_E_WRITE;
    if (fwrite(b, 1, n, o->f) != n) return DIO_E_WRITE;
    sha256_update(&o->sha, b, n);
    o->size += n;
    o->hashed = 1;
    return DIO_OK;
}

int dio_close(OutFile *o)
{
    if (!o->f) return DIO_E_CLOSE;
    int bad = fflush(o->f) != 0;
    if (!o->hashed) { long p = ftell(o->f); if (p < 0) bad = 1; else o->size = (unsigned long long)p; }
    if (fclose(o->f) != 0) bad = 1;
    o->f = NULL;
    if (bad) return DIO_E_CLOSE;
    if (file_size(o->part) != (long long)o->size) return DIO_E_SIZE;
    return DIO_OK;
}

int dio_digest(const OutFile *o, uint8_t out[32])
{
    if (!o->hashed) return -1;
    sha256_digest(&o->sha, out);
    return 0;
}

int dio_reread(OutFile *o, const void *expect, size_t len, char hex[65])
{
    FILE *f = fopen(o->part, "rb");
    if (!f) return DIO_E_REREAD;
    Sha256 s;
    sha256_init(&s);
    unsigned long long tot = 0;
    size_t r;
    int mism = 0;
    while ((r = fread(rbuf, 1, sizeof(rbuf), f)) > 0) {
        if (expect && (tot + r > len || memcmp(rbuf, (const uint8_t *)expect + tot, r) != 0)) mism = 1;
        sha256_update(&s, rbuf, r);
        tot += r;
    }
    int err = ferror(f);
    fclose(f);
    if (err) return DIO_E_REREAD;
    if (mism || tot != o->size || (expect && tot != len)) return DIO_E_MISMATCH;
    uint8_t d[32];
    sha256_final(&s, d);
    if (o->hashed) {
        uint8_t w[32];
        sha256_digest(&o->sha, w);
        if (memcmp(w, d, 32) != 0) return DIO_E_MISMATCH;
    }
    sha256_hex(d, hex);
    return DIO_OK;
}

int dio_commit(OutFile *o, const char hex[65])
{
    if (path_exists(o->fin)) return DIO_E_EXISTS;
    if (file_rename(o->part, o->fin) != 0) return DIO_E_RENAME;
    char p[300];
    snprintf(p, sizeof(p), "%s/SHA256SUMS.txt", o->dir);
    FILE *f = fopen(p, "ab");
    if (!f) return DIO_E_SUMS;
    int ok = fprintf(f, "%s  %s\n", hex, o->name) > 0;
    if (fflush(f) != 0) ok = 0;
    if (fclose(f) != 0) ok = 0;
    return ok ? DIO_OK : DIO_E_SUMS;
}

int dio_finish(OutFile *o, const void *expect, size_t len, char hex[65])
{
    int r = dio_close(o);
    if (r != DIO_OK) return r;
    r = dio_reread(o, expect, len, hex);
    if (r != DIO_OK) return r;
    return dio_commit(o, hex);
}

void dio_abort(OutFile *o)
{
    if (o->f) { fclose(o->f); o->f = NULL; }
    if (o->part[0]) file_remove(o->part);
}

int dio_save_buf(const char *dir, const char *name, const void *buf, size_t n, char hex[65])
{
    OutFile o;
    char h[65];
    int r = dio_open(&o, dir, name);
    if (r != DIO_OK) return r;
    if (n && dio_write(&o, buf, n) != DIO_OK) { dio_abort(&o); return DIO_E_WRITE; }
    if (!n) o.hashed = 1;
    r = dio_finish(&o, buf, n, h);
    if (r != DIO_OK && r != DIO_E_SUMS) dio_abort(&o);
    if (hex) memcpy(hex, h, 65);
    return r;
}

int dio_sums_lookup(const char *dir, const char *name, char hex[65])
{
    char p[300], line[256];
    snprintf(p, sizeof(p), "%s/SHA256SUMS.txt", dir);
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    size_t nl = strlen(name);
    int found = -1;
    while (fgets(line, sizeof(line), f)) {
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
        if (l == 64 + 2 + nl && line[64] == ' ' && line[65] == ' ' && strcmp(line + 66, name) == 0) {
            uint8_t d[32];
            if (sha256_parse(line, d) == 0) { memcpy(hex, line, 64); hex[64] = 0; found = 0; }
        }
    }
    fclose(f);
    return found;
}