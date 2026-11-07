#include "patch.h"
#include "dumpio.h"
#include "sys.h"
#include "prefs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ppu-lv2.h>
#include <lv2/sysfs.h>
#include <sys/systime.h>

#define P(x) ((u64)(uintptr_t)(x))

#define FLASH_NAND  0x0100000000000001ULL
#define FLASH_NOR   0x0100000000000004ULL
#define SECTOR      0x200
#define CHUNK_SECT  16
#define CHUNK       (CHUNK_SECT * SECTOR)          
#define FLAGS_RAW   0x22
#define EEID_SECT   0x80
#define EEID_SIZE   (EEID_SECT * SECTOR)           
#define EEID_CHUNKS (EEID_SECT / CHUNK_SECT)       
#define NOR_SIZE    0x1000000UL                    
#define NOR_SECT    (NOR_SIZE / SECTOR)            
#define NOR_CHUNKS  (NOR_SECT / CHUNK_SECT)        
#define NOR_BOOTLDR 0xFC0000UL                     
#define NOR_EEID    0x2F000UL
#define RECOVER_OFF 0x48C61
#define IDPS_LEN    16                             

static s32 flash_ext_flag(u8 *f) { lv2syscall1(874, P(f)); return_to_user_prog(s32); }
static s32 st_open(u64 dev, u32 *h) { lv2syscall4(600, dev, 0, P(h), 0); return_to_user_prog(s32); }
static s32 st_close(u32 h) { lv2syscall1(601, h); return_to_user_prog(s32); }
static s32 st_read(u32 h, u64 start, u64 count, void *buf, u32 *n, u64 flags)
{ lv2syscall7(602, h, 0, start, count, P(buf), P(n), flags); return_to_user_prog(s32); }
typedef struct {
    u8 res1[32]; u32 vendor_id; u32 device_id; u64 capacity; u32 sector_size; u32 media_count; u8 res2[8];
} __attribute__((packed)) StInfo;
static s32 st_info(u64 dev, StInfo *i) { lv2syscall2(609, dev, P(i)); return_to_user_prog(s32); }
static s32 st_read_exact(u32 h, u64 start, u64 count, void *buf)
{
    u32 n = 0;
    s32 r = st_read(h, start, count, buf, &n, FLAGS_RAW);
    if (r != 0) return r;
    return n == count ? 0 : -2;
}
static s32 um_read_eeprom(u64 off, u8 *v) { lv2syscall7(863, 0x600B, off, P(v), 0, 0, 0, 0); return_to_user_prog(s32); }

static s32 st_write(u32 h, u64 start, u64 count, const void *buf, u32 *n, u64 flags)
{ lv2syscall7(603, h, 0, start, count, P(buf), P(n), flags); return_to_user_prog(s32); }
static s32 st_write_exact(u32 h, u64 start, u64 count, const void *buf)
{
    u32 n = 0;
    s32 r = st_write(h, start, count, buf, &n, FLAGS_RAW);
    if (r != 0) return r;
    return n == count ? 0 : -2;
}

static s32 um_write_recover_flag(u8 v)
{
    if (v != 0x00 && v != 0xFF) return -1;
    lv2syscall7(863, 0x600C, RECOVER_OFF, v, 0, 0, 0, 0);
    return_to_user_prog(s32);
}
static s32 vtrm_init(void) { lv2syscall5(862, 0x2001, 0, 0, 0, 0); return_to_user_prog(s32); }
static s32 sm_shutdown(u64 op) { lv2syscall3(379, op, 0, 0); return_to_user_prog(s32); }
const PatchDef g_patches[P_COUNT] = {
    { "Reboot",
      "Redémarre la console. Rien n'est écrit : c'est l'équivalent du redémarrage du menu, en plus rapide.",
      "Rien (redémarrage seulement)",
      "Aucune condition.", RISK_ORANGE },
    { "Recover Mode Toggle",
      "Active ou désactive le drapeau Recovery (EEPROM du Syscon, 0x48C61). Activé, la console démarre directement dans le menu Recovery, sans la combinaison du bouton Power.",
      "1 octet de l'EEPROM du Syscon (0x48C61 : 0x00 ou 0xFF)",
      "CFW. Pour l'activer : une clé USB avec PS3/UPDATE/PS3UPDAT.PUP (pour pouvoir sortir du mode Recovery).", RISK_ORANGE },
    { "RSOD Fix",
      "Réinitialise la zone VTRM de la flash, cause habituelle de l'écran rouge (RSOD). Si l'écran rouge revient après quelques jours, c'est un problème matériel. Tes anciens trophées et sauvegardes peuvent ne plus fonctionner.",
      "La zone VTRM de la flash (commande VTRM 0x2001)",
      "CFW. Un Dump NAND/NOR vérifié de cette console. À utiliser seulement en cas de RSOD.", RISK_RED },
    { "Flash EEID",
      "Réécrit l'EEID (64 Ko) avec le fichier A_FLASHER/eeid.bin. Le fichier doit venir de CETTE console : l'appli le compare à l'EEID actuel et n'écrit que les blocs qui changent.",
      "L'EEID de la flash (64 Ko)",
      "CFW. A_FLASHER/eeid.bin de cette console. Un Dump NAND/NOR vérifié.", RISK_RED },
    { "Flash NOR",
      "Réécrit la flash NOR (16 Mo) avec le fichier A_FLASHER/flash_nor.bin, pour restaurer un dump. Consoles NOR uniquement. Seuls les blocs qui changent sont écrits ; le bootldr n'est jamais touché.",
      "La flash NOR (16 Mo), sauf le bootldr",
      "Console NOR. CFW. A_FLASHER/flash_nor.bin de cette console, même firmware.", RISK_RED },
};

DumpResult g_patch_res[P_COUNT];
PatchCheck g_pcheck;
PatchJob g_pjob;

static u8 e_cur[EEID_SIZE], e_new[EEID_SIZE], e_tmp[EEID_SIZE];
static u8 c_tmp[CHUNK];
static u8 *n_cur, *n_new;                 
static unsigned short n_diff[NOR_CHUNKS];
static int n_ndiff;
static unsigned char n_tried[NOR_CHUNKS];
static void result(int item, int st, const char *fmt, const char *arg)
{
    DumpResult *r = &g_patch_res[item];
    r->status = st;
    snprintf(r->msg_fmt, sizeof(r->msg_fmt), "%s", fmt);
    snprintf(r->msg_arg, sizeof(r->msg_arg), "%s", arg ? arg : "");
    log_add(st == DR_ERR ? "Modification : %s (échec)" : st == DR_WARN ? "Modification : %s (annulée)" : "Modification : %s (réussie)",
            g_patches[item].name);
}

