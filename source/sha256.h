#ifndef SHA256_H
#define SHA256_H
#include <stdint.h>
#include <stddef.h>
typedef struct { uint32_t h[8]; uint64_t len; uint8_t buf[64]; unsigned n; } Sha256;

void sha256_init(Sha256 *s);
void sha256_update(Sha256 *s, const void *data, size_t n);
void sha256_final(Sha256 *s, uint8_t out[32]);           
void sha256_digest(const Sha256 *s, uint8_t out[32]);    
void sha256_hex(const uint8_t d[32], char out[65]);
int  sha256_parse(const char *hex, uint8_t out[32]);     

#endif