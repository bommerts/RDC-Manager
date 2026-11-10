/*
 * RDC Manager - everything Remote Desktop Connection remembers about a
 * computer: dropdown entry, username hint, certificate, device prompt answer
 * and saved credential.
 */
#include "rdcm.h"

/* ------------------------------------------------------------- registry */

/* Opens a key below the Terminal Server Client root. sub may be NULL for the root itself. */
LSTATUS Reg_OpenSub(const wchar_t *sub, REGSAM sam, BOOL create, HKEY *out)
{
    wchar_t path[900];
    HRESULT hr;

    *out = NULL;
    if (sub && *sub)
        hr = StringCchPrintfW(path, ARRAY_LEN(path), L"%s\\%s", g_env.regRoot, sub);
    else
        hr = StringCchCopyW(path, ARRAY_LEN(path), g_env.regRoot);
    if (FAILED(hr)) return ERROR_FILENAME_EXCED_RANGE;

    if (create)
        return RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, NULL, 0, sam, NULL, out, NULL);
    return RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, sam, out);
}

BOOL Reg_GetSz(HKEY k, const wchar_t *name, wchar_t *out, DWORD cch)
{
    DWORD cb = cch * sizeof(wchar_t);
    LSTATUS s = RegGetValueW(k, NULL, name,
                             RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND,
                             NULL, out, &cb);
    if (s != ERROR_SUCCESS) {
        out[0] = 0;
        return FALSE;
    }
    return TRUE;
}

const wchar_t *Reg_TypeName(DWORD type)
{
    switch (type) {
    case REG_SZ:        return L"Text";
    case REG_EXPAND_SZ: return L"Text (expandable)";
    case REG_MULTI_SZ:  return L"Text list";
    case REG_DWORD:     return L"Number";
    case REG_QWORD:     return L"Number (64-bit)";
    case REG_BINARY:    return L"Binary";
    case REG_NONE:      return L"None";
    default:            return L"Other";
    }
}

/* Turns registry data into something readable for a list cell. */
void Reg_FormatValue(DWORD type, const BYTE *data, DWORD cb, wchar_t *out, int cch)
{
    out[0] = 0;
    switch (type) {
    case REG_SZ:
    case REG_EXPAND_SZ: {
        size_t n = cb / sizeof(wchar_t);
        if (n >= (size_t)cch) n = (size_t)cch - 1;
        memcpy(out, data, n * sizeof(wchar_t));
        out[n] = 0;
        break;
    }
    case REG_MULTI_SZ: {
        const wchar_t *p = (const wchar_t *)data;
        size_t n = cb / sizeof(wchar_t), i;
        int o = 0;
        for (i = 0; i < n && o < cch - 3; i++) {
            if (p[i]) {
                out[o++] = p[i];
            } else if (i + 1 < n && p[i + 1]) {
                out[o++] = L';';
                out[o++] = L' ';
            }
        }
        out[o] = 0;
        break;
    }
    case REG_DWORD:
        if (cb >= sizeof(DWORD)) {
            DWORD v;
            memcpy(&v, data, sizeof(v));
            StringCchPrintfW(out, (size_t)cch, L"%u (0x%X)", v, v);
        }
        break;
    case REG_QWORD:
        if (cb >= sizeof(ULONGLONG)) {
            ULONGLONG v;
            memcpy(&v, data, sizeof(v));
            StringCchPrintfW(out, (size_t)cch, L"%I64u", v);
        }
        break;
    default: {
        DWORD i, shown = cb > 24 ? 24 : cb;
        wchar_t part[40];
        for (i = 0; i < shown; i++) {
            StringCchPrintfW(part, ARRAY_LEN(part), i ? L" %02X" : L"%02X", data[i]);
            StringCchCatW(out, (size_t)cch, part);
        }
        if (cb > shown) {
            StringCchPrintfW(part, ARRAY_LEN(part), L" ... (%u bytes)", cb);
            StringCchCatW(out, (size_t)cch, part);
        }
        break;
    }
    }
}

/* Host names become registry key names, so they can't contain a backslash. */
static BOOL host_key_ok(const wchar_t *host)
{
    return host && host[0] && !wcschr(host, L'\\') && wcslen(host) < NAME_MAX_CCH;
}

