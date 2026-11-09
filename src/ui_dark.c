/*
 * RDC Manager - dark user interface toolkit: theme, buttons, list views and
 * modal dialogs.
 */
#include "ui.h"

Theme g_th;
int   g_modalDepth;

#define PROP_HOT   L"RdcmHot"
#define DLG_CLASS  L"RdcManagerDialog"
#define ID_EDIT0   100

/* ---------------------------------------------------------------- theme */

int S(int px)
{
    return MulDiv(px, g_th.dpi ? g_th.dpi : 96, 96);
}

void Theme_SetDpi(int dpi)
{
    NONCLIENTMETRICSW ncm;
    LOGFONTW lf;

    if (g_th.font) DeleteObject(g_th.font);
    if (g_th.fontBold) DeleteObject(g_th.fontBold);
    g_th.dpi = dpi > 0 ? dpi : 96;

    ZeroMemory(&ncm, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, (UINT)g_th.dpi)) {
        lf = ncm.lfMessageFont;
    } else {
        ZeroMemory(&lf, sizeof(lf));
        lf.lfHeight = -MulDiv(9, g_th.dpi, 72);
        StringCchCopyW(lf.lfFaceName, LF_FACESIZE, L"Segoe UI");
    }
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfWeight = FW_NORMAL;
    g_th.font = CreateFontIndirectW(&lf);
    lf.lfWeight = FW_SEMIBOLD;
    g_th.fontBold = CreateFontIndirectW(&lf);

    if (!g_th.brBg) {
        g_th.brBg = CreateSolidBrush(C_BG);
        g_th.brPanel = CreateSolidBrush(C_PANEL);
        g_th.brInput = CreateSolidBrush(C_INPUT);
        g_th.brBorder = CreateSolidBrush(C_BORDER);
        g_th.brLine = CreateSolidBrush(C_LINE);
        g_th.brSel = CreateSolidBrush(C_SEL);
        g_th.brSelInactive = CreateSolidBrush(C_SEL_INACTIVE);
        g_th.brAccent = CreateSolidBrush(C_ACCENT);
    }
}

void Theme_Free(void)
{
    HGDIOBJ objs[10];
    size_t i;

    objs[0] = g_th.font;       objs[1] = g_th.fontBold;  objs[2] = g_th.brBg;
    objs[3] = g_th.brPanel;    objs[4] = g_th.brInput;   objs[5] = g_th.brBorder;
    objs[6] = g_th.brLine;     objs[7] = g_th.brSel;     objs[8] = g_th.brSelInactive;
    objs[9] = g_th.brAccent;
    for (i = 0; i < ARRAY_LEN(objs); i++)
        if (objs[i]) DeleteObject(objs[i]);
    ZeroMemory(&g_th, sizeof(g_th));
}

int Text_Width(HFONT font, const wchar_t *text)
{
    HDC dc = GetDC(NULL);
    HGDIOBJ old = SelectObject(dc, font);
    SIZE sz = { 0, 0 };

    GetTextExtentPoint32W(dc, text, (int)wcslen(text), &sz);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    return sz.cx;
}

static int line_height(void)
{
    HDC dc = GetDC(NULL);
    HGDIOBJ old = SelectObject(dc, g_th.font);
    TEXTMETRICW tm;

    ZeroMemory(&tm, sizeof(tm));
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    return tm.tmHeight ? tm.tmHeight : S(16);
}

/* ---------------------------------------------------- app-wide dark mode */

/*
 * Popup menus and scroll bars are drawn by Windows. Two long-standing but
 * undocumented uxtheme functions switch them to dark (the same ones Notepad++
 * and File Explorer rely on). If Microsoft ever removes them the app still
 * works; those two elements would just fall back to the system colours.
 */
void Dark_InitApp(void)
{
    typedef int  (WINAPI *SetPreferredAppModeFn)(int);
    typedef void (WINAPI *FlushMenuThemesFn)(void);
    OSVERSIONINFOEXW vi;
    DWORDLONG mask = 0;
    HMODULE ux;

    ZeroMemory(&vi, sizeof(vi));
    vi.dwOSVersionInfoSize = sizeof(vi);
    vi.dwBuildNumber = 18362;                   /* Windows 10 1903, where these ordinals settled */
    mask = VerSetConditionMask(mask, VER_BUILDNUMBER, VER_GREATER_EQUAL);
    if (!VerifyVersionInfoW(&vi, VER_BUILDNUMBER, mask)) return;

    ux = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (ux) {
        SetPreferredAppModeFn setMode = (SetPreferredAppModeFn)GetProcAddress(ux, MAKEINTRESOURCEA(135));
        FlushMenuThemesFn flush = (FlushMenuThemesFn)GetProcAddress(ux, MAKEINTRESOURCEA(136));
        if (setMode) setMode(2);                /* 2 = always dark */
        if (flush) flush();
    }
}

