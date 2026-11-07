#ifndef TOOLS2_H
#define TOOLS2_H
#include <stdint.h>
enum { R_UNTESTED = 0, R_PASS, R_FAIL };

int export_dir(char *out, int n);


#define TH_MAX 1800                    
typedef struct { int cpu, rsx, load; } ThSample;    
typedef struct { int n, mn, av, mx; } ThStat;
void     thermal_tick(int load);       
int      thermal_count(void);
ThSample thermal_at(int i);           
ThStat   thermal_stat(int sensor, int window_s, int sel);
int      thermal_export(char *path, int n);      
#define DIAG_N 13
typedef struct { const char *name; int res; char detail[64]; } DiagItem;
extern DiagItem g_diag[DIAG_N];
extern int g_diag_pos;                 
extern int g_manual_screen;           
extern int g_manual_audio;             
void diag_start(void);
int  diag_step(void);                  
void diag_counts(int *pass, int *fail, int *skip);

enum { ST_CPU = 1, ST_MEM = 2, ST_RSX = 4 };
#define ST_LOG_N 6
typedef struct {
    int running, done, mask, dur_s;
    unsigned t0, t_end;                
    int errors;
    int cpu_err, mem_err;
    uint64_t cpu_iters, mem_mb, frames;
    int cpu0, rsx0, cpu_max, rsx_max;  
    int aborted;                      
    char log[ST_LOG_N][64];
    int nlog;
} StressState;
extern StressState g_stress;
#define STRESS_CPU_LIMIT 88
#define STRESS_RSX_LIMIT 85
void stress_start(int mask, int dur_s);
void stress_tick(void);               
void stress_stop(int why);
int  stress_elapsed_s(void);
#define FS_MAXF 16
typedef struct { char name[24]; uint64_t bytes; unsigned files; } FolderSize;
typedef struct {
    int active, done, incomplete;
    char root[32];
    FolderSize f[FS_MAXF];
    int nf;
    uint64_t used, free_b;
    int free_ok;
    unsigned files;
} DiskScan;
extern DiskScan g_scan;
void scan_disk_start(const char *root);
void scan_disk_tick(int budget_ms);
void scan_disk_cancel(void);

typedef struct {
    int state;                        
    char root[32], err[64];
    int mb;                            
    int done_mb;
    int w_x10, r_x10;                  
    unsigned bad;                      
} SpeedTest;
extern SpeedTest g_speed;
void speed_start(const char *root, int mb);
void speed_tick(void);
void speed_cancel(void);
#define USB_N 8
enum { UC_PS3 = 1, UC_PUP = 2, UC_SAVES = 4, UC_DUMPS = 8, UC_GAMES = 16, UC_ISO = 32, UC_EXPORT = 64 };
typedef struct { int present; char path[16]; uint64_t free_b; int free_ok; int content; } UsbPort;
extern UsbPort g_usb[USB_N];
typedef struct { unsigned t; int port, up; } UsbEvent;
#define USB_EV_N 6
extern UsbEvent g_usb_ev[USB_EV_N];
extern int g_usb_nev;                  
int  usb_count(void);
void usb_poll(void);                   
void usb_analyze(int i);
typedef struct {
    int present;
    char media[32];                   
    char title[64], titleid[16], version[16];
    int ps3disc;
    int state;                        
    unsigned files; uint64_t bytes;
    uint64_t read_b; int mbs_x10; int errors;
    char log[4][64]; int nlog;
} Optical;
extern Optical g_opt;
void optical_poll(void);
void optical_test_start(void);
void optical_tick(void);
void optical_test_cancel(void);
int  sfo_read_str(const char *path, const char *key, char *out, int n);

enum { AC_BOTH, AC_LEFT, AC_RIGHT, AC_ALT };
enum { AT_440, AT_1000, AT_SWEEP, AT_220 };
typedef struct {
    int open, playing, err;            
    int ch8;                           
    int chan, tone, vol;               
    int pulse;                         
} AudioState;
extern AudioState g_aud;
int  aud_open(void);                  
void aud_close(void);
void aud_play(int on);
void aud_tick(void);                   
int  aud_pulse_on(void);               
int  aud_played_s(void);

#define REP_TAGS 10
typedef struct {
    int valid;
    unsigned long long when;
    char fw[16];
    int mode;
    int d_pass, d_fail, d_skip;
    int rest_cpu, rest_rsx, load_cpu, load_rsx, max_cpu, max_rsx;   
    int st_s, st_err, st_cpu_rise;                                  
    int hdd_w, hdd_r, usb_w, usb_r;                                  
    int hdd_free_mb;
    int opt;                      
    unsigned tags;
} Report;
extern unsigned g_rep_tags;
extern int g_last_hdd_w, g_last_hdd_r, g_last_usb_w, g_last_usb_r;   
extern int g_last_opt;
extern int g_last_st_s, g_last_st_err, g_last_st_rise;
void report_capture(Report *r);
int  report_save(int slot, const Report *r);          
int  report_load(int slot, Report *r);
void report_notes_load(void);
void report_notes_save(void);
int  report_export(char *path, int n);
extern const char *g_rep_tag_names[REP_TAGS];

#endif