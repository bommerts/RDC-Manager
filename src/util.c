/*
 * RDC Manager - memory, string, file and environment helpers.
 */
#include "rdcm.h"
#include <initguid.h>
#include <knownfolders.h>

Env g_env;

/* --------------------------------------------------------------- memory */

static __declspec(noreturn) void die_oom(void)
{
    MessageBoxW(NULL, L"RDC Manager ran out of memory and has to close.",
                RDCM_NAME, MB_OK | MB_ICONERROR);
    ExitProcess(3);
}

_Ret_notnull_ void *xalloc(size_t bytes)
{
    void *p = calloc(1, bytes ? bytes : 1);
    if (!p) die_oom();
    return p;
}

_Ret_notnull_ void *xrealloc(void *p, size_t bytes)
{
    void *q = realloc(p, bytes ? bytes : 1);
    if (!q) die_oom();
    return q;
}

_Ret_notnull_ wchar_t *xwcsdup(const wchar_t *s)
{
    size_t n = wcslen(s) + 1;
    wchar_t *d = (wchar_t *)xalloc(n * sizeof(wchar_t));
    memcpy(d, s, n * sizeof(wchar_t));
    return d;
}

/* -------------------------------------------------------------- strings */

static void ws_reserve(WStr *s, size_t extra)
{
    size_t need = s->len + extra + 1;
    if (need > s->cap) {
        size_t cap = s->cap ? s->cap * 2 : 64;
        if (cap < need) cap = need;
        s->p = (wchar_t *)xrealloc(s->p, cap * sizeof(wchar_t));
        s->cap = cap;
    }
}

void ws_free(WStr *s)
{
    free(s->p);
    s->p = NULL;
    s->len = s->cap = 0;
}

void ws_clear(WStr *s)
{
    ws_reserve(s, 0);
    s->len = 0;
    s->p[0] = 0;
}

void ws_appendn(WStr *s, const wchar_t *t, size_t n)
{
    ws_reserve(s, n);
    if (n) memcpy(s->p + s->len, t, n * sizeof(wchar_t));
    s->len += n;
    s->p[s->len] = 0;
}

void ws_append(WStr *s, const wchar_t *t)
{
    ws_appendn(s, t, wcslen(t));
}

void ws_putc(WStr *s, wchar_t c)
{
    ws_appendn(s, &c, 1);
}

void ws_appendf(WStr *s, const wchar_t *fmt, ...)
{
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    StringCchVPrintfW(buf, ARRAY_LEN(buf), fmt, ap);
    va_end(ap);
    ws_append(s, buf);
}

BOOL str_ieq(const wchar_t *a, const wchar_t *b)
{
    return CompareStringOrdinal(a, -1, b, -1, TRUE) == CSTR_EQUAL;
}

BOOL str_istarts(const wchar_t *s, const wchar_t *prefix)
{
    size_t n = wcslen(prefix);
    if (wcslen(s) < n) return FALSE;
    return CompareStringOrdinal(s, (int)n, prefix, (int)n, TRUE) == CSTR_EQUAL;
}

void str_trim(wchar_t *s)
{
    size_t len = wcslen(s), start = 0;
    while (len > 0 && (s[len - 1] == L' ' || s[len - 1] == L'\t' ||
                       s[len - 1] == L'\r' || s[len - 1] == L'\n'))
        len--;
    while (start < len && (s[start] == L' ' || s[start] == L'\t'))
        start++;
    if (start) memmove(s, s + start, (len - start) * sizeof(wchar_t));
    s[len - start] = 0;
}

void path_join(wchar_t *out, size_t cch, const wchar_t *dir, const wchar_t *leaf)
{
    size_t n = wcslen(dir);
    if (n && (dir[n - 1] == L'\\' || dir[n - 1] == L'/'))
        StringCchPrintfW(out, cch, L"%s%s", dir, leaf);
    else
        StringCchPrintfW(out, cch, L"%s\\%s", dir, leaf);
}

/* ---------------------------------------------------------------- files */

