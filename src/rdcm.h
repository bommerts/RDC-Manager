/*
 * RDC Manager - shared declarations for the core (non-UI) code.
 * Pure C, Win32 only.
 */
#ifndef RDCM_H
#define RDCM_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <windowsx.h>
#include <objbase.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wincred.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <tlhelp32.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <strsafe.h>

#define RDCM_NAME     L"RDC Manager"
#define RDCM_VERSION  L"1.1.0"
#define RDCM_URL      L"https://github.com/bommerts/RDC-Manager"

/* The registry key (under HKEY_CURRENT_USER) where mstsc keeps its per-user data. */
#define TSC_CANON     L"Software\\Microsoft\\Terminal Server Client"

#define NAME_MAX_CCH  260
#define PATH_CCH      1024
#define MRU_MAX       10
#define ARRAY_LEN(a)  (sizeof(a) / sizeof((a)[0]))

/* ------------------------------------------------------------------ env */

/*
 * Every location the app touches goes through this struct, so demo mode and
 * the tests can point the whole app at a sandbox instead of the real data.
 */
typedef struct Env {
    BOOL    sandbox;                /* TRUE in demo mode and in tests */
    wchar_t regRoot[400];           /* HKCU-relative key standing in for TSC_CANON */
    wchar_t credKey[400];           /* sandbox only: HKCU key holding fake credentials */
    wchar_t rdpPath[PATH_CCH];      /* Default.rdp */
    wchar_t cacheDir[PATH_CCH];     /* bitmap cache folder */
    wchar_t jumpAuto[PATH_CCH];     /* taskbar jump list files */
    wchar_t jumpCustom[PATH_CCH];
    wchar_t backupDir[PATH_CCH];
} Env;

extern Env g_env;

void Env_InitReal(void);
void Env_InitSandbox(const wchar_t *regBase, const wchar_t *dirBase);

/* ----------------------------------------------------------------- util */

/* These never return NULL: the app exits if memory runs out. */
_Ret_notnull_ void    *xalloc(size_t bytes);    /* zero-filled */
_Ret_notnull_ void    *xrealloc(void *p, size_t bytes);
_Ret_notnull_ wchar_t *xwcsdup(const wchar_t *s);

typedef struct WStr { wchar_t *p; size_t len, cap; } WStr;
void ws_free(WStr *s);
void ws_clear(WStr *s);                         /* empties the string; s->p stays valid */
void ws_appendn(WStr *s, const wchar_t *t, size_t n);
void ws_append(WStr *s, const wchar_t *t);
void ws_putc(WStr *s, wchar_t c);
void ws_appendf(WStr *s, const wchar_t *fmt, ...);

BOOL str_ieq(const wchar_t *a, const wchar_t *b);
BOOL str_istarts(const wchar_t *s, const wchar_t *prefix);
void str_trim(wchar_t *s);
void path_join(wchar_t *out, size_t cch, const wchar_t *dir, const wchar_t *leaf);

BOOL file_exists(const wchar_t *path);
BOOL file_size(const wchar_t *path, ULONGLONG *size);
BOOL dir_ensure(const wchar_t *path);
BOOL file_read_all(const wchar_t *path, BYTE **data, DWORD *size);
BOOL file_write_atomic(const wchar_t *path, const void *data, DWORD size);
_Ret_notnull_ wchar_t *text_decode(const BYTE *data, DWORD size, BOOL *wasUtf16);
BOOL text_write_utf16(const wchar_t *path, const wchar_t *text, size_t cch);

void format_size(ULONGLONG bytes, wchar_t *out, int cch);
void format_filetime(const FILETIME *ft, wchar_t *out, int cch);
BOOL clipboard_set(HWND owner, const wchar_t *text);

/* ---------------------------------------------------------------- model */

typedef struct Host {
    wchar_t name[NAME_MAX_CCH];
    int     mru;                    /* position in the dropdown (0 = top), or -1 */
    BOOL    hasUser;
    wchar_t user[NAME_MAX_CCH];     /* Servers\<name>\UsernameHint */
    BOOL    hasCert;                /* Servers\<name>\CertHash */
    BOOL    hasDevices;             /* LocalDevices\<name> */
    DWORD   devices;
    BOOL    hasCred;                /* Credential Manager: TERMSRV/<name> */
    wchar_t credUser[NAME_MAX_CCH];
} Host;

