#ifndef NETINFO_H
#define NETINFO_H

enum { NETS_OFF, NETS_CONNECTING, NETS_ON };
enum { NT_IDLE, NT_RUNNING, NT_DONE };

typedef struct {
    int state;          
    int wireless;       
    char ip[20];
    char ssid[40];
} NetInfo;

typedef struct {
    volatile int status;    
    int dns_ok, web_ok;
    int ms;                 
} NetTest;

extern NetTest g_nettest;

void net_info(NetInfo *out);    
void net_test_start(void);      
void net_test_poll(void);       

#endif