/*
 * RDC Manager - .reg export/import and the backup folder.
 *
 * Backups are ordinary "Windows Registry Editor Version 5.00" files, so they
 * can be inspected in Notepad or merged by double-clicking even without this
 * app. The importer only ever writes below the Terminal Server Client key.
 */
#include "rdcm.h"

#define REG_HEADER    L"Windows Registry Editor Version 5.00"
#define KEY_PREFIX    L"HKEY_CURRENT_USER\\" TSC_CANON
#define AUTO_KEEP     100                       /* automatic backups kept per type */

/* --------------------------------------------------------------- export */

static int __cdecl name_cmp(const void *a, const void *b)
{
    const wchar_t *x = *(const wchar_t *const *)a, *y = *(const wchar_t *const *)b;
    int r = CompareStringOrdinal(x, -1, y, -1, TRUE);
    return r == CSTR_LESS_THAN ? -1 : r == CSTR_GREATER_THAN ? 1 : 0;
}

static void write_quoted(WStr *out, const wchar_t *s, size_t n)
{
    size_t i;
    ws_putc(out, L'"');
    for (i = 0; i < n; i++) {
        if (s[i] == L'\\' || s[i] == L'"') ws_putc(out, L'\\');
        ws_putc(out, s[i]);
    }
    ws_putc(out, L'"');
}

static void write_hex(WStr *out, const BYTE *data, DWORD cb)
{
    DWORD i;
    for (i = 0; i < cb; i++) {
        ws_appendf(out, L"%02x", data[i]);
        if (i + 1 < cb) {
            ws_putc(out, L',');
            if ((i + 1) % 25 == 0) ws_append(out, L"\\\r\n  ");
        }
    }
}

/* A string can be written in quotes only if it is well formed and has no control characters. */
static BOOL sz_is_plain(const BYTE *data, DWORD cb, size_t *len)
{
    const wchar_t *s = (const wchar_t *)data;
    size_t n = cb / sizeof(wchar_t), i;

    if (cb == 0 || cb % sizeof(wchar_t) || s[n - 1] != 0) return FALSE;
    for (i = 0; i < n - 1; i++)
        if (s[i] < 0x20) return FALSE;
    *len = n - 1;
    return TRUE;
}

static void write_value(WStr *out, const wchar_t *name, DWORD type, const BYTE *data, DWORD cb)
{
    size_t len = 0;

    if (name[0]) write_quoted(out, name, wcslen(name));
    else ws_putc(out, L'@');
    ws_putc(out, L'=');

    if (type == REG_SZ && sz_is_plain(data, cb, &len)) {
        write_quoted(out, (const wchar_t *)data, len);
    } else if (type == REG_DWORD && cb == sizeof(DWORD)) {
        DWORD v;
        memcpy(&v, data, sizeof(v));
        ws_appendf(out, L"dword:%08x", v);
    } else {
        if (type == REG_BINARY) ws_append(out, L"hex:");
        else ws_appendf(out, L"hex(%x):", type);
        write_hex(out, data, cb);
    }
    ws_append(out, L"\r\n");
}

/* Collects value names (values = TRUE) or subkey names of a key, sorted. */
static wchar_t **list_names(HKEY k, BOOL values, int *count)
{
    wchar_t **v = NULL;
    wchar_t *name = (wchar_t *)xalloc(16384 * sizeof(wchar_t));
    int n = 0;
    DWORD i;

    for (i = 0;; i++) {
        DWORD cch = 16384;
        LSTATUS s = values
            ? RegEnumValueW(k, i, name, &cch, NULL, NULL, NULL, NULL)
            : RegEnumKeyExW(k, i, name, &cch, NULL, NULL, NULL, NULL);
        if (s != ERROR_SUCCESS) break;
        v = (wchar_t **)xrealloc(v, ((size_t)n + 1) * sizeof(wchar_t *));
        v[n++] = xwcsdup(name);
    }
    free(name);
    if (n > 1) qsort(v, (size_t)n, sizeof(wchar_t *), name_cmp);
    *count = n;
    return v;
}