void Dark_ApplyTitleBar(HWND hwnd)
{
    BOOL on = TRUE;
    COLORREF caption = C_BG;

    /* 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (19 on early Windows 10 builds) */
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &on, sizeof(on))))
        DwmSetWindowAttribute(hwnd, 19, &on, sizeof(on));
    /* 35 = DWMWA_CAPTION_COLOR (Windows 11); makes the title bar match the window */
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
}

/* Enter presses the focused button, as it does in a normal dialog. */
BOOL Dark_PreTranslate(MSG *msg)
{
    if (msg->message == WM_KEYDOWN && msg->wParam == VK_RETURN) {
        HWND focus = GetFocus();
        wchar_t cls[16];
        if (focus && GetClassNameW(focus, cls, ARRAY_LEN(cls)) &&
            str_ieq(cls, L"Button") && IsWindowEnabled(focus)) {
            SendMessageW(focus, BM_CLICK, 0, 0);
            return TRUE;
        }
    }
    return FALSE;
}

/* -------------------------------------------------------------- buttons */

static LRESULT CALLBACK ButtonProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref)
{
    (void)ref;
    switch (m) {
    case WM_MOUSEMOVE:
        if (!GetPropW(h, PROP_HOT)) {
            TRACKMOUSEEVENT t;
            t.cbSize = sizeof(t);
            t.dwFlags = TME_LEAVE;
            t.hwndTrack = h;
            t.dwHoverTime = 0;
            SetPropW(h, PROP_HOT, (HANDLE)(INT_PTR)1);
            TrackMouseEvent(&t);
            InvalidateRect(h, NULL, FALSE);
        }
        break;
    case WM_MOUSELEAVE:
        RemovePropW(h, PROP_HOT);
        InvalidateRect(h, NULL, FALSE);
        break;
    case WM_LBUTTONDBLCLK:
        /* owner-drawn buttons swallow the second click of a fast double click */
        return DefSubclassProc(h, WM_LBUTTONDOWN, w, l);
    case WM_ERASEBKGND:
        return 1;
    case WM_NCDESTROY:
        RemovePropW(h, PROP_HOT);
        RemoveWindowSubclass(h, ButtonProc, id);
        break;
    }
    return DefSubclassProc(h, m, w, l);
}

HWND Dark_CreateButton(HWND parent, int id, const wchar_t *text, int kind)
{
    HWND h = CreateWindowExW(0, L"BUTTON", text,
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                             0, 0, 0, 0, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    if (!h) return NULL;
    SetWindowLongPtrW(h, GWLP_USERDATA, kind);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_th.font, FALSE);
    SetWindowSubclass(h, ButtonProc, 1, 0);
    return h;
}

