/*
 * RDC Manager - demo mode. Builds a sandbox full of sample data so the app
 * can be tried (and screenshotted) without touching real Remote Desktop
 * history. Everything is removed again on exit.
 */
#include "ui.h"

#define DEMO_REG_BASE L"Software\\RDC Manager Demo"

typedef struct DemoHost {
    const wchar_t *name;
    const wchar_t *user;        /* username hint, or NULL */
    BOOL           cert;
    DWORD          devices;     /* device prompt answer, or 0 for none */
    const wchar_t *cred;        /* saved credential username, or NULL */
    BOOL           inDropdown;
} DemoHost;

static const DemoHost kHosts[] = {
    { L"fileserver01",             L"CONTOSO\\alex",   TRUE,  0,    L"CONTOSO\\alex", TRUE  },
    { L"build-agent-07",           L"buildsvc",        TRUE,  0x4C, NULL,             TRUE  },
    { L"10.0.20.15",               L"administrator",   TRUE,  0,    L"administrator", TRUE  },
    { L"accounting-pc",            L"CONTOSO\\morgan", TRUE,  0,    NULL,             TRUE  },
    { L"lab-hyperv.contoso.local", L"CONTOSO\\alex",   TRUE,  0x44, L"CONTOSO\\alex", TRUE  },
    { L"kiosk-lobby",              NULL,               FALSE, 0,    NULL,             TRUE  },
    { L"old-laptop",               L"sam",             TRUE,  0,    NULL,             FALSE },
    { L"192.168.1.50",             NULL,               TRUE,  0,    L"pi",            FALSE },
};

static const wchar_t kDefaultRdp[] =
    L"screen mode id:i:2\r\n"
    L"use multimon:i:0\r\n"
    L"desktopwidth:i:1920\r\n"
    L"desktopheight:i:1080\r\n"
    L"session bpp:i:32\r\n"
    L"winposstr:s:0,1,320,120,1620,960\r\n"
    L"compression:i:1\r\n"
    L"keyboardhook:i:2\r\n"
    L"audiocapturemode:i:0\r\n"
    L"videoplaybackmode:i:1\r\n"
    L"connection type:i:7\r\n"
    L"networkautodetect:i:1\r\n"
    L"bandwidthautodetect:i:1\r\n"
    L"displayconnectionbar:i:1\r\n"
    L"disable wallpaper:i:0\r\n"
    L"allow font smoothing:i:1\r\n"
    L"allow desktop composition:i:1\r\n"
    L"bitmapcachepersistenable:i:1\r\n"
    L"full address:s:fileserver01\r\n"
    L"audiomode:i:0\r\n"
    L"redirectprinters:i:1\r\n"
    L"redirectclipboard:i:1\r\n"
    L"redirectsmartcards:i:1\r\n"
    L"drivestoredirect:s:\r\n"
    L"autoreconnection enabled:i:1\r\n"
    L"authentication level:i:2\r\n"
    L"prompt for credentials:i:0\r\n"
    L"negotiate security layer:i:1\r\n"
    L"gatewayusagemethod:i:4\r\n"
    L"gatewayprofileusagemethod:i:0\r\n";

static wchar_t s_dir[PATH_CCH];

static void set_sz(HKEY root, const wchar_t *key, const wchar_t *name, const wchar_t *value)
{
    HKEY k;
    if (RegCreateKeyExW(root, key, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS) return;
    RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)value, (DWORD)((wcslen(value) + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
}

static void set_raw(HKEY root, const wchar_t *key, const wchar_t *name, DWORD type, const void *data, DWORD cb)
{
    HKEY k;
    if (RegCreateKeyExW(root, key, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS) return;
    RegSetValueExW(k, name, 0, type, (const BYTE *)data, cb);
    RegCloseKey(k);
}

/* Creates a file of the given size without writing its contents. */
static void make_file(const wchar_t *path, LONG megabytes)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER size;

    if (h == INVALID_HANDLE_VALUE) return;
    size.QuadPart = (LONGLONG)megabytes * 1024 * 1024;
    if (SetFilePointerEx(h, size, NULL, FILE_BEGIN)) SetEndOfFile(h);
    CloseHandle(h);
}

static void remove_sandbox(void)
{
    wchar_t from[PATH_CCH + 2];
    SHFILEOPSTRUCTW op;

    RegDeleteTreeW(HKEY_CURRENT_USER, DEMO_REG_BASE);
    RegDeleteKeyW(HKEY_CURRENT_USER, DEMO_REG_BASE);

    if (!s_dir[0] || GetFileAttributesW(s_dir) == INVALID_FILE_ATTRIBUTES) return;
    ZeroMemory(from, sizeof(from));
    StringCchCopyW(from, PATH_CCH, s_dir);
    ZeroMemory(&op, sizeof(op));
    op.wFunc = FO_DELETE;
    op.pFrom = from;
    op.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    SHFileOperationW(&op);
}

void Demo_Setup(void)
{
    wchar_t tmp[PATH_CCH], key[600], path[PATH_CCH], mru[16];
    BYTE cert[20];
    size_t i;
    int next = 0, b;

    GetTempPathW(ARRAY_LEN(tmp), tmp);
    GetLongPathNameW(tmp, tmp, ARRAY_LEN(tmp));         /* C:\Users\LONGNA~1 -> the full name */
    path_join(s_dir, ARRAY_LEN(s_dir), tmp, L"RDC Manager Demo");
    remove_sandbox();
    Env_InitSandbox(DEMO_REG_BASE, s_dir);
    dir_ensure(s_dir);
    dir_ensure(g_env.cacheDir);

    for (i = 0; i < ARRAY_LEN(kHosts); i++) {
        const DemoHost *h = &kHosts[i];

        if (h->inDropdown) {
            StringCchPrintfW(key, ARRAY_LEN(key), L"%s\\Default", g_env.regRoot);
            StringCchPrintfW(mru, ARRAY_LEN(mru), L"MRU%d", next++);
            set_sz(HKEY_CURRENT_USER, key, mru, h->name);
        }
        StringCchPrintfW(key, ARRAY_LEN(key), L"%s\\Servers\\%s", g_env.regRoot, h->name);
        if (h->user) set_sz(HKEY_CURRENT_USER, key, L"UsernameHint", h->user);
        if (h->cert) {
            for (b = 0; b < (int)sizeof(cert); b++) cert[b] = (BYTE)(17 * (i + 3) + 31 * b);
            set_raw(HKEY_CURRENT_USER, key, L"CertHash", REG_BINARY, cert, sizeof(cert));
        }
        if (h->devices) {
            StringCchPrintfW(key, ARRAY_LEN(key), L"%s\\LocalDevices", g_env.regRoot);
            set_raw(HKEY_CURRENT_USER, key, h->name, REG_DWORD, &h->devices, sizeof(DWORD));
        }
        if (h->cred) set_sz(HKEY_CURRENT_USER, g_env.credKey, h->name, h->cred);
    }

    text_write_utf16(g_env.rdpPath, kDefaultRdp, ARRAY_LEN(kDefaultRdp) - 1);
    path_join(path, ARRAY_LEN(path), g_env.cacheDir, L"Cache0000.bin");
    make_file(path, 48);
    path_join(path, ARRAY_LEN(path), g_env.cacheDir, L"Cache0001.bin");
    make_file(path, 36);
    path_join(path, ARRAY_LEN(path), g_env.cacheDir, L"bcache24.bmc");
    make_file(path, 0);
    file_write_atomic(g_env.jumpAuto, kDefaultRdp, (DWORD)sizeof(kDefaultRdp));
}

void Demo_Teardown(void)
{
    remove_sandbox();
}