static void hexcode(char *a, int n, s32 code) { snprintf(a, n, "0x%08X", (unsigned)code); }

static void ck(int st, const char *fmt, const char *arg)
{
    if (st == CK_FAIL) g_pcheck.ready = 0;
    if (g_pcheck.n >= PC_MAX) return;
    CheckLine *l = &g_pcheck.l[g_pcheck.n++];
    l->st = st;
    snprintf(l->fmt, sizeof(l->fmt), "%s", fmt);
    snprintf(l->arg, sizeof(l->arg), "%s", arg ? arg : "");
}

static void plan(const char *fmt, const char *arg)
{
    snprintf(g_pcheck.plan_fmt, sizeof(g_pcheck.plan_fmt), "%s", fmt);
    snprintf(g_pcheck.plan_arg, sizeof(g_pcheck.plan_arg), "%s", arg ? arg : "");
}

static u64 be64(const u8 *p) { u64 v = 0; for (int i = 0; i < 8; i++) v = (v << 8) | p[i]; return v; }
static int idps_valid(const u8 *p) { return (be64(p) & 0xFFFFFFFFFFF0FF00ULL) == 0x0000000100800000ULL; }
typedef struct { u64 dev, eeid; const char *dump; u64 idps_in_dump; } Geo;
static int geo(Geo *g)
{
    if (g_denv.nand < 0) return -1;
    if (g_denv.nand) { g->dev = FLASH_NAND; g->eeid = 0x204; g->dump = "flash_nand.bin"; }
    else             { g->dev = FLASH_NOR;  g->eeid = 0x178; g->dump = "flash_nor.bin"; }
    g->idps_in_dump = g->eeid * SECTOR + 0x70;
    return 0;
}

static int geo_confirmed(void)
{
    u8 flag = 0;
    if (flash_ext_flag(&flag) != 0) return 0;
    return g_denv.nand == ((flag & 1) ? 1 : 0);
}

static s32 read_sectors(u64 dev, u64 start, u64 count, u8 *buf)
{
    u32 h = 0;
    s32 r = st_open(dev, &h);
    if (r != 0) return r;
    for (u64 i = 0; i < count && r == 0; i += CHUNK_SECT) {
        u64 c = count - i < CHUNK_SECT ? count - i : CHUNK_SECT;
        r = st_read_exact(h, start + i, c, buf + i * SECTOR);
    }
    st_close(h);
    return r;
}

static char g_bkdir[220];
static int new_backup_dir(const char *tag)
{
    g_bkdir[0] = 0;
    return dio_new_dir(g_denv.outdir, tag, g_bkdir, sizeof(g_bkdir));
}
static const char *bk_folder(void)
{
    const char *f = strrchr(g_bkdir, '/');
    return f ? f + 1 : g_bkdir;
}

static void write_recovery(const char *kind, const char *bkfile, const char *srcname, const char *hex, int nbad, int first_sector)
{
    static char t[2600];
    int n = snprintf(t, sizeof(t),
        "PS3 Tools Kit - RECUPERATION / RECOVERY\r\n\r\n"
        "[FR] La verification finale (ou le retour en arriere automatique) a echoue pendant %s.\r\n"
        "La flash n'est peut-etre pas dans son etat d'origine (%d bloc(s) differents, premier secteur 0x%X).\r\n"
        "Ta sauvegarde est dans ce dossier : %s\r\nSHA-256 de la sauvegarde : %s\r\n\r\n"
        "Que faire :\r\n"
        " 1. Ne lance aucun jeu et ne change rien. Si possible, ne coupe pas la console.\r\n"
        " 2. Controle la sauvegarde sur un PC : sha256sum -c SHA256SUMS.txt (dans ce dossier).\r\n"
        " 3. Copie %s sous le nom %s dans le dossier A_FLASHER de PS3TK_DUMPS.\r\n"
        " 4. Dumps Patch > Modifications > %s : lance la verification puis confirme. L'outil ne reecrit que les\r\n"
        "    blocs qui different de la sauvegarde.\r\n"
        " 5. Si la verification refuse, ou si la console ne demarre plus : flasheur materiel (NORway, E3, ...)\r\n"
        "    avec ce fichier de sauvegarde. Ne reflashe jamais un fichier dont le SHA-256 ne correspond pas.\r\n\r\n"
        "[EN] The final verification (or the automatic rollback) failed during %s.\r\n"
        "The flash may not be in its original state (%d differing block(s), first sector 0x%X).\r\n"
        "Your backup is in this folder: %s\r\nBackup SHA-256: %s\r\n\r\n"
        "What to do:\r\n"
        " 1. Do not start any game and change nothing. If possible, do not power the console off.\r\n"
        " 2. Check the backup on a PC: sha256sum -c SHA256SUMS.txt (in this folder).\r\n"
        " 3. Copy %s as %s into the A_FLASHER folder of PS3TK_DUMPS.\r\n"
        " 4. Dumps Patch > Modifications > %s: run the checks, then confirm. Only the blocks that differ from the\r\n"
        "    backup are rewritten.\r\n"
        " 5. If the checks refuse, or the console no longer boots: use a hardware flasher (NORway, E3, ...) with\r\n"
        "    this backup. Never flash a file whose SHA-256 does not match.\r\n",
        kind, nbad, (unsigned)first_sector, bk_folder(), hex, bkfile, srcname, kind,
        kind, nbad, (unsigned)first_sector, bk_folder(), hex, bkfile, srcname, kind);
    if (n > (int)sizeof(t)) n = sizeof(t) - 1;
    dio_save_buf(g_bkdir, "RECUPERATION.txt", t, (size_t)n, NULL);
}

static int pup_on_usb(char *where, int n)
{
    for (int i = 0; i < 8; i++) {
        char p[64];
        snprintf(p, sizeof(p), "/dev_usb00%d/PS3/UPDATE/PS3UPDAT.PUP", i);
        if (file_size(p) > 0x1000000LL) { snprintf(where, n, "%s", p); return 1; }
    }
    return 0;
}

typedef struct { char dir[160]; char name[64]; char file[200]; char hex[65]; } BkCand;
#define BK_MAX 6
static BkCand bk_c[BK_MAX];
static int bk_n, bk_i, bk_active, bk_line;
static FILE *bk_f;
static Sha256 bk_sha;
static u64 bk_pos, bk_size;

