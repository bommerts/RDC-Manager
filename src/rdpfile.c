/*
 * RDC Manager - reads and writes Default.rdp, the file where Remote Desktop
 * Connection keeps the settings it starts with (display, sound, devices...).
 *
 * The format is one "name:type:value" setting per line, where type is
 * i (number), s (text) or b (binary).
 */
#include "rdcm.h"

typedef struct Known {
    const wchar_t *name;
    wchar_t        type;
    const wchar_t *about;
} Known;

static const Known kKnown[] = {
    { L"full address",               L's', L"Computer to connect to" },
    { L"alternate full address",     L's', L"Alternate name or address for the computer" },
    { L"server port",                L'i', L"Port number (3389 by default)" },
    { L"username",                   L's', L"Username filled in at sign-in" },
    { L"domain",                     L's', L"Domain filled in at sign-in" },
    { L"screen mode id",             L'i', L"1 = window, 2 = full screen" },
    { L"use multimon",               L'i', L"1 = use all monitors" },
    { L"selectedmonitors",           L's', L"Which monitors to use" },
    { L"span monitors",              L'i', L"1 = stretch one desktop across monitors" },
    { L"desktopwidth",               L'i', L"Remote desktop width in pixels" },
    { L"desktopheight",              L'i', L"Remote desktop height in pixels" },
    { L"desktopscalefactor",         L'i', L"Display scaling percentage" },
    { L"session bpp",                L'i', L"Colour depth in bits (15, 16, 24 or 32)" },
    { L"winposstr",                  L's', L"Window position and size" },
    { L"smart sizing",               L'i', L"1 = scale the desktop to fit the window" },
    { L"dynamic resolution",         L'i', L"1 = change resolution when the window is resized" },
    { L"compression",                L'i', L"1 = compress data sent over the connection" },
    { L"keyboardhook",               L'i', L"Windows key shortcuts: 0 = this PC, 1 = remote PC, 2 = remote in full screen" },
    { L"audiomode",                  L'i', L"Sound: 0 = play here, 1 = play on remote PC, 2 = don't play" },
    { L"audiocapturemode",           L'i', L"1 = send this PC's microphone" },
    { L"videoplaybackmode",          L'i', L"1 = efficient video playback" },
    { L"connection type",            L'i', L"Connection speed: 1 = modem ... 6 = LAN, 7 = detect automatically" },
    { L"networkautodetect",          L'i', L"1 = detect connection quality automatically" },
    { L"bandwidthautodetect",        L'i', L"1 = detect bandwidth automatically" },
    { L"displayconnectionbar",       L'i', L"1 = show the connection bar in full screen" },
    { L"pinconnectionbar",           L'i', L"1 = keep the connection bar pinned" },
    { L"enableworkspacereconnect",   L'i', L"1 = reconnect to workspace resources" },
    { L"disable wallpaper",          L'i', L"1 = hide the remote desktop background" },
    { L"allow font smoothing",       L'i', L"1 = smooth fonts (ClearType)" },
    { L"allow desktop composition",  L'i', L"1 = allow desktop composition effects" },
    { L"disable full window drag",   L'i', L"1 = show only an outline while dragging windows" },
    { L"disable menu anims",         L'i', L"1 = turn off menu and window animations" },
    { L"disable themes",             L'i', L"1 = turn off visual styles" },
    { L"disable cursor setting",     L'i', L"1 = turn off cursor settings" },
    { L"bitmapcachepersistenable",   L'i', L"1 = keep the screen cache on disk" },
    { L"bitmapcachesize",            L'i', L"Screen cache size" },
    { L"redirectprinters",           L'i', L"1 = share this PC's printers" },
    { L"redirectcomports",           L'i', L"1 = share this PC's COM ports" },
    { L"redirectsmartcards",         L'i', L"1 = share smart cards and Windows Hello" },
    { L"redirectwebauthn",           L'i', L"1 = share security keys (WebAuthn)" },
    { L"redirectclipboard",          L'i', L"1 = share the clipboard" },
    { L"redirectposdevices",         L'i', L"1 = share point-of-service devices" },
    { L"redirectlocation",           L'i', L"1 = share this PC's location" },
    { L"redirectdirectx",            L'i', L"1 = redirect DirectX rendering" },
    { L"drivestoredirect",           L's', L"Drives shared with the remote PC (* = all)" },
    { L"devicestoredirect",          L's', L"Plug and Play devices shared with the remote PC" },
    { L"camerastoredirect",          L's', L"Cameras shared with the remote PC" },
    { L"usbdevicestoredirect",       L's', L"USB devices shared with the remote PC" },
    { L"autoreconnection enabled",   L'i', L"1 = reconnect automatically if the connection drops" },
    { L"autoreconnect max retries",  L'i', L"How many times to try reconnecting" },
    { L"authentication level",       L'i', L"If the PC can't be verified: 0 = connect, 1 = don't connect, 2 = warn me" },
    { L"prompt for credentials",     L'i', L"1 = always ask for credentials" },
    { L"promptcredentialonce",       L'i', L"1 = use the same credentials for gateway and PC" },
    { L"negotiate security layer",   L'i', L"1 = negotiate the security layer" },
    { L"enablecredsspsupport",       L'i', L"1 = use CredSSP (Network Level Authentication)" },
    { L"enablerdsaadauth",           L'i', L"1 = sign in with a Microsoft Entra account" },
    { L"administrative session",     L'i', L"1 = connect to the administrative session" },
    { L"remoteapplicationmode",      L'i', L"1 = start a RemoteApp program" },
    { L"remoteapplicationname",      L's', L"RemoteApp display name" },
    { L"remoteapplicationprogram",   L's', L"RemoteApp program to start" },
    { L"remoteappmousemoveinject",   L'i', L"1 = inject mouse moves for RemoteApp" },
    { L"alternate shell",            L's', L"Program to start on connection" },
    { L"shell working directory",    L's', L"Folder the start program runs in" },
    { L"gatewayhostname",            L's', L"Remote Desktop Gateway server" },
    { L"gatewayusagemethod",         L'i', L"When to use the gateway: 0 = never, 1 = always, 2 = if direct fails" },
    { L"gatewaycredentialssource",   L'i', L"How to sign in to the gateway" },
    { L"gatewayprofileusagemethod",  L'i', L"0 = default gateway settings, 1 = use these settings" },
    { L"gatewaybrokeringtype",       L'i', L"Gateway brokering type" },
    { L"use redirection server name",L'i', L"1 = use the redirection server name" },
    { L"rdgiskdcproxy",              L'i', L"1 = the gateway also acts as a KDC proxy" },
    { L"kdcproxyname",               L's', L"KDC proxy server" },
};

