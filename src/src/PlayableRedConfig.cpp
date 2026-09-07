// PlayableRedConfig.cpp -- the settings window of PlayableRed.asi.
//
// A borderless dark window: a header painted with GDI+ (smoke, "SILENT HILL 2"
// eroded and dripping, "PYRAMID HEAD" spaced beneath), then one button per
// feature -- lit green when on, dim when off, greyed when its parent is off --
// with value fields beside the ones that take a number, dark tooltips on
// hover for what each does, and Save / Defaults / Close.  Drag it by the header.
//
// Lives in the game's root folder (next to sh2pc.exe) and writes
// <game>\plugins\PlayableRedMod\PlayableRed.ini, which is where the plugin
// reads it; run from inside PlayableRedMod\ it uses the file beside itself.
// The plugin notices a change within a second, so this works with the game
// running.  Nothing here touches the game.
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <gdiplus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "playablered_logic.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

static char g_ini[MAX_PATH];
static HFONT g_font, g_fontBold;
static HWND g_win, g_status, g_tip;
static HBRUSH g_bgBrush, g_editBrush;
static const COLORREF C_BG = RGB(12, 14, 13), C_EDIT = RGB(26, 30, 27), C_TEXT = RGB(92, 232, 112),
                      C_DIM = RGB(46, 110, 58), C_OFF = RGB(30, 36, 32), C_ON = RGB(20, 64, 30), C_BORDER = RGB(44, 120, 58);
static const int W = 620, HEADER_H = 150, ROWH = 27, LEFT = 18, LABELW = 320, EDITW = 130;

enum Kind { K_TOGGLE, K_FLOAT, K_INT, K_HEX };
struct Row {
    const char* label;
    Kind kind;
    size_t off;
    int parent;          // index of the toggle this one needs, or -1
    int indent;
    const char* tip;
    HWND ctl;
    bool on;             // toggles only
};

