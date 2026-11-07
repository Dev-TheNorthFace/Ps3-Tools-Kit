#include "dumps.h"
#include "dumpio.h"
#include "sys.h"
#include "prefs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ppu-lv2.h>
#include <lv2/sysfs.h>

#define P(x) ((u64)(uintptr_t)(x))

static s32 sc8_2(u64 a, u64 b) { lv2syscall2(8, a, b); return_to_user_prog(s32); }
static u64 sc8_1(u64 a) { lv2syscall1(8, a); return_to_user_prog(u64); }
static u64 sc8_3(u64 a, u64 b, u64 c) { lv2syscall3(8, a, b, c); return_to_user_prog(u64); }
static u64 sc6_1(u64 a) { lv2syscall1(6, a); return_to_user_prog(u64); }

static s32 sm_get_system_info(void *b) { lv2syscall1(387, P(b)); return_to_user_prog(s32); }
static s32 sm_request_error_log(u64 off, u8 *st, u32 *code, u32 *tm) { lv2syscall4(390, off, P(st), P(code), P(tm)); return_to_user_prog(s32); }
static s32 sm_request_be_count(u8 *st, u32 *run, u32 *up, u32 *down) { lv2syscall4(391, P(st), P(run), P(up), P(down)); return_to_user_prog(s32); }
static s32 sm_get_hw_config(u8 *st, u64 *v) { lv2syscall2(393, P(st), P(v)); return_to_user_prog(s32); }
static s32 sm_request_scversion(u64 *soft, u64 *rom, u64 *ram) { lv2syscall3(394, P(soft), P(rom), P(ram)); return_to_user_prog(s32); }
static s32 ss_vtrm_mgr(u64 id, u64 a, u64 b, u64 c, u64 d) { lv2syscall5(862, id, a, b, c, d); return_to_user_prog(s32); }
static s32 ss_update_mgr(u64 id, u64 a, u64 b, u64 c, u64 d, u64 e, u64 f) { lv2syscall7(863, id, a, b, c, d, e, f); return_to_user_prog(s32); }
static s32 ss_aim(u64 id, void *out) { lv2syscall2(867, id, P(out)); return_to_user_prog(s32); }
static s32 ss_flash_ext_flag(u8 *f) { lv2syscall1(874, P(f)); return_to_user_prog(s32); }
static s32 storage_open(u64 dev, u32 *h) { lv2syscall4(600, dev, 0, P(h), 0); return_to_user_prog(s32); }
static s32 storage_close(u32 h) { lv2syscall1(601, h); return_to_user_prog(s32); }
static s32 storage_read(u32 h, u64 start, u64 count, void *buf, u32 *nread, u64 flags)
{ lv2syscall7(602, h, 0, start, count, P(buf), P(nread), flags); return_to_user_prog(s32); }

typedef struct {
    u8 res1[32]; u32 vendor_id; u32 device_id; u64 capacity; u32 sector_size; u32 media_count; u8 res2[8];
} __attribute__((packed)) StorageInfo;
static s32 storage_info(u64 dev, StorageInfo *i) { lv2syscall2(609, dev, P(i)); return_to_user_prog(s32); }

#define FLASH_NAND  0x0100000000000001ULL
#define FLASH_NOR   0x0100000000000004ULL
#define SECTOR      0x200

const DumpDef g_dumps[D_COUNT] = {
    { "Dump Syscon Error Log",
      "Lit le journal d'erreurs du Syscon (la puce qui gère l'alimentation, les ventilateurs et les températures) : 32 emplacements, avec le code et la date. Utile pour diagnostiquer un YLOD ou des extinctions.",
      "CFW", "syscon_errlog.txt" },
    { "AIM commands",
      "Demande à la console son identité : type d'appareil, identifiant console (IDPS), PS code et PSID.",
      "CFW. Peut être refusé selon le firmware.", "aim.txt" },
    { "Backup VTRM",
      "Demande au système une sauvegarde de la zone VTRM (données chiffrées propres à la console).",
      "CFW. Souvent refusé sans correctif de LV1, que PS3 Tools Kit n'applique jamais.", "vtrm_backup.bin" },
    { "Dump LV1",
      "Copie les 16 Mo de mémoire de l'hyperviseur (LV1) pendant que la console tourne. Sert à la recherche et au débogage.",
      "CFW avec accès LV1 (Cobra/Mamba).", "lv1.bin" },
    { "Dump LV2",
      "Copie la mémoire du noyau (LV2) : 8 Mo, plus un second segment de 512 Ko.",
      "CFW ou HEN avec lecture LV2.", "lv2.bin.0 / lv2.bin.1" },
    { "Dump Flash PTAB",
      "Copie la table des partitions de la flash (1 Ko), qui décrit comment la mémoire est découpée.",
      "CFW", "flash_ptab.bin" },
    { "Dump EEID",
      "Copie l'EEID (64 Ko) : un bloc chiffré unique à ta console, avec son identifiant et des clés. Ne le partage jamais.",
      "CFW", "eeid.bin" },
    { "Dump NAND/NOR",
      "Copie toute la mémoire flash en 3 lectures : lecture et écriture du fichier, seconde lecture de la console comparée à la première, puis relecture complète du fichier enregistré. Une empreinte SHA-256 est enregistrée. C'est la sauvegarde la plus importante : avec un flasher matériel, elle permet de réparer une console brickée.",
      "CFW. Clé USB en FAT32 conseillée (jusqu'à 256 Mo).", "flash_nand.bin / flash_nor.bin" },
    { "Dump SRK/SRH",
      "Demande les valeurs SRK/SRH liées à la zone VTRM (stockées dans le Syscon).",
      "CFW. Souvent refusé sans correctif de LV1, que PS3 Tools Kit n'applique jamais.", "srk_srh.bin" },
    { "Dump Sysrom",
      "Lit 256 Ko de la ROM système à l'adresse 0x2401FC00000, comme l'outil d'origine. Expérimental : si la console se fige, redémarre-la (sans danger pour la flash).",
      "CFW avec accès LV1 (Cobra/Mamba).", "sysrom.bin" },
    { "Get applicable version",
      "Demande au gestionnaire de mises à jour la plus ancienne version de firmware acceptée par la console.",
      "CFW. Peut être refusé selon le firmware.", "applicable_version.txt" },
    { "Get token seed",
      "Lit l'état du QA flag et la graine qui sert à fabriquer le jeton QA.",
      "CFW. Peut être refusé selon le firmware.", "token_seed.txt" },
    { "Sys game get Temperature",
      "Lit la température du processeur Cell et du RSX.",
      "Toutes consoles (CFW conseillé).", "temperature.txt" },
    { "Compare two dumps",
      "Compare les deux Dump NAND/NOR les plus récents de cette console, octet par octet, et écrit un rapport qui liste les zones différentes (sans afficher de données). Utile pour voir ce qui a changé après une réparation.",
      "Au moins deux Dump NAND/NOR dans l'emplacement choisi.", "compare_report.txt" },
};

