#ifndef SYS_H
#define SYS_H

#include <stdint.h>

enum { MODE_UNKNOWN = 0, MODE_CFW = 1, MODE_HEN = 2 };

int  path_exists(const char *p);
int  is_dir(const char *p);
long long file_size(const char *p);          

typedef int (*dir_cb)(const char *name, int isdir, void *user);
int  list_dir(const char *path, dir_cb cb, void *user);

void sys_firmware(char *out, int n);         
int  sys_detect_mode(void);                  
const char *mode_name(int mode);
int  sys_temperature(int which, int *celsius); 
int  sys_free_space(const char *path, uint64_t *bytes); 
void fmt_size(uint64_t bytes, char *out, int n);
unsigned ticks_ms(void);                    

int  file_remove(const char *p);                 
int  make_dir(const char *p);                     
int  file_rename(const char *from, const char *to); 
int  dir_remove(const char *p);                  
unsigned long long wall_time(void);               

#endif