#define OFF(f) offsetof(Config, f)
static Row g_rows[] = {
    {"Pyramid Head's animations", K_TOGGLE, OFF(animation), -1, 0, "James plays Pyramid Head's idle, walk, stance, swing and overhead while the Great Knife is equipped.\nOther weapons keep his own clips. Uses the mod's animation bank, sh2e\\chr\\jms\\jms_wpnata.anm."},
    {"Light attack damage", K_FLOAT, OFF(lightDamage), -1, 0, "Great Knife swing damage. Stock 600. 0 = stock."},
    {"Heavy attack damage", K_FLOAT, OFF(heavyDamage), -1, 0, "Great Knife overhead damage. Stock 1000. 0 = stock."},
    {"Great knife hit line follows the blade", K_TOGGLE, OFF(knifeHitFollowsBlade), -1, 0, "The Great Knife's hit line runs from its grip to a tip marker in the weapon file.\nThe mesh was turned 40 degrees into Pyramid Head's grip but the marker was not,\nso the game swung a line 30 degrees off the drawn blade. On: the marker follows the blade."},
    {"Light attack widen (degrees)", K_FLOAT, OFF(lightWiden), -1, 0, "Widens the light attack's sweep by this many degrees on each side. 0 = the game's own."},
    {"Max health", K_FLOAT, OFF(maxHealth), -1, 0, "Held at this value, so a loaded save cannot lower it.\nA brand new game starts at full health. 0 = leave the game's own."},
    {"Fatigue zero", K_TOGGLE, OFF(fatigueZero), -1, 0, "Fatigue held at 0: no tiring."},
    {"Hyper armour", K_TOGGLE, OFF(hyperArmour), -1, 0, "No flinch when hit."},
    {"Allow game over", K_TOGGLE, OFF(allowGameOver), 7, 1, "When health reaches 0 the game-over state is set.\nNeeds Hyper armour."},
    {"Immortal Maria", K_TOGGLE, OFF(mariaImmortal), -1, 0, "Maria cannot die: her death no longer ends the game,\nand James keeps normal controls after it."},
    {"Flashlight colour", K_TOGGLE, OFF(flashlightColour), -1, 0, "The torch takes the colour below. 127 / 0.001 / 0.001 is the red torch."},
    {"red", K_FLOAT, OFF(torchR), 10, 1, "Red, 0..255."},
    {"green", K_FLOAT, OFF(torchG), 10, 1, "Green, 0..255."},
    {"blue", K_FLOAT, OFF(torchB), 10, 1, "Blue, 0..255."},
    {"Floodlight", K_TOGGLE, OFF(floodlight), -1, 0, "The beam takes the size below."},
    {"beam size", K_FLOAT, OFF(floodSize), 14, 1, "Stock about 1.0."},
    {"Flashlight glow gone", K_TOGGLE, OFF(glowGone), -1, 0, "Removes the flashlight's lens glow (the two 0.5 constants set to 0)."},
    {"Great knife in inventory", K_TOGGLE, OFF(knifeInInventory), -1, 0, "Verifies the item is in the inventory and adds it when absent.\nOnly its own flag bit is set; every other item is left alone."},
    {"Flashlight in inventory", K_TOGGLE, OFF(flashlightInInventory), -1, 0, "Verifies the item is in the inventory and adds it when absent.\nOnly its own flag bit is set; every other item is left alone."},
    {"2D directional controls", K_TOGGLE, OFF(directional2D), -1, 0, "The game's 2D directional control scheme, held on."},
    {"Toggle run", K_TOGGLE, OFF(toggleRun), -1, 0, "Press the key or the pad button to switch run mode.\nWhile on, the walk travels at the run speed and its animation steps faster."},
    {"key", K_HEX, OFF(runKey), 20, 1, "Virtual-key code. 0x10 = Shift."},
    {"pad button", K_HEX, OFF(runPadButton), 20, 1, "XInput button mask. 0x4000 = X on an Xbox pad, Square on a PlayStation pad."},
    {"run speed", K_FLOAT, OFF(runSpeed), 20, 1, "Travel speed while running. The stock walk is 1.5."},
    {"run animation step", K_INT, OFF(runStep), 20, 1, "Animation step per tick while running. Stock about 1408."},
    {"Run mode on at start", K_TOGGLE, OFF(runDefaultOn), 20, 1, "Start the game in run mode."},
    {"Short Trail", K_TOGGLE, OFF(shortTrail), -1, 0, "Shortens the winding trail at the start."},
};
static const int NROWS = sizeof g_rows / sizeof g_rows[0];

enum { ID_FIRST = 1000, ID_SAVE = 2001, ID_DEFAULTS = 2002, ID_CLOSE = 2003, ID_X = 2004 };

// The settings live next to the plugin: <game>\plugins\PlayableRedMod\PlayableRed.ini.  This tool
// sits in the game folder (or, if someone moves it, in that settings folder itself).
static void ResolveIniPath()
{
    char self[MAX_PATH];
    GetModuleFileNameA(NULL, self, MAX_PATH);
    char* slash = strrchr(self, '\\');
    if (slash) slash[1] = 0;
    char here[MAX_PATH];
    sprintf(here, "%sPlayableRed.ini", self);
    if (GetFileAttributesA(here) != INVALID_FILE_ATTRIBUTES) { strcpy(g_ini, here); return; }
    char plugins[MAX_PATH], dir[MAX_PATH];
    sprintf(plugins, "%splugins", self);
    CreateDirectoryA(plugins, NULL);
    sprintf(dir, "%s\\PlayableRedMod", plugins);
    CreateDirectoryA(dir, NULL);
    sprintf(g_ini, "%s\\PlayableRed.ini", dir);
}

static void UpdateEnabled()
{
    for (int i = 0; i < NROWS; ++i) {
        bool en = g_rows[i].parent < 0 || g_rows[g_rows[i].parent].on;
        EnableWindow(g_rows[i].ctl, en);
        InvalidateRect(g_rows[i].ctl, NULL, TRUE);
    }
}