static const char *const tag_of[D_COUNT] = {
    "errlog", "aim", "vtrm", "lv1", "lv2", "ptab", "eeid", "flash", "srk", "sysrom", "appver", "token", "temp", "compare"
};

const FlashRegion g_nor_regions[] = {
    { 0x000000, 0x000800, "header" }, { 0x000800, 0x02E800, "asecure_loader" }, { 0x02F000, 0x010000, "eEID" },
    { 0x03F000, 0x000800, "cISD" }, { 0x03F800, 0x000800, "cCSD" }, { 0x040000, 0x020000, "trvk_prg0" },
    { 0x060000, 0x020000, "trvk_prg1" }, { 0x080000, 0x020000, "trvk_pkg0" }, { 0x0A0000, 0x020000, "trvk_pkg1" },
    { 0x0C0000, 0x700000, "ros0" }, { 0x7C0000, 0x700000, "ros1" }, { 0xEC0000, 0x040000, "cvtrm" },
    { 0xF00000, 0x020000, "header2" }, { 0xF20000, 0x020000, "extnor" }, { 0xF40000, 0x080000, "CRL/DRL" },
    { 0xFC0000, 0x040000, "bootldr" },
};
const int g_n_nor_regions = (int)(sizeof(g_nor_regions) / sizeof(g_nor_regions[0]));

DumpResult g_dump_res[D_COUNT];
DumpJob g_djob;
DumpEnv g_denv;
DumpDest g_dests[DEST_MAX];
int g_ndest;

static int is_err(u64 v) { return v == 0x80010003ULL || v == 0xFFFFFFFF80010003ULL; }

static u64 peek_lv2(u64 a) { return g_denv.cobra ? sc8_3(0x7777, 0x1006, a) : sc6_1(a); }
static u64 peek_lv1(u64 a) { return g_denv.cobra ? sc8_3(0x7777, 0x1008, a) : sc8_1(a); }

static void result(int item, int st, const char *fmt, const char *arg)
{
    g_dump_res[item].status = st;
    snprintf(g_dump_res[item].msg_fmt, sizeof(g_dump_res[item].msg_fmt), "%s", fmt);
    snprintf(g_dump_res[item].msg_arg, sizeof(g_dump_res[item].msg_arg), "%s", arg ? arg : "");
    log_add(st == DR_ERR ? "Dumps Patch : %s (échec)" : "Dumps Patch : %s (terminé)", g_dumps[item].name);
    for (int i = 0; i < g_ndest; i++)      /* espace libre à jour après l'écriture */
        if (strcmp(g_dests[i].root, g_denv.root) == 0) {
            uint64_t b = 0;
            g_dests[i].free_ok = sys_free_space(g_dests[i].root, &b) == 0;
            g_dests[i].free_bytes = b;
        }
}

static void result_code(int item, const char *fmt, s32 code)
{
    char a[24];
    snprintf(a, sizeof(a), "0x%08X", (unsigned)code);
    result(item, DR_ERR, fmt, a);
}

static char g_dump_last[D_COUNT][240];        

static const char *item_file(int item)
{
    if (item == D_FLASH && g_denv.nand >= 0) return g_denv.nand ? "flash_nand.bin" : "flash_nor.bin";
    return g_dumps[item].file;
}

static int ensure_rundir(int item)
{
    if (g_denv.rundir[0]) return 0;
    if (dio_new_dir(g_denv.outdir, tag_of[item], g_denv.rundir, sizeof(g_denv.rundir)) != 0) {
        g_denv.rundir[0] = 0;
        result(item, DR_ERR, "Impossible de créer le dossier daté dans %s", g_denv.outdir);
        return -1;
    }
    snprintf(g_dump_last[item], sizeof(g_dump_last[item]), "%s/%s", g_denv.rundir, item_file(item));
    return 0;
}

static void cleanup_rundir(int item)
{
    if (g_denv.rundir[0] && dir_remove(g_denv.rundir) == 0) g_dump_last[item][0] = 0;
}

static void fail_io(int item, int code, const char *name)
{
    if (code == DIO_E_OPEN || code == DIO_E_EXISTS) result(item, DR_ERR, "Impossible de créer le fichier dans %s", g_denv.outdir);
    else if (code == DIO_E_WRITE || code == DIO_E_CLOSE) result(item, DR_ERR, "Écriture du fichier impossible (espace plein ?)", "");
    else result(item, DR_ERR, "Fichier enregistré invalide : la relecture ne correspond pas (%s)", name);
}

static int out_open(int item, OutFile *o, const char *name)
{
    if (ensure_rundir(item) != 0) return -1;
    int r = dio_open(o, g_denv.rundir, name);
    if (r != DIO_OK) { fail_io(item, r, name); return -1; }
    return 0;
}

static int out_done(int item, OutFile *o)
{
    char hex[65];
    int r = dio_finish(o, NULL, 0, hex);
    if (r != DIO_OK) {
        fail_io(item, r, o->name);
        if (r != DIO_E_SUMS) dio_abort(o);
        return -1;
    }
    return 0;
}

static int out_bin(int item, const char *name, const void *buf, size_t n)
{
    if (ensure_rundir(item) != 0) return -1;
    char hex[65];
    int r = dio_save_buf(g_denv.rundir, name, buf, n, hex);
    if (r != DIO_OK) { fail_io(item, r, name); return -1; }
    return 0;
}

const char *dumps_last_path(int item)
{
    static char p[300];
    if (item < 0 || item >= D_COUNT) return "";
    if (g_dump_last[item][0]) return g_dump_last[item];
    snprintf(p, sizeof(p), "%s/<%s>_%s/%s", g_denv.outdir, T("date"), tag_of[item], item_file(item));
    return p;
}

static void hexline(FILE *f, const char *label, const u8 *b, int n)
{
    fprintf(f, "%s", label);
    for (int i = 0; i < n; i++) fprintf(f, "%02X", b[i]);
    fprintf(f, "\r\n");
}

static void fmt_utc(long long t, char *out, int n)
{
    long long days = t / 86400, rem = t % 86400;
    if (rem < 0) { rem += 86400; days--; }
    long long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long y = (long long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) y++;
    snprintf(out, n, "%04lld-%02u-%02u %02d:%02d:%02d UTC", y, m, d,
             (int)(rem / 3600), (int)(rem % 3600 / 60), (int)(rem % 60));
}