typedef struct HostList { Host *v; int n, cap; } HostList;

typedef struct CredInfo {
    wchar_t host[NAME_MAX_CCH];
    wchar_t user[NAME_MAX_CCH];
} CredInfo;

LSTATUS Reg_OpenSub(const wchar_t *sub, REGSAM sam, BOOL create, HKEY *out);
BOOL    Reg_GetSz(HKEY k, const wchar_t *name, wchar_t *out, DWORD cch);
void    Reg_FormatValue(DWORD type, const BYTE *data, DWORD cb, wchar_t *out, int cch);
const wchar_t *Reg_TypeName(DWORD type);

void  Model_Load(HostList *list);
void  Model_Free(HostList *list);
Host *Model_Find(HostList *list, const wchar_t *name);

int  Mru_Read(wchar_t names[MRU_MAX][NAME_MAX_CCH]);
BOOL Mru_Write(wchar_t names[][NAME_MAX_CCH], int count);

BOOL Host_AddToMru(const wchar_t *host);        /* puts it at the top */
BOOL Host_RemoveFromMru(const wchar_t *host);
BOOL Host_MoveMru(const wchar_t *host, int delta);
BOOL Host_MoveMruTo(const wchar_t *host, int to);
BOOL Host_SetUser(const wchar_t *host, const wchar_t *user);
BOOL Host_ClearUser(const wchar_t *host);
BOOL Host_ClearCert(const wchar_t *host);
BOOL Host_ClearDevices(const wchar_t *host);
BOOL Host_DeleteCred(const wchar_t *host);
void Host_RemoveAll(const wchar_t *host);

int  Creds_List(CredInfo **out);                /* caller frees *out */

/* -------------------------------------------------------------- regfile */

typedef struct BackupInfo {
    wchar_t   path[PATH_CCH];
    wchar_t   file[200];
    wchar_t   kind[32];             /* auto, manual or prerestore */
    BOOL      isRdp;                /* a copy of Default.rdp, not a .reg export */
    FILETIME  time;
    ULONGLONG size;
} BackupInfo;

BOOL RegFile_Export(const wchar_t *path);
BOOL RegFile_Import(const wchar_t *path, BOOL replaceManaged,
                    int *keys, int *values, wchar_t *err, size_t cchErr);

BOOL Backup_Create(const wchar_t *kind, BOOL reg, BOOL rdp);
BOOL Backup_BeforeRegChange(void);
BOOL Backup_BeforeRdpChange(void);
int  Backup_List(BackupInfo **out);             /* newest first; caller frees *out */
BOOL Backup_Restore(const BackupInfo *b, wchar_t *err, size_t cchErr);

/* -------------------------------------------------------------- rdpfile */

typedef struct RdpLine {
    wchar_t *name;                  /* NULL when the line couldn't be parsed */
    wchar_t  type;                  /* 'i', 's' or 'b' */
    wchar_t *value;                 /* the raw line when name is NULL */
} RdpLine;

typedef struct RdpFile {
    RdpLine *v;
    int      n, cap;
    BOOL     exists;
} RdpFile;

BOOL Rdp_Load(RdpFile *f);
void Rdp_Free(RdpFile *f);
BOOL Rdp_Save(const RdpFile *f);
int  Rdp_Find(const RdpFile *f, const wchar_t *name);
void Rdp_Set(RdpFile *f, const wchar_t *name, wchar_t type, const wchar_t *value);
void Rdp_Delete(RdpFile *f, int index);
const wchar_t *Rdp_Describe(const wchar_t *name);
wchar_t        Rdp_KnownType(const wchar_t *name);

/* -------------------------------------------------------------- cleanup */

typedef struct DirStat { int files; ULONGLONG bytes; } DirStat;

void Cache_Stat(DirStat *st);
void Cache_Clear(int *deleted, int *failed);
BOOL Jump_Stat(ULONGLONG *bytes);               /* TRUE when a jump list file exists */
BOOL Jump_Clear(void);
BOOL File_Recycle(const wchar_t *path);
BOOL Mstsc_IsRunning(void);

#endif /* RDCM_H */
