// Ventana de ajustes - HECHO POR @BYALVARO__
#include "common.h"
#include <commctrl.h>
#include <commdlg.h>
#include <cstdio>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

enum {
    IDC_VOLSTEP = 2001, IDC_VOLSTEP_L,
    IDC_CLICKS, IDC_CLICKS_L,
    IDC_HOLD, IDC_HOLD_L,
    IDC_PRESSMODE, IDC_PRESSKEY,
    IDC_INVERT,
    IDC_LIST, IDC_UP, IDC_DOWN,
    IDC_SIZE, IDC_SIZE_L,
    IDC_OPACITY, IDC_OPACITY_L,
    IDC_AUTOCLOSE, IDC_AUTOCLOSE_L,
    IDC_COLOR, IDC_SWATCH,
    IDC_CLOSEACTION,
    IDC_OSD, IDC_SOUNDS, IDC_STARTUP,
    IDC_TEST, IDC_CLOSE, IDC_CREDIT
};

static HWND   s_wnd = nullptr;
static HFONT  s_font = nullptr, s_fontTitle = nullptr, s_fontBold = nullptr;
static HBRUSH s_swatch = nullptr;
static UINT   s_dpi = 96;
static bool   s_populating = false;

static int S(int v) { return MulDiv(v, (int)s_dpi, 96); }

static HWND Ctl(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, DWORD ex = 0) {
    return CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h),
                           s_wnd, (HMENU)(INT_PTR)id, g_hInst, nullptr);
}

static HWND Track(int id, int x, int y, int w, int lo, int hi, int val, int page) {
    HWND t = Ctl(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, x, y, w, 28, id);
    SendMessageW(t, TBM_SETRANGE, TRUE, MAKELPARAM(lo, hi));
    SendMessageW(t, TBM_SETPAGESIZE, 0, page);
    SendMessageW(t, TBM_SETPOS, TRUE, val);
    return t;
}

static int TrackPos(int id) { return (int)SendDlgItemMessageW(s_wnd, id, TBM_GETPOS, 0, 0); }

static void SetLabel(int id, const wchar_t* fmt, int v) {
    wchar_t b[128];
    swprintf(b, 128, fmt, v);
    SetDlgItemTextW(s_wnd, id, b);
}

static void UpdateLabels() {
    SetLabel(IDC_VOLSTEP_L,   L"Volumen por muesca: %d %%", g_cfg.volStep);
    SetLabel(IDC_CLICKS_L,    L"Muescas para cambiar de opción: %d", g_cfg.menuClicks);
    SetLabel(IDC_HOLD_L,      L"Tiempo para «mantener»: %d ms", g_cfg.holdMs);
    SetLabel(IDC_SIZE_L,      L"Tamaño del menú: %d px", g_cfg.size);
    SetLabel(IDC_OPACITY_L,   L"Opacidad: %d %%", g_cfg.opacity * 100 / 255);
    SetLabel(IDC_AUTOCLOSE_L, L"Se cierra solo tras: %d s", g_cfg.autoClose);
}

