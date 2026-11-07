#ifndef FILETOOLS_H
#define FILETOOLS_H
#include <stdint.h>

typedef struct { int active, done, err; char path[300]; unsigned long long total, done_b; char hex[65]; } FtHash;
extern FtHash g_fthash;
void fthash_start(const char *path);
void fthash_tick(unsigned budget_ms);
void fthash_cancel(void);

typedef struct { int active, done, incomplete; char root[300]; unsigned long long bytes; unsigned files, dirs; } FtSize;
extern FtSize g_ftsize;
void ftsize_start(const char *dir);
void ftsize_tick(unsigned budget_ms);
void ftsize_cancel(void);

#define FT_RES 100
typedef struct { char path[220]; int isdir; long long size; } FtFound;
typedef struct { int active, done, incomplete; char root[300], query[48]; int n; FtFound r[FT_RES]; unsigned scanned; } FtFind;
extern FtFind g_ftfind;
void ftfind_start(const char *dir, const char *query);
void ftfind_tick(unsigned budget_ms);
void ftfind_cancel(void);

int  fthex_open(const char *path);                
void fthex_close(void);
long long fthex_size(void);
int  fthex_read(long long off, uint8_t *buf, int n);   
int  ft_parse_hex(const char *s, uint8_t *out, int max);
typedef struct { int active, done, found; long long from, at; uint8_t pat[32]; int plen; } FtSeek;
extern FtSeek g_ftseek;
void fthex_find_start(long long from, const uint8_t *pat, int plen);
void fthex_find_tick(unsigned budget_ms);
void fthex_find_cancel(void);

#define FT_DIFF_MAX 64
typedef struct {
    int active, done, err;
    char a[300], b[300];
    unsigned long long size_a, size_b, pos, ndiff;  
    int nr; unsigned long long off[FT_DIFF_MAX]; unsigned long long len[FT_DIFF_MAX]; int over;
    char ha[65], hb[65];
} FtCmp;
extern FtCmp g_ftcmp;
void ftcmp_start(const char *a, const char *b);
void ftcmp_tick(unsigned budget_ms);
void ftcmp_cancel(void);
int  ft_cmp_identical(void);

#endif