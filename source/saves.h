#ifndef SAVES_H
#define SAVES_H

#define USB_SAVE_DIR "/dev_usb000/PS3/SAVEDATA"

enum { SAVE_TO_USB, SAVE_TO_HDD };

typedef struct {
    char user[16];          
    int n_users;
    int hdd_count;          
    int usb_present;
    int usb_count;          
} SaveInfo;

typedef struct {
    int running, done, cancel;
    int dir;                
    int files_total, files_done;
    int saves_copied, saves_skipped, saves_failed, errors;
    unsigned long long bytes;
    char current[256];
} SaveJob;

extern SaveJob g_savejob;

void saves_info(SaveInfo *si, int user_index);
int  saves_start(int dir, const char *user);   
void saves_tick(unsigned budget_ms);

#endif