static void FillList(int select) {
    HWND lv = GetDlgItem(s_wnd, IDC_LIST);
    s_populating = true;
    ListView_DeleteAllItems(lv);
    for (int i = 0; i < IT_COUNT; i++) {
        LVITEMW it = {};
        it.mask = LVIF_TEXT;
        it.iItem = i;
        it.pszText = (LPWSTR)Item_Name(g_cfg.order[i]);
        ListView_InsertItem(lv, &it);
        ListView_SetCheckState(lv, i, g_cfg.enabled[g_cfg.order[i]] ? TRUE : FALSE);
    }
    if (select >= 0) {
        ListView_SetItemState(lv, select, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(lv, select, FALSE);
    }
    s_populating = false;
}

static void MoveItem(int delta) {
    HWND lv = GetDlgItem(s_wnd, IDC_LIST);
    int i = ListView_GetNextItem(lv, -1, LVNI_SELECTED);
    int j = i + delta;
    if (i < 0 || j < 0 || j >= IT_COUNT) return;
    std::swap(g_cfg.order[i], g_cfg.order[j]);
    FillList(j);
    Config_Save();
}

static void Build() {
    // cabecera
    HWND t = Ctl(L"STATIC", L"Rueda Suprema", SS_LEFT, 20, 12, 400, 32, 0);
    SendMessageW(t, WM_SETFONT, (WPARAM)s_fontTitle, TRUE);
    Ctl(L"STATIC", L"Menú radial para la rueda de volumen del AJAZZ AK820", SS_LEFT, 22, 46, 520, 20, 0);

    // ---- Rueda
    Ctl(L"BUTTON", L"Rueda", BS_GROUPBOX, 12, 74, 576, 214, 0);
    Ctl(L"STATIC", L"", SS_LEFT, 28, 100, 240, 20, IDC_VOLSTEP_L);
    Track(IDC_VOLSTEP, 280, 96, 290, 1, 10, g_cfg.volStep, 1);
    Ctl(L"STATIC", L"", SS_LEFT, 28, 132, 240, 20, IDC_CLICKS_L);
    Track(IDC_CLICKS, 280, 128, 290, 1, 5, g_cfg.menuClicks, 1);
    Ctl(L"STATIC", L"", SS_LEFT, 28, 164, 240, 20, IDC_HOLD_L);
    Track(IDC_HOLD, 280, 160, 290, 5, 20, g_cfg.holdMs / 50, 1);

    Ctl(L"STATIC", L"Botón de la rueda:", SS_LEFT, 28, 199, 170, 20, 0);
    HWND cb = Ctl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 200, 195, 370, 200, IDC_PRESSMODE);
    SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"Clic: abrir/confirmar  ·  Mantener: silenciar");
    SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"Doble clic: abrir menú  ·  Clic: silenciar");
    SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)L"Mantener: abrir menú  ·  Clic: silenciar");
    SendMessageW(cb, CB_SETCURSEL, g_cfg.pressMode, 0);

    Ctl(L"STATIC", L"Al pulsar, la rueda envía:", SS_LEFT, 28, 231, 170, 20, 0);
    HWND ck = Ctl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 200, 227, 370, 200, IDC_PRESSKEY);
    SendMessageW(ck, CB_ADDSTRING, 0, (LPARAM)L"Silenciar (Mute) - lo normal en el AK820");
    SendMessageW(ck, CB_ADDSTRING, 0, (LPARAM)L"Play / Pausa");
    SendMessageW(ck, CB_SETCURSEL, g_cfg.pressKey, 0);

    HWND inv = Ctl(L"BUTTON", L"Invertir sentido de giro", BS_AUTOCHECKBOX | WS_TABSTOP, 28, 260, 300, 22, IDC_INVERT);
    SendMessageW(inv, BM_SETCHECK, g_cfg.invert ? BST_CHECKED : BST_UNCHECKED, 0);

    // ---- Menú radial
    Ctl(L"BUTTON", L"Menú radial", BS_GROUPBOX, 12, 298, 576, 268, 0);
    Ctl(L"STATIC", L"Opciones (marca las que quieres y ordénalas):", SS_LEFT, 28, 320, 340, 20, 0);
    HWND lv = Ctl(WC_LISTVIEWW, L"", LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER | WS_TABSTOP,
                  28, 342, 240, 186, IDC_LIST);
    ListView_SetExtendedListViewStyle(lv, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    LVCOLUMNW col = {};
    col.mask = LVCF_WIDTH;
    col.cx = S(214);
    ListView_InsertColumn(lv, 0, &col);
    FillList(0);
    Ctl(L"BUTTON", L"▲ Subir", BS_PUSHBUTTON | WS_TABSTOP, 276, 342, 90, 28, IDC_UP);
    Ctl(L"BUTTON", L"▼ Bajar", BS_PUSHBUTTON | WS_TABSTOP, 276, 376, 90, 28, IDC_DOWN);

    Ctl(L"STATIC", L"", SS_LEFT, 384, 322, 196, 20, IDC_SIZE_L);
    Track(IDC_SIZE, 380, 342, 200, 13, 28, g_cfg.size / 20, 1);
    Ctl(L"STATIC", L"", SS_LEFT, 384, 376, 196, 20, IDC_OPACITY_L);
    Track(IDC_OPACITY, 380, 396, 200, 120, 255, g_cfg.opacity, 10);
    Ctl(L"STATIC", L"", SS_LEFT, 384, 430, 196, 20, IDC_AUTOCLOSE_L);
    Track(IDC_AUTOCLOSE, 380, 450, 200, 2, 20, g_cfg.autoClose, 1);
    Ctl(L"BUTTON", L"Color de acento…", BS_PUSHBUTTON | WS_TABSTOP, 384, 490, 140, 30, IDC_COLOR);
    Ctl(L"STATIC", L"", SS_NOTIFY | WS_BORDER, 532, 490, 44, 30, IDC_SWATCH);

    HWND ca = Ctl(L"BUTTON", L"Cerrar el menú tras Play / Siguiente / Anterior", BS_AUTOCHECKBOX | WS_TABSTOP, 28, 536, 400, 22, IDC_CLOSEACTION);
    SendMessageW(ca, BM_SETCHECK, g_cfg.closeAfterAction ? BST_CHECKED : BST_UNCHECKED, 0);

    // ---- General
    Ctl(L"BUTTON", L"General", BS_GROUPBOX, 12, 576, 576, 58, 0);
    HWND o = Ctl(L"BUTTON", L"OSD de volumen propio", BS_AUTOCHECKBOX | WS_TABSTOP, 28, 600, 180, 22, IDC_OSD);
    SendMessageW(o, BM_SETCHECK, g_cfg.osd ? BST_CHECKED : BST_UNCHECKED, 0);
    HWND sn = Ctl(L"BUTTON", L"Sonidos al girar", BS_AUTOCHECKBOX | WS_TABSTOP, 220, 600, 150, 22, IDC_SOUNDS);
    SendMessageW(sn, BM_SETCHECK, g_cfg.sounds ? BST_CHECKED : BST_UNCHECKED, 0);
    HWND st = Ctl(L"BUTTON", L"Iniciar con Windows", BS_AUTOCHECKBOX | WS_TABSTOP, 390, 600, 180, 22, IDC_STARTUP);
    SendMessageW(st, BM_SETCHECK, g_cfg.startup ? BST_CHECKED : BST_UNCHECKED, 0);

    // ---- Pie
    Ctl(L"BUTTON", L"Probar menú", BS_PUSHBUTTON | WS_TABSTOP, 12, 648, 120, 32, IDC_TEST);
    HWND cr = Ctl(L"STATIC", APP_CREDIT, SS_CENTER | SS_CENTERIMAGE, 150, 648, 300, 32, IDC_CREDIT);
    Ctl(L"BUTTON", L"Cerrar", BS_DEFPUSHBUTTON | WS_TABSTOP, 488, 648, 100, 32, IDC_CLOSE);

    EnumChildWindows(s_wnd, [](HWND c, LPARAM) -> BOOL {
        if ((HFONT)SendMessageW(c, WM_GETFONT, 0, 0) == nullptr) SendMessageW(c, WM_SETFONT, (WPARAM)s_font, TRUE);
        return TRUE;
    }, 0);
    SendMessageW(t, WM_SETFONT, (WPARAM)s_fontTitle, TRUE);
    SendMessageW(cr, WM_SETFONT, (WPARAM)s_fontBold, TRUE);
    UpdateLabels();
}

