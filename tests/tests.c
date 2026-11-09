/*
 * RDC Manager - tests for the core code.
 *
 * Everything runs against a throwaway registry key and temp folder, never the
 * real Remote Desktop data.
 */
#include "../src/rdcm.h"
#include <stdio.h>

static int g_checks, g_failed;
static wchar_t g_regBase[200], g_dirBase[PATH_CCH];

#define CHECK(cond) do { \
        g_checks++; \
        if (!(cond)) { g_failed++; wprintf(L"  FAIL %hs:%d  %hs\n", __FILE__, __LINE__, #cond); } \
    } while (0)

/* ---------------------------------------------------------------- setup */

static void set_sz(const wchar_t *sub, const wchar_t *name, const wchar_t *val)
{
    HKEY k;
    if (Reg_OpenSub(sub, KEY_SET_VALUE, TRUE, &k) != ERROR_SUCCESS) return;
    RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)val, (DWORD)((wcslen(val) + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
}

static void set_raw(const wchar_t *sub, const wchar_t *name, DWORD type, const void *data, DWORD cb)
{
    HKEY k;
    if (Reg_OpenSub(sub, KEY_SET_VALUE, TRUE, &k) != ERROR_SUCCESS) return;
    RegSetValueExW(k, name, 0, type, (const BYTE *)data, cb);
    RegCloseKey(k);
}

static void set_dword(const wchar_t *sub, const wchar_t *name, DWORD v)
{
    set_raw(sub, name, REG_DWORD, &v, sizeof(v));
}

static void set_cred(const wchar_t *host, const wchar_t *user)
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, g_env.credKey, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL)
            != ERROR_SUCCESS) return;
    RegSetValueExW(k, host, 0, REG_SZ, (const BYTE *)user, (DWORD)((wcslen(user) + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
}

static BOOL key_exists(const wchar_t *sub)
{
    HKEY k;
    if (Reg_OpenSub(sub, KEY_READ, FALSE, &k) != ERROR_SUCCESS) return FALSE;
    RegCloseKey(k);
    return TRUE;
}

static BOOL value_exists(const wchar_t *sub, const wchar_t *name)
{
    HKEY k;
    BOOL ok;
    if (Reg_OpenSub(sub, KEY_READ, FALSE, &k) != ERROR_SUCCESS) return FALSE;
    ok = RegQueryValueExW(k, name, NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
    RegCloseKey(k);
    return ok;
}

static void delete_dir(const wchar_t *dir)
{
    wchar_t from[PATH_CCH + 2];
    SHFILEOPSTRUCTW op;
    ZeroMemory(from, sizeof(from));
    StringCchCopyW(from, PATH_CCH, dir);
    ZeroMemory(&op, sizeof(op));
    op.wFunc = FO_DELETE;
    op.pFrom = from;
    op.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    SHFileOperationW(&op);
}

/* Starts every test from an empty sandbox. */
static void reset(void)
{
    RegDeleteTreeW(HKEY_CURRENT_USER, g_regBase);
    RegDeleteKeyW(HKEY_CURRENT_USER, g_regBase);
    delete_dir(g_dirBase);
    Env_InitSandbox(g_regBase, g_dirBase);
    dir_ensure(g_dirBase);
}

static void seed(void)
{
    static const BYTE cert[20] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 };

    set_sz(L"Default", L"MRU0", L"alpha");
    set_sz(L"Default", L"MRU1", L"beta");
    set_sz(L"Default", L"MRU3", L"10.0.0.7");       /* MRU2 left as a gap on purpose */
    set_sz(L"Default", L"MRU4", L"ALPHA");          /* duplicate of MRU0 */
    set_dword(L"Default", L"SomethingElse", 7);

    set_sz(L"Servers\\alpha", L"UsernameHint", L"CONTOSO\\alex");
    set_raw(L"Servers\\alpha", L"CertHash", REG_BINARY, cert, sizeof(cert));
    set_raw(L"Servers\\beta", L"CertHash", REG_BINARY, cert, sizeof(cert));
    set_sz(L"Servers\\orphan", L"UsernameHint", L"sam");

    set_dword(L"LocalDevices", L"beta", 0x4C);
    set_dword(L"LocalDevices", L"gamma", 0x44);

    set_cred(L"alpha", L"CONTOSO\\alex");
    set_cred(L"delta", L"root");
}

/* ---------------------------------------------------------------- tests */

static void test_load(void)
{
    HostList l = { 0 };
    Host *h;

    wprintf(L"model: load\n");
    reset();
    seed();
    Model_Load(&l);

    CHECK(l.n == 6);                                /* alpha beta 10.0.0.7 orphan gamma delta */
    CHECK(l.n >= 3 && str_ieq(l.v[0].name, L"alpha") && l.v[0].mru == 0);
    CHECK(l.n >= 3 && str_ieq(l.v[1].name, L"beta") && l.v[1].mru == 1);
    CHECK(l.n >= 3 && str_ieq(l.v[2].name, L"10.0.0.7") && l.v[2].mru == 2);

    h = Model_Find(&l, L"ALPHA");
    CHECK(h && h->hasUser && wcscmp(h->user, L"CONTOSO\\alex") == 0);
    CHECK(h && h->hasCert && h->hasCred && !h->hasDevices);
    CHECK(h && wcscmp(h->credUser, L"CONTOSO\\alex") == 0);

    h = Model_Find(&l, L"beta");
    CHECK(h && !h->hasUser && h->hasCert && h->hasDevices && h->devices == 0x4C && !h->hasCred);

    h = Model_Find(&l, L"orphan");
    CHECK(h && h->mru == -1 && h->hasUser && !h->hasCert);
    h = Model_Find(&l, L"gamma");
    CHECK(h && h->mru == -1 && h->hasDevices && h->devices == 0x44);
    h = Model_Find(&l, L"delta");
    CHECK(h && h->mru == -1 && h->hasCred && wcscmp(h->credUser, L"root") == 0);
    CHECK(Model_Find(&l, L"nobody") == NULL);

    Model_Free(&l);

    /* a machine that has never used Remote Desktop */
    reset();
    Model_Load(&l);
    CHECK(l.n == 0);
    Model_Free(&l);
}

static void test_mru(void)
{
    wchar_t m[MRU_MAX][NAME_MAX_CCH];
    wchar_t name[32];
    int n, i;

    wprintf(L"model: dropdown list\n");
    reset();
    seed();

    n = Mru_Read(m);
    CHECK(n == 3);

    CHECK(Host_RemoveFromMru(L"BETA"));
    n = Mru_Read(m);
    CHECK(n == 2 && str_ieq(m[0], L"alpha") && str_ieq(m[1], L"10.0.0.7"));
    CHECK(value_exists(L"Default", L"MRU0") && value_exists(L"Default", L"MRU1"));
    CHECK(!value_exists(L"Default", L"MRU2") && !value_exists(L"Default", L"MRU3"));
    CHECK(!value_exists(L"Default", L"MRU4"));
    CHECK(value_exists(L"Default", L"SomethingElse"));      /* unrelated values are left alone */

    CHECK(Host_RemoveFromMru(L"not-there"));
    CHECK(Mru_Read(m) == 2);

    CHECK(Host_AddToMru(L"newbox"));
    n = Mru_Read(m);
    CHECK(n == 3 && str_ieq(m[0], L"newbox") && str_ieq(m[1], L"alpha"));

    CHECK(Host_AddToMru(L"10.0.0.7"));                       /* already there: moves to the top */
    n = Mru_Read(m);
    CHECK(n == 3 && str_ieq(m[0], L"10.0.0.7") && str_ieq(m[1], L"newbox") && str_ieq(m[2], L"alpha"));

    CHECK(Host_MoveMru(L"alpha", -1));
    n = Mru_Read(m);
    CHECK(n == 3 && str_ieq(m[1], L"alpha") && str_ieq(m[2], L"newbox"));
    CHECK(Host_MoveMru(L"alpha", 1));
    CHECK(!Host_MoveMru(L"10.0.0.7", -1));                   /* already at the top */
    CHECK(!Host_MoveMru(L"alpha", 1));                       /* already at the bottom */
    CHECK(!Host_MoveMru(L"not-there", 1));
    CHECK(!Host_AddToMru(L""));

    /* the dropdown holds ten entries; the oldest falls off */
    for (i = 0; i < 12; i++) {
        StringCchPrintfW(name, ARRAY_LEN(name), L"pc%02d", i);
        CHECK(Host_AddToMru(name));
    }
    n = Mru_Read(m);
    CHECK(n == MRU_MAX && str_ieq(m[0], L"pc11") && str_ieq(m[9], L"pc02"));

    CHECK(Mru_Write(NULL, 0));
    CHECK(Mru_Read(m) == 0);
    CHECK(value_exists(L"Default", L"SomethingElse"));
}

static void test_host_ops(void)
{
    HostList l = { 0 };
    Host *h;

    wprintf(L"model: per-computer data\n");
    reset();
    seed();

    /* clearing the username keeps the key while the certificate is still there */
    CHECK(Host_ClearUser(L"alpha"));
    CHECK(key_exists(L"Servers\\alpha") && !value_exists(L"Servers\\alpha", L"UsernameHint"));
    CHECK(Host_ClearCert(L"alpha"));
    CHECK(!key_exists(L"Servers\\alpha"));                   /* empty key is removed */
    CHECK(Host_ClearCert(L"alpha"));                         /* doing it again is fine */

    CHECK(Host_SetUser(L"brandnew", L"me"));
    CHECK(value_exists(L"Servers\\brandnew", L"UsernameHint"));
    CHECK(Host_SetUser(L"brandnew", L""));                   /* empty username = forget */
    CHECK(!key_exists(L"Servers\\brandnew"));
    CHECK(!Host_SetUser(L"bad\\name", L"me"));               /* can't escape the Servers key */
    CHECK(!key_exists(L"Servers\\bad"));

    CHECK(Host_ClearDevices(L"beta"));
    CHECK(!value_exists(L"LocalDevices", L"beta") && value_exists(L"LocalDevices", L"gamma"));

    CHECK(Host_DeleteCred(L"alpha"));
    CHECK(!Host_DeleteCred(L"alpha"));

    Host_RemoveAll(L"beta");
    Host_RemoveAll(L"delta");
    Model_Load(&l);
    CHECK(Model_Find(&l, L"beta") == NULL);
    CHECK(Model_Find(&l, L"delta") == NULL);
    h = Model_Find(&l, L"alpha");
    CHECK(h && h->mru == 0 && !h->hasUser && !h->hasCert && !h->hasCred);
    CHECK(Model_Find(&l, L"orphan") != NULL);
    Model_Free(&l);
}

static wchar_t *read_text(const wchar_t *path)
{
    BYTE *data = NULL;
    DWORD size = 0;
    wchar_t *t;
    if (!file_read_all(path, &data, &size)) return xwcsdup(L"");
    t = text_decode(data, size, NULL);
    free(data);
    return t;
}

static void write_text(const wchar_t *path, const wchar_t *text)
{
    text_write_utf16(path, text, wcslen(text));
}

static void test_regfile(void)
{
    static const wchar_t multi[] = L"one\0two\0";
    BYTE big[70];
    wchar_t a[PATH_CCH], b[PATH_CCH], bad[PATH_CCH], err[256];
    wchar_t *ta, *tb;
    int keys = 0, values = 0, i;
    HKEY k;

    wprintf(L"regfile: export, import, round trip\n");
    reset();
    seed();
    for (i = 0; i < (int)sizeof(big); i++) big[i] = (BYTE)(i * 3);
    set_raw(L"Servers\\alpha", L"Big", REG_BINARY, big, sizeof(big));
    set_raw(L"Servers\\alpha", L"Empty", REG_BINARY, big, 0);
    set_raw(L"Default\\AddIns\\Thing", L"List", REG_MULTI_SZ, multi, sizeof(multi));
    set_sz(L"Default\\AddIns\\Thing", L"Tricky \"name\" with \\ slash", L"C:\\path\\with \"quotes\"");
    set_sz(L"Default\\AddIns\\Thing", L"", L"default value");
    set_sz(L"Default\\AddIns\\Thing", L"Blank", L"");
    set_sz(L"Default\\AddIns\\Thing", L"Lines", L"first\r\nsecond");
    set_raw(L"Default\\AddIns\\Thing", L"Expand", REG_EXPAND_SZ, L"%TEMP%", 14);

    path_join(a, ARRAY_LEN(a), g_dirBase, L"a.reg");
    path_join(b, ARRAY_LEN(b), g_dirBase, L"b.reg");
    path_join(bad, ARRAY_LEN(bad), g_dirBase, L"bad.reg");

    CHECK(RegFile_Export(a));
    ta = read_text(a);
    CHECK(str_istarts(ta, L"Windows Registry Editor Version 5.00"));
    CHECK(wcsstr(ta, L"[HKEY_CURRENT_USER\\Software\\Microsoft\\Terminal Server Client\\Servers\\alpha]") != NULL);
    CHECK(wcsstr(ta, L"\"UsernameHint\"=\"CONTOSO\\\\alex\"") != NULL);
    CHECK(wcsstr(ta, L"\"beta\"=dword:0000004c") != NULL);
    CHECK(wcsstr(ta, L"\"CertHash\"=hex:01,02,03") != NULL);
    CHECK(wcsstr(ta, g_regBase) == NULL);                    /* sandbox path never leaks into the file */

    /* wreck the registry, then restore from the file */
    Host_RemoveAll(L"alpha");
    Host_RemoveAll(L"beta");
    Host_AddToMru(L"added-later");
    Host_SetUser(L"added-later", L"someone");
    set_dword(L"LocalDevices", L"added-later", 1);

    CHECK(RegFile_Import(a, TRUE, &keys, &values, err, ARRAY_LEN(err)));
    CHECK(keys >= 6 && values >= 15);
    CHECK(!key_exists(L"Servers\\added-later"));
    CHECK(!value_exists(L"LocalDevices", L"added-later"));

    CHECK(RegFile_Export(b));
    tb = read_text(b);
    CHECK(wcscmp(ta, tb) == 0);                              /* identical after the round trip */
    free(tb);

    /* a merge (no replace) keeps what the file doesn't mention */
    Host_SetUser(L"added-later", L"someone");
    CHECK(RegFile_Import(a, FALSE, NULL, NULL, err, ARRAY_LEN(err)));
    CHECK(key_exists(L"Servers\\added-later"));
    free(ta);

    wprintf(L"regfile: rejects bad or foreign files\n");
    write_text(bad, L"REGEDIT4\r\n\r\n[HKEY_CURRENT_USER\\Software\\Microsoft\\Terminal Server Client]\r\n");
    CHECK(!RegFile_Import(bad, TRUE, NULL, NULL, err, ARRAY_LEN(err)));
    CHECK(err[0] != 0);
    CHECK(key_exists(L"Servers\\added-later"));              /* nothing was wiped */

    write_text(bad, L"");
    CHECK(!RegFile_Import(bad, TRUE, NULL, NULL, err, ARRAY_LEN(err)));

    /* broken halfway through: must be rejected before anything is changed */
    write_text(bad,
        L"Windows Registry Editor Version 5.00\r\n\r\n"
        L"[HKEY_CURRENT_USER\\Software\\Microsoft\\Terminal Server Client\\Servers\\x]\r\n"
        L"\"UsernameHint\"=\"ok\"\r\n"
        L"\"Broken\"=hex:zz\r\n");
    CHECK(!RegFile_Import(bad, TRUE, NULL, NULL, err, ARRAY_LEN(err)));
    CHECK(wcsstr(err, L"Line 5") != NULL);
    CHECK(key_exists(L"Servers\\added-later") && !key_exists(L"Servers\\x"));

    /* keys outside Terminal Server Client are ignored, including look-alike names */
    write_text(bad,
        L"Windows Registry Editor Version 5.00\r\n\r\n"
        L"[HKEY_CURRENT_USER\\Software\\RdcmTestEvil]\r\n\"a\"=\"b\"\r\n\r\n"
        L"[HKEY_CURRENT_USER\\Software\\Microsoft\\Terminal Server ClientEvil]\r\n\"a\"=\"b\"\r\n\r\n"
        L"[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Terminal Server Client]\r\n\"a\"=\"b\"\r\n\r\n"
        L"[HKEY_CURRENT_USER\\Software\\Microsoft\\Terminal Server Client\\Servers\\good]\r\n"
        L"\"UsernameHint\"=\"fine\"\r\n");
    keys = 0;
    CHECK(RegFile_Import(bad, FALSE, &keys, NULL, err, ARRAY_LEN(err)));
    CHECK(keys == 1 && key_exists(L"Servers\\good"));
    CHECK(RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\RdcmTestEvil", 0, KEY_READ, &k) != ERROR_SUCCESS);
    CHECK(RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Terminal Server ClientEvil", 0, KEY_READ, &k)
          != ERROR_SUCCESS);

    /* a file with only foreign keys has nothing for us */
    write_text(bad,
        L"Windows Registry Editor Version 5.00\r\n\r\n[HKEY_CURRENT_USER\\Software\\RdcmTestEvil]\r\n\"a\"=\"b\"\r\n");
    CHECK(!RegFile_Import(bad, TRUE, NULL, NULL, err, ARRAY_LEN(err)));
    CHECK(key_exists(L"Servers\\good"));

    CHECK(!RegFile_Import(L"Z:\\does\\not\\exist.reg", TRUE, NULL, NULL, err, ARRAY_LEN(err)));
}

static void test_backups(void)
{
    BackupInfo *v = NULL;
    wchar_t err[256];
    HostList l = { 0 };
    int n, i, regs = 0, rdps = 0;

    wprintf(L"backups: create, list, restore\n");
    reset();
    seed();
    write_text(g_env.rdpPath, L"screen mode id:i:2\r\n");

    /* every change gets its own backup, even two in the same second */
    CHECK(Backup_BeforeRegChange());
    Host_RemoveFromMru(L"beta");
    CHECK(Backup_BeforeRegChange());
    CHECK(Backup_BeforeRdpChange());
    n = Backup_List(&v);
    CHECK(n == 3);
    for (i = 0; i < n; i++) {
        if (v[i].isRdp) rdps++; else regs++;
        CHECK(str_ieq(v[i].kind, L"auto"));
        CHECK(v[i].size > 0);
    }
    CHECK(regs == 2 && rdps == 1);
    CHECK(n == 3 && v[0].isRdp && !v[2].isRdp);              /* newest first */

    /* change things, then restore the oldest registry backup and the Default.rdp one */
    Host_RemoveAll(L"alpha");
    write_text(g_env.rdpPath, L"screen mode id:i:1\r\n");
    CHECK(Backup_Restore(&v[2], err, ARRAY_LEN(err)));
    CHECK(Backup_Restore(&v[0], err, ARRAY_LEN(err)));
    free(v);

    Model_Load(&l);
    CHECK(Model_Find(&l, L"alpha") != NULL && Model_Find(&l, L"alpha")->hasCert);
    CHECK(Model_Find(&l, L"beta") != NULL && Model_Find(&l, L"beta")->mru == 1);
    Model_Free(&l);
    {
        wchar_t *t = read_text(g_env.rdpPath);
        CHECK(wcscmp(t, L"screen mode id:i:2\r\n") == 0);
        free(t);
    }

    n = Backup_List(&v);                                     /* 3 originals + 2 "prerestore" */
    CHECK(n == 5);
    regs = 0;
    for (i = 0; i < n; i++)
        if (str_ieq(v[i].kind, L"prerestore")) regs++;
    CHECK(regs == 2);
    free(v);

    CHECK(Backup_Create(L"manual", TRUE, TRUE));
    n = Backup_List(&v);
    CHECK(n == 7);
    free(v);

    /* only the newest 100 automatic backups are kept; manual ones are never pruned */
    wprintf(L"backups: old automatic backups are pruned\n");
    for (i = 0; i < 105; i++)
        CHECK(Backup_BeforeRegChange());
    n = Backup_List(&v);
    regs = rdps = 0;
    for (i = 0; i < n; i++) {
        if (str_ieq(v[i].kind, L"auto") && !v[i].isRdp) regs++;
        if (str_ieq(v[i].kind, L"manual")) rdps++;
    }
    CHECK(regs == 100 && rdps == 2);
    free(v);
}

static void test_rdpfile(void)
{
    RdpFile f = { 0 };
    wchar_t *t;
    int at;

    wprintf(L"rdpfile: load, edit, save\n");
    reset();
    CHECK(!Rdp_Load(&f) && !f.exists && f.n == 0);

    write_text(g_env.rdpPath,
        L"screen mode id:i:2\r\n"
        L"full address:s:server01:3390\r\n"
        L"this line is nonsense\r\n"
        L"\r\n"
        L"winposstr:s:0,1,100,100,900,700\r\n"
        L"audiomode:i:0");
    SetFileAttributesW(g_env.rdpPath, FILE_ATTRIBUTE_HIDDEN);

    CHECK(Rdp_Load(&f) && f.exists);
    CHECK(f.n == 5);
    at = Rdp_Find(&f, L"FULL ADDRESS");
    CHECK(at == 1 && f.v[at].type == L's' && wcscmp(f.v[at].value, L"server01:3390") == 0);
    CHECK(f.v[2].name == NULL && wcscmp(f.v[2].value, L"this line is nonsense") == 0);
    at = Rdp_Find(&f, L"audiomode");
    CHECK(at == 4 && wcscmp(f.v[at].value, L"0") == 0);
    CHECK(Rdp_Find(&f, L"missing") == -1);

    Rdp_Set(&f, L"audiomode", L'i', L"2");
    Rdp_Set(&f, L"redirectclipboard", L'i', L"1");
    Rdp_Delete(&f, Rdp_Find(&f, L"winposstr"));
    CHECK(f.n == 5);
    CHECK(Rdp_Save(&f));
    CHECK(GetFileAttributesW(g_env.rdpPath) & FILE_ATTRIBUTE_HIDDEN);   /* stays hidden */

    t = read_text(g_env.rdpPath);
    CHECK(wcscmp(t,
        L"screen mode id:i:2\r\n"
        L"full address:s:server01:3390\r\n"
        L"this line is nonsense\r\n"
        L"audiomode:i:2\r\n"
        L"redirectclipboard:i:1\r\n") == 0);
    free(t);
    Rdp_Free(&f);

    /* older files are plain ANSI text, not UTF-16 */
    {
        static const char ansi[] = "screen mode id:i:1\r\nusername:s:bob\r\n";
        SetFileAttributesW(g_env.rdpPath, FILE_ATTRIBUTE_NORMAL);
        CHECK(file_write_atomic(g_env.rdpPath, ansi, sizeof(ansi) - 1));
        CHECK(Rdp_Load(&f) && f.n == 2);
        at = Rdp_Find(&f, L"username");
        CHECK(at == 1 && wcscmp(f.v[at].value, L"bob") == 0);
        Rdp_Free(&f);
    }

    CHECK(Rdp_KnownType(L"Screen Mode ID") == L'i');
    CHECK(Rdp_KnownType(L"full address") == L's');
    CHECK(Rdp_KnownType(L"made up") == 0);
    CHECK(Rdp_Describe(L"audiomode")[0] != 0);
    CHECK(Rdp_Describe(L"made up")[0] == 0);
}

static void test_cleanup(void)
{
    wchar_t p[PATH_CCH];
    DirStat st;
    ULONGLONG bytes = 0;
    int deleted = 0, failed = 0;
    HANDLE held;

    wprintf(L"cleanup: screen cache and jump list\n");
    reset();
    Cache_Stat(&st);
    CHECK(st.files == 0 && st.bytes == 0);
    Cache_Clear(&deleted, &failed);
    CHECK(deleted == 0 && failed == 0);
    CHECK(!Jump_Stat(&bytes));
    CHECK(Jump_Clear());

    dir_ensure(g_env.cacheDir);
    path_join(p, ARRAY_LEN(p), g_env.cacheDir, L"Cache0000.bin");
    CHECK(file_write_atomic(p, "0123456789", 10));
    path_join(p, ARRAY_LEN(p), g_env.cacheDir, L"bcache24.bmc");
    CHECK(file_write_atomic(p, "abcde", 5));
    Cache_Stat(&st);
    CHECK(st.files == 2 && st.bytes == 15);

    /* a file held open (as a live session would) can't be deleted and is reported */
    held = CreateFileW(p, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    Cache_Clear(&deleted, &failed);
    CHECK(deleted == 1 && failed == 1);
    CloseHandle(held);
    Cache_Clear(&deleted, &failed);
    CHECK(deleted == 1 && failed == 0);
    Cache_Stat(&st);
    CHECK(st.files == 0);

    CHECK(file_write_atomic(g_env.jumpAuto, "jump", 4));
    CHECK(Jump_Stat(&bytes) && bytes == 4);
    CHECK(Jump_Clear());
    CHECK(!Jump_Stat(&bytes));
}

static void test_util(void)
{
    wchar_t buf[64];
    WStr s = { 0 };
    BOOL utf16 = FALSE;
    wchar_t *t;
    int i;

    wprintf(L"util: strings and text\n");
    StringCchCopyW(buf, ARRAY_LEN(buf), L"  \t hello world \r\n");
    str_trim(buf);
    CHECK(wcscmp(buf, L"hello world") == 0);
    StringCchCopyW(buf, ARRAY_LEN(buf), L"   ");
    str_trim(buf);
    CHECK(buf[0] == 0);

    CHECK(str_ieq(L"HostName", L"hostname") && !str_ieq(L"host", L"hostname"));
    CHECK(str_istarts(L"TERMSRV/box", L"termsrv/") && !str_istarts(L"TER", L"TERMSRV/"));

    for (i = 0; i < 500; i++) ws_appendf(&s, L"%d,", i);
    CHECK(s.len == 1890 && wcsncmp(s.p, L"0,1,2,", 6) == 0);
    ws_clear(&s);
    CHECK(s.len == 0 && s.p[0] == 0);
    ws_free(&s);

    t = text_decode((const BYTE *)"\xFF\xFEh\0i\0", 6, &utf16);
    CHECK(utf16 && wcscmp(t, L"hi") == 0);
    free(t);
    t = text_decode((const BYTE *)"\xEF\xBB\xBFok", 5, &utf16);
    CHECK(!utf16 && wcscmp(t, L"ok") == 0);
    free(t);
    t = text_decode((const BYTE *)"", 0, &utf16);
    CHECK(t[0] == 0);
    free(t);

    path_join(buf, ARRAY_LEN(buf), L"C:\\a", L"b");
    CHECK(wcscmp(buf, L"C:\\a\\b") == 0);
    path_join(buf, ARRAY_LEN(buf), L"C:\\a\\", L"b");
    CHECK(wcscmp(buf, L"C:\\a\\b") == 0);
}

int wmain(void)
{
    wchar_t tmp[PATH_CCH], leaf[64];

    StringCchPrintfW(g_regBase, ARRAY_LEN(g_regBase), L"Software\\RDC Manager Tests\\%u", GetCurrentProcessId());
    GetTempPathW(ARRAY_LEN(tmp), tmp);
    StringCchPrintfW(leaf, ARRAY_LEN(leaf), L"rdcm-tests-%u", GetCurrentProcessId());
    path_join(g_dirBase, ARRAY_LEN(g_dirBase), tmp, leaf);

    test_util();
    test_load();
    test_mru();
    test_host_ops();
    test_regfile();
    test_backups();
    test_rdpfile();
    test_cleanup();

    /* leave nothing behind */
    RegDeleteTreeW(HKEY_CURRENT_USER, g_regBase);
    RegDeleteKeyW(HKEY_CURRENT_USER, g_regBase);
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\RDC Manager Tests");
    delete_dir(g_dirBase);

    wprintf(L"\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