/* ---------------------------------------------------------- credentials */

#define CRED_PREFIX     L"TERMSRV/"
#define CRED_PREFIX_LEN 8

int Creds_List(CredInfo **out)
{
    CredInfo *v = NULL;
    int n = 0;

    *out = NULL;
    if (g_env.sandbox) {
        HKEY k;
        DWORD i;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, g_env.credKey, 0, KEY_READ, &k) != ERROR_SUCCESS)
            return 0;
        for (i = 0;; i++) {
            wchar_t name[NAME_MAX_CCH], user[NAME_MAX_CCH];
            DWORD cch = ARRAY_LEN(name);
            if (RegEnumValueW(k, i, name, &cch, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
            Reg_GetSz(k, name, user, ARRAY_LEN(user));
            v = (CredInfo *)xrealloc(v, ((size_t)n + 1) * sizeof(CredInfo));
            StringCchCopyW(v[n].host, NAME_MAX_CCH, name);
            StringCchCopyW(v[n].user, NAME_MAX_CCH, user);
            n++;
        }
        RegCloseKey(k);
    } else {
        PCREDENTIALW *creds = NULL;
        DWORD count = 0, i;
        if (!CredEnumerateW(CRED_PREFIX L"*", 0, &count, &creds)) return 0;
        for (i = 0; i < count; i++) {
            const wchar_t *target = creds[i]->TargetName;
            if (!target || !str_istarts(target, CRED_PREFIX) || !target[CRED_PREFIX_LEN]) continue;
            v = (CredInfo *)xrealloc(v, ((size_t)n + 1) * sizeof(CredInfo));
            StringCchCopyW(v[n].host, NAME_MAX_CCH, target + CRED_PREFIX_LEN);
            StringCchCopyW(v[n].user, NAME_MAX_CCH, creds[i]->UserName ? creds[i]->UserName : L"");
            n++;
        }
        CredFree(creds);
    }
    *out = v;
    return n;
}

BOOL Host_DeleteCred(const wchar_t *host)
{
    BOOL any = FALSE;

    if (g_env.sandbox) {
        HKEY k;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, g_env.credKey, 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
            any = RegDeleteValueW(k, host) == ERROR_SUCCESS;
            RegCloseKey(k);
        }
    } else {
        static const DWORD types[] = {
            CRED_TYPE_GENERIC, CRED_TYPE_DOMAIN_PASSWORD,
            CRED_TYPE_DOMAIN_CERTIFICATE, CRED_TYPE_DOMAIN_VISIBLE_PASSWORD
        };
        wchar_t target[NAME_MAX_CCH + CRED_PREFIX_LEN + 1];
        size_t i;
        if (FAILED(StringCchPrintfW(target, ARRAY_LEN(target), CRED_PREFIX L"%s", host)))
            return FALSE;
        for (i = 0; i < ARRAY_LEN(types); i++)
            if (CredDeleteW(target, types[i], 0)) any = TRUE;
    }
    return any;
}

/* ------------------------------------------------------------ host list */

static Host *hosts_get(HostList *list, const wchar_t *name)
{
    Host *h;
    int i;

    for (i = 0; i < list->n; i++)
        if (str_ieq(list->v[i].name, name)) return &list->v[i];

    if (list->n == list->cap) {
        list->cap = list->cap ? list->cap * 2 : 16;
        list->v = (Host *)xrealloc(list->v, (size_t)list->cap * sizeof(Host));
    }
    h = &list->v[list->n++];
    ZeroMemory(h, sizeof(*h));
    StringCchCopyW(h->name, NAME_MAX_CCH, name);
    h->mru = -1;
    return h;
}

static int __cdecl host_cmp(const void *a, const void *b)
{
    const Host *x = (const Host *)a, *y = (const Host *)b;
    if (x->mru >= 0 && y->mru >= 0) return x->mru - y->mru;
    if (x->mru >= 0) return -1;
    if (y->mru >= 0) return 1;
    return StrCmpLogicalW(x->name, y->name);
}

