#ifndef DUMPS_H
#define DUMPS_H

enum {
    D_ERRLOG, D_AIM, D_VTRM, D_LV1, D_LV2, D_PTAB, D_EEID, D_FLASH,
    D_SRK, D_SYSROM, D_APPVER, D_TOKEN, D_TEMP, D_COMPARE, D_COUNT
};

enum { DR_NONE, DR_OK, DR_WARN, DR_ERR };

typedef struct {
    const char *name;       
    const char *desc;       
    const char *needs;      
    const char *file;       
} DumpDef;

typedef struct {
    int status;             
    char msg_fmt[320];       
    char msg_arg[160];
} DumpResult;

typedef struct {
    int running, cancel;
    int item;               
    int phase;              
    unsigned long long done, total;   
} DumpJob;

typedef struct {
    int cobra, hen, mamba;
    int lv1_peek, lv2_peek;
    int nand;               
    char root[16];          
    char outdir[64];        
    char rundir[220];      
} DumpEnv;

#define DEST_MAX 9
typedef struct {
    char root[16];          
    int usb;                
    int free_ok;            
    unsigned long long free_bytes;
} DumpDest;

typedef struct { unsigned off, size; const char *name; } FlashRegion;
extern const FlashRegion g_nor_regions[];
extern const int g_n_nor_regions;

extern const DumpDef g_dumps[D_COUNT];
extern DumpResult g_dump_res[D_COUNT];
extern DumpJob g_djob;
extern DumpEnv g_denv;
extern DumpDest g_dests[DEST_MAX];
extern int g_ndest;

void dumps_detect(void);            
void dumps_reset_results(void);     
void dumps_scan_dests(void);        
int  dumps_dests_changed(void);     
void dumps_set_dest(const char *root);   
int  dumps_dest_ready(void);        
const char *dumps_dest_name(const char *root);   
void dumps_run(int item);           
void dumps_tick(unsigned budget_ms);
void dumps_result_text(int item, char *out, int n);   
const char *dumps_last_path(int item);   
const char *dumps_env_text(void);   

#endif