typedef struct { char root[16]; char name[64]; } BkName;
static BkName bk_names[48];
static int bk_nn;
static const char *bk_root_cur;
static int bk_name_cb(const char *name, int isdir, void *u)
{
    (void)u;
    if (isdir && strstr(name, "_flash") && bk_nn < 48 && strlen(name) < 63) {
        snprintf(bk_names[bk_nn].root, sizeof(bk_names[0].root), "%s", bk_root_cur);
        snprintf(bk_names[bk_nn].name, sizeof(bk_names[0].name), "%s", name);
        bk_nn++;
    }
    return 0;
}
static int bk_desc(const void *a, const void *b) { return strcmp(((const BkName *)b)->name, ((const BkName *)a)->name); }
static int bk_collect(const Geo *g, const u8 *idps)
{
    StInfo si;
    memset(&si, 0, sizeof(si));
    bk_n = 0;
    if (st_info(g->dev, &si) != 0 || si.capacity == 0) return 0;
    long long want = (long long)si.capacity * SECTOR;
    dumps_scan_dests();
    bk_nn = 0;
    for (int i = 0; i < g_ndest; i++) {
        char base[64];
        snprintf(base, sizeof(base), "%s/PS3TK_DUMPS", g_dests[i].root);
        bk_root_cur = g_dests[i].root;
        list_dir(base, bk_name_cb, NULL);
    }
    qsort(bk_names, bk_nn, sizeof(BkName), bk_desc);      
    for (int k = 0; k < bk_nn && bk_n < BK_MAX; k++) {
        BkCand *c = &bk_c[bk_n];
        snprintf(c->dir, sizeof(c->dir), "%s/PS3TK_DUMPS/%s", bk_names[k].root, bk_names[k].name);
        snprintf(c->name, sizeof(c->name), "%s", bk_names[k].name);
        snprintf(c->file, sizeof(c->file), "%s/%s", c->dir, g->dump);
        if (file_size(c->file) != want) continue;
        if (dio_sums_lookup(c->dir, g->dump, c->hex) != 0) continue;
        char info[200], txt[1024] = "", key[96];
        snprintf(info, sizeof(info), "%s/flash_info.txt", c->dir);
        FILE *f = fopen(info, "rb");
        if (!f) continue;
        size_t l = fread(txt, 1, sizeof(txt) - 1, f);
        fclose(f);
        txt[l] = 0;
        snprintf(key, sizeof(key), "SHA256: %s", c->hex);
        if (!strstr(txt, "Verified: YES") || !strstr(txt, key) || !strstr(txt, g_denv.nand ? "Type: NAND" : "Type: NOR")) continue;
        f = fopen(c->file, "rb");
        if (!f) continue;
        u8 id[IDPS_LEN];
        int ok = fseek(f, (long)g->idps_in_dump, SEEK_SET) == 0 && fread(id, 1, IDPS_LEN, f) == IDPS_LEN && memcmp(id, idps, IDPS_LEN) == 0;
        fclose(f);
        if (!ok) continue;
        bk_n++;
    }
    bk_size = (u64)want;
    return bk_n;
}

static int bk_begin_candidate(void)
{
    if (bk_f) { fclose(bk_f); bk_f = NULL; }
    bk_f = fopen(bk_c[bk_i].file, "rb");
    if (!bk_f) return -1;
    sha256_init(&bk_sha);
    bk_pos = 0;
    return 0;
}

static void need_backup(const Geo *g, const u8 *idps)
{
    if (bk_collect(g, idps) == 0) {
        ck(CK_FAIL, "Aucun Dump NAND/NOR vérifié de cette console : fais-le d'abord dans l'onglet Outils", "");
        return;
    }
    bk_line = g_pcheck.n;
    ck(CK_INFO, "Vérification de l'empreinte SHA-256 du dump…", "");
    bk_i = 0;
    while (bk_i < bk_n && bk_begin_candidate() != 0) bk_i++;
    if (bk_i >= bk_n) {
        g_pcheck.l[bk_line].st = CK_FAIL;
        g_pcheck.ready = 0;
        snprintf(g_pcheck.l[bk_line].fmt, sizeof(g_pcheck.l[bk_line].fmt), "%s", "Le dump ne peut pas être relu : refais-le dans l'onglet Outils");
        g_pcheck.l[bk_line].arg[0] = 0;
        return;
    }
    bk_active = 1;
    g_pcheck.running = 1;
    g_pcheck.done_b = 0;
    g_pcheck.total_b = bk_size;
}

static void bk_finish(int ok)
{
    if (bk_f) { fclose(bk_f); bk_f = NULL; }
    bk_active = 0;
    CheckLine *l = &g_pcheck.l[bk_line];
    if (ok) {
        l->st = CK_OK;
        snprintf(l->fmt, sizeof(l->fmt), "%s", "Dump de la flash vérifié (SHA-256 recalculé) : %s");
        snprintf(l->arg, sizeof(l->arg), "%s", bk_c[bk_i].name);
    } else {
        l->st = CK_FAIL;
        g_pcheck.ready = 0;
        snprintf(l->fmt, sizeof(l->fmt), "%s", "Empreinte SHA-256 du dump différente de celle enregistrée : fichier modifié. Refais le Dump NAND/NOR");
        l->arg[0] = 0;
    }
    g_pcheck.running = 0;
    g_pcheck.done = 1;
}

static void tick_bk(unsigned budget, unsigned t0)
{
    static u8 b[0x20000];
    while (bk_active && ticks_ms() - t0 < budget) {
        size_t c = bk_size - bk_pos < sizeof(b) ? (size_t)(bk_size - bk_pos) : sizeof(b);
        if (fread(b, 1, c, bk_f) != c) { bk_finish(0); return; }
        sha256_update(&bk_sha, b, c);
        bk_pos += c;
        g_pcheck.done_b = bk_pos;
        if (bk_pos >= bk_size) {
            uint8_t d[32];
            char hex[65];
            sha256_final(&bk_sha, d);
            sha256_hex(d, hex);
            if (strcmp(hex, bk_c[bk_i].hex) == 0) { bk_finish(1); return; }
            do bk_i++; while (bk_i < bk_n && bk_begin_candidate() != 0);
            if (bk_i >= bk_n) { bk_i = 0; bk_finish(0); return; }
        }
    }
}

const char *patch_src_path(int item)
{
    static char p[128];
    snprintf(p, sizeof(p), "%s/A_FLASHER/%s", g_denv.outdir, item == P_NOR ? "flash_nor.bin" : "eeid.bin");
    return p;
}