Host *Model_Find(HostList *list, const wchar_t *name)
{
    int i;
    for (i = 0; i < list->n; i++)
        if (str_ieq(list->v[i].name, name)) return &list->v[i];
    return NULL;
}

void Model_Free(HostList *list)
{
    free(list->v);
    list->v = NULL;
    list->n = list->cap = 0;
}

/* Builds one row per computer from all four places mstsc stores data. */
void Model_Load(HostList *list)
{
    wchar_t mru[MRU_MAX][NAME_MAX_CCH];
    CredInfo *creds = NULL;
    int i, n;
    HKEY k;

    list->n = 0;

    n = Mru_Read(mru);
    for (i = 0; i < n; i++)
        hosts_get(list, mru[i])->mru = i;

    if (Reg_OpenSub(L"Servers", KEY_READ, FALSE, &k) == ERROR_SUCCESS) {
        DWORD idx;
        for (idx = 0;; idx++) {
            wchar_t name[NAME_MAX_CCH];
            DWORD cch = ARRAY_LEN(name), cb = 0;
            LSTATUS s = RegEnumKeyExW(k, idx, name, &cch, NULL, NULL, NULL, NULL);
            HKEY sk;
            Host *h;
            if (s == ERROR_MORE_DATA) continue;         /* name too long for us to manage */
            if (s != ERROR_SUCCESS) break;
            h = hosts_get(list, name);
            if (RegOpenKeyExW(k, name, 0, KEY_READ, &sk) != ERROR_SUCCESS) continue;
            h->hasUser = Reg_GetSz(sk, L"UsernameHint", h->user, NAME_MAX_CCH);
            h->hasCert = RegQueryValueExW(sk, L"CertHash", NULL, NULL, NULL, &cb) == ERROR_SUCCESS;
            RegCloseKey(sk);
        }
        RegCloseKey(k);
    }

    if (Reg_OpenSub(L"LocalDevices", KEY_READ, FALSE, &k) == ERROR_SUCCESS) {
        DWORD idx;
        for (idx = 0;; idx++) {
            wchar_t name[NAME_MAX_CCH];
            DWORD cch = ARRAY_LEN(name), data = 0, cb = sizeof(data);
            LSTATUS s = RegEnumValueW(k, idx, name, &cch, NULL, NULL, NULL, NULL);
            Host *h;
            if (s == ERROR_MORE_DATA) continue;         /* name too long for us to manage */
            if (s != ERROR_SUCCESS) break;
            if (!name[0]) continue;
            h = hosts_get(list, name);
            h->hasDevices = TRUE;
            if (RegGetValueW(k, NULL, name, RRF_RT_REG_DWORD, NULL, &data, &cb) != ERROR_SUCCESS)
                data = 0;
            h->devices = data;
        }
        RegCloseKey(k);
    }

    n = Creds_List(&creds);
    for (i = 0; i < n; i++) {
        Host *h = hosts_get(list, creds[i].host);
        h->hasCred = TRUE;
        StringCchCopyW(h->credUser, NAME_MAX_CCH, creds[i].user);
    }
    free(creds);

    if (list->n > 1) qsort(list->v, (size_t)list->n, sizeof(Host), host_cmp);
}

/* ------------------------------------------------------------- dropdown */

/* Reads MRU0..MRU9 in order, skipping gaps, blanks and duplicates. */
int Mru_Read(wchar_t names[MRU_MAX][NAME_MAX_CCH])
{
    HKEY k;
    int n = 0, i, j;

    if (Reg_OpenSub(L"Default", KEY_READ, FALSE, &k) != ERROR_SUCCESS) return 0;
    for (i = 0; i < MRU_MAX; i++) {
        wchar_t vn[16], val[NAME_MAX_CCH];
        BOOL dup = FALSE;
        StringCchPrintfW(vn, ARRAY_LEN(vn), L"MRU%d", i);
        if (!Reg_GetSz(k, vn, val, ARRAY_LEN(val)) || !val[0]) continue;
        for (j = 0; j < n; j++)
            if (str_ieq(names[j], val)) dup = TRUE;
        if (dup) continue;
        StringCchCopyW(names[n++], NAME_MAX_CCH, val);
    }
    RegCloseKey(k);
    return n;
}