static void free_names(wchar_t **v, int n)
{
    int i;
    for (i = 0; i < n; i++) free(v[i]);
    free(v);
}

static void export_key(WStr *out, HKEY k, const wchar_t *canonPath, int depth)
{
    wchar_t **names;
    int n, i;

    ws_append(out, L"[HKEY_CURRENT_USER\\");
    ws_append(out, canonPath);
    ws_append(out, L"]\r\n");

    names = list_names(k, TRUE, &n);
    for (i = 0; i < n; i++) {
        DWORD type = 0, cb = 0;
        BYTE *data;
        if (RegQueryValueExW(k, names[i], NULL, &type, NULL, &cb) != ERROR_SUCCESS) continue;
        data = (BYTE *)xalloc((size_t)cb + 2);
        if (RegQueryValueExW(k, names[i], NULL, &type, data, &cb) == ERROR_SUCCESS)
            write_value(out, names[i], type, data, cb);
        free(data);
    }
    free_names(names, n);
    ws_append(out, L"\r\n");

    if (depth >= 32) return;
    names = list_names(k, FALSE, &n);
    for (i = 0; i < n; i++) {
        HKEY sk;
        WStr sub = { 0 };
        if (RegOpenKeyExW(k, names[i], 0, KEY_READ, &sk) != ERROR_SUCCESS) continue;
        ws_append(&sub, canonPath);
        ws_putc(&sub, L'\\');
        ws_append(&sub, names[i]);
        export_key(out, sk, sub.p, depth + 1);
        ws_free(&sub);
        RegCloseKey(sk);
    }
    free_names(names, n);
}

/* Writes the whole Terminal Server Client key to a .reg file. */
BOOL RegFile_Export(const wchar_t *path)
{
    WStr out = { 0 };
    HKEY k;
    BOOL ok;

    ws_append(&out, REG_HEADER L"\r\n\r\n");
    if (Reg_OpenSub(NULL, KEY_READ, FALSE, &k) == ERROR_SUCCESS) {
        export_key(&out, k, TSC_CANON, 0);
        RegCloseKey(k);
    }
    ok = text_write_utf16(path, out.p, out.len);
    ws_free(&out);
    return ok;
}

/* --------------------------------------------------------------- import */

typedef struct Parser {
    const wchar_t *p;       /* cursor into the file text */
    int  line;              /* physical line the cursor is on */
    int  start;             /* line number where the current logical line began */
    WStr cur;               /* current logical line, continuations joined */
} Parser;

static BOOL is_blank(wchar_t c)
{
    return c == L' ' || c == L'\t';
}

/* Reads the next logical line. A trailing backslash joins the following line. */
static BOOL next_line(Parser *ps)
{
    BOOL first = TRUE;

    if (!*ps->p) return FALSE;
    ws_clear(&ps->cur);
    for (;;) {
        const wchar_t *s = ps->p, *e = s;
        BOOL cont;

        while (*e && *e != L'\n') e++;
        ps->p = *e ? e + 1 : e;
        ps->line++;
        if (first) ps->start = ps->line;

        while (e > s && (e[-1] == L'\r' || is_blank(e[-1]))) e--;
        if (!first)
            while (s < e && is_blank(*s)) s++;
        cont = e > s && e[-1] == L'\\';
        if (cont) e--;
        ws_appendn(&ps->cur, s, (size_t)(e - s));
        first = FALSE;
        if (!cont || !*ps->p) break;
    }
    return TRUE;
}

/* Parses "text" with \\ and \" escapes. *pp must point at the opening quote. */
static BOOL parse_quoted(const wchar_t **pp, WStr *out)
{
    const wchar_t *p = *pp;

    if (*p != L'"') return FALSE;
    p++;
    ws_clear(out);
    for (;;) {
        wchar_t c = *p++;
        if (!c) return FALSE;
        if (c == L'"') break;
        if (c == L'\\' && (*p == L'\\' || *p == L'"')) c = *p++;
        ws_putc(out, c);
    }
    *pp = p;
    return TRUE;
}