void patch_check_cancel(void);

static void free_nor(void)
{
    free(n_cur); n_cur = NULL;
    free(n_new); n_new = NULL;
}

void patch_check_close(void)
{
    if (g_pjob.running) return;
    patch_check_cancel();
    free_nor();
    memset(&g_pcheck, 0, sizeof(g_pcheck));
    g_pcheck.item = -1;
}

void patch_reset(void)
{
    if (g_pjob.running) return;
    patch_check_close();
    memset(&g_pcheck, 0, sizeof(g_pcheck));
    g_pcheck.item = -1;
    memset(g_patch_res, 0, sizeof(g_patch_res));
}

int patch_busy(void) { return g_pjob.running; }
static void check_reboot(void)
{
    ck(CK_INFO, "Rien n'est écrit dans la flash.", "");
    ck(CK_INFO, "La console redémarre dès que tu confirmes.", "");
    plan("Redémarrer la console", "");
}

static void check_recover(void)
{
    u8 v[8];
    memset(v, 0xAA, sizeof(v));
    s32 r = um_read_eeprom(RECOVER_OFF, v);
    g_pcheck.recover_cur = -1;
    char a[48];
    if (r != 0) { hexcode(a, sizeof(a), r); ck(CK_FAIL, "Lecture du drapeau refusée (code %s)", a); return; }
    if (v[0] != 0x00 && v[0] != 0xFF) {
        snprintf(a, sizeof(a), "0x%02X", v[0]);
        ck(CK_FAIL, "Valeur inattendue : %s (seuls 0x00 et 0xFF sont acceptés)", a);
        return;
    }
    g_pcheck.recover_cur = v[0];
    ck(CK_OK, v[0] == 0xFF ? "Drapeau lu : désactivé (0xFF)" : "Drapeau lu : activé (0x00)", "");
    if (v[0] == 0xFF) {
        char w[64];
        if (pup_on_usb(w, sizeof(w))) ck(CK_OK, "Mise à jour trouvée : %s", w);
        else ck(CK_FAIL, "Aucune clé USB avec PS3/UPDATE/PS3UPDAT.PUP : prépare-la d'abord (elle sert à sortir du mode Recovery)", "");
        ck(CK_WARN, "Si Petitboot ou OtherOS++ est installé, n'active pas ce mode.", "");
        plan("Écrire 0x00 dans l'EEPROM (0x48C61) : démarrage en mode Recovery", "");
    } else {
        plan("Écrire 0xFF dans l'EEPROM (0x48C61) : démarrage normal", "");
    }
}

static int current_idps(const Geo *g, u8 *idps)
{
    u8 s[SECTOR];
    s32 r = read_sectors(g->dev, g->eeid, 1, s);
    char a[24];
    if (r != 0) { hexcode(a, sizeof(a), r); ck(CK_FAIL, "Lecture de l'EEID refusée (code %s)", a); return -1; }
    if (!idps_valid(s + 0x70)) { ck(CK_FAIL, "IDPS illisible dans la flash", ""); return -1; }
    memcpy(idps, s + 0x70, IDPS_LEN);
    ck(CK_OK, "IDPS de la console lu dans la flash", "");
    return 0;
}

static void check_rsod(void)
{
    Geo g;
    u8 idps[IDPS_LEN];
    if (geo(&g) != 0) { ck(CK_FAIL, "Type de flash inconnu", ""); return; }
    if (current_idps(&g, idps) != 0) return;
    need_backup(&g, idps);
    ck(CK_WARN, "À utiliser seulement si ta console a un écran rouge (RSOD) ou des erreurs de trophées.", "");
    ck(CK_WARN, "Tes anciens trophées, sauvegardes et dumps peuvent ne plus être valables.", "");
    plan("Réinitialiser la zone VTRM de la flash (commande 0x2001)", "");
}

static void check_eeid(void)
{
    Geo g;
    char a[96];
    if (geo(&g) != 0) { ck(CK_FAIL, "Type de flash inconnu", ""); return; }
    if (!dumps_dest_ready()) { ck(CK_FAIL, "Emplacement indisponible : %s", g_denv.root); return; }
    s32 r = read_sectors(g.dev, g.eeid, EEID_SECT, e_cur);
    if (r != 0) { hexcode(a, sizeof(a), r); ck(CK_FAIL, "Lecture de l'EEID refusée (code %s)", a); return; }
    if (!idps_valid(e_cur + 0x70)) { ck(CK_FAIL, "IDPS illisible dans la flash", ""); return; }
    ck(CK_OK, "EEID actuel lu (64 Ko)", "");

    char dir[128];
    snprintf(dir, sizeof(dir), "%s/A_FLASHER", g_denv.outdir);
    sysFsMkdir(g_denv.outdir, 0777);
    sysFsMkdir(dir, 0777);
    const char *src = patch_src_path(P_EEID);
    long long sz = file_size(src);
    if (sz < 0) { ck(CK_FAIL, "Fichier introuvable : %s", src); return; }
    if (sz != EEID_SIZE) {
        snprintf(a, sizeof(a), "%lld", sz);
        ck(CK_FAIL, "Taille incorrecte : %s octets (65536 attendus)", a);
        return;
    }
    FILE *f = fopen(src, "rb");
    if (!f || fread(e_new, 1, EEID_SIZE, f) != EEID_SIZE) { if (f) fclose(f); ck(CK_FAIL, "Lecture du fichier impossible", ""); return; }
    fclose(f);
    if (memcmp(e_new, e_cur, 0x70) != 0) { ck(CK_FAIL, "En-tête différent de celui de ta console : ce n'est pas l'EEID de cette PS3", ""); return; }
    if (memcmp(e_new + 0x70, e_cur + 0x70, IDPS_LEN) != 0) { ck(CK_FAIL, "IDPS différent : ce fichier vient d'une autre console", ""); return; }
    ck(CK_OK, "Fichier valide et de cette console (en-tête et IDPS identiques)", "");

    need_backup(&g, e_cur + 0x70);

    int bytes = 0, chunks = 0;
    for (int c = 0; c < EEID_CHUNKS; c++) {
        int d = 0;
        for (int i = 0; i < CHUNK; i++) if (e_new[c * CHUNK + i] != e_cur[c * CHUNK + i]) { d = 1; bytes++; }
        chunks += d;
    }
    if (chunks == 0) {
        g_pcheck.nothing = 1;
        ck(CK_OK, "Identique à la flash : rien à écrire", "");
        return;
    }
    snprintf(a, sizeof(a), "%d", bytes);
    ck(CK_INFO, "%s octet(s) différent(s)", a);
    snprintf(a, sizeof(a), "%d", chunks);
    plan(g_denv.nand ? "Écrire %s bloc(s) de 8 Ko sur 8 dans l'EEID (NAND, secteur 0x204)"
                     : "Écrire %s bloc(s) de 8 Ko sur 8 dans l'EEID (NOR, secteur 0x178)", a);
}