static unsigned crc_table[256];
static unsigned crc32_buf(unsigned crc, const u8 *b, size_t n)
{
    if (!crc_table[1])
        for (unsigned i = 0; i < 256; i++) {
            unsigned c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crc_table[i] = c;
        }
    crc = ~crc;
    while (n--) crc = crc_table[(crc ^ *b++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

const char *dumps_env_text(void)
{
    static char s[96];
    const char *p = g_denv.hen ? "PS3HEN" : g_denv.mamba ? "CFW Mamba" : g_denv.cobra ? "CFW Cobra" :
                    g_denv.lv2_peek ? "CFW" : "Pas de CFW détecté";
    const char *f = g_denv.nand == 1 ? "NAND" : g_denv.nand == 0 ? "NOR" : "Flash ?";
    snprintf(s, sizeof(s), "%s · %s", T(p), T(f));
    return s;
}

static void set_outdir(const char *root);

void dumps_detect(void)
{
    memset(&g_denv, 0, sizeof(g_denv));
    u32 ver = 0x99999999;
    s32 r = sc8_2(0x7000, P(&ver));
    g_denv.cobra = (r >= 0 && ver != 0x99999999);
    if (g_denv.cobra) {
        g_denv.hen = (sc8_1(0x1337) == 0x1337);
        g_denv.mamba = ((u32)sc8_1(0x7FFF) == 0x666);
    }
    g_denv.lv2_peek = !is_err(peek_lv2(0x8000000000000000ULL));
    if (!g_denv.hen) g_denv.lv1_peek = !is_err(peek_lv1(0));
    u8 flag = 0;
    if (ss_flash_ext_flag(&flag) == 0) g_denv.nand = (flag & 1) ? 1 : 0;
    else {
        StorageInfo si;
        if (storage_info(FLASH_NOR, &si) == 0) g_denv.nand = 0;
        else if (storage_info(FLASH_NAND, &si) == 0) g_denv.nand = 1;
        else g_denv.nand = -1;
    }

    dumps_scan_dests();
    const char *want = NULL;
    if (g_dump_dest[0]) {
        want = g_dump_dest;
        if (strncmp(want, "/dev_usb", 8) == 0 && !is_dir(want))
            for (int i = 0; i < g_ndest; i++)
                if (g_dests[i].usb) { want = g_dests[i].root; break; }  
    } else {
        want = g_dests[0].root;      
    }
    set_outdir(want);
}

static void set_outdir(const char *root)
{
    snprintf(g_denv.root, sizeof(g_denv.root), "%s", root);
    snprintf(g_denv.outdir, sizeof(g_denv.outdir), "%s/PS3TK_DUMPS", root);
}

static void add_dest(const char *root, int usb)
{
    if (g_ndest >= DEST_MAX) return;
    DumpDest *d = &g_dests[g_ndest++];
    memset(d, 0, sizeof(*d));
    snprintf(d->root, sizeof(d->root), "%s", root);
    d->usb = usb;
    uint64_t b = 0;
    if (sys_free_space(root, &b) == 0) { d->free_ok = 1; d->free_bytes = b; }
}

void dumps_scan_dests(void)
{
    g_ndest = 0;
    for (int i = 0; i < 8; i++) {
        char u[16];
        snprintf(u, sizeof(u), "/dev_usb00%d", i);
        if (is_dir(u)) add_dest(u, 1);
    }
    add_dest("/dev_hdd0", 0);
}

int dumps_dests_changed(void)
{
    int k = 0;
    for (int i = 0; i < 8; i++) {
        char u[16];
        snprintf(u, sizeof(u), "/dev_usb00%d", i);
        if (!is_dir(u)) continue;
        if (k >= g_ndest || !g_dests[k].usb || strcmp(g_dests[k].root, u) != 0) return 1;
        k++;
    }
    return k < g_ndest && g_dests[k].usb;
}

void dumps_set_dest(const char *root)
{
    if (!root || strncmp(root, "/dev_", 5) != 0) return;
    set_outdir(root);
    snprintf(g_dump_dest, sizeof(g_dump_dest), "%s", root);
    prefs_save();
    log_add("Dumps Patch : emplacement %s", g_denv.outdir);
}

int dumps_dest_ready(void)
{
    return g_denv.root[0] && is_dir(g_denv.root);
}

const char *dumps_dest_name(const char *root)
{
    return T(root && strncmp(root, "/dev_usb", 8) == 0 ? "Clé USB" : "Disque dur interne");
}

void dumps_reset_results(void)
{
    if (g_djob.running) return;
    memset(g_dump_res, 0, sizeof(g_dump_res));
}

static int check_space(int item, u64 need)
{
    uint64_t b = 0;
    if (sys_free_space(g_denv.root, &b) != 0 || b >= need + 0x100000ULL) return 1;
    char a[32];
    fmt_size(need, a, sizeof(a));
    result(item, DR_ERR, "Pas assez de place : il faut %s (change d'emplacement avec Triangle)", a);
    return 0;
}

static const char *errlog_desc(u32 code)
{
    switch (code & 0xFFFF) {
    case 0x1001: return "Power CELL";
    case 0x1002: return "Power RSX";
    case 0x1004: return "Power AC/DC";
    case 0x1103: return "Thermal Alert SYSTEM";
    case 0x1200: return "Thermal CELL";
    case 0x1201: return "Thermal RSX";
    case 0x1203: return "Thermal CELL VR";
    case 0x1204: return "Thermal South Bridge";
    case 0x1205: return "Thermal EE/GS";
    case 0x1301: return "CELL PLL";
    case 0x14FF: return "Check stop";
    case 0x1601: return "BE Livelock Detection";
    case 0x1701: return "CELL attention";
    case 0x1802: return "RSX init";
    case 0x1900: return "RTC Voltage";
    case 0x1901: return "RTC Oscillator";
    case 0x1902: return "RTC Access";
    case 0x2022: return "DVE Error (analog AV)";
    default: return "Unknown";
    }
}

static void do_errlog(void)
{
    int found = 0, read_ok = 0;
    s32 first_err = 0;
    OutFile o;
    if (out_open(D_ERRLOG, &o, "syscon_errlog.txt") != 0) return;
    FILE *f = o.f;
    fprintf(f, "PS3 Tools Kit - Syscon error log (read-only)\r\nCodes: https://www.psdevwiki.com/ps3/Syscon_Error_Codes\r\n\r\n");

    struct { u8 hi; u16 lo; u8 res; u8 unk1[4]; char platform[8]; u32 build; u8 unk2[4]; } __attribute__((packed)) si;
    memset(&si, 0, sizeof(si));
    if (sm_get_system_info(&si) == 0) {
        char plat[9]; memcpy(plat, si.platform, 8); plat[8] = 0;
        fprintf(f, "Firmware: %x.%02x (build %u)\r\nPlatform ID: %s\r\n", si.hi, si.lo >> 4, (unsigned)si.build, plat);
    }
    u8 code8[8];
    if (ss_aim(0x19004, code8) == 0)
        fprintf(f, "Product code: %02X %02X / sub code: %02X %02X\r\n", code8[2], code8[3], code8[4], code8[5]);
    u8 st = 0; u64 hw = 0;
    if (sm_get_hw_config(&st, &hw) == 0 && !st) fprintf(f, "Hardware config: %016llX\r\n", (unsigned long long)hw);
    u64 soft = 0, rom = 0, ram = 0;
    if (sm_request_scversion(&soft, &rom, &ram) == 0)
        fprintf(f, "Syscon firmware: %04llX.%016llX (EEPROM: %016llX)\r\n",
                (unsigned long long)soft, (unsigned long long)ram, (unsigned long long)rom);
    u32 run = 0, up = 0, down = 0;
    if (sm_request_be_count(&st, &run, &up, &down) == 0 && !st)
        fprintf(f, "Power-on count: %u, shutdown count: %u, run time: %u h %02u min\r\n",
                (unsigned)up, (unsigned)down, (unsigned)(run / 3600), (unsigned)(run % 3600 / 60));

    fprintf(f, "\r\nError log (32 slots):\r\n");
    for (int i = 0; i < 32; i++) {
        u32 code = 0, tm = 0;
        st = 0;
        s32 r = sm_request_error_log(i, &st, &code, &tm);
        if (r != 0 || st) { if (!read_ok) first_err = r ? r : st; break; }
        read_ok++;
        if (code == 0xFFFFFFFF) continue;
        char d[48];
        fmt_utc((long long)tm + 946684800LL, d, sizeof(d));   /* secondes depuis 2000 */
        fprintf(f, "%02d: %08X  %-24s %s\r\n", i + 1, (unsigned)code, errlog_desc(code), d);
        found++;
    }
    if (read_ok && !found) fprintf(f, "(no errors recorded)\r\n");
    if (out_done(D_ERRLOG, &o) != 0) return;
    if (!read_ok) { result_code(D_ERRLOG, "Refusé par la console (code %s)", first_err); return; }
    char a[16];
    snprintf(a, sizeof(a), "%d", found);
    result(D_ERRLOG, found ? DR_WARN : DR_OK, "Erreurs enregistrées : %s", a);
}

static void do_aim(void)
{
    static const struct { u32 id; const char *label; int len; } q[4] = {
        { 0x19002, "Device type", 0x10 }, { 0x19003, "IDPS (device ID)", 0x10 },
        { 0x19004, "PS code", 0x08 }, { 0x19005, "Open PSID", 0x10 },
    };
    OutFile o;
    if (out_open(D_AIM, &o, "aim.txt") != 0) return;
    FILE *f = o.f;
    fprintf(f, "PS3 Tools Kit - AIM (Appliance Info Manager), read-only\r\nKEEP THIS FILE PRIVATE.\r\n\r\n");
    int ok = 0; s32 last = 0;
    for (int i = 0; i < 4; i++) {
        u8 b[0x10];
        memset(b, 0, sizeof(b));
        s32 r = ss_aim(q[i].id, b);  
        char lab[48];
        snprintf(lab, sizeof(lab), "%s: ", q[i].label);
        if (r == 0) { hexline(f, lab, b, q[i].len); ok++; }
        else { fprintf(f, "%s error 0x%08X\r\n", q[i].label, (unsigned)r); last = r; }
    }
    if (out_done(D_AIM, &o) != 0) return;
    if (!ok) { result_code(D_AIM, "Refusé par la console (code %s)", last); return; }
    char a[16];
    snprintf(a, sizeof(a), "%d", ok);
    result(D_AIM, ok == 4 ? DR_OK : DR_WARN, "Réponses obtenues : %s sur 4", a);
}

static void do_vtrm(void)
{
    static u8 buf[0x8000];
    u64 nread = 0;
    memset(buf, 0, sizeof(buf));
    s32 r = ss_vtrm_mgr(0x2012, 0, sizeof(buf), P(buf), P(&nread));
    if (r != 0) { result_code(D_VTRM, "Refusé par la console (code %s)", r); return; }
    if (out_bin(D_VTRM, "vtrm_backup.bin", buf, sizeof(buf)) != 0) return;
    result(D_VTRM, DR_OK, "Fichier créé : %s (32 Ko)", "vtrm_backup.bin");
}

static void do_srk(void)
{
    u8 buf[0x80];
    memset(buf, 0, sizeof(buf));
    s32 r = ss_vtrm_mgr(0x2014, sizeof(buf), P(buf), 0, 0);
    if (r != 0) { result_code(D_SRK, "Refusé par la console (code %s)", r); return; }
    if (out_bin(D_SRK, "srk_srh.bin", buf, sizeof(buf)) != 0) return;
    result(D_SRK, DR_OK, "Fichier créé : %s", "srk_srh.bin");
}

static void do_ptab(void)
{
    u32 h = 0, nread = 0;
    u8 buf[2 * SECTOR];
    if (g_denv.nand < 0) { result(D_PTAB, DR_ERR, "Type de flash inconnu", ""); return; }
    s32 r = storage_open(FLASH_NAND, &h);   
    if (r != 0) { result_code(D_PTAB, "Accès à la flash refusé (code %s)", r); return; }
    r = storage_read(h, g_denv.nand ? 0x7600 : 0, 2, buf, &nread, 0x6);
    storage_close(h);
    if (r != 0) { result_code(D_PTAB, "Lecture impossible (code %s)", r); return; }
    if (nread != 2) { result(D_PTAB, DR_ERR, "La console n'a pas lu tous les secteurs demandés : fichier non créé", ""); return; }
    if (out_bin(D_PTAB, "flash_ptab.bin", buf, sizeof(buf)) != 0) return;
    result(D_PTAB, DR_OK, "Fichier créé : %s (1 Ko)", "flash_ptab.bin");
}

static int console_idps(u8 *out)
{
    memset(out, 0, 16);
    return ss_aim(0x19003, out) == 0;
}

static void do_eeid(void)
{
    static u8 buf[0x80 * SECTOR];
    u32 h = 0;
    if (g_denv.nand < 0) { result(D_EEID, DR_ERR, "Type de flash inconnu", ""); return; }
    u64 dev = g_denv.nand ? FLASH_NAND : FLASH_NOR, start = g_denv.nand ? 0x204 : 0x178;
    s32 r = storage_open(dev, &h);
    if (r != 0) { result_code(D_EEID, "Accès à la flash refusé (code %s)", r); return; }
    int short_read = 0;
    for (int i = 0; i < 0x80 && r == 0; i += 16) {
        u32 nread = 0;
        r = storage_read(h, start + i, 16, buf + i * SECTOR, &nread, 0x22);
        if (r == 0 && nread != 16) short_read = 1;
        if (short_read) break;
    }
    storage_close(h);
    if (r != 0) { result_code(D_EEID, "Lecture impossible (code %s)", r); return; }
    if (short_read) { result(D_EEID, DR_ERR, "La console n'a pas lu tous les secteurs demandés : fichier non créé", ""); return; }
    if (out_bin(D_EEID, "eeid.bin", buf, sizeof(buf)) != 0) return;
    u8 aim[16];
    int have = console_idps(aim);
    if (have && memcmp(aim, buf + 0x70, 16) == 0)
        result(D_EEID, DR_OK, "Fichier créé : %s (64 Ko), IDPS de 16 octets identique à la console", "eeid.bin");
    else if (have)
        result(D_EEID, DR_ERR, "Fichier créé, mais son IDPS est DIFFÉRENT de celui de la console : ne l'utilise pas (%s)", "eeid.bin");
    else {
        u64 idps = 0;
        for (int i = 0; i < 8; i++) idps = (idps << 8) | buf[0x70 + i];
        if ((idps & 0xFFFFFFFFFFF0FF00ULL) == 0x0000000100800000ULL)
            result(D_EEID, DR_WARN, "Fichier créé (%s) ; IDPS non comparé : la console a refusé la demande AIM", "eeid.bin");
        else
            result(D_EEID, DR_WARN, "Fichier créé, mais contenu inattendu : %s", "eeid.bin");
    }
}

static void do_appver(void)
{
    u8 out[0x20];
    memset(out, 0, sizeof(out));
    s32 r = ss_update_mgr(0x6011, 1, P(out), 0, 0, 0, 0);
    if (r != 0) { result_code(D_APPVER, "Refusé par la console (code %s)", r); return; }
    char v[16];
    snprintf(v, sizeof(v), "%x.%02x", out[1], out[3]);
    OutFile o;
    if (out_open(D_APPVER, &o, "applicable_version.txt") != 0) return;
    fprintf(o.f, "PS3 Tools Kit - Get applicable version (read-only)\r\n");
    fprintf(o.f, "Minimum version (webMAN interpretation): %s\r\n", v);
    hexline(o.f, "Raw: ", out, sizeof(out));
    if (out_done(D_APPVER, &o) != 0) return;
    result(D_APPVER, DR_OK, "Version minimale : %s", v);
}

static void do_token(void)
{
    u8 qa = 0, token[0x50], seed[0x50];
    memset(token, 0, sizeof(token));
    memset(seed, 0, sizeof(seed));
    s32 rq = ss_update_mgr(0x600B, 0x48C0A, P(&qa), 0, 0, 0, 0);   /* lecture EEPROM : QA flag */
    s32 r = ss_update_mgr(0x6009, P(token), sizeof(token), P(seed), sizeof(seed), 0, 0);
    if (r != 0 && rq != 0) { result_code(D_TOKEN, "Refusé par la console (code %s)", r); return; }
    OutFile o;
    if (out_open(D_TOKEN, &o, "token_seed.txt") != 0) return;
    fprintf(o.f, "PS3 Tools Kit - Get token seed (read-only)\r\n");
    if (rq == 0) fprintf(o.f, "QA flag (EEPROM 0x48C0A): 0x%02X\r\n", qa);
    else fprintf(o.f, "QA flag: error 0x%08X\r\n", (unsigned)rq);
    if (r == 0) { hexline(o.f, "Token seed: ", seed, sizeof(seed)); hexline(o.f, "Token: ", token, sizeof(token)); }
    else fprintf(o.f, "Token seed: error 0x%08X\r\n", (unsigned)r);
    if (out_done(D_TOKEN, &o) != 0) return;
    if (r != 0) { result_code(D_TOKEN, "QA flag lu, graine refusée (code %s)", r); g_dump_res[D_TOKEN].status = DR_WARN; return; }
    char a[16];
    snprintf(a, sizeof(a), "0x%02X", qa);
    result(D_TOKEN, DR_OK, "Graine lue · QA flag : %s", a);
}

static void do_temp(void)
{
    int c = -1, r = -1;
    int okc = sys_temperature(0, &c) == 0, okr = sys_temperature(1, &r) == 0;
    if (!okc && !okr) { result(D_TEMP, DR_ERR, "Non disponible (RPCS3 ou firmware)", ""); return; }
    OutFile o;
    if (out_open(D_TEMP, &o, "temperature.txt") != 0) return;
    fprintf(o.f, "CPU (Cell): %s%d C\r\nRSX: %s%d C\r\n", okc ? "" : "n/a ", okc ? c : 0, okr ? "" : "n/a ", okr ? r : 0);
    if (out_done(D_TEMP, &o) != 0) return;
    char a[48];
    snprintf(a, sizeof(a), "CPU %d °C · RSX %d °C", okc ? c : 0, okr ? r : 0);
    result(D_TEMP, DR_OK, "%s", a);
}

typedef struct { u64 addr, size; const char *file; } Seg;
static Seg segs[2];
static int nseg, cur_seg;
static u64 seg_off;
static OutFile pf;                 
static int pf_open;
static int lv1_mode;
static u32 fh;
static u64 f_sectors, f_pos;
static unsigned *f_crcs;
static unsigned f_crc_all;
static int f_mismatch, f_file_bad;
static u8 fbuf[16 * SECTOR];
static OutFile fo;
static int fo_open;
static FILE *rf;
static u8 f_digest[32];
static Sha256 f_rsha;
static u8 f_aim[16], f_idps_dump[16];
static int f_aim_ok, f_idps_seen;
static u64 f_eeid_sect;
static char f_name[24];
static FILE *ca, *cb;
static char cmp_a[80], cmp_b[80];
static u64 cmp_size, cmp_pos, cmp_dsect, cmp_dbytes;
#define CMP_MAX 256
static u64 rg_start[CMP_MAX], rg_len[CMP_MAX];
static int rg_n, rg_over;
static u64 rg_end_last;
static u8 cbuf_a[64 * 1024], cbuf_b[64 * 1024];

static void job_end(int st, const char *fmt, const char *arg)
{
    if (pf_open) { dio_abort(&pf); pf_open = 0; }
    if (fo_open) { dio_abort(&fo); fo_open = 0; }
    if (rf) { fclose(rf); rf = NULL; }
    if (ca) { fclose(ca); ca = NULL; }
    if (cb) { fclose(cb); cb = NULL; }
    if (fh) { storage_close(fh); fh = 0; }
    free(f_crcs); f_crcs = NULL;
    g_djob.running = 0;
    cleanup_rundir(g_djob.item);
    result(g_djob.item, st, fmt, arg);
}

static void start_peek_job(int item, int lv1, const Seg *s, int n)
{
    if (lv1 ? !g_denv.lv1_peek : !g_denv.lv2_peek) {
        result(item, DR_ERR, lv1 ? "Accès LV1 indisponible (CFW Cobra/Mamba requis)" : "Lecture LV2 indisponible (CFW ou HEN requis)", "");
        return;
    }
    u64 need = 0;
    for (int i = 0; i < n; i++) need += s[i].size;
    if (!check_space(item, need * 2)) return;
    memset(&g_djob, 0, sizeof(g_djob));
    g_djob.item = item;
    lv1_mode = lv1;
    nseg = n; cur_seg = 0; seg_off = 0;
    for (int i = 0; i < n; i++) { segs[i] = s[i]; g_djob.total += s[i].size; }
    if (ensure_rundir(item) != 0) return;
    int r = dio_open(&pf, g_denv.rundir, segs[0].file);
    if (r != DIO_OK) { fail_io(item, r, segs[0].file); cleanup_rundir(item); return; }
    pf_open = 1;
    g_djob.running = 1;
}

static void tick_peek(unsigned budget, unsigned t0)
{
    static u64 q[512];
    while (ticks_ms() - t0 < budget) {
        if (g_djob.cancel) { job_end(DR_WARN, "Annulé par l'utilisateur", ""); return; }
        Seg *s = &segs[cur_seg];
        u64 n = s->size - seg_off;
        if (n > sizeof(q)) n = sizeof(q);
        for (u64 i = 0; i < n / 8; i++)
            q[i] = lv1_mode ? peek_lv1(s->addr + seg_off + i * 8) : peek_lv2(s->addr + seg_off + i * 8);
        if (dio_write(&pf, q, n) != DIO_OK) { job_end(DR_ERR, "Écriture du fichier impossible (espace plein ?)", ""); return; }
        seg_off += n;
        g_djob.done += n;
        if (seg_off >= s->size) {
            char hex[65];
            int r = dio_finish(&pf, NULL, 0, hex);
            pf_open = 0;                        
            if (r != DIO_OK) { job_end(DR_ERR, "Fichier enregistré invalide : la relecture ne correspond pas (%s)", s->file); return; }
            if (++cur_seg >= nseg) {
                char a[96];
                snprintf(a, sizeof(a), "%s", g_dumps[g_djob.item].file);
                job_end(DR_OK, "Fichier créé : %s", a);
                return;
            }
            seg_off = 0;
            r = dio_open(&pf, g_denv.rundir, segs[cur_seg].file);
            if (r != DIO_OK) { job_end(DR_ERR, "Impossible de créer le fichier dans %s", g_denv.outdir); return; }
            pf_open = 1;
        }
    }
}

static void start_flash_job(void)
{
    if (g_denv.nand < 0) { result(D_FLASH, DR_ERR, "Type de flash inconnu", ""); return; }
    u64 dev = g_denv.nand ? FLASH_NAND : FLASH_NOR;
    StorageInfo si;
    memset(&si, 0, sizeof(si));
    s32 r = storage_info(dev, &si);
    if (r != 0 || si.capacity == 0 || si.capacity > 0x100000) { result_code(D_FLASH, "Accès à la flash refusé (code %s)", r); return; }
    if (!check_space(D_FLASH, si.capacity * SECTOR + 0x10000)) return;
    r = storage_open(dev, &fh);
    if (r != 0) { fh = 0; result_code(D_FLASH, "Accès à la flash refusé (code %s)", r); return; }
    memset(&g_djob, 0, sizeof(g_djob));
    g_djob.item = D_FLASH;
    f_sectors = si.capacity; f_pos = 0; f_crc_all = 0; f_mismatch = 0; f_file_bad = 0; f_idps_seen = 0;
    f_eeid_sect = g_denv.nand ? 0x204 : 0x178;
    f_aim_ok = console_idps(f_aim);
    snprintf(f_name, sizeof(f_name), "%s", g_denv.nand ? "flash_nand.bin" : "flash_nor.bin");
    f_crcs = calloc((f_sectors + 15) / 16, sizeof(unsigned));
    if (!f_crcs) { job_end(DR_ERR, "Mémoire insuffisante", ""); return; }
    if (ensure_rundir(D_FLASH) != 0) { job_end(DR_ERR, "Impossible de créer le dossier daté dans %s", g_denv.outdir); return; }
    r = dio_open(&fo, g_denv.rundir, f_name);
    if (r != DIO_OK) { job_end(DR_ERR, "Impossible de créer le fichier dans %s", g_denv.outdir); return; }
    fo_open = 1;
    g_djob.total = f_sectors * SECTOR * 3;
    g_djob.running = 1;
}

static void finish_flash(void)
{
    uint8_t d[32];
    char hex[65], a[96], info[1024];
    u8 extra;
    int extra_bytes = fread(&extra, 1, 1, rf) != 0 || ferror(rf);
    fclose(rf); rf = NULL;
    sha256_final(&f_rsha, d);
    int sha_ok = memcmp(d, f_digest, 32) == 0;
    sha256_hex(f_digest, hex);

    if (f_mismatch) {
        fo_open = 0;                      
        snprintf(a, sizeof(a), "%d", f_mismatch);
        job_end(DR_ERR, "Dump invalide : %s bloc(s) diffèrent entre les deux lectures de la console. Refais le dump", a);
        return;
    }
    if (f_file_bad || !sha_ok || extra_bytes) {
        fo_open = 0;
        snprintf(a, sizeof(a), "%d", f_file_bad);
        job_end(DR_ERR, "Dump invalide : le fichier relu ne correspond pas à la lecture (%s bloc(s)). Il reste en .partial", a);
        return;
    }
    int idps = (!f_aim_ok || !f_idps_seen) ? 0 : memcmp(f_aim, f_idps_dump, 16) == 0 ? 1 : -1;
    int r = dio_commit(&fo, hex);
    fo_open = 0;
    if (r != DIO_OK) { job_end(DR_ERR, "Fichier enregistré invalide : la relecture ne correspond pas (%s)", f_name); return; }

    const char *folder = strrchr(g_denv.rundir, '/');
    snprintf(info, sizeof(info),
             "PS3 Tools Kit - flash dump (read-only)\r\n"
             "Type: %s\r\nSize: %llu bytes\r\nFile: %s\r\n"
             "Console read, pass 1: OK\r\n"
             "Console read, pass 2 (compared with pass 1): OK (0 block(s) differ)\r\n"
             "Saved file re-read in full: OK (0 block(s) differ)\r\n"
             "CRC32: %08X\r\nSHA256: %s\r\n"
             "IDPS (16 bytes) against the console (AIM): %s\r\n"
             "Verified: %s\r\n"
             "Folder: %s\r\n"
             "Check the SHA256 again later with SHA256SUMS.txt (sha256sum -c) before relying on this dump.\r\n"
             "Validate it on a PC with PyPS3checker before relying on it.\r\n",
             g_denv.nand ? "NAND" : "NOR", (unsigned long long)(f_sectors * SECTOR), f_name, f_crc_all, hex,
             idps > 0 ? "MATCH" : idps < 0 ? "MISMATCH" : "NOT COMPARED", idps < 0 ? "NO" : "YES", folder ? folder + 1 : "");
    r = dio_save_buf(g_denv.rundir, "flash_info.txt", info, strlen(info), NULL);
    if (r != DIO_OK) { job_end(DR_ERR, "Fichier enregistré invalide : la relecture ne correspond pas (%s)", "flash_info.txt"); return; }

    snprintf(a, sizeof(a), "%s · SHA-256 %.16s…", f_name, hex);
    if (idps < 0) job_end(DR_WARN, "Copie lue et vérifiée, mais l'IDPS ne correspond pas à celui de la console : ne l'utilise pas pour réparer (%s)", a);
    else if (idps == 0) job_end(DR_OK, "Copie vérifiée en 3 lectures, IDPS non comparé (AIM refusé) : %s", a);
    else job_end(DR_OK, "Copie vérifiée en 3 lectures, IDPS identique à la console : %s", a);
}

static void tick_flash(unsigned budget, unsigned t0)
{
    while (ticks_ms() - t0 < budget) {
        if (g_djob.cancel) { job_end(DR_WARN, "Annulé par l'utilisateur", ""); return; }
        u64 n = f_sectors - f_pos;
        if (n > 16) n = 16;
        if (g_djob.phase < 2) {
            u32 nread = 0;
            s32 r = storage_read(fh, f_pos, n, fbuf, &nread, 0x22);
            if (r != 0) {
                char a[24];
                snprintf(a, sizeof(a), "0x%08X", (unsigned)r);
                job_end(DR_ERR, "Lecture de la flash impossible (code %s)", a);
                return;
            }
            if (nread != n) {
                char a[24];
                snprintf(a, sizeof(a), "%u", (unsigned)nread);
                job_end(DR_ERR, "La console n'a pas lu tous les secteurs demandés (%s lus) : dump annulé", a);
                return;
            }
        } else if (fread(fbuf, 1, n * SECTOR, rf) != n * SECTOR) {
            fo_open = 0;
            job_end(DR_ERR, "Relecture du fichier enregistré impossible : dump invalide, il reste en .partial", "");
            return;
        }
        unsigned c = crc32_buf(0, fbuf, n * SECTOR);
        if (g_djob.phase == 0) {
            if (dio_write(&fo, fbuf, n * SECTOR) != DIO_OK) { job_end(DR_ERR, "Écriture du fichier impossible (espace plein ?)", ""); return; }
            f_crcs[f_pos / 16] = c;
            f_crc_all = crc32_buf(f_crc_all, fbuf, n * SECTOR);
        } else if (g_djob.phase == 1) {
            if (f_crcs[f_pos / 16] != c) f_mismatch++;
        } else {
            if (f_crcs[f_pos / 16] != c) f_file_bad++;
            sha256_update(&f_rsha, fbuf, n * SECTOR);
            if (f_eeid_sect >= f_pos && f_eeid_sect < f_pos + n) {   
                memcpy(f_idps_dump, fbuf + (f_eeid_sect - f_pos) * SECTOR + 0x70, 16);
                f_idps_seen = 1;
            }
        }
        f_pos += n;
        g_djob.done += n * SECTOR;
        if (f_pos >= f_sectors) {
            if (g_djob.phase == 0) {     
                if (dio_close(&fo) != DIO_OK) { job_end(DR_ERR, "Écriture du fichier impossible (espace plein ?)", ""); return; }
                dio_digest(&fo, f_digest);
                g_djob.phase = 1;
                f_pos = 0;
                continue;
            }
            if (g_djob.phase == 1) {     
                rf = fopen(fo.part, "rb");
                if (!rf) { fo_open = 0; job_end(DR_ERR, "Relecture du fichier enregistré impossible : dump invalide, il reste en .partial", ""); return; }
                sha256_init(&f_rsha);
                g_djob.phase = 2;
                f_pos = 0;
                continue;
            }
            finish_flash();
            return;
        }
    }
}

typedef struct { char n[64]; } Nm;
static Nm cmp_names[64];
static int cmp_nn;
static int cmp_cb(const char *name, int isdir, void *u)
{
    (void)u;
    if (isdir && strstr(name, "_flash") && cmp_nn < 64 && strlen(name) < 63) snprintf(cmp_names[cmp_nn++].n, 64, "%s", name);
    return 0;
}
static int cmp_desc(const void *a, const void *b) { return strcmp(((const Nm *)b)->n, ((const Nm *)a)->n); }

static void start_compare_job(void)
{
    if (g_denv.nand < 0) { result(D_COMPARE, DR_ERR, "Type de flash inconnu", ""); return; }
    const char *file = g_denv.nand ? "flash_nand.bin" : "flash_nor.bin";
    cmp_nn = 0;
    list_dir(g_denv.outdir, cmp_cb, NULL);
    qsort(cmp_names, cmp_nn, sizeof(Nm), cmp_desc);          
    int pick[2] = { -1, -1 }, np = 0;
    char p[300];
    for (int i = 0; i < cmp_nn && np < 2; i++) {
        snprintf(p, sizeof(p), "%s/%s/%s", g_denv.outdir, cmp_names[i].n, file);
        if (file_size(p) > 0) pick[np++] = i;
    }
    if (np < 2) { result(D_COMPARE, DR_ERR, "Il faut deux Dump NAND/NOR dans cet emplacement : fais-en un second", ""); return; }
    snprintf(cmp_b, sizeof(cmp_b), "%s", cmp_names[pick[0]].n);   
    snprintf(cmp_a, sizeof(cmp_a), "%s", cmp_names[pick[1]].n);   
    char pa[300], pb[300];
    snprintf(pa, sizeof(pa), "%s/%s/%s", g_denv.outdir, cmp_a, file);
    snprintf(pb, sizeof(pb), "%s/%s/%s", g_denv.outdir, cmp_b, file);
    if (file_size(pa) != file_size(pb)) { result(D_COMPARE, DR_ERR, "Les deux dumps n'ont pas la même taille : comparaison impossible", ""); return; }
    memset(&g_djob, 0, sizeof(g_djob));
    g_djob.item = D_COMPARE;
    g_djob.phase = 3;
    ca = fopen(pa, "rb");
    cb = fopen(pb, "rb");
    if (!ca || !cb) { job_end(DR_ERR, "Lecture du fichier impossible", ""); return; }
    cmp_size = (u64)file_size(pa);
    cmp_pos = 0; cmp_dsect = 0; cmp_dbytes = 0; rg_n = 0; rg_over = 0; rg_end_last = ~0ULL;
    g_djob.total = cmp_size;
    g_djob.running = 1;
}

static const char *region_of(u64 off)
{
    if (g_denv.nand != 0) return "";
    for (int i = 0; i < g_n_nor_regions; i++)
        if (off >= g_nor_regions[i].off && off < (u64)g_nor_regions[i].off + g_nor_regions[i].size) return g_nor_regions[i].name;
    return "";
}

static void finish_compare(void)
{
    static char rep[28000];
    const char *file = g_denv.nand ? "flash_nand.bin" : "flash_nor.bin";
    char da[300], db[300], ha[65], hb[65];
    snprintf(da, sizeof(da), "%s/%s", g_denv.outdir, cmp_a);
    snprintf(db, sizeof(db), "%s/%s", g_denv.outdir, cmp_b);
    if (dio_sums_lookup(da, file, ha) != 0) snprintf(ha, sizeof(ha), "n/a");
    if (dio_sums_lookup(db, file, hb) != 0) snprintf(hb, sizeof(hb), "n/a");
    int n = snprintf(rep, sizeof(rep),
        "PS3 Tools Kit - flash dumps comparison (read-only, no data shown)\r\n\r\n"
        "A (older): %s\r\n  SHA256 %s\r\nB (newer): %s\r\n  SHA256 %s\r\n"
        "Size: %llu bytes\r\n\r\n", cmp_a, ha, cmp_b, hb, (unsigned long long)cmp_size);
    if (cmp_dsect == 0) n += snprintf(rep + n, sizeof(rep) - n, "Result: IDENTICAL (every byte is the same)\r\n");
    else {
        n += snprintf(rep + n, sizeof(rep) - n, "Result: %llu sector(s) differ, %llu byte(s) differ, %d zone(s)%s\r\n\r\n"
                      "Zones (offset, length, region):\r\n", (unsigned long long)cmp_dsect, (unsigned long long)cmp_dbytes, rg_n,
                      rg_over ? " (list truncated)" : "");
        for (int i = 0; i < rg_n && n < (int)sizeof(rep) - 120; i++)
            n += snprintf(rep + n, sizeof(rep) - n, "  0x%08llX  0x%llX  %s\r\n", (unsigned long long)rg_start[i], (unsigned long long)rg_len[i], region_of(rg_start[i]));
    }
    if (n >= (int)sizeof(rep)) n = sizeof(rep) - 1;
    if (ensure_rundir(D_COMPARE) != 0) { job_end(DR_ERR, "Impossible de créer le dossier daté dans %s", g_denv.outdir); return; }
    int r = dio_save_buf(g_denv.rundir, "compare_report.txt", rep, (size_t)n, NULL);
    if (r != DIO_OK) { job_end(DR_ERR, "Fichier enregistré invalide : la relecture ne correspond pas (%s)", "compare_report.txt"); return; }
    char a[32];
    snprintf(a, sizeof(a), "%d", rg_n);
    if (cmp_dsect == 0) job_end(DR_OK, "Les deux dumps sont identiques. Rapport : %s", "compare_report.txt");
    else job_end(DR_WARN, "%s zone(s) différente(s). Rapport : compare_report.txt", a);
}

static void tick_compare(unsigned budget, unsigned t0)
{
    while (ticks_ms() - t0 < budget) {
        if (g_djob.cancel) { job_end(DR_WARN, "Annulé par l'utilisateur", ""); return; }
        size_t n = cmp_size - cmp_pos < sizeof(cbuf_a) ? (size_t)(cmp_size - cmp_pos) : sizeof(cbuf_a);
        if (fread(cbuf_a, 1, n, ca) != n || fread(cbuf_b, 1, n, cb) != n) { job_end(DR_ERR, "Lecture du fichier impossible", ""); return; }
        if (memcmp(cbuf_a, cbuf_b, n) != 0)
            for (size_t o = 0; o < n; o += SECTOR) {
                size_t c = n - o < SECTOR ? n - o : SECTOR;
                if (memcmp(cbuf_a + o, cbuf_b + o, c) == 0) continue;
                u64 abs = cmp_pos + o;
                cmp_dsect++;
                for (size_t k = 0; k < c; k++) if (cbuf_a[o + k] != cbuf_b[o + k]) cmp_dbytes++;
                if (rg_n > 0 && rg_end_last == abs && rg_n <= CMP_MAX) { rg_len[rg_n - 1] += c; rg_end_last = abs + c; }
                else if (rg_n < CMP_MAX) { rg_start[rg_n] = abs; rg_len[rg_n] = c; rg_n++; rg_end_last = abs + c; }
                else { rg_over = 1; rg_end_last = ~0ULL; }
            }
        cmp_pos += n;
        g_djob.done = cmp_pos;
        if (cmp_pos >= cmp_size) { finish_compare(); return; }
    }
}

void dumps_run(int item)
{
    if (g_djob.running || item < 0 || item >= D_COUNT) return;
    g_denv.rundir[0] = 0;
    if (!dumps_dest_ready()) {
        result(item, DR_ERR, strncmp(g_denv.root, "/dev_usb", 8) == 0
               ? "Aucune clé USB branchée : branche-la ou choisis le disque dur (Triangle)"
               : "Emplacement indisponible : %s", g_denv.root);
        return;
    }
    switch (item) {
    case D_ERRLOG: do_errlog(); break;
    case D_AIM:    do_aim(); break;
    case D_VTRM:   do_vtrm(); break;
    case D_SRK:    do_srk(); break;
    case D_PTAB:   do_ptab(); break;
    case D_EEID:   do_eeid(); break;
    case D_APPVER: do_appver(); break;
    case D_TOKEN:  do_token(); break;
    case D_TEMP:   do_temp(); break;
    case D_LV1: { Seg s = { 0x0ULL, 0x1000000ULL, "lv1.bin" }; start_peek_job(D_LV1, 1, &s, 1); break; }
    case D_LV2: {
        Seg s[2] = { { 0x8000000000000000ULL, 0x800000ULL, "lv2.bin.0" },
                     { 0x8000000300000000ULL, 0x80000ULL, "lv2.bin.1" } };
        start_peek_job(D_LV2, 0, s, 2);
        break;
    }
    case D_SYSROM: { Seg s = { 0x2401FC00000ULL, 0x40000ULL, "sysrom.bin" }; start_peek_job(D_SYSROM, 1, &s, 1); break; }
    case D_FLASH:  start_flash_job(); break;
    case D_COMPARE: start_compare_job(); break;
    }
    if (!g_djob.running) cleanup_rundir(item);
}

void dumps_tick(unsigned budget_ms)
{
    if (!g_djob.running) return;
    unsigned t0 = ticks_ms();
    if (g_djob.item == D_FLASH) tick_flash(budget_ms, t0);
    else if (g_djob.item == D_COMPARE) tick_compare(budget_ms, t0);
    else tick_peek(budget_ms, t0);
}

void dumps_result_text(int item, char *out, int n)
{
    const DumpResult *r = &g_dump_res[item];
    if (r->status == DR_NONE) { snprintf(out, n, "%s", T("Pas encore lancé")); return; }
    snprintf(out, n, T(r->msg_fmt), r->msg_arg);
}