static int hex_val(wchar_t c)
{
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

/* Parses "aa,bb,cc". The caller frees *data. */
static BOOL parse_hex(const wchar_t *p, BYTE **data, DWORD *cb)
{
    BYTE *b = (BYTE *)xalloc(wcslen(p) / 2 + 2);
    DWORD n = 0;

    for (;;) {
        int hi, lo;
        while (is_blank(*p)) p++;
        if (!*p) break;
        hi = hex_val(p[0]);
        lo = hi < 0 ? -1 : hex_val(p[1]);
        if (lo < 0) {
            free(b);
            return FALSE;
        }
        b[n++] = (BYTE)((hi << 4) | lo);
        p += 2;
        while (is_blank(*p)) p++;
        if (*p == L',') {
            p++;
            continue;
        }
        if (!*p) break;
        free(b);
        return FALSE;
    }
    *data = b;
    *cb = n;
    return TRUE;
}

/* Maps a key path from the file onto the active root. Refuses anything outside it. */
static BOOL map_key(const wchar_t *full, wchar_t *out, size_t cch)
{
    size_t n = ARRAY_LEN(KEY_PREFIX) - 1;

    if (!str_istarts(full, KEY_PREFIX)) return FALSE;
    if (full[n] != 0 && full[n] != L'\\') return FALSE;
    return SUCCEEDED(StringCchPrintfW(out, cch, L"%s%s", g_env.regRoot, full + n));
}

static BOOL fail_at(const Parser *ps, wchar_t *err, size_t cchErr, const wchar_t *what)
{
    if (err && cchErr)
        StringCchPrintfW(err, cchErr, L"Line %d: %s", ps->start, what);
    return FALSE;
}

/*
 * Parses the file. With apply = FALSE nothing is written, so the whole file
 * can be checked before anything is changed.
 */
static BOOL run_import(const wchar_t *text, BOOL apply, int *keys, int *values,
                       wchar_t *err, size_t cchErr)
{
    Parser ps = { 0 };
    WStr name = { 0 }, str = { 0 };
    HKEY cur = NULL;
    BOOL inKey = FALSE, sawHeader = FALSE, ok = TRUE;
    int nKeys = 0, nValues = 0;

    ps.p = text;
    while (ok && next_line(&ps)) {
        const wchar_t *p = ps.cur.p;
        while (is_blank(*p)) p++;
        if (!*p || *p == L';') continue;

        if (!sawHeader) {
            if (wcscmp(p, REG_HEADER) != 0) {
                ok = fail_at(&ps, err, cchErr, L"this is not a version 5 registry file.");
                break;
            }
            sawHeader = TRUE;
            continue;
        }

        if (*p == L'[') {
            wchar_t mapped[900];
            wchar_t *end = wcsrchr(ps.cur.p, L']');

            if (cur) {
                RegCloseKey(cur);
                cur = NULL;
            }
            inKey = FALSE;
            if (!end) {
                ok = fail_at(&ps, err, cchErr, L"key name is missing its closing bracket.");
                break;
            }
            *end = 0;
            p++;
            if (*p == L'-') {
                ok = fail_at(&ps, err, cchErr, L"deleting keys is not supported.");
                break;
            }
            if (!map_key(p, mapped, ARRAY_LEN(mapped))) continue;   /* outside our key: skip section */
            inKey = TRUE;
            nKeys++;
            if (apply &&
                RegCreateKeyExW(HKEY_CURRENT_USER, mapped, 0, NULL, 0, KEY_SET_VALUE, NULL, &cur, NULL)
                    != ERROR_SUCCESS) {
                cur = NULL;
                ok = fail_at(&ps, err, cchErr, L"couldn't create the registry key.");
            }
            continue;
        }

        if (!inKey) continue;

        /* value name */
        if (*p == L'@') {
            ws_clear(&name);
            p++;
        } else if (!parse_quoted(&p, &name)) {
            ok = fail_at(&ps, err, cchErr, L"value name isn't quoted correctly.");
            break;
        }
        while (is_blank(*p)) p++;
        if (*p != L'=') {
            ok = fail_at(&ps, err, cchErr, L"expected '=' after the value name.");
            break;
        }
        p++;
        while (is_blank(*p)) p++;

        /* value data */
        if (*p == L'-' && !p[1]) {
            if (apply) RegDeleteValueW(cur, name.p);
        } else if (*p == L'"') {
            if (!parse_quoted(&p, &str)) {
                ok = fail_at(&ps, err, cchErr, L"text value isn't quoted correctly.");
                break;
            }
            if (apply &&
                RegSetValueExW(cur, name.p, 0, REG_SZ, (const BYTE *)str.p,
                               (DWORD)((str.len + 1) * sizeof(wchar_t))) != ERROR_SUCCESS)
                ok = fail_at(&ps, err, cchErr, L"couldn't write a registry value.");
            nValues++;
        } else if (str_istarts(p, L"dword:")) {
            DWORD v = 0;
            int digits = 0, h;
            p += 6;
            while ((h = hex_val(*p)) >= 0 && digits < 8) {
                v = (v << 4) | (DWORD)h;
                p++;
                digits++;
            }
            if (digits == 0 || *p) {
                ok = fail_at(&ps, err, cchErr, L"number value isn't valid.");
                break;
            }
            if (apply &&
                RegSetValueExW(cur, name.p, 0, REG_DWORD, (const BYTE *)&v, sizeof(v)) != ERROR_SUCCESS)
                ok = fail_at(&ps, err, cchErr, L"couldn't write a registry value.");
            nValues++;
        } else if (str_istarts(p, L"hex")) {
            DWORD type = REG_BINARY, cb = 0;
            BYTE *data = NULL;
            p += 3;
            if (*p == L'(') {
                int digits = 0, h;
                type = 0;
                p++;
                while ((h = hex_val(*p)) >= 0 && digits < 8) {
                    type = (type << 4) | (DWORD)h;
                    p++;
                    digits++;
                }
                if (digits == 0 || *p != L')') {
                    ok = fail_at(&ps, err, cchErr, L"value type isn't valid.");
                    break;
                }
                p++;
            }
            if (*p != L':' || !parse_hex(p + 1, &data, &cb)) {
                ok = fail_at(&ps, err, cchErr, L"binary value isn't valid.");
                break;
            }
            if (apply && RegSetValueExW(cur, name.p, 0, type, data, cb) != ERROR_SUCCESS)
                ok = fail_at(&ps, err, cchErr, L"couldn't write a registry value.");
            free(data);
            nValues++;
        } else {
            ok = fail_at(&ps, err, cchErr, L"unrecognised value.");
            break;
        }
    }

    if (ok && !sawHeader) {
        if (err && cchErr) StringCchCopyW(err, cchErr, L"The file is empty.");
        ok = FALSE;
    }
    if (cur) RegCloseKey(cur);
    ws_free(&ps.cur);
    ws_free(&name);
    ws_free(&str);
    if (keys) *keys = nKeys;
    if (values) *values = nValues;
    return ok;
}

/* Removes the parts of the registry this app manages, ready for a restore. */
static void wipe_managed(void)
{
    HKEY k;

    if (Reg_OpenSub(NULL, KEY_READ | KEY_WRITE, FALSE, &k) == ERROR_SUCCESS) {
        RegDeleteTreeW(k, L"Servers");
        RegDeleteKeyW(k, L"Servers");
        RegDeleteTreeW(k, L"LocalDevices");
        RegDeleteKeyW(k, L"LocalDevices");
        RegCloseKey(k);
    }
    Mru_Write(NULL, 0);
}

/*
 * Imports a .reg file. With replaceManaged the dropdown list, Servers and
 * LocalDevices are cleared first so the result matches the file exactly.
 * The file is fully validated before anything is changed.
 */
BOOL RegFile_Import(const wchar_t *path, BOOL replaceManaged,
                    int *keys, int *values, wchar_t *err, size_t cchErr)
{
    BYTE *data = NULL;
    DWORD size = 0;
    wchar_t *text;
    int nKeys = 0;
    BOOL ok;

    if (err && cchErr) err[0] = 0;
    if (!file_read_all(path, &data, &size)) {
        if (err && cchErr) StringCchCopyW(err, cchErr, L"The file couldn't be read.");
        return FALSE;
    }
    text = text_decode(data, size, NULL);
    free(data);

    ok = run_import(text, FALSE, &nKeys, NULL, err, cchErr);
    if (ok && nKeys == 0) {
        if (err && cchErr)
            StringCchCopyW(err, cchErr, L"The file has no Remote Desktop settings in it.");
        ok = FALSE;
    }
    if (ok) {
        if (replaceManaged) wipe_managed();
        ok = run_import(text, TRUE, keys, values, err, cchErr);
    }
    free(text);
    return ok;
}

/* -------------------------------------------------------------- backups */

/* Builds "<dir>\YYYYMMDD-HHMMSS-<kind>.<ext>", adding -2, -3... if the name is taken. */
static void backup_name(const wchar_t *kind, const wchar_t *ext, wchar_t *out, size_t cch)
{
    SYSTEMTIME t;
    wchar_t leaf[160];
    int n;

    GetLocalTime(&t);
    for (n = 1; n < 10000; n++) {
        if (n == 1)
            StringCchPrintfW(leaf, ARRAY_LEN(leaf), L"%04d%02d%02d-%02d%02d%02d-%s.%s",
                             t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, kind, ext);
        else
            StringCchPrintfW(leaf, ARRAY_LEN(leaf), L"%04d%02d%02d-%02d%02d%02d-%s-%d.%s",
                             t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, kind, n, ext);
        path_join(out, cch, g_env.backupDir, leaf);
        if (GetFileAttributesW(out) == INVALID_FILE_ATTRIBUTES) return;
    }
}

static int __cdecl backup_cmp(const void *a, const void *b)
{
    const BackupInfo *x = (const BackupInfo *)a, *y = (const BackupInfo *)b;
    int r = CompareFileTime(&y->time, &x->time);                /* newest first */
    if (r) return r;
    r = CompareStringOrdinal(y->file, -1, x->file, -1, TRUE);
    return r == CSTR_LESS_THAN ? -1 : r == CSTR_GREATER_THAN ? 1 : 0;
}

int Backup_List(BackupInfo **out)
{
    wchar_t pattern[PATH_CCH];
    WIN32_FIND_DATAW fd;
    BackupInfo *v = NULL;
    HANDLE h;
    int n = 0;

    *out = NULL;
    path_join(pattern, ARRAY_LEN(pattern), g_env.backupDir, L"*");
    h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        const wchar_t *ext = wcsrchr(fd.cFileName, L'.');
        BackupInfo *b;
        BOOL isRdp;
        size_t len = wcslen(fd.cFileName), i;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!ext) continue;
        isRdp = str_ieq(ext, L".rdp");
        if (!isRdp && !str_ieq(ext, L".reg")) continue;
        if (len >= ARRAY_LEN(v->file)) continue;

        v = (BackupInfo *)xrealloc(v, ((size_t)n + 1) * sizeof(BackupInfo));
        b = &v[n++];
        ZeroMemory(b, sizeof(*b));
        path_join(b->path, ARRAY_LEN(b->path), g_env.backupDir, fd.cFileName);
        StringCchCopyW(b->file, ARRAY_LEN(b->file), fd.cFileName);
        b->isRdp = isRdp;
        b->time = fd.ftLastWriteTime;
        b->size = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;

        /* the kind sits after "YYYYMMDD-HHMMSS-" */
        StringCchCopyW(b->kind, ARRAY_LEN(b->kind), L"manual");
        if (len > 16 && fd.cFileName[8] == L'-' && fd.cFileName[15] == L'-') {
            for (i = 0; i + 1 < ARRAY_LEN(b->kind); i++) {
                wchar_t c = fd.cFileName[16 + i];
                if (!c || c == L'.' || c == L'-') break;
                b->kind[i] = c;
            }
            b->kind[i] = 0;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    if (n > 1) qsort(v, (size_t)n, sizeof(BackupInfo), backup_cmp);
    *out = v;
    return n;
}

/* Keeps only the newest automatic backups so the folder can't grow forever. */
static void backup_prune(void)
{
    BackupInfo *v = NULL;
    int n = Backup_List(&v), i, regSeen = 0, rdpSeen = 0;

    for (i = 0; i < n; i++) {
        int *seen = v[i].isRdp ? &rdpSeen : &regSeen;
        if (!str_ieq(v[i].kind, L"auto")) continue;
        if (++*seen > AUTO_KEEP) DeleteFileW(v[i].path);
    }
    free(v);
}

/* Saves the registry settings and/or Default.rdp into the backup folder. */
BOOL Backup_Create(const wchar_t *kind, BOOL reg, BOOL rdp)
{
    wchar_t path[PATH_CCH];
    BOOL ok = TRUE;

    if (!g_env.backupDir[0] || !dir_ensure(g_env.backupDir)) return FALSE;
    if (reg) {
        backup_name(kind, L"reg", path, ARRAY_LEN(path));
        if (!RegFile_Export(path)) ok = FALSE;
    }
    if (rdp && file_exists(g_env.rdpPath)) {
        BYTE *data = NULL;
        DWORD size = 0;
        backup_name(kind, L"rdp", path, ARRAY_LEN(path));
        if (!file_read_all(g_env.rdpPath, &data, &size) || !file_write_atomic(path, data, size))
            ok = FALSE;
        free(data);
    }
    if (ok && str_ieq(kind, L"auto")) backup_prune();
    return ok;
}

/* Call before every change to the registry: saves the current state as an automatic backup. */
BOOL Backup_BeforeRegChange(void)
{
    return Backup_Create(L"auto", TRUE, FALSE);
}

/* Call before every change to Default.rdp. */
BOOL Backup_BeforeRdpChange(void)
{
    if (!file_exists(g_env.rdpPath)) return TRUE;
    return Backup_Create(L"auto", FALSE, TRUE);
}

/* Restores a backup. The current state is saved first as a "prerestore" backup. */
BOOL Backup_Restore(const BackupInfo *b, wchar_t *err, size_t cchErr)
{
    if (err && cchErr) err[0] = 0;

    if (b->isRdp) {
        BYTE *data = NULL;
        DWORD size = 0;
        BOOL ok;
        if (!file_read_all(b->path, &data, &size)) {
            if (err && cchErr) StringCchCopyW(err, cchErr, L"The backup file couldn't be read.");
            return FALSE;
        }
        Backup_Create(L"prerestore", FALSE, TRUE);
        ok = file_write_atomic(g_env.rdpPath, data, size);
        free(data);
        if (!ok && err && cchErr) StringCchCopyW(err, cchErr, L"Default.rdp couldn't be written.");
        return ok;
    }

    if (!Backup_Create(L"prerestore", TRUE, FALSE)) {
        if (err && cchErr)
            StringCchCopyW(err, cchErr, L"Couldn't save the current settings first, so nothing was changed.");
        return FALSE;
    }
    return RegFile_Import(b->path, TRUE, NULL, NULL, err, cchErr);
}