BOOL file_exists(const wchar_t *path)
{
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

BOOL file_size(const wchar_t *path, ULONGLONG *size)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fa)) return FALSE;
    if (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return FALSE;
    *size = ((ULONGLONG)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
    return TRUE;
}

BOOL dir_ensure(const wchar_t *path)
{
    int r = SHCreateDirectoryExW(NULL, path, NULL);
    return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS;
}

/* Reads a whole file (16 MB limit). The buffer gets two extra zero bytes. */
BOOL file_read_all(const wchar_t *path, BYTE **data, DWORD *size)
{
    HANDLE h;
    LARGE_INTEGER li;
    BYTE *buf;
    DWORD total = 0;

    *data = NULL;
    *size = 0;
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    if (!GetFileSizeEx(h, &li) || li.QuadPart > 16 * 1024 * 1024) {
        CloseHandle(h);
        return FALSE;
    }
    buf = (BYTE *)xalloc((size_t)li.QuadPart + 2);
    while (total < (DWORD)li.QuadPart) {
        DWORD got = 0;
        if (!ReadFile(h, buf + total, (DWORD)li.QuadPart - total, &got, NULL) || got == 0) break;
        total += got;
    }
    CloseHandle(h);
    if (total != (DWORD)li.QuadPart) {
        free(buf);
        return FALSE;
    }
    *data = buf;
    *size = total;
    return TRUE;
}

/*
 * Writes to a temporary file and then swaps it into place, so a failed write
 * never leaves a half-written file. Keeps the hidden/system/read-only
 * attributes of the file being replaced.
 */
BOOL file_write_atomic(const wchar_t *path, const void *data, DWORD size)
{
    wchar_t tmp[PATH_CCH + 16];
    HANDLE h;
    DWORD written = 0, attrs;
    const DWORD keep = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY;
    BOOL ok;

    if (FAILED(StringCchPrintfW(tmp, ARRAY_LEN(tmp), L"%s.rdcm-tmp", path))) return FALSE;
    h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    ok = WriteFile(h, data, size, &written, NULL) && written == size;
    if (ok) FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok) {
        DeleteFileW(tmp);
        return FALSE;
    }

    attrs = GetFileAttributesW(path);
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & keep))
        SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
    if (!MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & keep))
            SetFileAttributesW(path, attrs & (keep | FILE_ATTRIBUTE_ARCHIVE));
        DeleteFileW(tmp);
        return FALSE;
    }
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & keep))
        SetFileAttributesW(path, attrs & (keep | FILE_ATTRIBUTE_ARCHIVE));
    return TRUE;
}

/* Converts file bytes to text: UTF-16LE with a byte order mark, UTF-8, or the ANSI code page. */
_Ret_notnull_ wchar_t *text_decode(const BYTE *data, DWORD size, BOOL *wasUtf16)
{
    wchar_t *out;
    const BYTE *p = data;
    DWORD n = size, flags = MB_ERR_INVALID_CHARS;
    UINT cp = CP_UTF8;
    int cch;

    if (wasUtf16) *wasUtf16 = FALSE;
    if (size >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        DWORD count = (size - 2) / 2;
        out = (wchar_t *)xalloc(((size_t)count + 1) * sizeof(wchar_t));
        memcpy(out, data + 2, (size_t)count * 2);
        out[count] = 0;
        if (wasUtf16) *wasUtf16 = TRUE;
        return out;
    }
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        p += 3;
        n -= 3;
    }
    if (n == 0) return xwcsdup(L"");
    cch = MultiByteToWideChar(cp, flags, (LPCCH)p, (int)n, NULL, 0);
    if (cch <= 0) {
        cp = CP_ACP;
        flags = 0;
        cch = MultiByteToWideChar(cp, flags, (LPCCH)p, (int)n, NULL, 0);
    }
    if (cch <= 0) return xwcsdup(L"");
    out = (wchar_t *)xalloc(((size_t)cch + 1) * sizeof(wchar_t));
    MultiByteToWideChar(cp, flags, (LPCCH)p, (int)n, out, cch);
    out[cch] = 0;
    return out;
}

/* Writes text as UTF-16LE with a byte order mark (what mstsc and regedit use). */
BOOL text_write_utf16(const wchar_t *path, const wchar_t *text, size_t cch)
{
    size_t bytes = 2 + cch * sizeof(wchar_t);
    BYTE *buf;
    BOOL ok;

    if (bytes > 0x7FFFFFFF) return FALSE;
    buf = (BYTE *)xalloc(bytes);
    buf[0] = 0xFF;
    buf[1] = 0xFE;
    if (cch) memcpy(buf + 2, text, cch * sizeof(wchar_t));
    ok = file_write_atomic(path, buf, (DWORD)bytes);
    free(buf);
    return ok;
}

/* ----------------------------------------------------------- formatting */