/* Rewrites the dropdown as MRU0..MRU(count-1) with no gaps. */
BOOL Mru_Write(wchar_t names[][NAME_MAX_CCH], int count)
{
    HKEY k;
    BOOL ok = TRUE;
    int i;

    if (count > MRU_MAX) count = MRU_MAX;
    if (Reg_OpenSub(L"Default", KEY_READ | KEY_WRITE, TRUE, &k) != ERROR_SUCCESS) return FALSE;
    for (i = 0; i < MRU_MAX; i++) {
        wchar_t vn[16];
        StringCchPrintfW(vn, ARRAY_LEN(vn), L"MRU%d", i);
        if (i < count) {
            DWORD cb = (DWORD)((wcslen(names[i]) + 1) * sizeof(wchar_t));
            if (RegSetValueExW(k, vn, 0, REG_SZ, (const BYTE *)names[i], cb) != ERROR_SUCCESS)
                ok = FALSE;
        } else {
            LSTATUS s = RegDeleteValueW(k, vn);
            if (s != ERROR_SUCCESS && s != ERROR_FILE_NOT_FOUND) ok = FALSE;
        }
    }
    RegCloseKey(k);
    return ok;
}

static int mru_index(wchar_t names[MRU_MAX][NAME_MAX_CCH], int n, const wchar_t *host)
{
    int i;
    for (i = 0; i < n; i++)
        if (str_ieq(names[i], host)) return i;
    return -1;
}

BOOL Host_AddToMru(const wchar_t *host)
{
    wchar_t cur[MRU_MAX][NAME_MAX_CCH], next[MRU_MAX][NAME_MAX_CCH];
    int n, i, count = 0;

    if (!host || !host[0] || wcslen(host) >= NAME_MAX_CCH) return FALSE;
    n = Mru_Read(cur);
    StringCchCopyW(next[count++], NAME_MAX_CCH, host);
    for (i = 0; i < n && count < MRU_MAX; i++) {
        if (str_ieq(cur[i], host)) continue;
        StringCchCopyW(next[count++], NAME_MAX_CCH, cur[i]);
    }
    return Mru_Write(next, count);
}

BOOL Host_RemoveFromMru(const wchar_t *host)
{
    wchar_t cur[MRU_MAX][NAME_MAX_CCH];
    int n = Mru_Read(cur), at, i;

    at = mru_index(cur, n, host);
    if (at < 0) return TRUE;
    for (i = at; i < n - 1; i++)
        StringCchCopyW(cur[i], NAME_MAX_CCH, cur[i + 1]);
    return Mru_Write(cur, n - 1);
}

/* Moves a dropdown entry up (delta < 0) or down (delta > 0) by one place. */
BOOL Host_MoveMru(const wchar_t *host, int delta)
{
    wchar_t cur[MRU_MAX][NAME_MAX_CCH], tmp[NAME_MAX_CCH];
    int n = Mru_Read(cur), at, to;

    at = mru_index(cur, n, host);
    if (at < 0) return FALSE;
    to = at + (delta < 0 ? -1 : 1);
    if (to < 0 || to >= n) return FALSE;
    StringCchCopyW(tmp, NAME_MAX_CCH, cur[at]);
    StringCchCopyW(cur[at], NAME_MAX_CCH, cur[to]);
    StringCchCopyW(cur[to], NAME_MAX_CCH, tmp);
    return Mru_Write(cur, n);
}

/*
 * Moves a dropdown entry to a given place (0 = top), keeping the others in
 * their order. A place past the end means the bottom.
 */
BOOL Host_MoveMruTo(const wchar_t *host, int to)
{
    wchar_t cur[MRU_MAX][NAME_MAX_CCH], tmp[NAME_MAX_CCH];
    int n = Mru_Read(cur), at, i;

    if (n > MRU_MAX) n = MRU_MAX;
    at = mru_index(cur, n, host);
    if (at < 0) return FALSE;
    if (to < 0) to = 0;
    if (to > n - 1) to = n - 1;
    if (to == at) return TRUE;
    StringCchCopyW(tmp, NAME_MAX_CCH, cur[at]);
    for (i = at; i < to; i++) StringCchCopyW(cur[i], NAME_MAX_CCH, cur[i + 1]);
    for (i = at; i > to; i--) StringCchCopyW(cur[i], NAME_MAX_CCH, cur[i - 1]);
    StringCchCopyW(cur[to], NAME_MAX_CCH, tmp);
    return Mru_Write(cur, n);
}

