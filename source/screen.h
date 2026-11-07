#ifndef PS3TK_SCREEN_MODULE_H
#define PS3TK_SCREEN_MODULE_H

#include <stdint.h>
int  screen_init(void);                                  
void screen_present(const uint32_t *pixels, int stride);  
void screen_end(void);
int  screen_width(void);
int  screen_height(void);

#endif