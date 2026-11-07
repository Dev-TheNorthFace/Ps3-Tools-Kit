#ifndef SCAN_H
#define SCAN_H

#define SCAN_STEPS 6
#define MAX_FIND 64

enum { SEV_INFO, SEV_WARN, SEV_ERR };
enum { ST_TODO, ST_RUN, ST_DONE };

typedef struct {
    int sev;
    char title[128];        
    char arg[96];           
    char path[256];
    char advice[200];
} Finding;

typedef struct {
    int mode;                       
    int running, done;
    int step;
    const char *names[SCAN_STEPS];
    int state[SCAN_STEPS];
    const char *info_fmt[SCAN_STEPS];   
    char info[SCAN_STEPS][48];          
    int files, warns, errs;
    char current[256];
    float progress;                 
    Finding f[MAX_FIND];
    int nf;
    unsigned start_ms, end_ms;
} Scan;

extern Scan scan;

void scan_start(int mode);
void scan_tick(unsigned budget_ms); 
void scan_stop(void);
int  scan_export(char *out_path, int n);
void scan_finding_title(const Finding *f, char *out, int n);  
void scan_step_info(int i, char *out, int n);                 

#endif