static const Known *known_find(const wchar_t *name)
{
    size_t i;
    for (i = 0; i < ARRAY_LEN(kKnown); i++)
        if (str_ieq(kKnown[i].name, name)) return &kKnown[i];
    return NULL;
}

const wchar_t *Rdp_Describe(const wchar_t *name)
{
    const Known *k = known_find(name);
    return k ? k->about : L"";
}

/* Returns the type a known setting uses, or 0 for settings we don't know. */
wchar_t Rdp_KnownType(const wchar_t *name)
{
    const Known *k = known_find(name);
    return k ? k->type : 0;
}

static RdpLine *add_line(RdpFile *f)
{
    if (f->n == f->cap) {
        f->cap = f->cap ? f->cap * 2 : 64;
        f->v = (RdpLine *)xrealloc(f->v, (size_t)f->cap * sizeof(RdpLine));
    }
    ZeroMemory(&f->v[f->n], sizeof(RdpLine));
    return &f->v[f->n++];
}

void Rdp_Free(RdpFile *f)
{
    int i;
    for (i = 0; i < f->n; i++) {
        /* every line below f->n was zeroed by add_line; the analyzer can't see that */
#pragma warning(suppress: 6001)
        free(f->v[i].name);
#pragma warning(suppress: 6001)
        free(f->v[i].value);
    }
    free(f->v);
    ZeroMemory(f, sizeof(*f));
}

/* Loads Default.rdp. Returns FALSE (with an empty list) when there is no file. */
BOOL Rdp_Load(RdpFile *f)
{
    BYTE *data = NULL;
    DWORD size = 0;
    wchar_t *text, *p;

    Rdp_Free(f);
    if (!g_env.rdpPath[0] || !file_read_all(g_env.rdpPath, &data, &size)) return FALSE;
    f->exists = TRUE;
    text = text_decode(data, size, NULL);
    free(data);

    p = text;
    while (*p) {
        wchar_t *line = p, *end = p, *colon;
        RdpLine *ln;

        while (*end && *end != L'\n') end++;
        p = *end ? end + 1 : end;
        while (end > line && (end[-1] == L'\r' || end[-1] == L' ' || end[-1] == L'\t')) end--;
        *end = 0;
        if (!*line) continue;

        ln = add_line(f);
        colon = wcschr(line, L':');
        if (colon && colon > line && colon[1] && colon[2] == L':') {
            *colon = 0;
            ln->name = xwcsdup(line);
            ln->type = colon[1];
            ln->value = xwcsdup(colon + 3);
        } else {
            ln->value = xwcsdup(line);          /* keep lines we don't understand as they are */
        }
    }
    free(text);
    return TRUE;
}

BOOL Rdp_Save(const RdpFile *f)
{
    WStr out = { 0 };
    BOOL ok;
    int i;

    if (!g_env.rdpPath[0]) return FALSE;
    ws_clear(&out);
    for (i = 0; i < f->n; i++) {
        const RdpLine *ln = &f->v[i];
        if (ln->name) {
            ws_append(&out, ln->name);
            ws_putc(&out, L':');
            ws_putc(&out, ln->type);
            ws_putc(&out, L':');
        }
        ws_append(&out, ln->value);
        ws_append(&out, L"\r\n");
    }
    ok = text_write_utf16(g_env.rdpPath, out.p, out.len);
    ws_free(&out);
    return ok;
}

int Rdp_Find(const RdpFile *f, const wchar_t *name)
{
    int i;
    for (i = 0; i < f->n; i++)
        if (f->v[i].name && str_ieq(f->v[i].name, name)) return i;
    return -1;
}

/* Adds a setting or changes an existing one. */
void Rdp_Set(RdpFile *f, const wchar_t *name, wchar_t type, const wchar_t *value)
{
    int at = Rdp_Find(f, name);
    RdpLine *ln;

    if (at >= 0) {
        ln = &f->v[at];
        free(ln->value);
    } else {
        ln = add_line(f);
        ln->name = xwcsdup(name);
    }
    ln->type = type;
    ln->value = xwcsdup(value);
}

void Rdp_Delete(RdpFile *f, int index)
{
    if (index < 0 || index >= f->n) return;
    free(f->v[index].name);
    free(f->v[index].value);
    memmove(&f->v[index], &f->v[index + 1], (size_t)(f->n - index - 1) * sizeof(RdpLine));
    f->n--;
}
