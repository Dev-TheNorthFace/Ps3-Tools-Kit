#ifndef DEVTOOLS_H
#define DEVTOOLS_H
#define DV_LINES 2000
enum { DC_NORMAL, DC_OK, DC_WARN, DC_ERR, DC_HEAD, DC_MUTED };
typedef struct { char t[110]; unsigned char c; } DvLine;
extern DvLine *g_dvl;
extern int g_dvn;
int  dv_init(void);                         
void dv_clear(void);
void dv_add(int col, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int  dv_write_lines(const char *path);      
int  dv_elf(const char *path);              
int  dv_sfo(const char *path);              
void dv_env(int pads, int usb, int sysmode, const char *fw, const char *res);
void dv_api(int pads_connected);            
void dv_perf(void);                         
void dv_journal(int errors_only);           
int  dv_logfile(const char *path);          
void dv_log_filter(int errors_only);        
int  dv_is_error_line(const char *s);
int  dv_report(char *outdir, int n, int pads, int usb, int sysmode, const char *fw, const char *res);
extern int g_dv_api_done, g_dv_api_fail;
extern const char *g_dv_ver;
#endif