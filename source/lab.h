#ifndef LAB_H
#define LAB_H
enum { LT_CELL, LT_MEM, LT_FILE, LT_RSX, LT_N };       
#define LT_MASK(t) (1u << (t))

typedef struct { char name[24]; unsigned mask; int dur_s, reps, mem_mb, file_mb, stop_fail; } LabCfg;
extern LabCfg g_labcfg;                                  
extern const int lab_dur_opts[5], lab_rep_opts[5], lab_mem_opts[5], lab_file_opts[5];
const char *lab_test_name(int t);

typedef struct { char key[16]; int val; } LabMetric;
#define LAB_NMET 8
#define LAB_NLINES 24
typedef struct {
    int done, fail, ms, nlines, nmet;
    char summary[110];
    char lines[LAB_NLINES][100];
    LabMetric met[LAB_NMET];
} LabRes;
extern LabRes g_labres[LT_N];

extern volatile int g_lab_state;     
extern volatile int g_lab_prog;      
extern volatile int g_lab_test;
int  lab_start(int test);            
void lab_reap(void);                 
void lab_cancel(void);
const char *lab_metric_fmt(const char *key, int val, char *out, int n);   
#define LAB_LOG_N 800
enum { LL_INFO, LL_OK, LL_WARN, LL_ERR };
typedef struct { unsigned ms; unsigned char lvl; char t[104]; } LabLogLine;
extern LabLogLine g_lablog[LAB_LOG_N];
extern volatile int g_lablog_n;
void lab_log(int lvl, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void lab_log_clear(void);
#define LAB_PROF_N 8
void lab_prof_init(void);
const LabCfg *lab_prof_get(int i);
int  lab_prof_is_user(int i);
int  lab_prof_used(int i);                 
int  lab_prof_save(int i, const LabCfg *c);
int  lab_prof_clear(int i);
#define LAB_SESS_MAX 16
#define LAB_SMET 24
typedef struct { unsigned long long when; char label[16]; char profile[24]; int fails, nmet; LabMetric met[LAB_SMET]; } LabSession;
extern LabSession g_sess[LAB_SESS_MAX];
extern int g_nsess;
void lab_sess_load(void);
int  lab_sess_add(const LabSession *s);    
int  lab_sess_set_label(int i, const char *label);
void lab_sess_compare(int a, int b);      
#endif