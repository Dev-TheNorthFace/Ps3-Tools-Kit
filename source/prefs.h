#ifndef PREFS_H
#define PREFS_H

enum { LANG_FR, LANG_EN, LANG_RU, LANG_COUNT };
extern int g_lang;
const char *T(const char *fr);          
void lang_init(void);
extern char g_dump_dest[16];            
const char *prefs_dir(void);            
void prefs_load(const char *usrdir);
void prefs_save(void);
#define LOG_MAX 200
typedef struct { unsigned long long when; char key[96]; char arg[96]; } LogEntry;
extern LogEntry g_log[LOG_MAX];
extern int g_log_n;
void log_add(const char *key, const char *arg);
void log_clear(void);
void log_format(const LogEntry *e, char *out, int n);   
void log_time(const LogEntry *e, char *out, int n);     

#endif