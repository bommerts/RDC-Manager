/*
 * RDC Manager - the screen cache, the taskbar jump list, and checking
 * whether Remote Desktop Connection is running.
 */
#include "rdcm.h"

typedef void (*FileFn)(const wchar_t *path, const WIN32_FIND_DATAW *fd, void *ctx);

static void for_each_cache_file(FileFn fn, void *ctx)
{
    wchar_t pattern[PATH_CCH], path[PATH_CCH];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    if (!g_env.cacheDir[0]) return;
    path_join(pattern, ARRAY_LEN(pattern), g_env.cacheDir, L"*");
    h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        path_join(path, ARRAY_LEN(path), g_env.cacheDir, fd.cFileName);
        fn(path, &fd, ctx);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static void stat_fn(const wchar_t *path, const WIN32_FIND_DATAW *fd, void *ctx)
{
    DirStat *st = (DirStat *)ctx;
    (void)path;
    st->files++;
    st->bytes += ((ULONGLONG)fd->nFileSizeHigh << 32) | fd->nFileSizeLow;
}

/* Counts the files mstsc keeps in its screen (bitmap) cache. */
void Cache_Stat(DirStat *st)
{
    st->files = 0;
    st->bytes = 0;
    for_each_cache_file(stat_fn, st);
}

typedef struct ClearCtx { int deleted, failed; } ClearCtx;

static void clear_fn(const wchar_t *path, const WIN32_FIND_DATAW *fd, void *ctx)
{
    ClearCtx *c = (ClearCtx *)ctx;
    if (fd->dwFileAttributes & FILE_ATTRIBUTE_READONLY)
        SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
    if (DeleteFileW(path)) c->deleted++;
    else c->failed++;
}

/* Deletes the cache files. Files held open by a live session are counted as failed. */
void Cache_Clear(int *deleted, int *failed)
{
    ClearCtx c = { 0, 0 };
    for_each_cache_file(clear_fn, &c);
    if (deleted) *deleted = c.deleted;
    if (failed) *failed = c.failed;
}

BOOL Jump_Stat(ULONGLONG *bytes)
{
    ULONGLONG a = 0, b = 0;
    BOOL hasA = g_env.jumpAuto[0] && file_size(g_env.jumpAuto, &a);
    BOOL hasB = g_env.jumpCustom[0] && file_size(g_env.jumpCustom, &b);
    if (bytes) *bytes = (hasA ? a : 0) + (hasB ? b : 0);
    return hasA || hasB;
}

/* Removes the "recent" list shown when right-clicking Remote Desktop Connection on the taskbar. */
BOOL Jump_Clear(void)
{
    BOOL ok = TRUE;
    if (g_env.jumpAuto[0] && file_exists(g_env.jumpAuto) && !DeleteFileW(g_env.jumpAuto)) ok = FALSE;
    if (g_env.jumpCustom[0] && file_exists(g_env.jumpCustom) && !DeleteFileW(g_env.jumpCustom)) ok = FALSE;
    return ok;
}

/* Sends a file to the Recycle Bin. */
BOOL File_Recycle(const wchar_t *path)
{
    wchar_t from[PATH_CCH + 2];
    SHFILEOPSTRUCTW op;
    size_t len = wcslen(path);

    if (len + 2 > ARRAY_LEN(from)) return FALSE;
    ZeroMemory(from, sizeof(from));             /* the path list must end with two NULs */
    memcpy(from, path, len * sizeof(wchar_t));

    ZeroMemory(&op, sizeof(op));
    op.wFunc = FO_DELETE;
    op.pFrom = from;
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
}

BOOL Mstsc_IsRunning(void)
{
    PROCESSENTRY32W pe;
    BOOL found = FALSE;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (snap == INVALID_HANDLE_VALUE) return FALSE;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (str_ieq(pe.szExeFile, L"mstsc.exe")) {
                found = TRUE;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}
