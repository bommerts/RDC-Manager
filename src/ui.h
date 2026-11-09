/*
 * RDC Manager - dark user interface toolkit and main window.
 *
 * Windows has no supported dark mode for its standard controls, so the app
 * draws its own buttons, list rows, list headers and dialogs.
 */
#ifndef RDCM_UI_H
#define RDCM_UI_H

#include "rdcm.h"

/* The palette. The app is dark only. */
#define C_BG            RGB(30, 30, 30)
#define C_PANEL         RGB(37, 37, 38)
#define C_INPUT         RGB(51, 51, 55)
#define C_BORDER        RGB(63, 63, 70)
#define C_LINE          RGB(43, 43, 43)
#define C_TEXT          RGB(230, 230, 230)
#define C_DIM           RGB(150, 150, 150)
#define C_DISABLED      RGB(100, 100, 100)
#define C_ACCENT        RGB(55, 148, 255)
#define C_SEL           RGB(9, 71, 113)
#define C_SEL_INACTIVE  RGB(55, 55, 61)
#define C_BTN           RGB(45, 45, 48)
#define C_BTN_HOT       RGB(62, 62, 66)
#define C_BTN_DOWN      RGB(27, 27, 28)
#define C_PRIMARY       RGB(14, 99, 156)
#define C_PRIMARY_HOT   RGB(17, 119, 187)
#define C_PRIMARY_DOWN  RGB(11, 79, 125)
#define C_DANGER        RGB(244, 135, 113)
#define C_DANGER_HOT    RGB(90, 29, 29)
#define C_WARN          RGB(229, 192, 123)

typedef struct Theme {
    int    dpi;
    HFONT  font, fontBold;
    HBRUSH brBg, brPanel, brInput, brBorder, brLine, brSel, brSelInactive, brAccent;
} Theme;

extern Theme g_th;
extern int   g_modalDepth;          /* > 0 while one of our dialogs is open */

/* Button looks. Stored in the button's GWLP_USERDATA. */
enum {
    BK_NORMAL   = 0,
    BK_PRIMARY  = 1,
    BK_DANGER   = 2,
    BK_NAV      = 3,                /* a tab in the navigation bar */
    BK_KINDMASK = 0xFF,
    BK_ACTIVE   = 0x100,            /* the selected navigation tab */
    BK_ONPANEL  = 0x200             /* sits on the panel colour, not the window colour */
};

int  S(int px);                     /* scales 96-dpi pixels to the current dpi */
void Theme_SetDpi(int dpi);
void Theme_Free(void);
int  Text_Width(HFONT font, const wchar_t *text);

void Dark_InitApp(void);
void Dark_ApplyTitleBar(HWND hwnd);
BOOL Dark_PreTranslate(MSG *msg);

HWND Dark_CreateButton(HWND parent, int id, const wchar_t *text, int kind);
void Dark_DrawButton(const DRAWITEMSTRUCT *di);

void Dark_SetupListView(HWND list);
void Dark_DrawListItem(const DRAWITEMSTRUCT *di);
void Dark_RemeasureListView(HWND list);

/* Returns IDOK or IDCANCEL. cancelLabel may be NULL for a single-button dialog. */
int  Dark_Dialog(HWND owner, const wchar_t *title, const wchar_t *text,
                 const wchar_t *okLabel, const wchar_t *cancelLabel, int okKind);
void Dark_Info(HWND owner, const wchar_t *title, const wchar_t *text);
/* Asks for one or two lines of text. label2/buf2 may be NULL. Returns TRUE on OK. */
BOOL Dark_Input(HWND owner, const wchar_t *title, const wchar_t *text,
                const wchar_t *label1, wchar_t *buf1, int cch1,
                const wchar_t *label2, wchar_t *buf2, int cch2);

/* main window */
BOOL Main_Create(HINSTANCE inst, int show, BOOL demo);
BOOL Main_PreTranslate(MSG *msg);
HWND Main_Window(void);

/* demo mode */
void Demo_Setup(void);
void Demo_Teardown(void);

#endif /* RDCM_UI_H */
