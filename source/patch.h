#ifndef PATCH_H
#define PATCH_H

#include "dumps.h"

enum { P_REBOOT, P_RECOVER, P_RSOD, P_EEID, P_NOR, P_COUNT };
enum { RISK_ORANGE, RISK_RED };

typedef struct {
    const char *name;       
    const char *desc;       
    const char *writes;     
    const char *needs;      
    int risk;               
} PatchDef;

enum { CK_OK, CK_WARN, CK_FAIL, CK_INFO };
typedef struct { int st; char fmt[200]; char arg[96]; } CheckLine;

#define PC_MAX 12
typedef struct {
    int item;
    int running;            
    int done;               
    int ready;              
    int nothing;            
    int n;
    CheckLine l[PC_MAX];
    char plan_fmt[200];     
    char plan_arg[96];
    int recover_cur;        
    unsigned long long done_b, total_b;   
} PatchCheck;

typedef struct {
    int running;
    int item;
    int phase;              
    int exit_pending;       
    int cancel;             
    unsigned long long done, total;
} PatchJob;
enum { PH_BACKUP, PH_WRITE, PH_VERIFY, PH_ROLLBACK, PH_BACKUPCHK, PH_RBVERIFY };

extern const PatchDef g_patches[P_COUNT];
extern DumpResult g_patch_res[P_COUNT];
extern PatchCheck g_pcheck;
extern PatchJob g_pjob;

void patch_reset(void);                 
void patch_check(int item);             
void patch_check_cancel(void);          
void patch_check_close(void);           
void patch_execute(int item, int option);   
void patch_tick(unsigned budget_ms);    
int  patch_busy(void);                  
void patch_result_text(int item, char *out, int n);
void patch_line_text(const CheckLine *l, char *out, int n);
void patch_plan_text(char *out, int n);
const char *patch_src_path(int item);   

#endif