void Dark_DrawButton(const DRAWITEMSTRUCT *di)
{
    HWND h = di->hwndItem;
    HDC dc = di->hDC;
    RECT rc = di->rcItem;
    int flags = (int)GetWindowLongPtrW(h, GWLP_USERDATA);
    int kind = flags & BK_KINDMASK;
    BOOL down = (di->itemState & ODS_SELECTED) != 0;
    BOOL disabled = (di->itemState & ODS_DISABLED) != 0;
    BOOL focus = (di->itemState & ODS_FOCUS) && !(di->itemState & ODS_NOFOCUSRECT);
    BOOL hot = GetPropW(h, PROP_HOT) != NULL && !disabled;
    BOOL onPanel = (flags & BK_ONPANEL) || kind == BK_NAV;
    COLORREF fill = C_BTN, border = C_BORDER, text = C_TEXT;
    wchar_t label[128];
    HGDIOBJ oldFont;

    FillRect(dc, &rc, onPanel ? g_th.brPanel : g_th.brBg);
    GetWindowTextW(h, label, ARRAY_LEN(label));
    SetBkMode(dc, TRANSPARENT);

    if (kind == BK_NAV) {
        BOOL active = (flags & BK_ACTIVE) != 0;
        oldFont = SelectObject(dc, active ? g_th.fontBold : g_th.font);
        SetTextColor(dc, active || hot ? C_TEXT : C_DIM);
        DrawTextW(dc, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (active) {
            RECT bar = { rc.left + S(10), rc.bottom - S(3), rc.right - S(10), rc.bottom };
            FillRect(dc, &bar, g_th.brAccent);
        }
        if (focus) {
            RECT fr = { rc.left + S(4), rc.top + S(8), rc.right - S(4), rc.bottom - S(8) };
            FrameRect(dc, &fr, g_th.brBorder);
        }
        SelectObject(dc, oldFont);
        return;
    }

    if (kind == BK_PRIMARY) {
        fill = down ? C_PRIMARY_DOWN : hot ? C_PRIMARY_HOT : C_PRIMARY;
        border = fill;
        text = RGB(255, 255, 255);
    } else if (kind == BK_DANGER) {
        fill = down ? C_BTN_DOWN : hot ? C_DANGER_HOT : C_BTN;
        text = C_DANGER;
    } else {
        fill = down ? C_BTN_DOWN : hot ? C_BTN_HOT : C_BTN;
    }
    if (disabled) {
        fill = onPanel ? C_PANEL : C_BG;
        border = C_LINE;
        text = C_DISABLED;
    }
    if (focus) border = kind == BK_PRIMARY ? RGB(160, 210, 255) : C_ACCENT;

    {
        HBRUSH br = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, border);
        HGDIOBJ oldBr = SelectObject(dc, br), oldPen = SelectObject(dc, pen);
        RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, S(6), S(6));
        SelectObject(dc, oldBr);
        SelectObject(dc, oldPen);
        DeleteObject(br);
        DeleteObject(pen);
    }

    oldFont = SelectObject(dc, g_th.font);
    SetTextColor(dc, text);
    DrawTextW(dc, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, oldFont);
}

/* ------------------------------------------------------------ list view */

/* Paints the column header ourselves; the stock one can't be made dark. */
static LRESULT CALLBACK HeaderProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref)
{
    (void)ref;
    switch (m) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        HGDIOBJ old = SelectObject(dc, g_th.font);
        int i, n = Header_GetItemCount(h);
        RECT rc, line;

        GetClientRect(h, &rc);
        FillRect(dc, &rc, g_th.brPanel);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, C_DIM);
        for (i = 0; i < n; i++) {
            wchar_t text[128];
            HDITEMW it;
            RECT r, t, div;

            ZeroMemory(&it, sizeof(it));
            it.mask = HDI_TEXT;
            it.pszText = text;
            it.cchTextMax = ARRAY_LEN(text);
            text[0] = 0;
            if (!Header_GetItemRect(h, i, &r) || !Header_GetItem(h, i, &it)) continue;
            t = r;
            t.left += S(10);
            t.right -= S(6);
            if (t.right > t.left)
                DrawTextW(dc, text, -1, &t, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            div.left = r.right - 1;
            div.right = r.right;
            div.top = r.top + S(7);
            div.bottom = r.bottom - S(7);
            FillRect(dc, &div, g_th.brBorder);
        }
        line = rc;
        line.top = rc.bottom - 1;
        FillRect(dc, &line, g_th.brBorder);
        SelectObject(dc, old);
        EndPaint(h, &ps);
        return 0;
    }
    case HDM_LAYOUT: {
        LRESULT r = DefSubclassProc(h, m, w, l);
        HDLAYOUT *hl = (HDLAYOUT *)l;
        if (r && hl && hl->pwpos && hl->prc) {
            int want = S(32);
            hl->prc->top += want - hl->pwpos->cy;
            hl->pwpos->cy = want;
        }
        return r;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(h, HeaderProc, id);
        break;
    }
    return DefSubclassProc(h, m, w, l);
}

void Dark_SetupListView(HWND list)
{
    HWND header = ListView_GetHeader(list);

    SetWindowTheme(list, L"DarkMode_Explorer", NULL);           /* dark scroll bars */
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_SetBkColor(list, C_BG);
    ListView_SetTextBkColor(list, C_BG);
    ListView_SetTextColor(list, C_TEXT);
    SendMessageW(list, WM_SETFONT, (WPARAM)g_th.font, FALSE);
    if (header) SetWindowSubclass(header, HeaderProc, 1, 0);
}