static FILE *n_f;
static u32 n_h;
static int n_stage;         
static u64 n_pos;

static void check_nor_start(void)
{
    char a[96];
    if (g_denv.nand < 0) { ck(CK_FAIL, "Type de flash inconnu", ""); return; }
    if (g_denv.nand == 1) { ck(CK_FAIL, "Ta console a une flash NAND : Flash NOR ne s'applique pas", ""); return; }
    StInfo si;
    memset(&si, 0, sizeof(si));
    s32 r = st_info(FLASH_NOR, &si);
    if (r != 0) { hexcode(a, sizeof(a), r); ck(CK_FAIL, "Accès à la flash refusé (code %s)", a); return; }
    if (si.capacity != NOR_SECT) { ck(CK_FAIL, "Taille de la flash NOR inattendue", ""); return; }
    if (!dumps_dest_ready()) { ck(CK_FAIL, "Emplacement indisponible : %s", g_denv.root); return; }

    char dir[128];
    snprintf(dir, sizeof(dir), "%s/A_FLASHER", g_denv.outdir);
    sysFsMkdir(g_denv.outdir, 0777);
    sysFsMkdir(dir, 0777);
    const char *src = patch_src_path(P_NOR);
    long long sz = file_size(src);
    if (sz < 0) { ck(CK_FAIL, "Fichier introuvable : %s", src); return; }
    if (sz != (long long)NOR_SIZE) {
        snprintf(a, sizeof(a), "%lld", sz);
        ck(CK_FAIL, "Taille incorrecte : %s octets (16777216 attendus)", a);
        return;
    }
    n_cur = malloc(NOR_SIZE);
    n_new = malloc(NOR_SIZE);
    if (!n_cur || !n_new) { free_nor(); ck(CK_FAIL, "Mémoire insuffisante", ""); return; }
    n_f = fopen(src, "rb");
    if (!n_f) { free_nor(); ck(CK_FAIL, "Lecture du fichier impossible", ""); return; }
    n_stage = 0; n_pos = 0;
    g_pcheck.running = 1;
    g_pcheck.done_b = 0;
    g_pcheck.total_b = NOR_SIZE * 2;
}

static void nor_analyze(void)
{
    char a[96];
    ck(CK_OK, "Fichier et flash lus (2 × 16 Mo)", "");
    if (memcmp(n_new, n_cur, 0x800) != 0) { ck(CK_FAIL, "En-tête de flash différent : dump inversé (E3) ou abîmé ?", ""); return; }
    if (!idps_valid(n_cur + NOR_EEID + 0x70) || memcmp(n_new + NOR_EEID + 0x70, n_cur + NOR_EEID + 0x70, IDPS_LEN) != 0) {
        ck(CK_FAIL, "IDPS différent : ce fichier vient d'une autre console", "");
        return;
    }
    if (memcmp(n_new + NOR_BOOTLDR, n_cur + NOR_BOOTLDR, NOR_SIZE - NOR_BOOTLDR) != 0) {
        ck(CK_FAIL, "bootldr différent : ce dump ne vient pas de cette console", "");
        return;
    }
    ck(CK_OK, "Fichier de cette console (en-tête, IDPS et bootldr identiques)", "");
    char names[96] = "";
    int ros = 0;
    for (unsigned i = 0; i < (unsigned)g_n_nor_regions; i++)
        if (memcmp(n_new + g_nor_regions[i].off, n_cur + g_nor_regions[i].off, g_nor_regions[i].size) != 0) {
            if (strlen(names) + strlen(g_nor_regions[i].name) + 3 < sizeof(names)) {
                if (names[0]) strcat(names, ", ");
                strcat(names, g_nor_regions[i].name);
            }
            if (g_nor_regions[i].off == 0x0C0000 || g_nor_regions[i].off == 0x7C0000) ros = 1;
        }

    n_ndiff = 0;
    for (int c = 0; c < NOR_CHUNKS; c++)
        if (memcmp(n_new + (size_t)c * CHUNK, n_cur + (size_t)c * CHUNK, CHUNK) != 0) n_diff[n_ndiff++] = (unsigned short)c;
    if (n_ndiff == 0) {
        g_pcheck.nothing = 1;
        ck(CK_OK, "Identique à la flash : rien à écrire", "");
        free_nor();
        return;
    }
    ck(CK_INFO, "Zones modifiées : %s", names);
    if (memcmp(n_new + 0xF20000, n_cur + 0xF20000, 0x20000) != 0)
        ck(CK_WARN, "La zone du disque dur change : si le disque dur a été changé depuis ce dump, la console voudra le formater", "");
    if (ros) ck(CK_WARN, "Le CoreOS (ros0/ros1) change : le dump doit venir du même firmware que celui installé", "");
    char w[64];
    if (pup_on_usb(w, sizeof(w))) ck(CK_OK, "Mise à jour trouvée : %s", w);
    else ck(CK_WARN, "Conseillé : une clé USB avec PS3/UPDATE/PS3UPDAT.PUP de ton CFW", "");
    snprintf(a, sizeof(a), "%d", n_ndiff);
    plan("Écrire %s bloc(s) de 8 Ko sur 2048 (seulement ce qui change)", a);
}

