#include "netinfo.h"
#include "sys.h"
#include <stdio.h>
#include <string.h>
#include <ppu-types.h>
#include <sysmodule/sysmodule.h>
#include <net/net.h>
#include <net/netctl.h>
#include <net/netdb.h>
#include <net/socket.h>
#include <netinet/in.h>
#include <sys/thread.h>
#include <lv2/thread.h>

NetTest g_nettest;
static int inited = 0;
static sys_ppu_thread_t tid;
static int thread_alive = 0;

static void net_init_once(void)
{
    if (inited) return;
    inited = 1;
    sysModuleLoad(SYSMODULE_NET);
    netInitialize();
    sysModuleLoad(SYSMODULE_NETCTL);
    netCtlInit();
}

void net_info(NetInfo *o)
{
    net_init_once();
    memset(o, 0, sizeof(*o));
    s32 st = NET_CTL_STATE_Disconnected;
    if (netCtlGetState(&st) != 0) st = NET_CTL_STATE_Disconnected;
    o->state = st == NET_CTL_STATE_IPObtained ? NETS_ON :
               (st == NET_CTL_STATE_Connecting || st == NET_CTL_STATE_IPObtaining) ? NETS_CONNECTING : NETS_OFF;
    union net_ctl_info info;
    if (netCtlGetInfo(NET_CTL_INFO_DEVICE, &info) == 0)
        o->wireless = info.device == NET_CTL_DEVICE_WIRELESS;
    if (o->state == NETS_ON && netCtlGetInfo(NET_CTL_INFO_IP_ADDRESS, &info) == 0)
        snprintf(o->ip, sizeof(o->ip), "%s", info.ip_address);
    if (o->wireless && netCtlGetInfo(NET_CTL_INFO_SSID, &info) == 0) {
        int l = sizeof(info.ssid.data) < sizeof(o->ssid) - 1 ? (int)sizeof(info.ssid.data) : (int)sizeof(o->ssid) - 1;
        memcpy(o->ssid, info.ssid.data, l);
        o->ssid[l] = 0;
    }
}

static void test_thread(void *arg)
{
    (void)arg;
    g_nettest.dns_ok = g_nettest.web_ok = 0;
    g_nettest.ms = -1;
    struct hostent *h = gethostbyname("www.google.com");
    if (h && h->h_addr_list && h->h_addr_list[0]) {
        g_nettest.dns_ok = 1;
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s >= 0) {
            struct sockaddr_in sa;
            memset(&sa, 0, sizeof(sa));
            sa.sin_family = AF_INET;
            sa.sin_port = htons(80);
            memcpy(&sa.sin_addr, h->h_addr_list[0], sizeof(sa.sin_addr));
            unsigned t0 = ticks_ms();
            if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) == 0) {
                g_nettest.web_ok = 1;
                g_nettest.ms = (int)(ticks_ms() - t0);
            }
            close(s);
        }
    }
    g_nettest.status = NT_DONE;
    sysThreadExit(0);
}

void net_test_start(void)
{
    if (g_nettest.status == NT_RUNNING) return;
    net_init_once();
    net_test_poll();
    g_nettest.status = NT_RUNNING;
    if (sysThreadCreate(&tid, test_thread, NULL, 1000, 0x10000, THREAD_JOINABLE, "ps3tk_net") == 0)
        thread_alive = 1;
    else {
        g_nettest.dns_ok = g_nettest.web_ok = 0;
        g_nettest.status = NT_DONE;
    }
}

void net_test_poll(void)
{
    if (thread_alive && g_nettest.status == NT_DONE) {
        u64 r;
        sysThreadJoin(tid, &r);
        thread_alive = 0;
    }
}