/* Makes the list ask for its row height again (after a dpi change). */
void Dark_RemeasureListView(HWND list)
{
    RECT rc;
    WINDOWPOS wp;

    GetWindowRect(list, &rc);
    ZeroMemory(&wp, sizeof(wp));
    wp.hwnd = list;
    wp.cx = rc.right - rc.left;
    wp.cy = rc.bottom - rc.top;
    wp.flags = SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER;
    SendMessageW(list, WM_WINDOWPOSCHANGED, 0, (LPARAM)&wp);
}

/* Draws one row. A cell holding just an em dash means "nothing here" and is dimmed. */
void Dark_DrawListItem(const DRAWITEMSTRUCT *di)
{
    HWND list = di->hwndItem, header = ListView_GetHeader(list);
    HDC dc = di->hDC;
    RECT rc = di->rcItem, client;
    BOOL selected = (di->itemState & ODS_SELECTED) != 0;
    BOOL active = GetFocus() == list;
    int cols = header ? Header_GetItemCount(header) : 0, c;
    HGDIOBJ old;

    if ((int)di->itemID < 0) return;
    /* a row always spans the whole list, even if the columns stop short of the edge */
    GetClientRect(list, &client);
    if (rc.right < client.right) rc.right = client.right;
    FillRect(dc, &rc, selected ? (active ? g_th.brSel : g_th.brSelInactive) : g_th.brBg);
    if (!selected) {
        RECT line = rc;
        line.top = rc.bottom - 1;
        FillRect(dc, &line, g_th.brLine);
    }

    old = SelectObject(dc, g_th.font);
    SetBkMode(dc, TRANSPARENT);
    for (c = 0; c < cols; c++) {
        wchar_t text[512];
        RECT hr, cell;

        if (!Header_GetItemRect(header, c, &hr)) continue;
        cell.left = rc.left + hr.left + S(10);
        cell.right = rc.left + hr.right - S(6);
        cell.top = rc.top;
        cell.bottom = rc.bottom;
        if (cell.right <= cell.left) continue;
        text[0] = 0;
        ListView_GetItemText(list, (int)di->itemID, c, text, ARRAY_LEN(text));
        if (text[0] == 0x2014 && text[1] == 0)
            SetTextColor(dc, selected ? RGB(170, 170, 170) : C_DISABLED);
        else
            SetTextColor(dc, c == 0 || selected ? C_TEXT : RGB(204, 204, 204));
        DrawTextW(dc, text, -1, &cell, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    SelectObject(dc, old);

    if ((di->itemState & ODS_FOCUS) && active && !(di->itemState & ODS_NOFOCUSRECT))
        FrameRect(dc, &rc, g_th.brAccent);
}

/* -------------------------------------------------------------- dialogs */

typedef struct DlgState {
    const wchar_t *text;
    const wchar_t *label[2];
    wchar_t       *buf[2];
    int            cch[2];
    int            nEdits;
    const wchar_t *okLabel, *cancelLabel;
    int            okKind;
    HWND           edit[2], btnOk, btnCancel, lastFocus;
    RECT           rcText, rcLabel[2], rcFrame[2];
    int            stripTop, clientW, clientH;
    int            result;
    BOOL           done;
} DlgState;

static void dlg_layout(DlgState *st)
{
    HDC dc = GetDC(NULL);
    HGDIOBJ old = SelectObject(dc, g_th.font);
    int pad = S(20), w = S(470), y = pad, lineH = line_height(), i;

    st->clientW = w;
    SetRectEmpty(&st->rcText);
    if (st->text && st->text[0]) {
        RECT r = { pad, y, w - pad, y };
        DrawTextW(dc, st->text, -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS);
        SetRect(&st->rcText, pad, y, w - pad, r.bottom);
        y = r.bottom + S(16);
    }
    for (i = 0; i < st->nEdits; i++) {
        SetRect(&st->rcLabel[i], pad, y, w - pad, y + lineH);
        y += lineH + S(6);
        SetRect(&st->rcFrame[i], pad, y, w - pad, y + S(32));
        y += S(32) + S(14);
    }
    st->stripTop = y + S(4);
    st->clientH = st->stripTop + S(58);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
}

static void dlg_create_children(HWND hwnd, DlgState *st)
{
    HINSTANCE inst = GetModuleHandleW(NULL);
    int lineH = line_height(), i;
    int btnH = S(30), btnY = st->stripTop + (S(58) - btnH) / 2, x = st->clientW - S(16);
    int wOk = Text_Width(g_th.font, st->okLabel) + S(32), wCancel = 0;

    for (i = 0; i < st->nEdits; i++) {
        RECT f = st->rcFrame[i];
        int h = lineH + S(2);
        st->edit[i] = CreateWindowExW(0, L"EDIT", st->buf[i],
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                      f.left + S(9), f.top + (f.bottom - f.top - h) / 2,
                                      f.right - f.left - S(18), h,
                                      hwnd, (HMENU)(INT_PTR)(ID_EDIT0 + i), inst, NULL);
        SendMessageW(st->edit[i], WM_SETFONT, (WPARAM)g_th.font, FALSE);
        SendMessageW(st->edit[i], EM_SETLIMITTEXT, (WPARAM)(st->cch[i] - 1), 0);
    }

    if (wOk < S(88)) wOk = S(88);
    if (st->cancelLabel) {
        wCancel = Text_Width(g_th.font, st->cancelLabel) + S(32);
        if (wCancel < S(88)) wCancel = S(88);
    }
    st->btnOk = Dark_CreateButton(hwnd, IDOK, st->okLabel, st->okKind | BK_ONPANEL);
    if (st->cancelLabel) {
        st->btnCancel = Dark_CreateButton(hwnd, IDCANCEL, st->cancelLabel, BK_NORMAL | BK_ONPANEL);
        x -= wCancel;
        MoveWindow(st->btnCancel, x, btnY, wCancel, btnH, FALSE);
        x -= S(8);
    }
    x -= wOk;
    MoveWindow(st->btnOk, x, btnY, wOk, btnH, FALSE);
}

static void dlg_paint(HWND hwnd, DlgState *st)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    HGDIOBJ old = SelectObject(dc, g_th.font);
    RECT rc, strip, line;
    int i;

    GetClientRect(hwnd, &rc);
    FillRect(dc, &rc, g_th.brBg);
    strip = rc;
    strip.top = st->stripTop;
    FillRect(dc, &strip, g_th.brPanel);
    line = strip;
    line.bottom = line.top + 1;
    FillRect(dc, &line, g_th.brBorder);

    SetBkMode(dc, TRANSPARENT);
    if (st->text && st->text[0]) {
        RECT r = st->rcText;
        SetTextColor(dc, C_TEXT);
        DrawTextW(dc, st->text, -1, &r, DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS);
    }
    for (i = 0; i < st->nEdits; i++) {
        RECT r = st->rcLabel[i];
        SetTextColor(dc, C_DIM);
        DrawTextW(dc, st->label[i], -1, &r, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        FillRect(dc, &st->rcFrame[i], g_th.brInput);
        FrameRect(dc, &st->rcFrame[i], GetFocus() == st->edit[i] ? g_th.brAccent : g_th.brBorder);
    }
    SelectObject(dc, old);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK DialogProc(HWND hwnd, UINT m, WPARAM w, LPARAM l)
{
    DlgState *st = (DlgState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    int i;

    switch (m) {
    case WM_NCCREATE:
        st = (DlgState *)((CREATESTRUCTW *)l)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
        break;
    case WM_CREATE:
        Dark_ApplyTitleBar(hwnd);
        dlg_create_children(hwnd, st);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (st) {
            dlg_paint(hwnd, st);
            return 0;
        }
        break;
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)w, C_TEXT);
        SetBkColor((HDC)w, C_INPUT);
        return (LRESULT)g_th.brInput;
    case WM_DRAWITEM:
        if (((DRAWITEMSTRUCT *)l)->CtlType == ODT_BUTTON) {
            Dark_DrawButton((DRAWITEMSTRUCT *)l);
            return TRUE;
        }
        break;
    case DM_GETDEFID:
        return MAKELRESULT(IDOK, DC_HASDEFID);
    case WM_ACTIVATE:
        if (st && LOWORD(w) == WA_INACTIVE) st->lastFocus = GetFocus();
        break;
    case WM_SETFOCUS:
        if (st && st->lastFocus && IsWindow(st->lastFocus)) SetFocus(st->lastFocus);
        return 0;
    case WM_COMMAND:
        if (!st) break;
        if (LOWORD(w) >= ID_EDIT0 && LOWORD(w) < ID_EDIT0 + 2) {
            if (HIWORD(w) == EN_SETFOCUS || HIWORD(w) == EN_KILLFOCUS)
                InvalidateRect(hwnd, &st->rcFrame[LOWORD(w) - ID_EDIT0], FALSE);
            return 0;
        }
        if (LOWORD(w) == IDOK) {
            for (i = 0; i < st->nEdits; i++)
                GetWindowTextW(st->edit[i], st->buf[i], st->cch[i]);
            st->result = IDOK;
            st->done = TRUE;
        } else if (LOWORD(w) == IDCANCEL) {
            st->result = IDCANCEL;
            st->done = TRUE;
        }
        return 0;
    case WM_CLOSE:
        if (st) {
            st->result = IDCANCEL;
            st->done = TRUE;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, m, w, l);
}

static int dlg_run(HWND owner, const wchar_t *title, DlgState *st)
{
    static BOOL registered;
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU, exStyle = WS_EX_DLGMODALFRAME;
    HINSTANCE inst = GetModuleHandleW(NULL);
    MONITORINFO mi;
    RECT wr, anchor;
    HWND hwnd;
    MSG msg;
    int w, h, x, y;
    BOOL quit = FALSE;

    ZeroMemory(&msg, sizeof(msg));
    if (!registered) {
        WNDCLASSEXW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DialogProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
        wc.lpszClassName = DLG_CLASS;
        RegisterClassExW(&wc);
        registered = TRUE;
    }

    st->result = IDCANCEL;
    dlg_layout(st);
    SetRect(&wr, 0, 0, st->clientW, st->clientH);
    AdjustWindowRectExForDpi(&wr, style, FALSE, exStyle, (UINT)g_th.dpi);
    w = wr.right - wr.left;
    h = wr.bottom - wr.top;

    /* centre on the owner, kept inside its monitor's work area */
    if (owner && !IsIconic(owner)) GetWindowRect(owner, &anchor);
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &anchor, 0);
    x = anchor.left + (anchor.right - anchor.left - w) / 2;
    y = anchor.top + (anchor.bottom - anchor.top - h) / 2;
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &mi)) {
        if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
        if (y + h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;
        if (x < mi.rcWork.left) x = mi.rcWork.left;
        if (y < mi.rcWork.top) y = mi.rcWork.top;
    }

    hwnd = CreateWindowExW(exStyle, DLG_CLASS, title, style, x, y, w, h, owner, NULL, inst, st);
    if (!hwnd) return IDCANCEL;

    g_modalDepth++;
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    if (st->nEdits) {
        SetFocus(st->edit[0]);
        SendMessageW(st->edit[0], EM_SETSEL, 0, -1);
    } else {
        /* for destructive questions the safe button starts focused */
        SetFocus(st->okKind == BK_DANGER && st->btnCancel ? st->btnCancel : st->btnOk);
    }

    while (!st->done) {
        BOOL r = GetMessageW(&msg, NULL, 0, 0);
        if (r <= 0) {
            quit = r == 0;
            break;
        }
        if (Dark_PreTranslate(&msg)) continue;
        if (IsDialogMessageW(hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (owner) EnableWindow(owner, TRUE);
    DestroyWindow(hwnd);
    g_modalDepth--;
    if (owner) SetForegroundWindow(owner);
    if (quit) PostQuitMessage((int)msg.wParam);
    return st->result;
}

int Dark_Dialog(HWND owner, const wchar_t *title, const wchar_t *text,
                const wchar_t *okLabel, const wchar_t *cancelLabel, int okKind)
{
    DlgState st;

    ZeroMemory(&st, sizeof(st));
    st.text = text;
    st.okLabel = okLabel ? okLabel : L"OK";
    st.cancelLabel = cancelLabel;
    st.okKind = okKind;
    return dlg_run(owner, title, &st);
}

void Dark_Info(HWND owner, const wchar_t *title, const wchar_t *text)
{
    Dark_Dialog(owner, title, text, L"OK", NULL, BK_PRIMARY);
}

BOOL Dark_Input(HWND owner, const wchar_t *title, const wchar_t *text,
                const wchar_t *label1, wchar_t *buf1, int cch1,
                const wchar_t *label2, wchar_t *buf2, int cch2)
{
    DlgState st;

    ZeroMemory(&st, sizeof(st));
    st.text = text;
    st.okLabel = L"OK";
    st.cancelLabel = L"Cancel";
    st.okKind = BK_PRIMARY;
    st.label[0] = label1;
    st.buf[0] = buf1;
    st.cch[0] = cch1;
    st.nEdits = 1;
    if (label2 && buf2) {
        st.label[1] = label2;
        st.buf[1] = buf2;
        st.cch[1] = cch2;
        st.nEdits = 2;
    }
    return dlg_run(owner, title, &st) == IDOK;
}