static void ToControls(const Config& c)
{
    for (int i = 0; i < NROWS; ++i) {
        const char* p = (const char*)&c + g_rows[i].off;
        char buf[64];
        switch (g_rows[i].kind) {
        case K_TOGGLE: g_rows[i].on = *(const bool*)p; break;
        case K_FLOAT: sprintf(buf, "%g", *(const float*)p); SetWindowTextA(g_rows[i].ctl, buf); break;
        case K_INT: sprintf(buf, "%d", *(const int*)p); SetWindowTextA(g_rows[i].ctl, buf); break;
        case K_HEX: sprintf(buf, "0x%X", *(const int*)p); SetWindowTextA(g_rows[i].ctl, buf); break;
        }
    }
    UpdateEnabled();
}

static bool FromControls(Config& c, char* err, size_t errn)
{
    for (int i = 0; i < NROWS; ++i) {
        char* p = (char*)&c + g_rows[i].off;
        char buf[64] = {0};
        if (g_rows[i].kind == K_TOGGLE) { *(bool*)p = g_rows[i].on; continue; }
        GetWindowTextA(g_rows[i].ctl, buf, sizeof buf);
        char* end = NULL;
        if (g_rows[i].kind == K_FLOAT) {
            float v = (float)strtod(buf, &end);
            if (end == buf || *end) { snprintf(err, errn, "'%s' is not a number for: %s", buf, g_rows[i].label); return false; }
            *(float*)p = v;
        } else {
            long v = strtol(buf, &end, 0);
            if (end == buf || *end) { snprintf(err, errn, "'%s' is not a number for: %s", buf, g_rows[i].label); return false; }
            *(int*)p = (int)v;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// the header
// ---------------------------------------------------------------------------
static unsigned g_seed = 20260907;
static int Rnd(int lo, int hi) { g_seed = g_seed * 1103515245u + 12345u; return lo + (int)((g_seed >> 16) % (unsigned)(hi - lo + 1)); }

static void PaintHeader(HDC hdc)
{
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);
    SolidBrush bg(Color(255, 8, 10, 9));
    g.FillRectangle(&bg, 0, 0, W, HEADER_H);
    // smoke: soft blobs of teal and blue, a few warm ones, faded at the edges
    g_seed = 20260907;
    for (int i = 0; i < 26; ++i) {
        int cx = Rnd(40, W - 40), cy = Rnd(10, HEADER_H - 10), rx = Rnd(60, 190), ry = Rnd(22, 60);
        GraphicsPath path;
        path.AddEllipse(cx - rx, cy - ry, 2 * rx, 2 * ry);
        PathGradientBrush pgb(&path);
        int t = Rnd(0, 9);
        Color centre = t < 6 ? Color(150, 70, 140, 150) : (t < 8 ? Color(140, 60, 100, 170) : Color(120, 150, 110, 70));
        Color edge(0, 0, 0, 0);
        pgb.SetCenterColor(centre);
        int one = 1;
        pgb.SetSurroundColors(&edge, &one);
        g.FillPath(&pgb, &path);
    }
    // vignette to the dark
    LinearGradientBrush fade(Point(0, 0), Point(0, HEADER_H), Color(0, 0, 0, 0), Color(200, 8, 10, 9));
    g.FillRectangle(&fade, 0, HEADER_H / 2, W, HEADER_H / 2);
    // the title, in a heavy serif, with a dim glow behind
    const WCHAR* families[] = {L"Cambria", L"Georgia", L"Times New Roman"};
    FontFamily* fam = NULL;
    for (int i = 0; i < 3 && !fam; ++i) { FontFamily* f = new FontFamily(families[i]); if (f->IsAvailable()) fam = f; else delete f; }
    if (!fam) fam = new FontFamily(L"Arial");
    Font title(fam, 60.0f, FontStyleBold, UnitPixel);
    RectF box(0, 18, (REAL)W, 80);
    StringFormat fmt; fmt.SetAlignment(StringAlignmentCenter);
    SolidBrush glow(Color(70, 120, 210, 220));
    for (int dx = -2; dx <= 2; dx += 2) for (int dy = -2; dy <= 2; dy += 2) { RectF b2 = box; b2.Offset((REAL)dx, (REAL)dy); g.DrawString(L"SILENT HILL 2", -1, &title, b2, &fmt, &glow); }
    SolidBrush white(Color(255, 236, 240, 238));
    g.DrawString(L"SILENT HILL 2", -1, &title, box, &fmt, &white);
    // erosion: dark holes bitten out of the letters, and drips running down from the baseline
    RectF measured; g.MeasureString(L"SILENT HILL 2", -1, &title, box, &fmt, &measured);
    SolidBrush hole(Color(255, 8, 10, 9));
    g_seed = 99;
    for (int i = 0; i < 140; ++i) {
        int x = Rnd((int)measured.X + 4, (int)(measured.X + measured.Width) - 4), y = Rnd((int)measured.Y + 12, (int)(measured.Y + measured.Height) - 8);
        int r = Rnd(1, 4);
        g.FillEllipse(&hole, x - r, y - r / 2, 2 * r, r);
    }
    Pen drip(Color(255, 236, 240, 238), 1.5f);
    int base = (int)(measured.Y + measured.Height) - 14;
    for (int i = 0; i < 16; ++i) {
        int x = Rnd((int)measured.X + 8, (int)(measured.X + measured.Width) - 8), len = Rnd(6, 34);
        g.DrawLine(&drip, x, base, x, base + len);
        g.FillEllipse(&white, x - 2, base + len - 2, 4, 4);
    }
    // the subtitle, letter-spaced
    Font* sf = new Font(L"Bahnschrift", 22.0f, FontStyleRegular, UnitPixel);
    if (!sf->IsAvailable()) { delete sf; sf = new Font(fam, 20.0f, FontStyleRegular, UnitPixel); }
    const WCHAR* text = L"PYRAMID HEAD";
    REAL spacing = 9.0f, total = 0;
    for (const WCHAR* p = text; *p; ++p) { WCHAR s[2] = {*p, 0}; RectF m; g.MeasureString(s, 1, sf, PointF(0, 0), &m); total += m.Width - 4 + spacing; }
    REAL x = (W - total) / 2, y = (REAL)(base + 38);
    for (const WCHAR* p = text; *p; ++p) {
        WCHAR s[2] = {*p, 0}; RectF m; g.MeasureString(s, 1, sf, PointF(0, 0), &m);
        g.DrawString(s, 1, sf, PointF(x, y), &white);
        x += m.Width - 4 + spacing;
    }
    delete sf;
    delete fam;
    // the frame
    Pen border(Color(255, GetRValue(C_BORDER), GetGValue(C_BORDER), GetBValue(C_BORDER)), 1.0f);
    g.DrawLine(&border, 0, HEADER_H - 1, W, HEADER_H - 1);
}

// ---------------------------------------------------------------------------
// drawing the buttons
// ---------------------------------------------------------------------------
static void DrawButton(const DRAWITEMSTRUCT* d)
{
    RECT r = d->rcItem;
    int id = (int)d->CtlID;
    bool isRow = id >= ID_FIRST && id < ID_FIRST + NROWS;
    bool on = isRow && g_rows[id - ID_FIRST].on;
    bool enabled = IsWindowEnabled(d->hwndItem) != FALSE;
    bool pressed = (d->itemState & ODS_SELECTED) != 0;
    COLORREF fill = !enabled ? C_BG : (on ? (pressed ? RGB(28, 84, 40) : C_ON) : (pressed ? RGB(40, 48, 42) : C_OFF));
    COLORREF text = !enabled ? RGB(40, 60, 44) : (on ? C_TEXT : C_DIM);
    HBRUSH b = CreateSolidBrush(fill);
    FillRect(d->hDC, &r, b);
    DeleteObject(b);
    HPEN pen = CreatePen(PS_SOLID, 1, !enabled ? C_OFF : (on ? C_TEXT : C_BORDER));
    HGDIOBJ oldPen = SelectObject(d->hDC, pen), oldBrush = SelectObject(d->hDC, GetStockObject(NULL_BRUSH));
    Rectangle(d->hDC, r.left, r.top, r.right, r.bottom);
    SelectObject(d->hDC, oldPen); SelectObject(d->hDC, oldBrush); DeleteObject(pen);
    char label[64]; GetWindowTextA(d->hwndItem, label, sizeof label);
    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, text);
    SelectObject(d->hDC, on || !isRow ? g_fontBold : g_font);
    RECT tr = r; tr.left += 10;
    DrawTextA(d->hDC, label, -1, &tr, (isRow ? DT_LEFT : DT_CENTER) | DT_VCENTER | DT_SINGLELINE);
    if (isRow && enabled) {
        // a small lamp at the right edge
        RECT lamp = {r.right - 18, (r.top + r.bottom) / 2 - 4, r.right - 10, (r.top + r.bottom) / 2 + 4};
        HBRUSH lb = CreateSolidBrush(on ? C_TEXT : C_DIM);
        FillRect(d->hDC, &lamp, lb);
        DeleteObject(lb);
    }
}

static void AddTip(HWND ctl, const char* text)
{
    if (!text || !*text) return;
    TOOLINFOA ti = {sizeof ti};
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = g_win;
    ti.uId = (UINT_PTR)ctl;
    ti.lpszText = (LPSTR)text;
    SendMessageA(g_tip, TTM_ADDTOOLA, 0, (LPARAM)&ti);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        FillRect(hdc, &rc, g_bgBrush);
        PaintHeader(hdc);
        HPEN pen = CreatePen(PS_SOLID, 1, C_BORDER);
        HGDIOBJ op = SelectObject(hdc, pen), ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, 0, 0, rc.right, rc.bottom);
        SelectObject(hdc, op); SelectObject(hdc, ob); DeleteObject(pen);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_NCHITTEST: {
        POINT pt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ScreenToClient(h, &pt);
        if (pt.y < HEADER_H && pt.y >= 0 && pt.x < W - 40) return HTCAPTION;    // drag by the header
        return DefWindowProcA(h, m, w, l);
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetTextColor((HDC)w, C_TEXT);
        SetBkColor((HDC)w, C_BG);
        return (LRESULT)g_bgBrush;
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)w, C_TEXT);
        SetBkColor((HDC)w, C_EDIT);
        return (LRESULT)g_editBrush;
    case WM_DRAWITEM:
        DrawButton((const DRAWITEMSTRUCT*)l);
        return TRUE;
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id >= ID_FIRST && id < ID_FIRST + NROWS && g_rows[id - ID_FIRST].kind == K_TOGGLE) {
            g_rows[id - ID_FIRST].on = !g_rows[id - ID_FIRST].on;
            UpdateEnabled();
        } else if (id == ID_SAVE) {
            Config c; ConfigDefaults(&c);
            char err[256];
            if (!FromControls(c, err, sizeof err)) { SetWindowTextA(g_status, err); MessageBeep(MB_ICONWARNING); break; }
            if (SaveConfig(g_ini, &c)) SetWindowTextA(g_status, "Saved. A running game picks it up within a second.");
            else SetWindowTextA(g_status, "Could not write the .ini (is the folder writable?)");
        } else if (id == ID_DEFAULTS) {
            Config c; ConfigDefaults(&c); ToControls(c);
            SetWindowTextA(g_status, "Defaults shown -- press Save to keep them.");
        } else if (id == ID_CLOSE || id == ID_X) {
            DestroyWindow(h);
        }
        break;
    }
    case WM_DESTROY: PostQuitMessage(0); break;
    default: return DefWindowProcA(h, m, w, l);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int)
{
    INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_STANDARD_CLASSES | ICC_BAR_CLASSES}; InitCommonControlsEx(&icc);
    GdiplusStartupInput gsi; ULONG_PTR gtoken; GdiplusStartup(&gtoken, &gsi, NULL);
    ResolveIniPath();
    g_bgBrush = CreateSolidBrush(C_BG);
    g_editBrush = CreateSolidBrush(C_EDIT);
    g_font = CreateFontA(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Consolas");
    g_fontBold = CreateFontA(-14, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Consolas");

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc; wc.hInstance = inst; wc.lpszClassName = "PlayableRedConfig";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = NULL;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassA(&wc);

    int H = HEADER_H + 14 + ROWH * NROWS + 92;
    g_win = CreateWindowExA(WS_EX_APPWINDOW, "PlayableRedConfig", "Playable Pyramid Head",
                            WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, W, H, NULL, NULL, inst, NULL);
    // centre on the primary screen
    RECT wa; SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    SetWindowPos(g_win, NULL, (wa.right - wa.left - W) / 2, (wa.bottom - wa.top - H) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    g_tip = CreateWindowExA(WS_EX_TOPMOST, TOOLTIPS_CLASSA, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0, 0, g_win, NULL, inst, NULL);
    SetWindowTheme(g_tip, L"", L"");
    SendMessageA(g_tip, TTM_SETTIPBKCOLOR, (WPARAM)C_EDIT, 0);
    SendMessageA(g_tip, TTM_SETTIPTEXTCOLOR, (WPARAM)C_TEXT, 0);
    SendMessageA(g_tip, TTM_SETMAXTIPWIDTH, 0, 420);
    SendMessageA(g_tip, TTM_SETDELAYTIME, TTDT_INITIAL, 350);
    SendMessageA(g_tip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 12000);

    HWND x = CreateWindowExA(0, "BUTTON", "X", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, W - 34, 8, 26, 22, g_win, (HMENU)ID_X, inst, NULL);
    SendMessageA(x, WM_SETFONT, (WPARAM)g_fontBold, TRUE);
    AddTip(x, "Close");

    int y = HEADER_H + 14;
    for (int i = 0; i < NROWS; ++i) {
        Row& r = g_rows[i];
        int ind = LEFT + 22 * r.indent;
        if (r.kind == K_TOGGLE) {
            r.ctl = CreateWindowExA(0, "BUTTON", r.label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, ind, y, LABELW + EDITW + 22 - 22 * r.indent, ROWH - 4, g_win, (HMENU)(ID_FIRST + i), inst, NULL);
            AddTip(r.ctl, r.tip);
        } else {
            HWND lab = CreateWindowExA(0, "STATIC", r.label, WS_CHILD | WS_VISIBLE | SS_NOTIFY, ind + 10, y + 4, LABELW - 10 - 22 * r.indent, ROWH - 6, g_win, NULL, inst, NULL);
            SendMessageA(lab, WM_SETFONT, (WPARAM)g_font, TRUE);
            AddTip(lab, r.tip);
            r.ctl = CreateWindowExA(0, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, LEFT + 22 + LABELW, y, EDITW, ROWH - 4, g_win, (HMENU)(ID_FIRST + i), inst, NULL);
            SetWindowTheme(r.ctl, L"", L"");
            AddTip(r.ctl, r.tip);
        }
        SendMessageA(r.ctl, WM_SETFONT, (WPARAM)g_font, TRUE);
        y += ROWH;
    }
    y += 12;
    HWND b1 = CreateWindowExA(0, "BUTTON", "Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, LEFT, y, 110, 30, g_win, (HMENU)ID_SAVE, inst, NULL);
    HWND b2 = CreateWindowExA(0, "BUTTON", "Defaults", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, LEFT + 120, y, 110, 30, g_win, (HMENU)ID_DEFAULTS, inst, NULL);
    HWND b3 = CreateWindowExA(0, "BUTTON", "Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, LEFT + 240, y, 110, 30, g_win, (HMENU)ID_CLOSE, inst, NULL);
    AddTip(b1, "Write PlayableRed.ini. A running game applies it within a second.");
    AddTip(b2, "Show the defaults (not saved until you press Save).");
    y += 40;
    g_status = CreateWindowExA(0, "STATIC", g_ini, WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS, LEFT, y, W - 2 * LEFT, 22, g_win, NULL, inst, NULL);
    HWND fonted[4] = {b1, b2, b3, g_status};
    for (int i = 0; i < 4; ++i) SendMessageA(fonted[i], WM_SETFONT, (WPARAM)g_font, TRUE);

    Config c;
    bool had = LoadConfig(g_ini, &c);
    ToControls(c);
    if (!had) SetWindowTextA(g_status, "No PlayableRed.ini yet: these are the defaults; Save writes the file.");
    ShowWindow(g_win, SW_SHOW);
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) { if (!IsDialogMessageA(g_win, &msg)) { TranslateMessage(&msg); DispatchMessageA(&msg); } }
    GdiplusShutdown(gtoken);
    return 0;
}
