/*
 * RDC Manager - the main window: navigation bar, one list per view, and the
 * commands that act on the selection.
 */
#include "ui.h"

#define MAIN_CLASS   L"RdcManagerMain"
#define DASH         L"\u2014"          /* shown in empty cells; drawn dimmed */
#define ID_NAV_BASE  1000
#define ID_LIST_BASE 1100
#define ID_ABOUT     1200
#define TIMER_MSTSC  1
#define TIMER_ANIM   2                  /* rows sliding into place */
#define TIMER_DRAG   3                  /* scrolls the list while a drag sits near its edge */
#define ANIM_MS      150

enum { V_COMPUTERS, V_DEFAULTS, V_CLEANUP, V_BACKUPS, V_ADVANCED, V_COUNT };

enum {
    CM_REFRESH = 2000,
    CM_C_ADD, CM_C_CONNECT, CM_C_USER, CM_C_UP, CM_C_DOWN, CM_C_MORE, CM_C_REMOVE,
    CM_C_ADDMRU, CM_C_RMMRU, CM_C_FUSER, CM_C_FCERT, CM_C_FDEV, CM_C_FCRED, CM_C_COPY,
    CM_D_EDIT, CM_D_ADD, CM_D_DELETE, CM_D_NOTEPAD, CM_D_RESET,
    CM_X_CLEAR, CM_X_CLEARALL,
    CM_B_NOW, CM_B_RESTORE, CM_B_DELETE, CM_B_FOLDER,
    CM_A_REGEDIT, CM_A_COPY,
    CM_C_TOP, CM_C_BOTTOM
};

enum { CL_MRU, CL_USERS, CL_CERTS, CL_DEVICES, CL_CREDS, CL_CACHE, CL_JUMP, CL_COUNT };

typedef struct ColDef { const wchar_t *title; int width; } ColDef;     /* width 0 = takes the spare room */
typedef struct BtnDef { int id; const wchar_t *text; int kind; BOOL right; } BtnDef;

typedef struct View {
    const wchar_t *name;
    const wchar_t *empty;
    const ColDef  *cols;
    int            ncols;
    const BtnDef  *btns;
    int            nbtns;
    DWORD          listStyle;
    HWND           list, nav, btn[10];
} View;

static const ColDef kColsComputers[] = {
    { L"Computer", 0 }, { L"Dropdown", 90 }, { L"Username", 210 },
    { L"Certificate", 100 }, { L"Device prompt", 125 }, { L"Password", 90 }
};
static const ColDef kColsDefaults[] = {
    { L"Setting", 210 }, { L"Value", 190 }, { L"Type", 80 }, { L"What it does", 0 }
};
static const ColDef kColsCleanup[] = {
    { L"Item", 190 }, { L"Stored now", 170 }, { L"Can be restored", 190 }, { L"Where it lives", 0 }
};
static const ColDef kColsBackups[] = {
    { L"Created", 170 }, { L"Contents", 170 }, { L"Reason", 150 }, { L"Size", 80 }, { L"File", 0 }
};
static const ColDef kColsAdvanced[] = {
    { L"Location", 0 }, { L"Name", 200 }, { L"Value", 210 }, { L"Type", 100 }
};

static const BtnDef kBtnsComputers[] = {
    { CM_C_ADD, L"Add\u2026", BK_PRIMARY, FALSE },
    { CM_C_CONNECT, L"Connect", BK_NORMAL, FALSE },
    { CM_C_USER, L"Edit username\u2026", BK_NORMAL, FALSE },
    { CM_C_UP, L"Move up", BK_NORMAL, FALSE },
    { CM_C_DOWN, L"Move down", BK_NORMAL, FALSE },
    { CM_C_MORE, L"More\u2026", BK_NORMAL, FALSE },
    { CM_C_REMOVE, L"Remove", BK_DANGER, FALSE },
    { CM_REFRESH, L"Refresh", BK_NORMAL, TRUE }
};
static const BtnDef kBtnsDefaults[] = {
    { CM_D_EDIT, L"Edit\u2026", BK_PRIMARY, FALSE },
    { CM_D_ADD, L"Add\u2026", BK_NORMAL, FALSE },
    { CM_D_DELETE, L"Delete", BK_NORMAL, FALSE },
    { CM_D_NOTEPAD, L"Open in Notepad", BK_NORMAL, FALSE },
    { CM_D_RESET, L"Reset to defaults", BK_DANGER, FALSE },
    { CM_REFRESH, L"Refresh", BK_NORMAL, TRUE }
};
static const BtnDef kBtnsCleanup[] = {
    { CM_X_CLEAR, L"Clear selected", BK_PRIMARY, FALSE },
    { CM_X_CLEARALL, L"Clear everything", BK_DANGER, FALSE },
    { CM_REFRESH, L"Refresh", BK_NORMAL, TRUE }
};
static const BtnDef kBtnsBackups[] = {
    { CM_B_NOW, L"Back up now", BK_PRIMARY, FALSE },
    { CM_B_RESTORE, L"Restore\u2026", BK_NORMAL, FALSE },
    { CM_B_DELETE, L"Delete", BK_NORMAL, FALSE },
    { CM_B_FOLDER, L"Open folder", BK_NORMAL, FALSE },
    { CM_REFRESH, L"Refresh", BK_NORMAL, TRUE }
};
static const BtnDef kBtnsAdvanced[] = {
    { CM_A_REGEDIT, L"Open in Registry Editor", BK_NORMAL, FALSE },
    { CM_A_COPY, L"Copy", BK_NORMAL, FALSE },
    { CM_REFRESH, L"Refresh", BK_NORMAL, TRUE }
};

static View g_views[V_COUNT] = {
    { L"Computers", L"No remote computers are remembered on this PC.",
      kColsComputers, ARRAY_LEN(kColsComputers), kBtnsComputers, ARRAY_LEN(kBtnsComputers), 0 },
    { L"Connection defaults", L"Default.rdp doesn't exist yet. Remote Desktop Connection creates it when it first runs.",
      kColsDefaults, ARRAY_LEN(kColsDefaults), kBtnsDefaults, ARRAY_LEN(kBtnsDefaults), 0 },
    { L"Cleanup", L"",
      kColsCleanup, ARRAY_LEN(kColsCleanup), kBtnsCleanup, ARRAY_LEN(kBtnsCleanup), 0 },
    { L"Backups", L"No backups yet. One is made automatically before the first change.",
      kColsBackups, ARRAY_LEN(kColsBackups), kBtnsBackups, ARRAY_LEN(kBtnsBackups), 0 },
    { L"Advanced", L"No other Remote Desktop settings were found.",
      kColsAdvanced, ARRAY_LEN(kColsAdvanced), kBtnsAdvanced, ARRAY_LEN(kBtnsAdvanced), LVS_SINGLESEL }
};

typedef struct AdvRow { wchar_t key[700]; } AdvRow;
typedef struct NameList { wchar_t (*v)[NAME_MAX_CCH]; int n; } NameList;

static HINSTANCE   g_inst;
static HWND        g_main, g_about, g_lastFocus;
static int         g_view = V_COMPUTERS;
static BOOL        g_demo, g_ready, g_populating, g_mstsc, g_statusWarn;
static RECT        g_rcList;
static wchar_t     g_status[600];
static HostList    g_hosts;
static RdpFile     g_rdp;
static BackupInfo *g_backups;
static int         g_nBackups;
static AdvRow     *g_adv;
static int         g_nAdv, g_capAdv;

/*
 * Reordering the dropdown. A row is dragged by its handle (the dots at its
 * left edge): a translucent copy of the row follows the pointer, and the other
 * rows slide out of the way as it passes over them. Nothing is written to the
 * registry until the row is dropped inside the list.
 */
enum { DRAG_IDLE, DRAG_ARMED, DRAG_ACTIVE };            /* armed: handle pressed, not yet moved */
typedef struct RowAnim { int from; ULONGLONG start; } RowAnim;  /* slides from `from` pixels away; 0 = at rest */

static struct {
    int   state;
    int   slot, origSlot;               /* the row being dragged: where it is now, where it started */
    POINT down, pt;                     /* list client coordinates: the press, the latest position */
    POINT grab;                         /* the press, measured from the row's top left corner */
    HWND  ghost;                        /* the copy that follows the pointer */
    BOOL  eatRUp;
} g_drag;
static RowAnim g_anim[MRU_MAX];
static BOOL    g_animOn;
static int     g_nMru;                  /* rows 0 .. g_nMru-1 are the dropdown, in order */
static int     g_gripHot = -1;          /* row whose handle is under the pointer */

static void RefreshView(int v);
static void UpdateButtons(void);
static void UpdateStatus(void);

HWND Main_Window(void)
{
    return g_main;
}

/* ------------------------------------------------------ list view helpers */

static void LV_Begin(HWND lv)
{
    g_populating = TRUE;
    SendMessageW(lv, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(lv);
}

static void LV_End(HWND lv)
{
    SendMessageW(lv, WM_SETREDRAW, TRUE, 0);
    g_populating = FALSE;
    InvalidateRect(lv, NULL, TRUE);
}

static int LV_Row(HWND lv, LPARAM param, const wchar_t *text)
{
    LVITEMW it;
    ZeroMemory(&it, sizeof(it));
    it.mask = LVIF_TEXT | LVIF_PARAM;
    it.iItem = 0x7FFFFFFF;
    it.pszText = (LPWSTR)text;
    it.lParam = param;
    return (int)SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&it);
}

static void LV_Cell(HWND lv, int row, int col, const wchar_t *text)
{
    LVITEMW it;
    ZeroMemory(&it, sizeof(it));
    it.iSubItem = col;
    it.pszText = (LPWSTR)text;
    SendMessageW(lv, LVM_SETITEMTEXTW, (WPARAM)row, (LPARAM)&it);
}

static LPARAM LV_Param(HWND lv, int row)
{
    LVITEMW it;
    ZeroMemory(&it, sizeof(it));
    it.mask = LVIF_PARAM;
    it.iItem = row;
    it.lParam = -1;
    if (!SendMessageW(lv, LVM_GETITEMW, 0, (LPARAM)&it)) return -1;
    return it.lParam;
}

static int LV_SelCount(HWND lv)
{
    return (int)ListView_GetSelectedCount(lv);
}

static int LV_Next(HWND lv, int after)
{
    return ListView_GetNextItem(lv, after, LVNI_SELECTED);
}

/* The lParam of the selected row when exactly one row is selected, else -1. */
static LPARAM LV_Single(HWND lv)
{
    if (LV_SelCount(lv) != 1) return -1;
    return LV_Param(lv, LV_Next(lv, -1));
}