static void tick_nor_check(unsigned budget, unsigned t0)
{
    while (ticks_ms() - t0 < budget) {
        if (n_stage == 0) {
            size_t c = NOR_SIZE - n_pos < 0x80000 ? NOR_SIZE - n_pos : 0x80000;
            if (fread(n_new + n_pos, 1, c, n_f) != c) {
                fclose(n_f); n_f = NULL; free_nor();
                g_pcheck.running = 0; g_pcheck.done = 1;
                ck(CK_FAIL, "Lecture du fichier impossible", "");
                return;
            }
            n_pos += c;
            g_pcheck.done_b = n_pos;
            if (n_pos >= NOR_SIZE) {
                fclose(n_f); n_f = NULL;
                s32 r = st_open(FLASH_NOR, &n_h);
                if (r != 0) {
                    char a[24]; hexcode(a, sizeof(a), r);
                    free_nor(); g_pcheck.running = 0; g_pcheck.done = 1;
                    ck(CK_FAIL, "Accès à la flash refusé (code %s)", a);
                    return;
                }
                n_stage = 1; n_pos = 0;
            }
        } else {
            s32 r = st_read_exact(n_h, n_pos, CHUNK_SECT, n_cur + n_pos * SECTOR);
            if (r != 0) {
                char a[24]; hexcode(a, sizeof(a), r);
                st_close(n_h); free_nor(); g_pcheck.running = 0; g_pcheck.done = 1;
                ck(CK_FAIL, "Lecture de la flash impossible (code %s)", a);
                return;
            }
            n_pos += CHUNK_SECT;
            g_pcheck.done_b = NOR_SIZE + n_pos * SECTOR;
            if (n_pos >= NOR_SECT) {
                st_close(n_h);
                g_pcheck.running = 0;
                nor_analyze();
                g_pcheck.done = 1;
                return;
            }
        }
    }
}

void patch_check_cancel(void)
{
    if (!g_pcheck.running) return;
    int was_bk = bk_active;
    if (bk_active) { if (bk_f) { fclose(bk_f); bk_f = NULL; } bk_active = 0; }
    if (n_f) { fclose(n_f); n_f = NULL; }
    if (n_stage == 1 && !was_bk) st_close(n_h);
    g_pcheck.running = 0;
    free_nor();
    memset(&g_pcheck, 0, sizeof(g_pcheck));
    g_pcheck.item = -1;
}

void patch_check(int item)
{
    if (g_pjob.running || g_pcheck.running || item < 0 || item >= P_COUNT) return;
    free_nor();
    n_stage = 0;
    memset(&g_pcheck, 0, sizeof(g_pcheck));
    g_pcheck.item = item;
    g_pcheck.ready = 1;
    g_pcheck.recover_cur = -1;
    switch (item) {
    case P_REBOOT:  check_reboot(); break;
    case P_RECOVER: check_recover(); break;
    case P_RSOD:    check_rsod(); break;
    case P_EEID:    check_eeid(); break;
    case P_NOR:     check_nor_start(); break;
    }
    if (!g_pcheck.running) g_pcheck.done = 1;
}

static int check_valid_for(int item)
{
    return g_pcheck.item == item && g_pcheck.done && !g_pcheck.running && g_pcheck.ready && !g_pcheck.nothing;
}

static void exec_reboot(int option)
{
    log_add("Modification : %s (lancée)", "Reboot");
    sysUsleep(700000);                     
    s32 r = sm_shutdown(option ? 0x200 : 0x1200);   
    char a[24];
    hexcode(a, sizeof(a), r);
    result(P_REBOOT, DR_ERR, "Redémarrage refusé (code %s)", a);
}

static void exec_recover(void)
{
    u8 v[8];
    char a[24];
    memset(v, 0xAA, sizeof(v));
    s32 r = um_read_eeprom(RECOVER_OFF, v);
    if (r != 0 || v[0] != g_pcheck.recover_cur) { result(P_RECOVER, DR_ERR, "Le drapeau a changé depuis la vérification : relance la vérification", ""); return; }
    u8 target = v[0] == 0xFF ? 0x00 : 0xFF;
    r = um_write_recover_flag(target);
    if (r != 0) { hexcode(a, sizeof(a), r); result(P_RECOVER, DR_ERR, "Écriture refusée par la console (code %s) : rien n'a changé", a); return; }
    memset(v, 0xAA, sizeof(v));
    r = um_read_eeprom(RECOVER_OFF, v);
    if (r != 0 || v[0] != target) {
        snprintf(a, sizeof(a), "0x%02X", v[0]);
        result(P_RECOVER, DR_ERR, "La relecture ne correspond pas (%s) : relance la vérification", a);
        return;
    }
    if (target == 0x00) result(P_RECOVER, DR_OK, "Activé et vérifié : au prochain démarrage, la console ira dans le menu Recovery", "");
    else result(P_RECOVER, DR_OK, "Désactivé et vérifié : la console démarrera normalement", "");
}

static void exec_rsod(void)
{
    s32 r = vtrm_init();
    char a[24];
    hexcode(a, sizeof(a), r);
    char t[160];
    snprintf(t, sizeof(t), "PS3 Tools Kit - RSOD Fix\r\nVTRM init (0x2001): 0x%08X\r\n", (unsigned)r);
    if (new_backup_dir("rsod") == 0) dio_save_buf(g_bkdir, "rsod_fix.log", t, strlen(t), NULL);
    if (r != 0) result(P_RSOD, DR_ERR, "Refusé par la console (code %s) : rien n'a changé", a);
    else result(P_RSOD, DR_OK, "VTRM réinitialisée. Redémarre, puis refais un Dump NAND/NOR", "");
}

static int write_chunk(u32 h, u64 sector, const u8 *data, s32 *code)
{
    s32 r = st_write_exact(h, sector, CHUNK_SECT, data);
    if (r == 0) r = st_read_exact(h, sector, CHUNK_SECT, c_tmp);
    if (r == 0 && memcmp(c_tmp, data, CHUNK) != 0) r = -1;
    if (code) *code = r;
    return r;
}