/* -------------------------------------------------------- per-host data */

static BOOL server_path(const wchar_t *host, wchar_t *out, size_t cch)
{
    if (!host_key_ok(host)) return FALSE;
    return SUCCEEDED(StringCchPrintfW(out, cch, L"Servers\\%s", host));
}

/* Deletes Servers\<host> once it holds nothing, so no empty keys pile up. */
static void server_prune(const wchar_t *host)
{
    wchar_t sub[NAME_MAX_CCH + 16];
    DWORD subkeys = 1, values = 1;
    HKEY k;

    if (!server_path(host, sub, ARRAY_LEN(sub))) return;
    if (Reg_OpenSub(sub, KEY_READ, FALSE, &k) != ERROR_SUCCESS) return;
    RegQueryInfoKeyW(k, NULL, NULL, NULL, &subkeys, NULL, NULL, &values, NULL, NULL, NULL, NULL);
    RegCloseKey(k);
    if (subkeys || values) return;
    if (Reg_OpenSub(L"Servers", KEY_READ | KEY_WRITE, FALSE, &k) == ERROR_SUCCESS) {
        RegDeleteKeyW(k, host);
        RegCloseKey(k);
    }
}

static BOOL server_delete_value(const wchar_t *host, const wchar_t *value)
{
    wchar_t sub[NAME_MAX_CCH + 16];
    LSTATUS s;
    HKEY k;

    if (!server_path(host, sub, ARRAY_LEN(sub))) return FALSE;
    s = Reg_OpenSub(sub, KEY_SET_VALUE, FALSE, &k);
    if (s == ERROR_FILE_NOT_FOUND) return TRUE;
    if (s != ERROR_SUCCESS) return FALSE;
    s = RegDeleteValueW(k, value);
    RegCloseKey(k);
    server_prune(host);
    return s == ERROR_SUCCESS || s == ERROR_FILE_NOT_FOUND;
}

BOOL Host_SetUser(const wchar_t *host, const wchar_t *user)
{
    wchar_t sub[NAME_MAX_CCH + 16];
    LSTATUS s;
    HKEY k;

    if (!user || !user[0]) return Host_ClearUser(host);
    if (!server_path(host, sub, ARRAY_LEN(sub))) return FALSE;
    if (Reg_OpenSub(sub, KEY_SET_VALUE, TRUE, &k) != ERROR_SUCCESS) return FALSE;
    s = RegSetValueExW(k, L"UsernameHint", 0, REG_SZ, (const BYTE *)user,
                       (DWORD)((wcslen(user) + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    return s == ERROR_SUCCESS;
}

BOOL Host_ClearUser(const wchar_t *host)
{
    return server_delete_value(host, L"UsernameHint");
}

BOOL Host_ClearCert(const wchar_t *host)
{
    return server_delete_value(host, L"CertHash");
}

BOOL Host_ClearDevices(const wchar_t *host)
{
    LSTATUS s;
    HKEY k;

    s = Reg_OpenSub(L"LocalDevices", KEY_SET_VALUE, FALSE, &k);
    if (s == ERROR_FILE_NOT_FOUND) return TRUE;
    if (s != ERROR_SUCCESS) return FALSE;
    s = RegDeleteValueW(k, host);
    RegCloseKey(k);
    return s == ERROR_SUCCESS || s == ERROR_FILE_NOT_FOUND;
}

/* Forgets a computer completely. */
void Host_RemoveAll(const wchar_t *host)
{
    HKEY k;

    Host_RemoveFromMru(host);
    if (host_key_ok(host) && Reg_OpenSub(L"Servers", KEY_READ | KEY_WRITE, FALSE, &k) == ERROR_SUCCESS) {
        RegDeleteTreeW(k, host);
        RegDeleteKeyW(k, host);
        RegCloseKey(k);
    }
    Host_ClearDevices(host);
    Host_DeleteCred(host);
}