static void LV_SelectOnly(HWND lv, int row)
{
    ListView_SetItemState(lv, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    if (row >= 0) {
        ListView_SetItemState(lv, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(lv, row, FALSE);
    }
}

static int LV_FocusRow(HWND lv)
{
    return ListView_GetNextItem(lv, -1, LVNI_FOCUSED);
}

/* Puts the selection back on the same row number after a list was refilled. */
static void LV_RestoreRow(HWND lv, int row)
{
    int n = ListView_GetItemCount(lv);
    if (row < 0 || n == 0) return;
    if (row >= n) row = n - 1;
    LV_SelectOnly(lv, row);
}

static int ViewOfList(HWND lv)
{
    int i;
    for (i = 0; i < V_COUNT; i++)
        if (g_views[i].list == lv) return i;
    return -1;
}

static HWND ViewBtn(int view, int id)
{
    int i;
    for (i = 0; i < g_views[view].nbtns; i++)
        if (g_views[view].btns[i].id == id) return g_views[view].btn[i];
    return NULL;
}

static void EnableBtn(int view, int id, BOOL on)
{
    HWND b = ViewBtn(view, id);
    if (!b || !!IsWindowEnabled(b) == !!on) return;
    if (!on && GetFocus() == b) SetFocus(g_views[view].list);
    EnableWindow(b, on);
}

/* --------------------------------------------------------------- layout */

static void LayoutColumns(int view)
{
    const View *v = &g_views[view];
    RECT rc;
    int fixed = 0, flex, i;

    GetClientRect(v->list, &rc);
    for (i = 0; i < v->ncols; i++)
        if (v->cols[i].width) fixed += S(v->cols[i].width);
    flex = rc.right - fixed;
    if (flex < S(170)) flex = S(170);
    for (i = 0; i < v->ncols; i++)
        ListView_SetColumnWidth(v->list, i, v->cols[i].width ? S(v->cols[i].width) : flex);

    /* Changing the widths can make a scroll bar appear or go away, which changes
       the room available. One more pass settles it. */
    GetClientRect(v->list, &rc);
    if (rc.right - fixed != flex && rc.right - fixed >= S(170)) {
        flex = rc.right - fixed;
        for (i = 0; i < v->ncols; i++)
            if (!v->cols[i].width) ListView_SetColumnWidth(v->list, i, flex);
    }
}

static void Layout(void)
{
    const View *v = &g_views[g_view];
    RECT rc;
    int navH = S(46), btnH = S(30), x, y, right, w, i;

    if (!g_main || !g_views[V_COUNT - 1].list) return;     /* children not created yet */
    GetClientRect(g_main, &rc);

    x = S(8);
    for (i = 0; i < V_COUNT; i++) {
        w = Text_Width(g_th.fontBold, g_views[i].name) + S(28);
        MoveWindow(g_views[i].nav, x, 0, w, navH - 1, TRUE);
        x += w;
    }
    w = Text_Width(g_th.fontBold, L"About") + S(28);
    MoveWindow(g_about, rc.right - w - S(8), 0, w, navH - 1, TRUE);

    y = navH + S(12);
    x = S(16);
    right = rc.right - S(16);
    for (i = 0; i < v->nbtns; i++) {
        w = Text_Width(g_th.font, v->btns[i].text) + S(28);
        if (w < S(76)) w = S(76);
        if (v->btns[i].right) {
            right -= w;
            MoveWindow(v->btn[i], right, y, w, btnH, TRUE);
            right -= S(8);
        } else {
            MoveWindow(v->btn[i], x, y, w, btnH, TRUE);
            x += w + S(8);
        }
    }

    y += btnH + S(12);
    SetRect(&g_rcList, S(16), y, rc.right - S(16), rc.bottom - S(38));
    if (g_rcList.bottom < g_rcList.top + S(40)) g_rcList.bottom = g_rcList.top + S(40);
    MoveWindow(v->list, g_rcList.left + 1, g_rcList.top + 1,
               g_rcList.right - g_rcList.left - 2, g_rcList.bottom - g_rcList.top - 2, TRUE);
    LayoutColumns(g_view);
    InvalidateRect(g_main, NULL, FALSE);
}

static void PaintMain(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    HGDIOBJ old = SelectObject(dc, g_th.font);
    RECT rc, r;

    GetClientRect(hwnd, &rc);
    FillRect(dc, &rc, g_th.brBg);

    SetRect(&r, 0, 0, rc.right, S(46));
    FillRect(dc, &r, g_th.brPanel);
    r.top = r.bottom - 1;
    FillRect(dc, &r, g_th.brBorder);

    FrameRect(dc, &g_rcList, g_th.brBorder);

    SetRect(&r, S(16), rc.bottom - S(38), rc.right - S(16), rc.bottom);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, g_statusWarn ? C_WARN : C_DIM);
    DrawTextW(dc, g_status, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    SelectObject(dc, old);
    EndPaint(hwnd, &ps);
}

/* --------------------------------------------------------------- status */

static void UpdateStatus(void)
{
    wchar_t text[600];
    BOOL warn = FALSE;
    int i, inMru = 0;

    text[0] = 0;
    if (g_drag.state == DRAG_ACTIVE) {
        StringCchCopyW(text, ARRAY_LEN(text),
            L"Drop the computer where you want it in the dropdown. Press Esc, or drop it outside the list, to cancel.");
    } else if (g_mstsc) {
        warn = TRUE;
        StringCchCopyW(text, ARRAY_LEN(text),
            L"Remote Desktop Connection is open. It can overwrite the dropdown list when it connects, "
            L"so close it before making changes.");
    } else switch (g_view) {
    case V_COMPUTERS:
        for (i = 0; i < g_hosts.n; i++)
            if (g_hosts.v[i].mru >= 0) inMru++;
        StringCchPrintfW(text, ARRAY_LEN(text),
            L"%d computer%s  \u00b7  %d of %d dropdown places used  \u00b7  Drag a computer by its dots to reorder "
            L"the dropdown  \u00b7  Right-click for more options",
            g_hosts.n, g_hosts.n == 1 ? L"" : L"s", inMru, MRU_MAX);
        break;
    case V_DEFAULTS:
        StringCchPrintfW(text, ARRAY_LEN(text), L"%s", g_env.rdpPath);
        break;
    case V_CLEANUP:
        StringCchCopyW(text, ARRAY_LEN(text),
            L"Registry items are backed up before they are cleared. Passwords, the screen cache and the jump list can't be restored.");
        break;
    case V_BACKUPS:
        StringCchPrintfW(text, ARRAY_LEN(text), L"%s", g_env.backupDir);
        break;
    case V_ADVANCED:
        StringCchCopyW(text, ARRAY_LEN(text),
            L"Read-only. These settings come from Windows, Group Policy and add-ins.");
        break;
    }
    if (wcscmp(text, g_status) != 0 || warn != g_statusWarn) {
        StringCchCopyW(g_status, ARRAY_LEN(g_status), text);
        g_statusWarn = warn;
        if (g_main) InvalidateRect(g_main, NULL, FALSE);
    }
}

/* ------------------------------------------------------- shared helpers */

/*
 * Remote Desktop Connection keeps its own copy of the dropdown list and writes
 * it back when it connects, which silently undoes changes made while it is
 * open. Asks once per time it is found running.
 */
static BOOL ConfirmMstscOpen(void)
{
    static BOOL acknowledged;

    if (g_demo || !Mstsc_IsRunning()) {
        acknowledged = FALSE;
        return TRUE;
    }
    if (acknowledged) return TRUE;
    if (Dark_Dialog(g_main, L"Remote Desktop Connection is open",
                    L"Remote Desktop Connection keeps its own copy of the dropdown list and writes it "
                    L"back the next time it connects. That can undo this change.\n\n"
                    L"Close Remote Desktop Connection first, or continue anyway.",
                    L"Continue anyway", L"Cancel", BK_NORMAL) != IDOK)
        return FALSE;
    acknowledged = TRUE;
    return TRUE;
}

/* Call before every registry change. FALSE means the user backed out. */
static BOOL EnsureRegBackup(void)
{
    if (!ConfirmMstscOpen()) return FALSE;
    if (Backup_BeforeRegChange()) return TRUE;
    return Dark_Dialog(g_main, L"Backup failed",
                       L"RDC Manager couldn't save a backup of the current settings.\n\nMake the change anyway?",
                       L"Continue without a backup", L"Cancel", BK_DANGER) == IDOK;
}

static BOOL EnsureRdpBackup(void)
{
    if (Backup_BeforeRdpChange()) return TRUE;
    return Dark_Dialog(g_main, L"Backup failed",
                       L"RDC Manager couldn't save a backup of Default.rdp.\n\nMake the change anyway?",
                       L"Continue without a backup", L"Cancel", BK_DANGER) == IDOK;
}

static void SystemExe(const wchar_t *leaf, BOOL windowsDir, wchar_t *out, size_t cch)
{
    wchar_t dir[MAX_PATH];
    UINT n = windowsDir ? GetWindowsDirectoryW(dir, ARRAY_LEN(dir))
                        : GetSystemDirectoryW(dir, ARRAY_LEN(dir));
    if (n == 0 || n >= ARRAY_LEN(dir)) {
        out[0] = 0;                             /* an empty path makes the launch fail safely */
        return;
    }
    path_join(out, cch, dir, leaf);
}

static BOOL IsInteger(const wchar_t *s)
{
    if (*s == L'-') s++;
    if (!*s) return FALSE;
    for (; *s; s++)
        if (*s < L'0' || *s > L'9') return FALSE;
    return TRUE;
}

static BOOL ValidHost(const wchar_t *s)
{
    size_t n = wcslen(s), i;
    if (n == 0 || n >= 255) return FALSE;
    for (i = 0; i < n; i++) {
        wchar_t c = s[i];
        if (c <= 0x20 || c == 0x7F || c == L'"' || c == L'\\' || c == L'/') return FALSE;
    }
    return TRUE;
}

static void AppendNames(WStr *t, const NameList *sel)
{
    int show = sel->n > 8 ? 7 : sel->n, i;
    for (i = 0; i < show; i++) ws_appendf(t, L"\n     \u2022  %s", sel->v[i]);
    if (sel->n > show) ws_appendf(t, L"\n     \u2022  and %d more", sel->n - show);
}

/* ------------------------------------------------------------ computers */

static Host *HostAtRow(int row)
{
    LPARAM i = LV_Param(g_views[V_COMPUTERS].list, row);
    return (i >= 0 && i < g_hosts.n) ? &g_hosts.v[i] : NULL;
}

static Host *SingleHost(void)
{
    LPARAM i = LV_Single(g_views[V_COMPUTERS].list);
    return (i >= 0 && i < g_hosts.n) ? &g_hosts.v[i] : NULL;
}

static NameList SelectedHosts(void)
{
    HWND lv = g_views[V_COMPUTERS].list;
    NameList nl = { NULL, 0 };
    int count = LV_SelCount(lv), i = -1;

    if (count <= 0) return nl;
    nl.v = (wchar_t (*)[NAME_MAX_CCH])xalloc((size_t)count * sizeof(*nl.v));
    while (nl.n < count && (i = LV_Next(lv, i)) != -1) {
        Host *h = HostAtRow(i);
        if (h) StringCchCopyW(nl.v[nl.n++], NAME_MAX_CCH, h->name);
    }
    return nl;
}

static BOOL NameListHas(const NameList *nl, const wchar_t *name)
{
    int i;
    for (i = 0; i < nl->n; i++)
        if (str_ieq(nl->v[i], name)) return TRUE;
    return FALSE;
}

static void FillComputerRow(HWND lv, int row, const Host *h)
{
    wchar_t pos[16];

    if (h->mru >= 0) StringCchPrintfW(pos, ARRAY_LEN(pos), L"#%d", h->mru + 1);
    else StringCchCopyW(pos, ARRAY_LEN(pos), DASH);
    LV_Cell(lv, row, 0, h->name);
    LV_Cell(lv, row, 1, pos);
    LV_Cell(lv, row, 2, h->hasUser && h->user[0] ? h->user
                      : h->hasCred && h->credUser[0] ? h->credUser : DASH);
    LV_Cell(lv, row, 3, h->hasCert ? L"Trusted" : DASH);
    LV_Cell(lv, row, 4, h->hasDevices ? L"Remembered" : DASH);
    LV_Cell(lv, row, 5, h->hasCred ? L"Saved" : DASH);
}

static int RowHeight(void)
{
    RECT r;
    if (ListView_GetItemRect(g_views[V_COMPUTERS].list, 0, &r, LVIR_BOUNDS) && r.bottom > r.top)
        return r.bottom - r.top;
    return S(30);
}

/* ------------------------------------------- rows sliding into place */

static void InvalidateMruRows(void)
{
    HWND lv = g_views[V_COMPUTERS].list;
    RECT first, last, client;

    if (g_nMru <= 0 || !ListView_GetItemRect(lv, 0, &first, LVIR_BOUNDS) ||
        !ListView_GetItemRect(lv, g_nMru - 1, &last, LVIR_BOUNDS))
        return;
    GetClientRect(lv, &client);
    client.top = first.top;
    client.bottom = last.bottom;
    InvalidateRect(lv, &client, FALSE);
}

/* How far a row is drawn from its place right now. It eases out to 0. */
static int Anim_Dy(int row)
{
    ULONGLONG t;
    LONGLONG u;

    if (row < 0 || row >= MRU_MAX || !g_anim[row].from) return 0;
    t = GetTickCount64() - g_anim[row].start;
    if (t >= ANIM_MS) return 0;
    u = 1000 - (LONGLONG)t * 1000 / ANIM_MS;
    return (int)(g_anim[row].from * u * u * u / 1000000000LL);
}

static BOOL Anim_Running(void)
{
    ULONGLONG now = GetTickCount64();
    int i;
    for (i = 0; i < MRU_MAX; i++)
        if (g_anim[i].from && now - g_anim[i].start < ANIM_MS) return TRUE;
    return FALSE;
}

static void Anim_Kick(void)
{
    if (!g_animOn) {
        g_animOn = TRUE;
        SetTimer(g_main, TIMER_ANIM, 10, NULL);
    }
    InvalidateMruRows();
}

static void Anim_Tick(void)
{
    if (!Anim_Running()) {
        ZeroMemory(g_anim, sizeof(g_anim));
        g_animOn = FALSE;
        KillTimer(g_main, TIMER_ANIM);
    }
    InvalidateMruRows();
}

/* While this is TRUE the dropdown rows are painted by PaintReorderRows. */
static BOOL ReorderPainting(void)
{
    return g_drag.state == DRAG_ACTIVE || g_animOn;
}

/* The dropdown order before a change, so the rows can slide to their new places after it. */
typedef struct MruSnap { wchar_t names[MRU_MAX][NAME_MAX_CCH]; int n; } MruSnap;

static void Snap_Take(MruSnap *s)
{
    for (s->n = 0; s->n < g_nMru && s->n < MRU_MAX; s->n++)
        StringCchCopyW(s->names[s->n], NAME_MAX_CCH, g_hosts.v[s->n].name);
}

static void Snap_Animate(const MruSnap *s)
{
    ULONGLONG now = GetTickCount64();
    int h = RowHeight(), i, j;
    BOOL any = FALSE;

    for (i = 0; i < g_nMru && i < MRU_MAX; i++)
        for (j = 0; j < s->n; j++)
            if (j != i && str_ieq(s->names[j], g_hosts.v[i].name)) {
                g_anim[i].from = (j - i) * h;
                g_anim[i].start = now;
                any = TRUE;
            }
    if (any) Anim_Kick();
}

static void Drag_Stop(void);

static void RefreshComputers(void)
{
    HWND lv = g_views[V_COMPUTERS].list;
    NameList keep;
    wchar_t focus[NAME_MAX_CCH] = L"";
    Host *fh;
    int i;

    Drag_Stop();                        /* the rows are about to be rebuilt under it */
    ZeroMemory(g_anim, sizeof(g_anim));
    g_gripHot = -1;
    keep = SelectedHosts();
    fh = HostAtRow(LV_FocusRow(lv));
    if (fh) StringCchCopyW(focus, ARRAY_LEN(focus), fh->name);
    Model_Load(&g_hosts);
    for (g_nMru = 0; g_nMru < g_hosts.n && g_nMru < MRU_MAX && g_hosts.v[g_nMru].mru >= 0; g_nMru++) { }

    LV_Begin(lv);
    for (i = 0; i < g_hosts.n; i++) {
        const Host *h = &g_hosts.v[i];
        int row = LV_Row(lv, i, h->name);

        FillComputerRow(lv, row, h);

        if (NameListHas(&keep, h->name))
            ListView_SetItemState(lv, row, LVIS_SELECTED, LVIS_SELECTED);
        if (focus[0] && str_ieq(focus, h->name)) {
            ListView_SetItemState(lv, row, LVIS_FOCUSED, LVIS_FOCUSED);
            ListView_EnsureVisible(lv, row, FALSE);
        }
    }
    LV_End(lv);
    free(keep.v);
}

static void SelectHost(const wchar_t *name)
{
    HWND lv = g_views[V_COMPUTERS].list;
    int i, n = ListView_GetItemCount(lv);
    for (i = 0; i < n; i++) {
        Host *h = HostAtRow(i);
        if (h && str_ieq(h->name, name)) {
            LV_SelectOnly(lv, i);
            return;
        }
    }
}

static void Cmd_C_Add(void)
{
    wchar_t name[NAME_MAX_CCH] = L"", user[NAME_MAX_CCH] = L"";

    for (;;) {
        if (!Dark_Input(g_main, L"Add computer",
                        L"Adds a computer to the top of the Remote Desktop Connection dropdown list.",
                        L"Computer name or IP address", name, ARRAY_LEN(name),
                        L"Username (optional)", user, ARRAY_LEN(user)))
            return;
        str_trim(name);
        str_trim(user);
        if (ValidHost(name)) break;
        Dark_Info(g_main, L"Add computer",
                  L"Enter a computer name or IP address without spaces, quotes or slashes.");
    }
    if (!EnsureRegBackup()) return;
    if (!Host_AddToMru(name)) {
        Dark_Info(g_main, L"Add computer", L"The dropdown list couldn't be updated.");
        return;
    }
    if (user[0]) Host_SetUser(name, user);
    RefreshView(V_COMPUTERS);
    SelectHost(name);
}

static void Cmd_C_Connect(void)
{
    Host *h = SingleHost();
    wchar_t exe[MAX_PATH + 32], args[NAME_MAX_CCH + 16];

    if (!h) return;
    if (g_demo) {
        WStr t = { 0 };
        ws_appendf(&t, L"Demo mode: this would open a Remote Desktop connection to \"%s\".", h->name);
        Dark_Info(g_main, L"Connect", t.p);
        ws_free(&t);
        return;
    }
    if (wcschr(h->name, L'"')) return;
    SystemExe(L"mstsc.exe", FALSE, exe, ARRAY_LEN(exe));
    if (!exe[0]) return;
    StringCchPrintfW(args, ARRAY_LEN(args), L"/v:\"%s\"", h->name);
    ShellExecuteW(g_main, L"open", exe, args, NULL, SW_SHOWNORMAL);
}

static void Cmd_C_EditUser(void)
{
    Host *h = SingleHost();
    wchar_t name[NAME_MAX_CCH], user[NAME_MAX_CCH], label[NAME_MAX_CCH + 64];

    if (!h) return;
    StringCchCopyW(name, ARRAY_LEN(name), h->name);
    StringCchCopyW(user, ARRAY_LEN(user), h->hasUser ? h->user : L"");
    StringCchPrintfW(label, ARRAY_LEN(label), L"Username for %s (leave empty to forget it)", name);
    if (!Dark_Input(g_main, L"Edit username", NULL, label, user, ARRAY_LEN(user), NULL, NULL, 0)) return;
    str_trim(user);
    if (!EnsureRegBackup()) return;
    if (!(user[0] ? Host_SetUser(name, user) : Host_ClearUser(name)))
        Dark_Info(g_main, L"Edit username", L"The username couldn't be saved.");
    RefreshView(V_COMPUTERS);
}

/* Moves the selected computer one place up or down the dropdown, or all the way. */
static void Cmd_C_Move(int delta, BOOL toEdge)
{
    Host *h = SingleHost();
    wchar_t name[NAME_MAX_CCH];
    MruSnap snap;

    if (!h || h->mru < 0) return;
    StringCchCopyW(name, ARRAY_LEN(name), h->name);
    if (!EnsureRegBackup()) return;
    Snap_Take(&snap);
    if (toEdge) Host_MoveMruTo(name, delta < 0 ? 0 : MRU_MAX);
    else Host_MoveMru(name, delta);
    RefreshView(V_COMPUTERS);
    Snap_Animate(&snap);
}

static void Cmd_C_Remove(void)
{
    NameList sel = SelectedHosts();
    WStr t = { 0 };
    int i;

    if (!sel.n) return;
    if (sel.n == 1) {
        ws_appendf(&t, L"Remove \"%s\" from Remote Desktop?", sel.v[0]);
    } else {
        ws_appendf(&t, L"Remove these %d computers from Remote Desktop?\n", sel.n);
        AppendNames(&t, &sel);
    }
    ws_append(&t, L"\n\nThis removes the dropdown entry, saved username, trusted certificate, "
                  L"device prompt answer and saved password.\n\n"
                  L"The registry settings are backed up first. A saved password can't be restored.");
    if (Dark_Dialog(g_main, L"Remove computer", t.p, L"Remove", L"Cancel", BK_DANGER) == IDOK &&
        EnsureRegBackup()) {
        for (i = 0; i < sel.n; i++) Host_RemoveAll(sel.v[i]);
        RefreshView(V_COMPUTERS);
    }
    ws_free(&t);
    free(sel.v);
}

/* Applies one of the "forget this" context menu commands to every selected computer. */
static void Cmd_C_ForEach(int cmd)
{
    NameList sel = SelectedHosts();
    BOOL go = TRUE;
    int i;

    if (!sel.n) return;
    if (cmd == CM_C_FCRED) {
        WStr t = { 0 };
        if (sel.n == 1) ws_appendf(&t, L"Delete the saved password for \"%s\"?", sel.v[0]);
        else ws_appendf(&t, L"Delete the saved passwords for %d computers?", sel.n);
        ws_append(&t, L"\n\nThis can't be undone. You will be asked for the password the next time you connect.");
        go = Dark_Dialog(g_main, L"Delete saved password", t.p, L"Delete", L"Cancel", BK_DANGER) == IDOK;
        ws_free(&t);
    } else {
        go = EnsureRegBackup();
    }
    if (go) {
        if (cmd == CM_C_ADDMRU) {
            /* last to first, so the first selected computer ends up on top */
            for (i = sel.n - 1; i >= 0; i--) {
                Host *h = Model_Find(&g_hosts, sel.v[i]);
                if (h && h->mru < 0) Host_AddToMru(sel.v[i]);
            }
        } else for (i = 0; i < sel.n; i++) {
            switch (cmd) {
            case CM_C_RMMRU: Host_RemoveFromMru(sel.v[i]); break;
            case CM_C_FUSER: Host_ClearUser(sel.v[i]); break;
            case CM_C_FCERT: Host_ClearCert(sel.v[i]); break;
            case CM_C_FDEV:  Host_ClearDevices(sel.v[i]); break;
            case CM_C_FCRED: Host_DeleteCred(sel.v[i]); break;
            }
        }
        RefreshView(V_COMPUTERS);
    }
    free(sel.v);
}

static void Cmd_C_Copy(void)
{
    NameList sel = SelectedHosts();
    WStr t = { 0 };
    int i;

    if (!sel.n) return;
    ws_clear(&t);
    for (i = 0; i < sel.n; i++) {
        if (i) ws_append(&t, L"\r\n");
        ws_append(&t, sel.v[i]);
    }
    clipboard_set(g_main, t.p);
    ws_free(&t);
    free(sel.v);
}

static void ShowComputerMenu(int x, int y)
{
    HWND lv = g_views[V_COMPUTERS].list;
    BOOL inMru = FALSE, notInMru = FALSE, user = FALSE, cert = FALSE, dev = FALSE, cred = FALSE;
    int n = LV_SelCount(lv), i = -1;
    UINT single = n == 1 ? 0 : MF_GRAYED;
    HMENU m;

    if (n <= 0) return;
    while ((i = LV_Next(lv, i)) != -1) {
        Host *h = HostAtRow(i);
        if (!h) continue;
        if (h->mru >= 0) inMru = TRUE; else notInMru = TRUE;
        if (h->hasUser) user = TRUE;
        if (h->hasCert) cert = TRUE;
        if (h->hasDevices) dev = TRUE;
        if (h->hasCred) cred = TRUE;
    }

    m = CreatePopupMenu();
    if (!m) return;
    AppendMenuW(m, MF_STRING | single, CM_C_CONNECT, L"Connect");
    AppendMenuW(m, MF_STRING | single, CM_C_USER, L"Edit username\u2026\tF2");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    if (notInMru) AppendMenuW(m, MF_STRING, CM_C_ADDMRU, L"Add to dropdown");
    if (inMru) AppendMenuW(m, MF_STRING, CM_C_RMMRU, L"Remove from dropdown");
    if (inMru) {
        const Host *h = SingleHost();
        BOOL up = h && h->mru > 0, down = h && h->mru >= 0 && h->mru < g_nMru - 1;
        AppendMenuW(m, MF_STRING | (up ? 0 : MF_GRAYED), CM_C_TOP, L"Move to top");
        AppendMenuW(m, MF_STRING | (down ? 0 : MF_GRAYED), CM_C_BOTTOM, L"Move to bottom");
    }
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (user ? 0 : MF_GRAYED), CM_C_FUSER, L"Forget username");
    AppendMenuW(m, MF_STRING | (cert ? 0 : MF_GRAYED), CM_C_FCERT, L"Forget certificate");
    AppendMenuW(m, MF_STRING | (dev ? 0 : MF_GRAYED), CM_C_FDEV, L"Forget device prompt answer");
    AppendMenuW(m, MF_STRING | (cred ? 0 : MF_GRAYED), CM_C_FCRED, L"Delete saved password\u2026");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, CM_C_COPY, L"Copy name\tCtrl+C");
    AppendMenuW(m, MF_STRING, CM_C_REMOVE, L"Remove everything\u2026\tDel");
    TrackPopupMenuEx(m, TPM_RIGHTBUTTON, x, y, g_main, NULL);
    DestroyMenu(m);
}