static bool Checked(int id) { return IsDlgButtonChecked(s_wnd, id) == BST_CHECKED; }

static LRESULT CALLBACK SettingsProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_HSCROLL: {
        int id = GetDlgCtrlID((HWND)l);
        switch (id) {
        case IDC_VOLSTEP:   g_cfg.volStep = TrackPos(id); break;
        case IDC_CLICKS:    g_cfg.menuClicks = TrackPos(id); break;
        case IDC_HOLD:      g_cfg.holdMs = TrackPos(id) * 50; break;
        case IDC_SIZE:      g_cfg.size = TrackPos(id) * 20; break;
        case IDC_OPACITY:   g_cfg.opacity = TrackPos(id); break;
        case IDC_AUTOCLOSE: g_cfg.autoClose = TrackPos(id); break;
        default: return 0;
        }
        UpdateLabels();
        if (LOWORD(w) == TB_ENDTRACK || LOWORD(w) == TB_THUMBPOSITION) Config_Save();
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        switch (id) {
        case IDC_PRESSMODE:
            if (HIWORD(w) == CBN_SELCHANGE) { g_cfg.pressMode = (int)SendDlgItemMessageW(h, id, CB_GETCURSEL, 0, 0); Config_Save(); }
            break;
        case IDC_PRESSKEY:
            if (HIWORD(w) == CBN_SELCHANGE) { g_cfg.pressKey = (int)SendDlgItemMessageW(h, id, CB_GETCURSEL, 0, 0); Config_Save(); }
            break;
        case IDC_INVERT:      g_cfg.invert = Checked(id); Config_Save(); break;
        case IDC_CLOSEACTION: g_cfg.closeAfterAction = Checked(id); Config_Save(); break;
        case IDC_OSD:         g_cfg.osd = Checked(id); Config_Save(); break;
        case IDC_SOUNDS:      g_cfg.sounds = Checked(id); Config_Save(); if (g_cfg.sounds) Sound_Confirm(); break;
        case IDC_STARTUP:     g_cfg.startup = Checked(id); Startup_Apply(g_cfg.startup); Config_Save(); break;
        case IDC_UP:          MoveItem(-1); break;
        case IDC_DOWN:        MoveItem(+1); break;
        case IDC_COLOR:
        case IDC_SWATCH: {
            if (id == IDC_SWATCH && HIWORD(w) != STN_CLICKED) break;
            static COLORREF custom[16] = {
                RGB(0,170,255), RGB(124,92,255), RGB(255,64,129), RGB(255,145,0),
                RGB(0,200,120), RGB(255,214,0), RGB(0,229,255), RGB(240,240,240) };
            CHOOSECOLORW cc = { sizeof(cc) };
            cc.hwndOwner = h;
            cc.rgbResult = g_cfg.accent;
            cc.lpCustColors = custom;
            cc.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorW(&cc)) {
                g_cfg.accent = cc.rgbResult;
                if (s_swatch) DeleteObject(s_swatch);
                s_swatch = CreateSolidBrush(g_cfg.accent);
                InvalidateRect(GetDlgItem(h, IDC_SWATCH), nullptr, TRUE);
                Config_Save();
            }
            break;
        }
        case IDC_TEST:
            Radial_Open();
            break;
        case IDC_CLOSE:
        case IDCANCEL:
            DestroyWindow(h);
            break;
        }
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR* hdr = (NMHDR*)l;
        if (hdr->idFrom == IDC_LIST && hdr->code == LVN_ITEMCHANGED && !s_populating) {
            NMLISTVIEW* nm = (NMLISTVIEW*)l;
            if ((nm->uChanged & LVIF_STATE) && ((nm->uNewState ^ nm->uOldState) & LVIS_STATEIMAGEMASK) &&
                nm->iItem >= 0 && nm->iItem < IT_COUNT) {
                int itemId = g_cfg.order[nm->iItem];
                bool on = ListView_GetCheckState(hdr->hwndFrom, nm->iItem) != FALSE;
                g_cfg.enabled[itemId] = on;
                bool any = false;
                for (int i = 0; i < IT_COUNT; i++) any |= g_cfg.enabled[i];
                if (!any) {   // al menos una opción
                    g_cfg.enabled[itemId] = true;
                    s_populating = true;
                    ListView_SetCheckState(hdr->hwndFrom, nm->iItem, TRUE);
                    s_populating = false;
                }
                Config_Save();
            }
        }
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HWND c = (HWND)l;
        HDC dc = (HDC)w;
        if (GetDlgCtrlID(c) == IDC_SWATCH) {
            if (!s_swatch) s_swatch = CreateSolidBrush(g_cfg.accent);
            return (LRESULT)s_swatch;
        }
        SetBkMode(dc, TRANSPARENT);
        if (GetDlgCtrlID(c) == IDC_CREDIT) SetTextColor(dc, g_cfg.accent);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        Config_Save();
        s_wnd = nullptr;
        if (s_font) { DeleteObject(s_font); s_font = nullptr; }
        if (s_fontTitle) { DeleteObject(s_fontTitle); s_fontTitle = nullptr; }
        if (s_fontBold) { DeleteObject(s_fontBold); s_fontBold = nullptr; }
        if (s_swatch) { DeleteObject(s_swatch); s_swatch = nullptr; }
        TrimMemory();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void Settings_Show() {
    if (s_wnd) {
        ShowWindow(s_wnd, SW_RESTORE);
        SetForegroundWindow(s_wnd);
        return;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = SettingsProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        wc.lpszClassName = SETTINGS_CLASS;
        wc.hIcon = g_iconBig;
        wc.hIconSm = g_iconSmall;
        RegisterClassExW(&wc);
        registered = true;
    }
    s_dpi = GetDpiForSystem();
    s_font = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    s_fontBold = CreateFontW(-S(14), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    s_fontTitle = CreateFontW(-S(24), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc = { 0, 0, S(600), S(694) };
    AdjustWindowRectEx(&rc, style, FALSE, 0);
    int w = rc.right - rc.left, hgt = rc.bottom - rc.top;
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    s_wnd = CreateWindowExW(0, SETTINGS_CLASS, L"Rueda Suprema · Ajustes", style,
                            (wa.left + wa.right - w) / 2, (wa.top + wa.bottom - hgt) / 2, w, hgt,
                            nullptr, nullptr, g_hInst, nullptr);
    if (!s_wnd) return;
    Build();
    ShowWindow(s_wnd, SW_SHOW);
    SetForegroundWindow(s_wnd);
}