static void exec_eeid(void)
{
    Geo g;
    char a[96], hex[65];
    if (geo(&g) != 0 || !geo_confirmed()) { result(P_EEID, DR_ERR, "Type de flash non confirmé : rien n'a été écrit", ""); return; }
    s32 r = read_sectors(g.dev, g.eeid, EEID_SECT, e_tmp);
    if (r != 0 || memcmp(e_tmp, e_cur, EEID_SIZE) != 0) { result(P_EEID, DR_ERR, "La flash a changé depuis la vérification : relance la vérification", ""); return; }
    if (new_backup_dir("backup_eeid") != 0 || dio_save_buf(g_bkdir, "eeid_avant_flash.bin", e_cur, EEID_SIZE, hex) != DIO_OK) {
        result(P_EEID, DR_ERR, "Sauvegarde impossible : rien n'a été écrit", "");
        return;
    }

    u32 h = 0;
    r = st_open(g.dev, &h);
    if (r != 0) { hexcode(a, sizeof(a), r); result(P_EEID, DR_ERR, "Accès à la flash refusé (code %s) : rien n'a été écrit", a); return; }
    int tried[EEID_CHUNKS] = { 0 }, failed = 0;
    s32 code = 0;
    for (int c = 0; c < EEID_CHUNKS && !failed; c++) {
        if (memcmp(e_new + c * CHUNK, e_cur + c * CHUNK, CHUNK) == 0) continue;
        tried[c] = 1;
        if (write_chunk(h, g.eeid + (u64)c * CHUNK_SECT, e_new + c * CHUNK, &code) != 0) failed = 1;
    }
    int vfail = 0;
    if (!failed) {
        r = read_sectors(g.dev, g.eeid, EEID_SECT, e_tmp);
        if (r != 0 || memcmp(e_tmp, e_new, EEID_SIZE) != 0) vfail = 1;
    }
    if (!failed && !vfail) {
        st_close(h);
        snprintf(a, sizeof(a), "%s/eeid_avant_flash.bin", bk_folder());
        result(P_EEID, DR_OK, "EEID écrit et vérifié. Sauvegarde : %s", a);
        return;
    }
    int back = 1;
    for (int c = 0; c < EEID_CHUNKS; c++)
        if (tried[c] && write_chunk(h, g.eeid + (u64)c * CHUNK_SECT, e_cur + c * CHUNK, NULL) != 0) back = 0;
    st_close(h);
    r = read_sectors(g.dev, g.eeid, EEID_SECT, e_tmp);
    int nbad = 0, first = -1;
    if (r == 0)
        for (int c = 0; c < EEID_CHUNKS; c++)
            if (memcmp(e_tmp + c * CHUNK, e_cur + c * CHUNK, CHUNK) != 0) { nbad++; if (first < 0) first = c; }
    int restored = back && r == 0 && nbad == 0;
    if (restored) {
        if (failed) { hexcode(a, sizeof(a), code); result(P_EEID, DR_ERR, "Échec de l'écriture (code %s) : l'EEID d'origine a été remis et vérifié", a); }
        else result(P_EEID, DR_ERR, "La relecture finale ne correspond pas : l'EEID d'origine a été remis et vérifié, rien n'a changé", "");
        return;
    }
    write_recovery("Flash EEID", "eeid_avant_flash.bin", "eeid.bin", hex, nbad ? nbad : EEID_CHUNKS, first < 0 ? (int)g.eeid : (int)(g.eeid + first * CHUNK_SECT));
    result(P_EEID, DR_ERR, "ÉCHEC CRITIQUE : l'EEID n'est peut-être pas dans l'état d'origine. N'éteins pas la console et suis RECUPERATION.txt dans %s", bk_folder());
}

static FILE *j_f;                 
static OutFile j_o;               
static int j_o_open;
static char j_hex[65];
static u32 j_h;
static size_t j_pos;
static int j_idx, j_fail, j_back;
static s32 j_code;
static int j_bad, j_vbad;

static void nor_job_end(int st, const char *fmt, const char *arg)
{
    if (j_f) { fclose(j_f); j_f = NULL; }
    if (j_o_open) { dio_abort(&j_o); j_o_open = 0; }   
    if (j_h) { st_close(j_h); j_h = 0; }
    g_pjob.running = 0;
    result(P_NOR, st, fmt, arg);
    free_nor();
    memset(&g_pcheck, 0, sizeof(g_pcheck));
    g_pcheck.item = -1;
}

static void exec_nor_start(void)
{
    if (!n_cur || !n_new || g_denv.nand != 0 || !geo_confirmed()) { result(P_NOR, DR_ERR, "Type de flash non confirmé : rien n'a été écrit", ""); return; }
    if (new_backup_dir("backup_nor") != 0 || dio_open(&j_o, g_bkdir, "flash_nor_avant_flash.bin") != DIO_OK) {
        result(P_NOR, DR_ERR, "Sauvegarde impossible : rien n'a été écrit", "");
        return;
    }
    j_o_open = 1;
    j_f = NULL;
    memset(&g_pjob, 0, sizeof(g_pjob));
    memset(n_tried, 0, sizeof(n_tried));
    g_pjob.running = 1;
    g_pjob.item = P_NOR;
    g_pjob.phase = PH_BACKUP;
    g_pjob.total = NOR_SIZE;
    j_pos = 0; j_idx = 0; j_fail = 0; j_back = 1; j_code = 0; j_bad = 0; j_vbad = 0; j_h = 0;
}

