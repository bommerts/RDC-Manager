/*
 * RDC Manager - program entry point.
 *
 *   RDCManager.exe          manage this PC's Remote Desktop history
 *   RDCManager.exe --demo   try the app on sample data; nothing real is touched
 */
#include "ui.h"

static BOOL has_arg(const wchar_t *want)
{
    int argc = 0, i;
    BOOL found = FALSE;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    if (!argv) return FALSE;
    for (i = 1; i < argc; i++)
        if (str_ieq(argv[i], want) || (argv[i][0] == L'/' && str_ieq(argv[i] + 1, want + 2)))
            found = TRUE;
    LocalFree(argv);
    return found;
}

int WINAPI wWinMain(_In_ HINSTANCE inst, _In_opt_ HINSTANCE prev, _In_ PWSTR cmdLine, _In_ int show)
{
    INITCOMMONCONTROLSEX icc;
    BOOL demo = has_arg(L"--demo");
    MSG msg;
    int code = 0;

    (void)prev;
    (void)cmdLine;

    /* never load DLLs from the folder the exe happens to sit in */
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);

    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) return 1;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    if (demo) Demo_Setup();
    else Env_InitReal();

    Dark_InitApp();
    Theme_SetDpi((int)GetDpiForSystem());

    if (!Main_Create(inst, show, demo)) {
        MessageBoxW(NULL, L"RDC Manager couldn't create its window.", RDCM_NAME, MB_OK | MB_ICONERROR);
        code = 1;
    } else {
        while (GetMessageW(&msg, NULL, 0, 0) > 0) {
            if (Main_PreTranslate(&msg)) continue;
            if (Dark_PreTranslate(&msg)) continue;
            if (Main_Window() && IsDialogMessageW(Main_Window(), &msg)) continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        code = (int)msg.wParam;
    }

    if (demo) Demo_Teardown();
    Theme_Free();
    CoUninitialize();
    return code;
}