/* -------------------------------------------- reordering by dragging */

/* The dropdown row whose handle is at pt (list client coordinates), or -1. */
static int GripRowAt(POINT pt)
{
    HWND lv = g_views[V_COMPUTERS].list;
    int i;

    for (i = 0; i < g_nMru; i++) {
        RECT r;
        if (!ListView_GetItemRect(lv, i, &r, LVIR_BOUNDS)) continue;
        if (pt.y >= r.top && pt.y < r.bottom && pt.x >= r.left && pt.x < r.left + S(LIST_GRIP_W))
            return i;
    }
    return -1;
}

static void SetGripHot(int row)
{
    HWND lv = g_views[V_COMPUTERS].list;
    RECT r;

    if (row == g_gripHot) return;
    if (g_gripHot >= 0 && ListView_GetItemRect(lv, g_gripHot, &r, LVIR_BOUNDS)) InvalidateRect(lv, &r, FALSE);
    g_gripHot = row;
    if (row >= 0 && ListView_GetItemRect(lv, row, &r, LVIR_BOUNDS)) InvalidateRect(lv, &r, FALSE);
}

static UINT RowFlags(int row)
{
    HWND lv = g_views[V_COMPUTERS].list;
    UINT state = ListView_GetItemState(lv, row, LVIS_SELECTED | LVIS_FOCUSED), f = LR_GRIPSPACE;
    const Host *h = HostAtRow(row);

    if (state & LVIS_SELECTED) f |= LR_SELECTED;
    if ((state & LVIS_FOCUSED) && !(SendMessageW(lv, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) f |= LR_FOCUS;
    if (h && h->mru >= 0) f |= LR_GRIP;
    if (row == g_gripHot) f |= LR_GRIPHOT;
    return f;
}

static void DrawComputerItem(const DRAWITEMSTRUCT *di)
{
    int row = (int)di->itemID;
    const Host *h = HostAtRow(row);
    UINT f = LR_GRIPSPACE;

    if (ReorderPainting() && row < g_nMru) return;      /* PaintReorderRows draws these */
    if (h && h->mru >= 0) f |= LR_GRIP;
    if (row == g_gripHot) f |= LR_GRIPHOT;
    Dark_DrawListItem(di, f);
}

/*
 * Paints the dropdown rows while they are being reordered: each one where its
 * slide has got to, and the row being dragged faded in the place it would land.
 */
static void PaintReorderRows(HDC dc)
{
    HWND lv = g_views[V_COMPUTERS].list, header = ListView_GetHeader(lv);
    RECT client, hr = { 0, 0, 0, 0 }, first, last, area;
    int pass, i, saved;

    if (g_nMru <= 0 || !ListView_GetItemRect(lv, 0, &first, LVIR_BOUNDS) ||
        !ListView_GetItemRect(lv, g_nMru - 1, &last, LVIR_BOUNDS))
        return;
    GetClientRect(lv, &client);
    if (header) GetWindowRect(header, &hr);

    saved = SaveDC(dc);
    IntersectClipRect(dc, 0, hr.bottom - hr.top, client.right, client.bottom);
    SetRect(&area, 0, first.top, client.right, last.bottom);
    FillRect(dc, &area, g_th.brBg);
    for (pass = 0; pass < 2; pass++) {                  /* the dragged row's place first, sliding rows over it */
        for (i = 0; i < g_nMru; i++) {
            BOOL dragged = g_drag.state == DRAG_ACTIVE && i == g_drag.slot;
            RECT r;

            if (dragged != (pass == 0) || !ListView_GetItemRect(lv, i, &r, LVIR_BOUNDS)) continue;
            OffsetRect(&r, 0, Anim_Dy(i));
            Dark_DrawListRow(lv, dc, i, &r, RowFlags(i));
            if (dragged) {
                if (r.right < client.right) r.right = client.right;
                Dark_Veil(dc, &r, 178);                 /* leaves the row at 30% */
            }
        }
    }
    RestoreDC(dc, saved);
}

/* Moves one element of an array from one index to another, shifting those in between. */
static void Rotate(void *base, size_t size, int from, int to, void *tmp)
{
    char *b = (char *)base;

    memcpy(tmp, b + (size_t)from * size, size);
    if (from < to) memmove(b + (size_t)from * size, b + ((size_t)from + 1) * size, (size_t)(to - from) * size);
    else memmove(b + ((size_t)to + 1) * size, b + (size_t)to * size, (size_t)(from - to) * size);
    memcpy(b + (size_t)to * size, tmp, size);
}

/*
 * Moves a dropdown row to another place in the list and in g_hosts, but not
 * in the registry. The rows it passes slide over; the moved row jumps.
 */
static void MoveRow(int from, int to)
{
    HWND lv = g_views[V_COMPUTERS].list;
    int lo = from < to ? from : to, hi = from < to ? to : from, h = RowHeight(), i, tmpInt;
    int shown[MRU_MAX] = { 0 };                         /* where each row is drawn now, slide included */
    UINT state[MRU_MAX] = { 0 }, tmpState;
    ULONGLONG now = GetTickCount64();
    Host tmpHost;

    if (from == to || lo < 0 || hi >= g_nMru || hi >= MRU_MAX || hi >= g_hosts.n) return;
    for (i = lo; i <= hi; i++) {
        shown[i] = i * h + Anim_Dy(i);
        state[i] = ListView_GetItemState(lv, i, LVIS_SELECTED | LVIS_FOCUSED);
    }
    Rotate(g_hosts.v, sizeof(Host), from, to, &tmpHost);
    Rotate(shown, sizeof(shown[0]), from, to, &tmpInt);
    Rotate(state, sizeof(state[0]), from, to, &tmpState);

    g_populating = TRUE;                                /* selection moves with its row; nothing new is selected */
    for (i = lo; i <= hi; i++) {
        g_hosts.v[i].mru = i;
        g_anim[i].from = i == to ? 0 : shown[i] - i * h;
        g_anim[i].start = now;
        FillComputerRow(lv, i, &g_hosts.v[i]);
        ListView_SetItemState(lv, i, state[i], LVIS_SELECTED | LVIS_FOCUSED);
    }
    g_populating = FALSE;
    Anim_Kick();
}

/* Follows the pointer: moves the copy of the row and, inside the list, the row itself. */
static void Drag_Track(void)
{
    HWND lv = g_views[V_COMPUTERS].list;
    POINT screen = g_drag.pt;
    RECT client, r0;
    BOOL inside;

    if (g_drag.state != DRAG_ACTIVE) return;
    GetClientRect(lv, &client);
    inside = PtInRect(&client, g_drag.pt);
    ClientToScreen(lv, &screen);
    if (g_drag.ghost)
        SetWindowPos(g_drag.ghost, NULL, screen.x - g_drag.grab.x, screen.y - g_drag.grab.y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetCursor(LoadCursorW(NULL, inside ? IDC_SIZENS : IDC_NO));

    if (inside && g_nMru > 0 && ListView_GetItemRect(lv, 0, &r0, LVIR_BOUNDS) && r0.bottom > r0.top) {
        int slot = g_drag.pt.y < r0.top ? 0 : (g_drag.pt.y - r0.top) / (r0.bottom - r0.top);
        if (slot > g_nMru - 1) slot = g_nMru - 1;
        if (slot != g_drag.slot) {
            MoveRow(g_drag.slot, slot);
            g_drag.slot = slot;
        }
    }
}

/* The handle has been pulled far enough: lift the row. */
static void Drag_Begin(void)
{
    HWND lv = g_views[V_COMPUTERS].list;
    RECT row, client;
    HDC screen, mem;
    HBITMAP bmp;
    int w, h;

    if (!ListView_GetItemRect(lv, g_drag.slot, &row, LVIR_BOUNDS)) {
        Drag_Stop();
        return;
    }
    GetClientRect(lv, &client);
    w = client.right;
    h = row.bottom - row.top;
    g_drag.state = DRAG_ACTIVE;
    g_drag.grab.x = g_drag.down.x;
    g_drag.grab.y = g_drag.down.y - row.top;
    SetGripHot(-1);

    /* the copy that follows the pointer: a click-through window showing the row at half strength */
    g_drag.ghost = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                                   L"STATIC", NULL, WS_POPUP, 0, 0, w, h, g_main, NULL, g_inst, NULL);
    screen = GetDC(NULL);
    mem = screen ? CreateCompatibleDC(screen) : NULL;
    bmp = screen ? CreateCompatibleBitmap(screen, w, h) : NULL;
    if (g_drag.ghost && mem && bmp) {
        HGDIOBJ old = SelectObject(mem, bmp);
        RECT at = { row.left, 0, row.right, h }, frame = { 0, 0, w, h };
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 128, 0 };
        POINT src = { 0, 0 }, dst = g_drag.pt;
        SIZE size = { w, h };

        Dark_DrawListRow(lv, mem, g_drag.slot, &at, RowFlags(g_drag.slot) | LR_GRIPHOT);
        FrameRect(mem, &frame, g_th.brBorder);
        ClientToScreen(lv, &dst);
        dst.x -= g_drag.grab.x;
        dst.y -= g_drag.grab.y;
        UpdateLayeredWindow(g_drag.ghost, screen, &dst, &size, mem, &src, 0, &bf, ULW_ALPHA);
        ShowWindow(g_drag.ghost, SW_SHOWNOACTIVATE);
        SelectObject(mem, old);
    }
    if (bmp) DeleteObject(bmp);
    if (mem) DeleteDC(mem);
    if (screen) ReleaseDC(NULL, screen);

    SetTimer(g_main, TIMER_DRAG, 80, NULL);
    InvalidateMruRows();
    UpdateStatus();
}

static void Drag_Move(POINT pt)
{
    g_drag.pt = pt;
    if (g_drag.state == DRAG_ARMED) {
        if (abs(pt.x - g_drag.down.x) < S(6) && abs(pt.y - g_drag.down.y) < S(6)) return;
        Drag_Begin();
    }
    Drag_Track();
}

/* Keeps the list scrolling while the row is held near its top or bottom edge. */
static void Drag_Tick(void)
{
    HWND lv = g_views[V_COMPUTERS].list, header = ListView_GetHeader(lv);
    RECT client, hr = { 0, 0, 0, 0 }, last;
    int top, h = RowHeight();

    if (g_drag.state != DRAG_ACTIVE) return;
    GetClientRect(lv, &client);
    if (!PtInRect(&client, g_drag.pt)) return;
    if (header) GetWindowRect(header, &hr);
    top = ListView_GetTopIndex(lv);
    if (g_drag.pt.y < hr.bottom - hr.top + S(24)) {
        ListView_Scroll(lv, 0, -h);
    } else if (g_drag.pt.y >= client.bottom - S(24) && g_nMru > 0 &&
               ListView_GetItemRect(lv, g_nMru - 1, &last, LVIR_BOUNDS) && last.bottom > client.bottom) {
        ListView_Scroll(lv, 0, h);                      /* only while dropdown rows are still out of sight */
    }
    if (ListView_GetTopIndex(lv) != top) {
        InvalidateRect(lv, NULL, FALSE);
        Drag_Track();
    }
}

/* Ends a drag where it stands. The rows stay as they are on screen. */
static void Drag_Stop(void)
{
    HWND lv = g_views[V_COMPUTERS].list;
    int was = g_drag.state;

    if (was == DRAG_IDLE) return;
    g_drag.state = DRAG_IDLE;                           /* first: releasing the capture comes back here */
    KillTimer(g_main, TIMER_DRAG);
    if (g_drag.ghost) {
        DestroyWindow(g_drag.ghost);
        g_drag.ghost = NULL;
    }
    if (GetCapture() == lv) ReleaseCapture();
    if (was == DRAG_ACTIVE) {
        InvalidateMruRows();
        UpdateStatus();
    }
}

/* Ends a drag: saves the new order when the row was dropped in the list, else puts it back. */
static void Drag_End(BOOL dropped)
{
    BOOL moved = g_drag.state == DRAG_ACTIVE && g_drag.slot != g_drag.origSlot;
    int to = g_drag.slot;
    wchar_t name[NAME_MAX_CCH];
    RowAnim keep[MRU_MAX];

    if (g_drag.state == DRAG_IDLE) return;
    Drag_Stop();
    if (!moved) return;
    if (!dropped || to < 0 || to >= g_hosts.n || !EnsureRegBackup()) {
        RefreshView(V_COMPUTERS);                       /* reloading puts every row back */
        return;
    }
    StringCchCopyW(name, ARRAY_LEN(name), g_hosts.v[to].name);
    if (!Host_MoveMruTo(name, to)) {
        RefreshView(V_COMPUTERS);
        Dark_Info(g_main, L"Reorder", L"The dropdown list couldn't be updated.");
        return;
    }
    memcpy(keep, g_anim, sizeof(keep));                 /* let the rows finish sliding */
    RefreshView(V_COMPUTERS);
    memcpy(g_anim, keep, sizeof(keep));
    if (Anim_Running()) Anim_Kick();
}

static LRESULT CALLBACK ComputersListProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref)
{
    POINT pt;
    RECT client;

    (void)ref;
    switch (m) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (g_drag.state != DRAG_IDLE) return 0;
        pt.x = GET_X_LPARAM(l);
        pt.y = GET_Y_LPARAM(l);
        if (GripRowAt(pt) >= 0) {
            /* a press on the handle only readies a drag; it doesn't change the selection */
            g_drag.state = DRAG_ARMED;
            g_drag.slot = g_drag.origSlot = GripRowAt(pt);
            g_drag.down = g_drag.pt = pt;
            if (GetFocus() != h) SetFocus(h);
            SetCapture(h);
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        pt.x = GET_X_LPARAM(l);
        pt.y = GET_Y_LPARAM(l);
        if (g_drag.state != DRAG_IDLE) {
            Drag_Move(pt);
            return 0;
        }
        SetGripHot(GripRowAt(pt));
        if (g_gripHot >= 0) {
            TRACKMOUSEEVENT t;
            t.cbSize = sizeof(t);
            t.dwFlags = TME_LEAVE;
            t.hwndTrack = h;
            t.dwHoverTime = 0;
            TrackMouseEvent(&t);
        }
        break;
    case WM_MOUSELEAVE:
        SetGripHot(-1);
        break;
    case WM_LBUTTONUP:
        if (g_drag.state != DRAG_IDLE) {
            pt.x = GET_X_LPARAM(l);
            pt.y = GET_Y_LPARAM(l);
            GetClientRect(h, &client);
            Drag_End(PtInRect(&client, pt));
            return 0;
        }
        break;
    case WM_RBUTTONDOWN:
        g_drag.eatRUp = g_drag.state != DRAG_IDLE;
        if (g_drag.eatRUp) {                            /* the other button cancels, and opens no menu */
            Drag_End(FALSE);
            return 0;
        }
        break;
    case WM_RBUTTONUP:
        if (g_drag.eatRUp) {
            g_drag.eatRUp = FALSE;
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        if (g_drag.state != DRAG_IDLE && (HWND)l != h) Drag_End(FALSE);
        break;
    case WM_SETCURSOR:
        if ((HWND)w == h && LOWORD(l) == HTCLIENT && g_drag.state == DRAG_IDLE) {
            DWORD pos = GetMessagePos();
            pt.x = GET_X_LPARAM(pos);
            pt.y = GET_Y_LPARAM(pos);
            ScreenToClient(h, &pt);
            if (GripRowAt(pt) >= 0) {
                SetCursor(LoadCursorW(NULL, IDC_SIZENS));
                return TRUE;
            }
        }
        break;
    case WM_MOUSEWHEEL: {
        LRESULT r = DefSubclassProc(h, m, w, l);
        if (g_drag.state == DRAG_ACTIVE) {
            InvalidateRect(h, NULL, FALSE);
            Drag_Track();
        }
        return r;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(h, ComputersListProc, id);
        break;
    }
    return DefSubclassProc(h, m, w, l);
}

/* ------------------------------------------------- connection defaults */

static const wchar_t *RdpTypeName(wchar_t t)
{
    switch (t) {
    case L'i': return L"Number";
    case L's': return L"Text";
    case L'b': return L"Binary";
    default:   return L"Other";
    }
}

static void RefreshDefaults(void)
{
    HWND lv = g_views[V_DEFAULTS].list;
    int keep = LV_FocusRow(lv), i;

    Rdp_Load(&g_rdp);
    LV_Begin(lv);
    for (i = 0; i < g_rdp.n; i++) {
        const RdpLine *ln = &g_rdp.v[i];
        int row;
        if (ln->name) {
            const wchar_t *about = Rdp_Describe(ln->name);
            row = LV_Row(lv, i, ln->name);
            LV_Cell(lv, row, 1, ln->value);
            LV_Cell(lv, row, 2, RdpTypeName(ln->type));
            LV_Cell(lv, row, 3, about[0] ? about : DASH);
        } else {
            row = LV_Row(lv, i, L"(unrecognised line)");
            LV_Cell(lv, row, 1, ln->value);
            LV_Cell(lv, row, 2, DASH);
            LV_Cell(lv, row, 3, DASH);
        }
    }
    LV_End(lv);
    LV_RestoreRow(lv, keep);
}

static void SaveDefaults(const wchar_t *title)
{
    if (!Rdp_Save(&g_rdp))
        Dark_Info(g_main, title, L"Default.rdp couldn't be saved.");
    RefreshView(V_DEFAULTS);
}

static void Cmd_D_Edit(void)
{
    LPARAM idx = LV_Single(g_views[V_DEFAULTS].list);
    wchar_t value[1024], label[400];
    const wchar_t *about;
    RdpLine *ln;

    if (idx < 0 || idx >= g_rdp.n) return;
    ln = &g_rdp.v[idx];
    if (!ln->name || wcslen(ln->value) >= ARRAY_LEN(value)) {
        Dark_Info(g_main, L"Edit setting",
                  L"This line can't be edited here. Use \"Open in Notepad\" to change it.");
        return;
    }
    StringCchCopyW(value, ARRAY_LEN(value), ln->value);
    about = Rdp_Describe(ln->name);
    for (;;) {
        StringCchCopyW(label, ARRAY_LEN(label), ln->name);
        if (!Dark_Input(g_main, L"Edit setting", about[0] ? about : NULL,
                        label, value, ARRAY_LEN(value), NULL, NULL, 0))
            return;
        str_trim(value);
        if (ln->type != L'i' || IsInteger(value)) break;
        Dark_Info(g_main, L"Edit setting", L"This setting needs a whole number.");
    }
    if (!EnsureRdpBackup()) return;
    Rdp_Set(&g_rdp, ln->name, ln->type, value);
    SaveDefaults(L"Edit setting");
}

static void Cmd_D_Add(void)
{
    wchar_t name[128] = L"", value[1024] = L"", type = L's';

    for (;;) {
        if (!Dark_Input(g_main, L"Add setting",
                        L"Settings use the names from Microsoft's .rdp file format, "
                        L"for example \"audiomode\" or \"redirectclipboard\".",
                        L"Setting name", name, ARRAY_LEN(name), L"Value", value, ARRAY_LEN(value)))
            return;
        str_trim(name);
        str_trim(value);
        if (name[0] && !wcschr(name, L':')) {
            type = Rdp_KnownType(name);
            if (!type) type = IsInteger(value) ? L'i' : L's';
            if (type != L'i' || IsInteger(value)) break;
            Dark_Info(g_main, L"Add setting", L"This setting needs a whole number.");
        } else {
            Dark_Info(g_main, L"Add setting", L"Enter a setting name without a colon.");
        }
    }
    if (!EnsureRdpBackup()) return;
    Rdp_Set(&g_rdp, name, type, value);
    SaveDefaults(L"Add setting");
}

static void Cmd_D_Delete(void)
{
    HWND lv = g_views[V_DEFAULTS].list;
    int n = ListView_GetItemCount(lv), i, deleted = 0;

    if (LV_SelCount(lv) <= 0 || !EnsureRdpBackup()) return;
    /* bottom to top, so earlier line numbers stay valid while deleting */
    for (i = n - 1; i >= 0; i--) {
        if (ListView_GetItemState(lv, i, LVIS_SELECTED) & LVIS_SELECTED) {
            LPARAM idx = LV_Param(lv, i);
            if (idx >= 0 && idx < g_rdp.n) {
                Rdp_Delete(&g_rdp, (int)idx);
                deleted++;
            }
        }
    }
    if (deleted) SaveDefaults(L"Delete setting");
}

static void Cmd_D_Notepad(void)
{
    wchar_t exe[MAX_PATH + 32], args[PATH_CCH + 4];

    if (!file_exists(g_env.rdpPath)) {
        Dark_Info(g_main, L"Open in Notepad", L"Default.rdp doesn't exist yet.");
        return;
    }
    SystemExe(L"notepad.exe", FALSE, exe, ARRAY_LEN(exe));
    if (!exe[0]) return;
    StringCchPrintfW(args, ARRAY_LEN(args), L"\"%s\"", g_env.rdpPath);
    ShellExecuteW(g_main, L"open", exe, args, NULL, SW_SHOWNORMAL);
}

static void Cmd_D_Reset(void)
{
    if (!file_exists(g_env.rdpPath)) return;
    if (Dark_Dialog(g_main, L"Reset to defaults",
                    L"Reset Remote Desktop Connection to its default settings?\n\n"
                    L"Default.rdp is moved to the Recycle Bin. Remote Desktop Connection "
                    L"creates a fresh one the next time it runs.",
                    L"Reset", L"Cancel", BK_DANGER) != IDOK)
        return;
    if (!File_Recycle(g_env.rdpPath))
        Dark_Info(g_main, L"Reset to defaults", L"Default.rdp couldn't be moved to the Recycle Bin.");
    RefreshView(V_DEFAULTS);
}

/* -------------------------------------------------------------- cleanup */

static const wchar_t *const kCleanNames[CL_COUNT] = {
    L"Dropdown history", L"Saved usernames", L"Trusted certificates", L"Device prompt answers",
    L"Saved passwords", L"Screen cache", L"Taskbar jump list"
};

static void CountText(int n, const wchar_t *one, const wchar_t *many, wchar_t *out, size_t cch)
{
    if (n <= 0) StringCchCopyW(out, cch, DASH);
    else StringCchPrintfW(out, cch, L"%d %s", n, n == 1 ? one : many);
}

static void RefreshCleanup(void)
{
    static const wchar_t yes[] = L"Yes, from Backups";
    HWND lv = g_views[V_CLEANUP].list;
    int keep = LV_FocusRow(lv), counts[5] = { 0, 0, 0, 0, 0 }, i, row;
    wchar_t text[128], size[64], where[PATH_CCH + 64];
    ULONGLONG jumpBytes = 0;
    DirStat cache;

    Model_Load(&g_hosts);
    for (i = 0; i < g_hosts.n; i++) {
        const Host *h = &g_hosts.v[i];
        if (h->mru >= 0) counts[CL_MRU]++;
        if (h->hasUser) counts[CL_USERS]++;
        if (h->hasCert) counts[CL_CERTS]++;
        if (h->hasDevices) counts[CL_DEVICES]++;
        if (h->hasCred) counts[CL_CREDS]++;
    }
    Cache_Stat(&cache);

    LV_Begin(lv);
    row = LV_Row(lv, CL_MRU, kCleanNames[CL_MRU]);
    CountText(counts[CL_MRU], L"computer", L"computers", text, ARRAY_LEN(text));
    LV_Cell(lv, row, 1, text);
    LV_Cell(lv, row, 2, yes);
    LV_Cell(lv, row, 3, L"Registry: Terminal Server Client\\Default (MRU0 to MRU9)");

    row = LV_Row(lv, CL_USERS, kCleanNames[CL_USERS]);
    CountText(counts[CL_USERS], L"username", L"usernames", text, ARRAY_LEN(text));
    LV_Cell(lv, row, 1, text);
    LV_Cell(lv, row, 2, yes);
    LV_Cell(lv, row, 3, L"Registry: Terminal Server Client\\Servers\\<computer>\\UsernameHint");

    row = LV_Row(lv, CL_CERTS, kCleanNames[CL_CERTS]);
    CountText(counts[CL_CERTS], L"certificate", L"certificates", text, ARRAY_LEN(text));
    LV_Cell(lv, row, 1, text);
    LV_Cell(lv, row, 2, yes);
    LV_Cell(lv, row, 3, L"Registry: Terminal Server Client\\Servers\\<computer>\\CertHash");

    row = LV_Row(lv, CL_DEVICES, kCleanNames[CL_DEVICES]);
    CountText(counts[CL_DEVICES], L"answer", L"answers", text, ARRAY_LEN(text));
    LV_Cell(lv, row, 1, text);
    LV_Cell(lv, row, 2, yes);
    LV_Cell(lv, row, 3, L"Registry: Terminal Server Client\\LocalDevices");

    row = LV_Row(lv, CL_CREDS, kCleanNames[CL_CREDS]);
    CountText(counts[CL_CREDS], L"password", L"passwords", text, ARRAY_LEN(text));
    LV_Cell(lv, row, 1, text);
    LV_Cell(lv, row, 2, L"No");
    LV_Cell(lv, row, 3, L"Windows Credential Manager (TERMSRV/<computer>)");

    row = LV_Row(lv, CL_CACHE, kCleanNames[CL_CACHE]);
    if (cache.files > 0) {
        format_size(cache.bytes, size, ARRAY_LEN(size));
        StringCchPrintfW(text, ARRAY_LEN(text), L"%d file%s, %s", cache.files, cache.files == 1 ? L"" : L"s", size);
    } else {
        StringCchCopyW(text, ARRAY_LEN(text), DASH);
    }
    LV_Cell(lv, row, 1, text);
    LV_Cell(lv, row, 2, L"No (rebuilt automatically)");
    LV_Cell(lv, row, 3, g_env.cacheDir);

    row = LV_Row(lv, CL_JUMP, kCleanNames[CL_JUMP]);
    if (Jump_Stat(&jumpBytes)) {
        format_size(jumpBytes, size, ARRAY_LEN(size));
        StringCchPrintfW(text, ARRAY_LEN(text), L"Recent list, %s", size);
    } else {
        StringCchCopyW(text, ARRAY_LEN(text), DASH);
    }
    LV_Cell(lv, row, 1, text);
    LV_Cell(lv, row, 2, L"No (rebuilt automatically)");
    StringCchPrintfW(where, ARRAY_LEN(where), L"%s", g_env.jumpAuto);
    LV_Cell(lv, row, 3, where);
    LV_End(lv);
    LV_RestoreRow(lv, keep);
}

static void DoCleanup(const BOOL pick[CL_COUNT])
{
    BOOL registry = pick[CL_MRU] || pick[CL_USERS] || pick[CL_CERTS] || pick[CL_DEVICES];
    int i, failed = 0;

    if (registry && !EnsureRegBackup()) return;
    Model_Load(&g_hosts);
    if (pick[CL_MRU]) Mru_Write(NULL, 0);
    for (i = 0; i < g_hosts.n; i++) {
        const Host *h = &g_hosts.v[i];
        if (pick[CL_USERS] && h->hasUser) Host_ClearUser(h->name);
        if (pick[CL_CERTS] && h->hasCert) Host_ClearCert(h->name);
        if (pick[CL_DEVICES] && h->hasDevices) Host_ClearDevices(h->name);
        if (pick[CL_CREDS] && h->hasCred) Host_DeleteCred(h->name);
    }
    if (pick[CL_CACHE]) Cache_Clear(NULL, &failed);
    if (pick[CL_JUMP] && !Jump_Clear())
        Dark_Info(g_main, L"Cleanup", L"The taskbar jump list couldn't be deleted. It may be in use.");
    if (failed) {
        WStr t = { 0 };
        ws_appendf(&t, L"%d screen cache file%s couldn't be deleted because an open Remote Desktop "
                       L"session is using %s.\n\nClose the session and try again.",
                   failed, failed == 1 ? L"" : L"s", failed == 1 ? L"it" : L"them");
        Dark_Info(g_main, L"Cleanup", t.p);
        ws_free(&t);
    }
    RefreshView(V_CLEANUP);
}

static void Cmd_X_Clear(BOOL everything)
{
    HWND lv = g_views[V_CLEANUP].list;
    BOOL pick[CL_COUNT] = { 0 }, any = FALSE, permanent;
    WStr t = { 0 };
    int i = -1, c;

    if (everything) {
        for (c = 0; c < CL_COUNT; c++) pick[c] = TRUE;
        any = TRUE;
    } else while ((i = LV_Next(lv, i)) != -1) {
        LPARAM id = LV_Param(lv, i);
        if (id >= 0 && id < CL_COUNT) {
            pick[id] = TRUE;
            any = TRUE;
        }
    }
    if (!any) return;

    ws_append(&t, everything ? L"Clear everything Remote Desktop remembers?\n" : L"Clear these items?\n");
    for (c = 0; c < CL_COUNT; c++)
        if (pick[c]) ws_appendf(&t, L"\n     \u2022  %s", kCleanNames[c]);
    permanent = pick[CL_CREDS] || pick[CL_CACHE] || pick[CL_JUMP];
    if (pick[CL_MRU] || pick[CL_USERS] || pick[CL_CERTS] || pick[CL_DEVICES])
        ws_append(&t, L"\n\nThe registry settings are backed up first, so they can be restored from Backups.");
    if (permanent)
        ws_append(&t, L"\n\nSaved passwords, the screen cache and the jump list are deleted permanently.");
    if (Dark_Dialog(g_main, L"Cleanup", t.p, L"Clear", L"Cancel", BK_DANGER) == IDOK)
        DoCleanup(pick);
    ws_free(&t);
}

/* -------------------------------------------------------------- backups */

static void RefreshBackups(void)
{
    HWND lv = g_views[V_BACKUPS].list;
    int keep = LV_FocusRow(lv), i;

    free(g_backups);
    g_backups = NULL;
    g_nBackups = Backup_List(&g_backups);

    LV_Begin(lv);
    for (i = 0; i < g_nBackups; i++) {
        const BackupInfo *b = &g_backups[i];
        wchar_t text[128];
        int row;

        format_filetime(&b->time, text, ARRAY_LEN(text));
        row = LV_Row(lv, i, text);
        LV_Cell(lv, row, 1, b->isRdp ? L"Default.rdp" : L"Registry settings");
        LV_Cell(lv, row, 2, str_ieq(b->kind, L"auto") ? L"Before a change"
                          : str_ieq(b->kind, L"prerestore") ? L"Before a restore"
                          : L"Made by you");
        format_size(b->size, text, ARRAY_LEN(text));
        LV_Cell(lv, row, 3, text);
        LV_Cell(lv, row, 4, b->file);
    }
    LV_End(lv);
    LV_RestoreRow(lv, keep);
}

static void Cmd_B_Now(void)
{
    if (!Backup_Create(L"manual", TRUE, TRUE))
        Dark_Info(g_main, L"Back up now", L"The backup couldn't be saved.");
    RefreshView(V_BACKUPS);
    if (ListView_GetItemCount(g_views[V_BACKUPS].list) > 0)
        LV_SelectOnly(g_views[V_BACKUPS].list, 0);
}

static void Cmd_B_Restore(void)
{
    LPARAM idx = LV_Single(g_views[V_BACKUPS].list);
    wchar_t when[128], err[300];
    BackupInfo b;
    WStr t = { 0 };

    if (idx < 0 || idx >= g_nBackups) return;
    b = g_backups[idx];
    format_filetime(&b.time, when, ARRAY_LEN(when));
    if (b.isRdp)
        ws_appendf(&t, L"Restore Default.rdp from the backup made on %s?\n\n"
                       L"The current connection defaults are replaced. They are backed up first.", when);
    else
        ws_appendf(&t, L"Restore the registry settings from the backup made on %s?\n\n"
                       L"The current dropdown list, usernames, certificates and device prompt answers "
                       L"are replaced. They are backed up first. Saved passwords are not affected.", when);
    if (Dark_Dialog(g_main, L"Restore backup", t.p, L"Restore", L"Cancel", BK_PRIMARY) == IDOK &&
        (b.isRdp || ConfirmMstscOpen())) {
        if (!Backup_Restore(&b, err, ARRAY_LEN(err))) {
            WStr e = { 0 };
            ws_appendf(&e, L"The backup couldn't be restored.\n\n%s", err);
            Dark_Info(g_main, L"Restore backup", e.p);
            ws_free(&e);
        }
        RefreshView(V_BACKUPS);
    }
    ws_free(&t);
}

static void Cmd_B_Delete(void)
{
    HWND lv = g_views[V_BACKUPS].list;
    int n = LV_SelCount(lv), i = -1;
    WStr t = { 0 };

    if (n <= 0) return;
    if (n == 1) ws_append(&t, L"Delete this backup?");
    else ws_appendf(&t, L"Delete these %d backups?", n);
    ws_append(&t, L"\n\nDeleted backups go to the Recycle Bin.");
    if (Dark_Dialog(g_main, L"Delete backup", t.p, L"Delete", L"Cancel", BK_DANGER) == IDOK) {
        while ((i = LV_Next(lv, i)) != -1) {
            LPARAM idx = LV_Param(lv, i);
            if (idx >= 0 && idx < g_nBackups) File_Recycle(g_backups[idx].path);
        }
        RefreshView(V_BACKUPS);
    }
    ws_free(&t);
}

static void Cmd_B_Folder(void)
{
    dir_ensure(g_env.backupDir);
    ShellExecuteW(g_main, L"open", g_env.backupDir, NULL, NULL, SW_SHOWNORMAL);
}

/* ------------------------------------------------------------- advanced */

static void AdvAdd(HWND lv, const wchar_t *hiveLong, const wchar_t *hiveShort, const wchar_t *path,
                   const wchar_t *name, const wchar_t *value, const wchar_t *type)
{
    wchar_t loc[700];
    int row;

    if (g_nAdv >= 3000) return;
    if (g_nAdv == g_capAdv) {
        g_capAdv = g_capAdv ? g_capAdv * 2 : 64;
        g_adv = (AdvRow *)xrealloc(g_adv, (size_t)g_capAdv * sizeof(AdvRow));
    }
    StringCchPrintfW(g_adv[g_nAdv].key, ARRAY_LEN(g_adv[g_nAdv].key), L"%s\\%s", hiveLong, path);
    StringCchPrintfW(loc, ARRAY_LEN(loc), L"%s\\%s", hiveShort, path);
    row = LV_Row(lv, g_nAdv, loc);
    LV_Cell(lv, row, 1, name);
    LV_Cell(lv, row, 2, value);
    LV_Cell(lv, row, 3, type);
    g_nAdv++;
}

/*
 * Lists every value under a key. mode 1 = the per-user Terminal Server Client
 * root and mode 2 = its Default key; the parts shown on the Computers page
 * are skipped there.
 */
static void AdvWalk(HWND lv, HKEY hive, const wchar_t *hiveLong, const wchar_t *hiveShort,
                    const wchar_t *path, int mode, int depth)
{
    wchar_t name[512], text[400], sub[700];
    DWORD i;
    HKEY k;

    if (depth > 6) return;
    if (RegOpenKeyExW(hive, path, 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return;

    for (i = 0;; i++) {
        DWORD cch = ARRAY_LEN(name), type = 0, cb = 0;
        LSTATUS s = RegEnumValueW(k, i, name, &cch, NULL, &type, NULL, &cb);
        BYTE *data;

        if (s == ERROR_MORE_DATA) continue;
        if (s != ERROR_SUCCESS) break;
        if (mode == 2 && str_istarts(name, L"MRU") && name[3] >= L'0' && name[3] <= L'9' && !name[4])
            continue;
        data = (BYTE *)xalloc((size_t)cb + 4);
        if (RegQueryValueExW(k, name, NULL, &type, data, &cb) == ERROR_SUCCESS) {
            Reg_FormatValue(type, data, cb, text, ARRAY_LEN(text));
            AdvAdd(lv, hiveLong, hiveShort, path, name[0] ? name : L"(Default)", text, Reg_TypeName(type));
        }
        free(data);
    }

    for (i = 0;; i++) {
        DWORD cch = ARRAY_LEN(name);
        LSTATUS s = RegEnumKeyExW(k, i, name, &cch, NULL, NULL, NULL, NULL);

        if (s == ERROR_MORE_DATA) continue;
        if (s != ERROR_SUCCESS) break;
        if (mode == 1 && (str_ieq(name, L"Servers") || str_ieq(name, L"LocalDevices"))) continue;
        if (FAILED(StringCchPrintfW(sub, ARRAY_LEN(sub), L"%s\\%s", path, name))) continue;
        AdvWalk(lv, hive, hiveLong, hiveShort, sub,
                mode == 1 && str_ieq(name, L"Default") ? 2 : 0, depth + 1);
    }
    RegCloseKey(k);
}

static void RefreshAdvanced(void)
{
    HWND lv = g_views[V_ADVANCED].list;
    int keep = LV_FocusRow(lv);

    g_nAdv = 0;
    LV_Begin(lv);
    AdvWalk(lv, HKEY_CURRENT_USER, L"HKEY_CURRENT_USER", L"HKCU", g_env.regRoot, 1, 0);
    AdvWalk(lv, HKEY_CURRENT_USER, L"HKEY_CURRENT_USER", L"HKCU",
            L"Software\\Policies\\Microsoft\\Windows NT\\Terminal Services", 0, 0);
    AdvWalk(lv, HKEY_LOCAL_MACHINE, L"HKEY_LOCAL_MACHINE", L"HKLM",
            L"SOFTWARE\\Policies\\Microsoft\\Windows NT\\Terminal Services", 0, 0);
    AdvWalk(lv, HKEY_LOCAL_MACHINE, L"HKEY_LOCAL_MACHINE", L"HKLM",
            L"SOFTWARE\\Microsoft\\Terminal Server Client", 0, 0);
    LV_End(lv);
    LV_RestoreRow(lv, keep);
}

/* Opens Registry Editor at a key by setting the key it remembers as last opened. */
static void Cmd_A_Regedit(void)
{
    LPARAM idx = LV_Single(g_views[V_ADVANCED].list);
    wchar_t last[800], exe[MAX_PATH + 32];
    HKEY k;

    if (idx >= 0 && idx < g_nAdv)
        StringCchPrintfW(last, ARRAY_LEN(last), L"Computer\\%s", g_adv[idx].key);
    else
        StringCchPrintfW(last, ARRAY_LEN(last), L"Computer\\HKEY_CURRENT_USER\\%s", g_env.regRoot);

    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Applets\\Regedit",
                        0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(k, L"LastKey", 0, REG_SZ, (const BYTE *)last,
                       (DWORD)((wcslen(last) + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
    }
    SystemExe(L"regedit.exe", TRUE, exe, ARRAY_LEN(exe));
    if (!exe[0]) return;
    ShellExecuteW(g_main, L"open", exe, L"/m", NULL, SW_SHOWNORMAL);
}

static void Cmd_A_Copy(void)
{
    HWND lv = g_views[V_ADVANCED].list;
    int row = LV_Next(lv, -1);
    wchar_t name[512], value[512];
    LPARAM idx;
    WStr t = { 0 };

    if (row < 0) return;
    idx = LV_Param(lv, row);
    if (idx < 0 || idx >= g_nAdv) return;
    name[0] = value[0] = 0;
    ListView_GetItemText(lv, row, 1, name, ARRAY_LEN(name));
    ListView_GetItemText(lv, row, 2, value, ARRAY_LEN(value));
    ws_appendf(&t, L"%s\r\n%s = %s", g_adv[idx].key, name, value);
    clipboard_set(g_main, t.p);
    ws_free(&t);
}

/* ------------------------------------------------------- view switching */

static void UpdateButtons(void)
{
    HWND lv = g_views[g_view].list;
    int n = LV_SelCount(lv), i, inMru = 0;
    Host *h;

    switch (g_view) {
    case V_COMPUTERS:
        h = SingleHost();
        for (i = 0; i < g_hosts.n; i++)
            if (g_hosts.v[i].mru >= 0) inMru++;
        EnableBtn(g_view, CM_C_CONNECT, h != NULL);
        EnableBtn(g_view, CM_C_USER, h != NULL);
        EnableBtn(g_view, CM_C_UP, h && h->mru > 0);
        EnableBtn(g_view, CM_C_DOWN, h && h->mru >= 0 && h->mru < inMru - 1);
        EnableBtn(g_view, CM_C_MORE, n > 0);
        EnableBtn(g_view, CM_C_REMOVE, n > 0);
        break;
    case V_DEFAULTS:
        EnableBtn(g_view, CM_D_EDIT, n == 1);
        EnableBtn(g_view, CM_D_DELETE, n > 0);
        EnableBtn(g_view, CM_D_NOTEPAD, g_rdp.exists);
        EnableBtn(g_view, CM_D_RESET, g_rdp.exists);
        break;
    case V_CLEANUP:
        EnableBtn(g_view, CM_X_CLEAR, n > 0);
        break;
    case V_BACKUPS:
        EnableBtn(g_view, CM_B_RESTORE, n == 1);
        EnableBtn(g_view, CM_B_DELETE, n > 0);
        break;
    case V_ADVANCED:
        EnableBtn(g_view, CM_A_COPY, n == 1);
        break;
    }
}

static void RefreshView(int v)
{
    switch (v) {
    case V_COMPUTERS: RefreshComputers(); break;
    case V_DEFAULTS:  RefreshDefaults(); break;
    case V_CLEANUP:   RefreshCleanup(); break;
    case V_BACKUPS:   RefreshBackups(); break;
    case V_ADVANCED:  RefreshAdvanced(); break;
    }
    if (v == g_view) {
        LayoutColumns(v);
        UpdateButtons();
        UpdateStatus();
    }
}

static void SwitchView(int v)
{
    int i, b;

    if (v < 0 || v >= V_COUNT) return;
    g_view = v;
    for (i = 0; i < V_COUNT; i++) {
        int show = i == v ? SW_SHOW : SW_HIDE;
        ShowWindow(g_views[i].list, show);
        for (b = 0; b < g_views[i].nbtns; b++) ShowWindow(g_views[i].btn[b], show);
        SetWindowLongPtrW(g_views[i].nav, GWLP_USERDATA, BK_NAV | (i == v ? BK_ACTIVE : 0));
        InvalidateRect(g_views[i].nav, NULL, FALSE);
    }
    Layout();
    RefreshView(v);
    SetFocus(g_views[v].list);
}

static void DefaultAction(void)
{
    switch (g_view) {
    case V_COMPUTERS: Cmd_C_Connect(); break;
    case V_DEFAULTS:  Cmd_D_Edit(); break;
    case V_BACKUPS:   Cmd_B_Restore(); break;
    case V_ADVANCED:  Cmd_A_Regedit(); break;
    }
}

static void ShowAbout(void)
{
    if (Dark_Dialog(g_main, L"About RDC Manager",
                    RDCM_NAME L" " RDCM_VERSION L"\n\n"
                    L"Manages everything Remote Desktop Connection remembers: the dropdown list, "
                    L"usernames, certificates, device prompt answers, saved passwords, connection "
                    L"defaults, the screen cache and the taskbar jump list.\n\n"
                    L"Free and open source under the MIT License. Not affiliated with Microsoft.\n\n"
                    RDCM_URL,
                    L"Open GitHub page", L"Close", BK_NORMAL) == IDOK)
        ShellExecuteW(g_main, L"open", RDCM_URL, NULL, NULL, SW_SHOWNORMAL);
}

/* ------------------------------------------------------------- commands */

static void OnCommand(int id)
{
    if (id >= ID_NAV_BASE && id < ID_NAV_BASE + V_COUNT) {
        SwitchView(id - ID_NAV_BASE);
        return;
    }
    switch (id) {
    case IDOK:          if (ViewOfList(GetFocus()) == g_view) DefaultAction(); break;
    case ID_ABOUT:      ShowAbout(); break;
    case CM_REFRESH:    RefreshView(g_view); break;

    case CM_C_ADD:      Cmd_C_Add(); break;
    case CM_C_CONNECT:  Cmd_C_Connect(); break;
    case CM_C_USER:     Cmd_C_EditUser(); break;
    case CM_C_UP:       Cmd_C_Move(-1, FALSE); break;
    case CM_C_DOWN:     Cmd_C_Move(1, FALSE); break;
    case CM_C_TOP:      Cmd_C_Move(-1, TRUE); break;
    case CM_C_BOTTOM:   Cmd_C_Move(1, TRUE); break;
    case CM_C_REMOVE:   Cmd_C_Remove(); break;
    case CM_C_COPY:     Cmd_C_Copy(); break;
    case CM_C_MORE: {
        RECT r;
        GetWindowRect(ViewBtn(V_COMPUTERS, CM_C_MORE), &r);
        ShowComputerMenu(r.left, r.bottom + S(2));
        break;
    }
    case CM_C_ADDMRU: case CM_C_RMMRU: case CM_C_FUSER:
    case CM_C_FCERT:  case CM_C_FDEV:  case CM_C_FCRED:
        Cmd_C_ForEach(id);
        break;

    case CM_D_EDIT:     Cmd_D_Edit(); break;
    case CM_D_ADD:      Cmd_D_Add(); break;
    case CM_D_DELETE:   Cmd_D_Delete(); break;
    case CM_D_NOTEPAD:  Cmd_D_Notepad(); break;
    case CM_D_RESET:    Cmd_D_Reset(); break;

    case CM_X_CLEAR:    Cmd_X_Clear(FALSE); break;
    case CM_X_CLEARALL: Cmd_X_Clear(TRUE); break;

    case CM_B_NOW:      Cmd_B_Now(); break;
    case CM_B_RESTORE:  Cmd_B_Restore(); break;
    case CM_B_DELETE:   Cmd_B_Delete(); break;
    case CM_B_FOLDER:   Cmd_B_Folder(); break;

    case CM_A_REGEDIT:  Cmd_A_Regedit(); break;
    case CM_A_COPY:     Cmd_A_Copy(); break;
    }
}

static void OnListKey(WORD vk)
{
    BOOL ctrl = GetKeyState(VK_CONTROL) < 0;

    switch (g_view) {
    case V_COMPUTERS:
        if (vk == VK_DELETE) Cmd_C_Remove();
        else if (vk == VK_F2) Cmd_C_EditUser();
        else if (ctrl && vk == 'C') Cmd_C_Copy();
        break;
    case V_DEFAULTS:
        if (vk == VK_DELETE) Cmd_D_Delete();
        else if (vk == VK_F2) Cmd_D_Edit();
        break;
    case V_CLEANUP:
        if (vk == VK_DELETE) Cmd_X_Clear(FALSE);
        break;
    case V_BACKUPS:
        if (vk == VK_DELETE) Cmd_B_Delete();
        break;
    case V_ADVANCED:
        if (ctrl && vk == 'C') Cmd_A_Copy();
        break;
    }
}

static LRESULT OnNotify(NMHDR *nm, LPARAM lParam)
{
    int v = ViewOfList(nm->hwndFrom);

    if (v < 0) return 0;
    switch (nm->code) {
    case LVN_ITEMCHANGED:
        if (!g_populating && v == g_view && (((NMLISTVIEW *)lParam)->uChanged & LVIF_STATE))
            UpdateButtons();
        break;
    case NM_DBLCLK:
        if (v == g_view && ((NMITEMACTIVATE *)lParam)->iItem >= 0) DefaultAction();
        break;
    case LVN_KEYDOWN:
        if (v == g_view) OnListKey(((NMLVKEYDOWN *)lParam)->wVKey);
        break;
    case NM_SETFOCUS:
    case NM_KILLFOCUS:
        InvalidateRect(nm->hwndFrom, NULL, FALSE);      /* selection colour depends on focus */
        break;
    case NM_CUSTOMDRAW: {
        NMLVCUSTOMDRAW *cd = (NMLVCUSTOMDRAW *)lParam;
        if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYPOSTPAINT;
        if (cd->nmcd.dwDrawStage == CDDS_POSTPAINT) {
            HWND header = ListView_GetHeader(nm->hwndFrom);
            int count = ListView_GetItemCount(nm->hwndFrom);
            RECT rc, hr = { 0, 0, 0, 0 }, last = { 0, 0, 0, 0 };

            /* Windows draws column lines in the empty space below the rows; paint over them */
            GetClientRect(nm->hwndFrom, &rc);
            if (header) GetWindowRect(header, &hr);
            rc.top += hr.bottom - hr.top;
            if (count > 0 && ListView_GetItemRect(nm->hwndFrom, count - 1, &last, LVIR_BOUNDS) &&
                last.bottom > rc.top)
                rc.top = last.bottom;
            if (rc.top < rc.bottom) FillRect(cd->nmcd.hdc, &rc, g_th.brBg);

            if (count == 0 && g_views[v].empty[0]) {
                HGDIOBJ old = SelectObject(cd->nmcd.hdc, g_th.font);
                rc.top += S(28);
                rc.left += S(20);
                rc.right -= S(20);
                SetBkMode(cd->nmcd.hdc, TRANSPARENT);
                SetTextColor(cd->nmcd.hdc, C_DIM);
                DrawTextW(cd->nmcd.hdc, g_views[v].empty, -1, &rc, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
                SelectObject(cd->nmcd.hdc, old);
            }
            if (v == V_COMPUTERS && ReorderPainting()) PaintReorderRows(cd->nmcd.hdc);
        }
        return CDRF_DODEFAULT;
    }
    }
    return 0;
}

/* -------------------------------------------------------------- window */

static void CreateChildren(HWND hwnd)
{
    int i, c, b;

    /* creation order is Tab order: navigation, then the view's buttons, then its list */
    for (i = 0; i < V_COUNT; i++)
        g_views[i].nav = Dark_CreateButton(hwnd, ID_NAV_BASE + i, g_views[i].name, BK_NAV);
    g_about = Dark_CreateButton(hwnd, ID_ABOUT, L"About", BK_NAV);

    for (i = 0; i < V_COUNT; i++) {
        View *v = &g_views[i];

        for (b = 0; b < v->nbtns; b++) {
            v->btn[b] = Dark_CreateButton(hwnd, v->btns[b].id, v->btns[b].text, v->btns[b].kind);
            ShowWindow(v->btn[b], SW_HIDE);
        }
        v->list = CreateWindowExW(0, WC_LISTVIEWW, L"",
                                  WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS |
                                  LVS_OWNERDRAWFIXED | LVS_NOSORTHEADER | v->listStyle,
                                  0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)(ID_LIST_BASE + i), g_inst, NULL);
        Dark_SetupListView(v->list, i == V_COMPUTERS);
        if (i == V_COMPUTERS) SetWindowSubclass(v->list, ComputersListProc, 2, 0);
        for (c = 0; c < v->ncols; c++) {
            LVCOLUMNW col;
            ZeroMemory(&col, sizeof(col));
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
            col.fmt = LVCFMT_LEFT;
            col.cx = S(v->cols[c].width ? v->cols[c].width : 200);
            col.pszText = (LPWSTR)v->cols[c].title;
            SendMessageW(v->list, LVM_INSERTCOLUMNW, (WPARAM)c, (LPARAM)&col);
        }
    }
}

static void OnDpiChanged(HWND hwnd, int dpi, const RECT *suggested)
{
    int i, b;

    Theme_SetDpi(dpi);
    for (i = 0; i < V_COUNT; i++) {
        SendMessageW(g_views[i].nav, WM_SETFONT, (WPARAM)g_th.font, FALSE);
        SendMessageW(g_views[i].list, WM_SETFONT, (WPARAM)g_th.font, FALSE);
        for (b = 0; b < g_views[i].nbtns; b++)
            SendMessageW(g_views[i].btn[b], WM_SETFONT, (WPARAM)g_th.font, FALSE);
        Dark_RemeasureListView(g_views[i].list);
    }
    SendMessageW(g_about, WM_SETFONT, (WPARAM)g_th.font, FALSE);
    SetWindowPos(hwnd, NULL, suggested->left, suggested->top,
                 suggested->right - suggested->left, suggested->bottom - suggested->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    Layout();
}

static LRESULT CALLBACK MainProc(HWND hwnd, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        g_main = hwnd;
        Theme_SetDpi((int)GetDpiForWindow(hwnd));
        Dark_ApplyTitleBar(hwnd);
        CreateChildren(hwnd);
        if (!g_demo) SetTimer(hwnd, TIMER_MSTSC, 2000, NULL);
        return 0;

    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintMain(hwnd);
        return 0;
    case WM_SIZE:
        if (w != SIZE_MINIMIZED) Layout();
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)l;
        mm->ptMinTrackSize.x = S(900);
        mm->ptMinTrackSize.y = S(460);
        return 0;
    }
    case WM_DPICHANGED:
        OnDpiChanged(hwnd, HIWORD(w), (const RECT *)l);
        return 0;

    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *mi = (MEASUREITEMSTRUCT *)l;
        if (mi->CtlType == ODT_LISTVIEW) {
            mi->itemHeight = (UINT)S(30);
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *di = (const DRAWITEMSTRUCT *)l;
        if (di->CtlType == ODT_BUTTON) Dark_DrawButton(di);
        else if (di->CtlType == ODT_LISTVIEW) {
            if (di->hwndItem == g_views[V_COMPUTERS].list) DrawComputerItem(di);
            else Dark_DrawListItem(di, 0);
        }
        return TRUE;
    }

    case WM_COMMAND:
        OnCommand(LOWORD(w));
        return 0;
    case WM_NOTIFY:
        return OnNotify((NMHDR *)l, l);
    case WM_CONTEXTMENU:
        if (g_view == V_COMPUTERS && (HWND)w == g_views[V_COMPUTERS].list) {
            HWND lv = (HWND)w;
            POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
            if (pt.x == -1 && pt.y == -1) {             /* opened from the keyboard */
                RECT r = { 0, 0, 0, 0 };
                int row = LV_Next(lv, -1);
                if (row >= 0) ListView_GetItemRect(lv, row, &r, LVIR_BOUNDS);
                pt.x = r.left + S(24);
                pt.y = r.bottom;
                ClientToScreen(lv, &pt);
            }
            ShowComputerMenu(pt.x, pt.y);
            return 0;
        }
        break;

    case WM_TIMER:
        if (w == TIMER_MSTSC) {
            BOOL running = Mstsc_IsRunning();
            if (running != g_mstsc) {
                g_mstsc = running;
                UpdateStatus();
            }
        } else if (w == TIMER_ANIM) {
            Anim_Tick();
        } else if (w == TIMER_DRAG) {
            Drag_Tick();
        }
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE) {
            HWND f = GetFocus();
            if (f && IsChild(hwnd, f)) g_lastFocus = f;
        }
        break;
    case WM_SETFOCUS:
        if (g_lastFocus && IsWindow(g_lastFocus) && IsWindowVisible(g_lastFocus) && IsWindowEnabled(g_lastFocus))
            SetFocus(g_lastFocus);
        else
            SetFocus(g_views[g_view].list);
        return 0;
    case WM_ACTIVATEAPP:
        /* coming back from another app (mstsc, Notepad...): show what's there now */
        if (w && g_ready && !g_modalDepth) RefreshView(g_view);
        break;

    case WM_DESTROY:
        Drag_Stop();
        KillTimer(hwnd, TIMER_ANIM);
        KillTimer(hwnd, TIMER_MSTSC);
        g_ready = FALSE;
        Model_Free(&g_hosts);
        Rdp_Free(&g_rdp);
        free(g_backups);
        g_backups = NULL;
        free(g_adv);
        g_adv = NULL;
        g_main = NULL;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, m, w, l);
}

/* Keyboard shortcuts that work anywhere in the main window. */
BOOL Main_PreTranslate(MSG *msg)
{
    if (!g_main || g_modalDepth) return FALSE;
    if (msg->hwnd != g_main && !IsChild(g_main, msg->hwnd)) return FALSE;

    if (g_drag.state != DRAG_IDLE && (msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN)) {
        if (msg->wParam == VK_ESCAPE) Drag_End(FALSE);
        return TRUE;                    /* no other shortcut runs in the middle of a drag */
    }
    if (msg->message == WM_KEYDOWN) {
        if (msg->wParam == VK_F5) {
            RefreshView(g_view);
            return TRUE;
        }
        if (GetKeyState(VK_CONTROL) < 0 && msg->wParam >= '1' && msg->wParam < (WPARAM)('1' + V_COUNT)) {
            SwitchView((int)(msg->wParam - '1'));
            return TRUE;
        }
    }
    if (msg->message == WM_SYSKEYDOWN && g_view == V_COMPUTERS &&
        (msg->wParam == VK_UP || msg->wParam == VK_DOWN) &&
        GetFocus() == g_views[V_COMPUTERS].list) {
        Cmd_C_Move(msg->wParam == VK_UP ? -1 : 1, FALSE);
        return TRUE;
    }
    return FALSE;
}

BOOL Main_Create(HINSTANCE inst, int show, BOOL demo)
{
    WNDCLASSEXW wc;
    MONITORINFO mi;
    HWND hwnd;

    g_inst = inst;
    g_demo = demo;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    wc.lpszClassName = MAIN_CLASS;
    if (!RegisterClassExW(&wc)) return FALSE;

    hwnd = CreateWindowExW(0, MAIN_CLASS,
                           demo ? RDCM_NAME L" (demo mode, sample data)" : RDCM_NAME,
                           WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           CW_USEDEFAULT, CW_USEDEFAULT, 1060, 660, NULL, NULL, inst, NULL);
    if (!hwnd) return FALSE;

    /* size for the monitor's dpi and centre in its work area */
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
        int ww = mi.rcWork.right - mi.rcWork.left, wh = mi.rcWork.bottom - mi.rcWork.top;
        int w = S(1060), h = S(660);
        if (w > ww) w = ww;
        if (h > wh) h = wh;
        SetWindowPos(hwnd, NULL, mi.rcWork.left + (ww - w) / 2, mi.rcWork.top + (wh - h) / 2, w, h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    g_mstsc = !demo && Mstsc_IsRunning();
    SwitchView(V_COMPUTERS);
    g_ready = TRUE;
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    return TRUE;
}