static void tick_nor_job(unsigned budget, unsigned t0)
{
    char a[96];
    while (g_pjob.running && ticks_ms() - t0 < budget) {
        switch (g_pjob.phase) {
        case PH_BACKUP: {
            if (g_pjob.exit_pending || g_pjob.cancel) { nor_job_end(DR_WARN, "Annulé : rien n'a été écrit", ""); return; }
            size_t c = NOR_SIZE - j_pos < 0x40000 ? NOR_SIZE - j_pos : 0x40000;
            if (dio_write(&j_o, n_cur + j_pos, c) != DIO_OK) { nor_job_end(DR_ERR, "Sauvegarde impossible : rien n'a été écrit", ""); return; }
            j_pos += c;
            g_pjob.done = j_pos;
            if (j_pos >= NOR_SIZE) {
                uint8_t d[32];
                if (dio_close(&j_o) != DIO_OK || dio_digest(&j_o, d) != 0) { nor_job_end(DR_ERR, "Sauvegarde impossible : rien n'a été écrit", ""); return; }
                sha256_hex(d, j_hex);
                j_f = fopen(j_o.part, "rb");
                if (!j_f) { nor_job_end(DR_ERR, "Sauvegarde impossible à relire : rien n'a été écrit", ""); return; }
                g_pjob.phase = PH_BACKUPCHK;
                g_pjob.done = 0;
                j_pos = 0;
            }
            break;
        }
        case PH_BACKUPCHK: {
            if (g_pjob.exit_pending || g_pjob.cancel) { nor_job_end(DR_WARN, "Annulé : rien n'a été écrit", ""); return; }
            static u8 bk_tmp[0x40000];
            size_t c = NOR_SIZE - j_pos < sizeof(bk_tmp) ? NOR_SIZE - j_pos : sizeof(bk_tmp);
            if (fread(bk_tmp, 1, c, j_f) != c || memcmp(bk_tmp, n_cur + j_pos, c) != 0) {
                nor_job_end(DR_ERR, "La sauvegarde relue ne correspond pas à la flash : rien n'a été écrit", "");
                return;
            }
            j_pos += c;
            g_pjob.done = j_pos;
            if (j_pos >= NOR_SIZE) {
                int extra = fread(bk_tmp, 1, 1, j_f) != 0 || ferror(j_f);
                fclose(j_f);
                j_f = NULL;
                if (extra) { nor_job_end(DR_ERR, "La sauvegarde relue ne correspond pas à la flash : rien n'a été écrit", ""); return; }
                if (dio_commit(&j_o, j_hex) != DIO_OK) { nor_job_end(DR_ERR, "Sauvegarde impossible : rien n'a été écrit", ""); return; }
                j_o_open = 0;
                s32 r = st_open(FLASH_NOR, &j_h);
                if (r != 0) { j_h = 0; hexcode(a, sizeof(a), r); nor_job_end(DR_ERR, "Accès à la flash refusé (code %s) : rien n'a été écrit", a); return; }
                g_pjob.phase = PH_WRITE;
                g_pjob.done = 0;
                g_pjob.total = (unsigned long long)n_ndiff * CHUNK;
                j_idx = 0;
            }
            break;
        }
        case PH_WRITE: {
            if (j_idx >= n_ndiff) {
                g_pjob.phase = PH_VERIFY;
                g_pjob.done = 0;
                g_pjob.total = NOR_SIZE;
                j_pos = 0;
                j_bad = 0;
                break;
            }
            int c = n_diff[j_idx];
            if ((size_t)c * CHUNK >= NOR_BOOTLDR) {   
                j_fail = 1; j_code = -1;
                g_pjob.phase = PH_ROLLBACK; j_idx = 0;
                break;
            }
            n_tried[c] = 1;
            if (write_chunk(j_h, (u64)c * CHUNK_SECT, n_new + (size_t)c * CHUNK, &j_code) != 0) {
                j_fail = 1;
                g_pjob.phase = PH_ROLLBACK; j_idx = 0;
                g_pjob.done = 0;
                break;
            }
            j_idx++;
            g_pjob.done = (unsigned long long)j_idx * CHUNK;
            break;
        }
        case PH_VERIFY: {
            s32 r = st_read_exact(j_h, j_pos / SECTOR, CHUNK_SECT, c_tmp);
            if (r != 0 || memcmp(c_tmp, n_new + j_pos, CHUNK) != 0) j_bad++;
            j_pos += CHUNK;
            g_pjob.done = j_pos;
            if (j_pos >= NOR_SIZE) {
                if (j_bad) {
                    j_vbad = j_bad;
                    j_fail = 1; j_code = -3;
                    g_pjob.phase = PH_ROLLBACK; j_idx = 0;
                    g_pjob.done = 0;
                } else {
                    snprintf(a, sizeof(a), "%s/flash_nor_avant_flash.bin", bk_folder());
                    nor_job_end(DR_OK, "Flash NOR écrite et vérifiée. Sauvegarde : %s", a);
                    return;
                }
            }
            break;
        }
        case PH_ROLLBACK: {
            while (j_idx < NOR_CHUNKS && !n_tried[j_idx]) j_idx++;
            if (j_idx >= NOR_CHUNKS) {
                g_pjob.phase = PH_RBVERIFY;
                g_pjob.done = 0;
                g_pjob.total = NOR_SIZE;
                j_pos = 0;
                j_bad = 0;
                break;
            }
            if (write_chunk(j_h, (u64)j_idx * CHUNK_SECT, n_cur + (size_t)j_idx * CHUNK, NULL) != 0) j_back = 0;
            j_idx++;
            g_pjob.done = (unsigned long long)j_idx * CHUNK;
            g_pjob.total = NOR_SIZE;
            break;
        }
        case PH_RBVERIFY: {
            s32 r = st_read_exact(j_h, j_pos / SECTOR, CHUNK_SECT, c_tmp);
            if (r != 0 || memcmp(c_tmp, n_cur + j_pos, CHUNK) != 0) {
                if (!j_bad) j_idx = (int)(j_pos / CHUNK);      
                j_bad++;
            }
            j_pos += CHUNK;
            g_pjob.done = j_pos;
            if (j_pos >= NOR_SIZE) {
                if (j_bad == 0 && j_back) {
                    if (j_code == -3) {
                        snprintf(a, sizeof(a), "%d", j_vbad);
                        nor_job_end(DR_ERR, "La vérification finale a échoué (%s bloc(s) différents) : la flash d'origine a été remise et vérifiée, rien n'a changé", a);
                    } else {
                        hexcode(a, sizeof(a), j_code);
                        nor_job_end(DR_ERR, "Échec de l'écriture (code %s) : la flash d'origine a été remise et vérifiée", a);
                    }
                } else {
                    write_recovery("Flash NOR", "flash_nor_avant_flash.bin", "flash_nor.bin", j_hex, j_bad ? j_bad : 1, j_bad ? j_idx * CHUNK_SECT : 0);
                    nor_job_end(DR_ERR, "ÉCHEC CRITIQUE : la flash n'est peut-être pas dans l'état d'origine. N'éteins pas la console et suis RECUPERATION.txt dans %s", bk_folder());
                }
                return;
            }
            break;
        }
        }
    }
}

void patch_execute(int item, int option)
{
    if (g_pjob.running || g_pcheck.running || item < 0 || item >= P_COUNT) return;
    if (item != P_REBOOT && !check_valid_for(item)) return;   
    switch (item) {
    case P_REBOOT:  exec_reboot(option); break;
    case P_RECOVER: exec_recover(); break;
    case P_RSOD:    exec_rsod(); break;
    case P_EEID:    exec_eeid(); break;
    case P_NOR:     exec_nor_start(); break;
    }
    if (!(item == P_NOR && g_pjob.running)) {
        if (item == P_NOR) free_nor();
        memset(&g_pcheck, 0, sizeof(g_pcheck));
        g_pcheck.item = -1;
    }
}

void patch_tick(unsigned budget_ms)
{
    unsigned t0 = ticks_ms();
    if (g_pcheck.running && bk_active) tick_bk(budget_ms, t0);
    else if (g_pcheck.running) tick_nor_check(budget_ms, t0);
    if (g_pjob.running) tick_nor_job(budget_ms, t0);
}

void patch_result_text(int item, char *out, int n)
{
    const DumpResult *r = &g_patch_res[item];
    if (r->status == DR_NONE) { snprintf(out, n, "%s", T("Pas encore lancé")); return; }
    snprintf(out, n, T(r->msg_fmt), r->msg_arg);
}

void patch_line_text(const CheckLine *l, char *out, int n)
{
    snprintf(out, n, T(l->fmt), l->arg);
}

void patch_plan_text(char *out, int n)
{
    snprintf(out, n, T(g_pcheck.plan_fmt), g_pcheck.plan_arg);
}