void format_size(ULONGLONG bytes, wchar_t *out, int cch)
{
    if (!StrFormatByteSizeW((LONGLONG)bytes, out, (UINT)cch))
        StringCchPrintfW(out, (size_t)cch, L"%I64u bytes", bytes);
}

void format_filetime(const FILETIME *ft, wchar_t *out, int cch)
{
    SYSTEMTIME utc, local;
    wchar_t d[64] = L"", t[64] = L"";

    out[0] = 0;
    if (!FileTimeToSystemTime(ft, &utc)) return;
    if (!SystemTimeToTzSpecificLocalTime(NULL, &utc, &local)) local = utc;
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &local, NULL, d, ARRAY_LEN(d), NULL);
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &local, NULL, t, ARRAY_LEN(t));
    StringCchPrintfW(out, (size_t)cch, L"%s  %s", d, t);
}

BOOL clipboard_set(HWND owner, const wchar_t *text)
{
    size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    HGLOBAL mem;
    void *p;

    if (!OpenClipboard(owner)) return FALSE;
    EmptyClipboard();
    mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) {
        CloseClipboard();
        return FALSE;
    }
    p = GlobalLock(mem);
    if (!p) {
        GlobalFree(mem);
        CloseClipboard();
        return FALSE;
    }
    memcpy(p, text, bytes);
    GlobalUnlock(mem);
    if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
    CloseClipboard();
    return TRUE;
}

/* ------------------------------------------------------------------ env */

static BOOL known_folder(const KNOWNFOLDERID *id, wchar_t *out, size_t cch)
{
    PWSTR p = NULL;
    BOOL ok = FALSE;

    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, NULL, &p)) && p)
        ok = SUCCEEDED(StringCchCopyW(out, cch, p));
    CoTaskMemFree(p);
    if (!ok) out[0] = 0;
    return ok;
}

void Env_InitReal(void)
{
    wchar_t dir[PATH_CCH];

    ZeroMemory(&g_env, sizeof(g_env));
    StringCchCopyW(g_env.regRoot, ARRAY_LEN(g_env.regRoot), TSC_CANON);

    if (known_folder(&FOLDERID_Documents, dir, ARRAY_LEN(dir)))
        path_join(g_env.rdpPath, ARRAY_LEN(g_env.rdpPath), dir, L"Default.rdp");

    if (known_folder(&FOLDERID_LocalAppData, dir, ARRAY_LEN(dir))) {
        path_join(g_env.cacheDir, ARRAY_LEN(g_env.cacheDir), dir,
                  L"Microsoft\\Terminal Server Client\\Cache");
        path_join(g_env.backupDir, ARRAY_LEN(g_env.backupDir), dir, L"RDC Manager\\Backups");
    }

    /* 1bc392b8e104a00e is the jump list ID Windows gives Remote Desktop Connection. */
    if (known_folder(&FOLDERID_Recent, dir, ARRAY_LEN(dir))) {
        path_join(g_env.jumpAuto, ARRAY_LEN(g_env.jumpAuto), dir,
                  L"AutomaticDestinations\\1bc392b8e104a00e.automaticDestinations-ms");
        path_join(g_env.jumpCustom, ARRAY_LEN(g_env.jumpCustom), dir,
                  L"CustomDestinations\\1bc392b8e104a00e.customDestinations-ms");
    }
}

void Env_InitSandbox(const wchar_t *regBase, const wchar_t *dirBase)
{
    ZeroMemory(&g_env, sizeof(g_env));
    g_env.sandbox = TRUE;
    StringCchPrintfW(g_env.regRoot, ARRAY_LEN(g_env.regRoot), L"%s\\Terminal Server Client", regBase);
    StringCchPrintfW(g_env.credKey, ARRAY_LEN(g_env.credKey), L"%s\\Credentials", regBase);
    path_join(g_env.rdpPath, ARRAY_LEN(g_env.rdpPath), dirBase, L"Default.rdp");
    path_join(g_env.cacheDir, ARRAY_LEN(g_env.cacheDir), dirBase, L"Cache");
    path_join(g_env.jumpAuto, ARRAY_LEN(g_env.jumpAuto), dirBase, L"jump.automaticDestinations-ms");
    path_join(g_env.jumpCustom, ARRAY_LEN(g_env.jumpCustom), dirBase, L"jump.customDestinations-ms");
    path_join(g_env.backupDir, ARRAY_LEN(g_env.backupDir), dirBase, L"Backups");
}
