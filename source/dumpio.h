#ifndef DUMPIO_H
#define DUMPIO_H
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include "sha256.h"

enum { DIO_OK = 0, DIO_E_OPEN = -1, DIO_E_WRITE = -2, DIO_E_CLOSE = -3, DIO_E_SIZE = -4,
       DIO_E_REREAD = -5, DIO_E_MISMATCH = -6, DIO_E_RENAME = -7, DIO_E_SUMS = -8, DIO_E_EXISTS = -9 };

typedef struct {
    FILE *f;
    char dir[200], name[96], part[300], fin[300];
    Sha256 sha;
    unsigned long long size;
    int hashed;             
} OutFile;

int  dio_new_dir(const char *base, const char *tag, char *out, int n);
int  dio_open(OutFile *o, const char *dir, const char *name);      
int  dio_write(OutFile *o, const void *b, size_t n);               
int  dio_close(OutFile *o);                                        
int  dio_digest(const OutFile *o, uint8_t out[32]);                
int  dio_reread(OutFile *o, const void *expect, size_t len, char hex[65]);   
int  dio_commit(OutFile *o, const char hex[65]);                   
int  dio_finish(OutFile *o, const void *expect, size_t len, char hex[65]);   
void dio_abort(OutFile *o);                                        
int  dio_save_buf(const char *dir, const char *name, const void *buf, size_t n, char hex[65]);
int  dio_sums_lookup(const char *dir, const char *name, char hex[65]);      
#endif