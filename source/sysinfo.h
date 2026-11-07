#ifndef SYSINFO_H
#define SYSINFO_H
typedef struct {
    int done;
    unsigned mops_x10;      
    unsigned mem_mbs;       
    unsigned ms_total;      
} SiMeasure;
extern SiMeasure g_si;
void si_measure(void);      
void si_frame(unsigned now_ms);
unsigned si_frame_ms_x10(void);     
unsigned si_frame_max_ms(void);
#define SI_CAP_MAX 14
typedef struct { const char *name; int state; char detail[48]; } SiCap;   
extern SiCap g_sicap[SI_CAP_MAX];
extern int g_sicap_n;
void si_detect(int pads_connected, int usb_present, int sysmode);
#endif