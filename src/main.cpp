// Rueda Suprema - arranque, hook de la rueda, bandeja y sonidos - HECHO POR @BYALVARO__
#include "common.h"
#include <shellapi.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "advapi32.lib")

HINSTANCE g_hInst = nullptr;
HWND      g_hMain = nullptr;
HICON     g_iconBig = nullptr;
HICON     g_iconSmall = nullptr;
bool      g_paused = false;

static HHOOK s_hook = nullptr;
static UINT  s_taskbarCreated = 0;

#define RS_TAG ((ULONG_PTR)0x52554544)

#define TMR_HOLD 100
#define TMR_DBL  101

#define ID_TRAY_OPEN     1001
#define ID_TRAY_SETTINGS 1002
#define ID_TRAY_PAUSE    1003
#define ID_TRAY_EXIT     1004
#define ID_TRAY_LOG      1005
#define ID_TRAY_MIC      1006

// ------------------------------------------------------------------ sonidos (generados en memoria)
static std::vector<BYTE> s_wavTick, s_wavOk;

static std::vector<BYTE> MakeWav(const float* freqs, const float* msArr, int count, float amp) {
    const int rate = 44100;
    std::vector<short> pcm;
    for (int t = 0; t < count; t++) {
        int n = (int)(rate * msArr[t] / 1000.f);
        for (int i = 0; i < n; i++) {
            float x = (float)i / rate;
            float env = expf(-x * 1000.f / (msArr[t] * 0.35f));
            float attack = std::min(1.f, i / (rate * 0.002f));
            float s = sinf(2 * 3.14159265f * freqs[t] * x) * env * attack * amp;
            pcm.push_back((short)(s * 32767));
        }
    }
    DWORD dataBytes = (DWORD)(pcm.size() * 2);
    std::vector<BYTE> w(44 + dataBytes);
    auto put32 = [&](int off, DWORD v) { memcpy(&w[off], &v, 4); };
    auto put16 = [&](int off, WORD v) { memcpy(&w[off], &v, 2); };
    memcpy(&w[0], "RIFF", 4); put32(4, 36 + dataBytes); memcpy(&w[8], "WAVE", 4);
    memcpy(&w[12], "fmt ", 4); put32(16, 16); put16(20, 1); put16(22, 1);
    put32(24, rate); put32(28, rate * 2); put16(32, 2); put16(34, 16);
    memcpy(&w[36], "data", 4); put32(40, dataBytes);
    if (dataBytes) memcpy(&w[44], pcm.data(), dataBytes);
    return w;
}

static void InitSounds() {
    const float f1[] = { 2100.f }, m1[] = { 16.f };
    s_wavTick = MakeWav(f1, m1, 1, 0.16f);
    const float f2[] = { 1320.f, 1980.f }, m2[] = { 32.f, 55.f };
    s_wavOk = MakeWav(f2, m2, 2, 0.18f);
}

void Sound_Tick() {
    if (g_cfg.sounds && !s_wavTick.empty())
        PlaySoundW((LPCWSTR)s_wavTick.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
void Sound_Confirm() {
    if (g_cfg.sounds && !s_wavOk.empty())
        PlaySoundW((LPCWSTR)s_wavOk.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

// ------------------------------------------------------------------ utilidades
void Media_Key(WORD vk) {
    INPUT in[2] = {};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = vk;
    in[0].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
    in[0].ki.dwExtraInfo = RS_TAG;
    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

void TrimMemory() {
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

float StepVolume(float cur, int dir) {
    float s = g_cfg.volStep / 100.f;
    float v = roundf((cur + dir * s) / s) * s;   // se ajusta a múltiplos del paso
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    return v;
}

void ToggleMuteWithOsd() {
    if (g_cfg.muteTarget == 1) {        // silenciar el micrófono directamente
        bool m = !Audio_GetMicMute();
        Audio_SetMicMute(m);
        Sound_Confirm();
        Osd_Show(Audio_GetMic(), m, true);
        return;
    }
    bool m = !Audio_GetMute();
    Audio_SetMute(m);
    Osd_Show(Audio_GetMaster(), m);
}

// ------------------------------------------------------------------ registro de diagnóstico
// Solo guarda teclas multimedia/volumen (nunca lo que escribes) en %APPDATA%\RuedaSuprema\registro.txt
static int s_logLines = 0;

static std::wstring LogPath() {
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    std::wstring dir = (n > 0 && n < MAX_PATH) ? std::wstring(buf) : std::wstring(L".");
    dir += L"\\RuedaSuprema";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\registro.txt";
}

static void Log(const wchar_t* fmt, ...) {
    if (s_logLines > 300) return;
    s_logLines++;
    wchar_t line[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(line, 512, _TRUNCATE, fmt, ap);
    va_end(ap);
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t full[600];
    swprintf(full, 600, L"[%02d:%02d:%02d.%03d] %s\r\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, line);
    char utf8[1400];
    int len = WideCharToMultiByte(CP_UTF8, 0, full, -1, utf8, sizeof(utf8), nullptr, nullptr);
    if (len <= 1) return;
    HANDLE f = CreateFileW(LogPath().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD wr = 0;
    WriteFile(f, utf8, (DWORD)(len - 1), &wr, nullptr);
    CloseHandle(f);
}

static bool InterestingVk(DWORD vk) {
    return (vk >= 0xA6 && vk <= 0xB7) ||      // navegador, volumen y multimedia
           (vk >= 0x7C && vk <= 0x87) ||      // F13-F24
           vk == 0 || vk == 0xFF;
}

// ------------------------------------------------------------------ hook de teclado (la rueda)
static LRESULT CALLBACK KeyboardHook(int code, WPARAM w, LPARAM l) {
    if (code == HC_ACTION) {
        const KBDLLHOOKSTRUCT* k = (const KBDLLHOOKSTRUCT*)l;
        bool down = (w == WM_KEYDOWN || w == WM_SYSKEYDOWN);
        DWORD vk = k->vkCode;
        if (InterestingVk(vk))
            Log(L"HOOK  vk=0x%02X scan=0x%02X flags=0x%02X %s%s", vk, k->scanCode, k->flags,
                down ? L"pulsada" : L"soltada", g_paused ? L" (pausado)" : L"");
        // Ya NO se ignoran las teclas marcadas como "inyectadas": el AK820 puede mandarlas así.
        // Solo se ignoran las que manda este mismo programa.
        if (!g_paused && k->dwExtraInfo != RS_TAG) {
            DWORD pressVk = g_cfg.pressKey == 1 ? VK_MEDIA_PLAY_PAUSE : VK_VOLUME_MUTE;
            if (vk == VK_VOLUME_UP) {
                if (down) PostMessageW(g_hMain, WM_APP_KNOB, (WPARAM)1, 0);
                return 1;
            }
            if (vk == VK_VOLUME_DOWN) {
                if (down) PostMessageW(g_hMain, WM_APP_KNOB, (WPARAM)(INT_PTR)-1, 0);
                return 1;
            }
            if (vk == pressVk) {
                PostMessageW(g_hMain, WM_APP_PRESS, down ? 1 : 0, 0);
                return 1;
            }
            if (vk == VK_ESCAPE && g_menuOpen) {
                if (down) PostMessageW(g_hMain, WM_APP_ESC, 0, 0);
                return 1;
            }
        }
    }
    return CallNextHookEx(s_hook, code, w, l);
}

// Entrada "cruda" del control multimedia (solo para el registro de diagnóstico)
static void OnRawInput(LPARAM l) {
    UINT size = 0;
    GetRawInputData((HRAWINPUT)l, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
    if (size == 0 || size > 1024) return;
    alignas(8) BYTE buf[1024];
    if (GetRawInputData((HRAWINPUT)l, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) != size) return;
    RAWINPUT* ri = (RAWINPUT*)buf;
    if (ri->header.dwType != RIM_TYPEHID) return;
    DWORD cnt = ri->data.hid.dwCount, sz = ri->data.hid.dwSizeHid;
    for (DWORD i = 0; i < cnt && i < 4; i++) {
        std::wstring hex;
        for (DWORD b = 0; b < sz && b < 16; b++) {
            wchar_t h[4];
            swprintf(h, 4, L"%02X ", ri->data.hid.bRawData[i * sz + b]);
            hex += h;
        }
        Log(L"RAW   control multimedia: %s", hex.c_str());
    }
}

// ------------------------------------------------------------------ botón de la rueda
static bool s_down = false, s_holdFired = false, s_waitDbl = false;

static void OnClick() {
    if (Radial_IsOpen()) { Radial_Click(); return; }
    switch (g_cfg.pressMode) {
    case PM_CLICK_MENU:
        Radial_Open();
        break;
    case PM_DOUBLE_MENU:
        if (s_waitDbl) { KillTimer(g_hMain, TMR_DBL); s_waitDbl = false; Radial_Open(); }
        else { s_waitDbl = true; SetTimer(g_hMain, TMR_DBL, 320, nullptr); }
        break;
    case PM_HOLD_MENU:
        ToggleMuteWithOsd();
        break;
    }
}

static void OnHold() {
    if (Radial_IsOpen()) { Radial_Hold(); return; }
    if (g_cfg.pressMode == PM_HOLD_MENU) Radial_Open();
    else ToggleMuteWithOsd();
}

// ------------------------------------------------------------------ bandeja
static void TrayAdd(bool balloon) {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hMain;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = g_iconSmall;
    wcscpy_s(nid.szTip, L"Rueda Suprema · HECHO POR @BYALVARO__");
    if (balloon) {
        nid.uFlags |= NIF_INFO;
        wcscpy_s(nid.szInfoTitle, L"Rueda Suprema está activa");
        wcscpy_s(nid.szInfo, L"Pulsa la rueda del teclado para abrir el menú. Clic derecho en este icono para los ajustes.");
        nid.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
        nid.hBalloonIcon = g_iconBig;
    }
    Shell_NotifyIconW(NIM_ADD, &nid);
}

static void TrayRemove() {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hMain;
    nid.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

static void TrayMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_TRAY_OPEN, L"Abrir menú radial");
    AppendMenuW(m, MF_STRING, ID_TRAY_SETTINGS, L"Ajustes…");
    AppendMenuW(m, MF_STRING | (Audio_GetMicMute() ? MF_CHECKED : 0), ID_TRAY_MIC, L"Silenciar micrófono");
    AppendMenuW(m, MF_STRING | (g_paused ? MF_CHECKED : 0), ID_TRAY_PAUSE, L"Pausar (la rueda vuelve a ser normal)");
    AppendMenuW(m, MF_STRING, ID_TRAY_LOG, L"Ver registro de diagnóstico");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, APP_CREDIT);
    AppendMenuW(m, MF_STRING, ID_TRAY_EXIT, L"Salir");
    SetMenuDefaultItem(m, ID_TRAY_SETTINGS, FALSE);
    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(g_hMain);
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g_hMain, nullptr);
    PostMessageW(g_hMain, WM_NULL, 0, 0);
    DestroyMenu(m);
}

// ------------------------------------------------------------------ ventana principal (oculta)
static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == s_taskbarCreated && s_taskbarCreated) { TrayAdd(false); return 0; }
    switch (m) {
    case WM_APP_KNOB: {
        int dir = (int)(INT_PTR)w;
        if (g_cfg.invert) dir = -dir;
        if (Radial_IsOpen()) Radial_Rotate(dir);
        else {
            float v = StepVolume(Audio_GetMaster(), dir);
            Audio_SetMaster(v);
            bool mute = Audio_GetMute();
            if (dir > 0 && mute) { Audio_SetMute(false); mute = false; }
            Osd_Show(v, mute);
        }
        return 0;
    }
    case WM_APP_PRESS:
        if (w) {
            if (s_down) return 0;      // auto-repetición mientras se mantiene
            s_down = true;
            s_holdFired = false;
            SetTimer(h, TMR_HOLD, (UINT)g_cfg.holdMs, nullptr);
        } else {
            if (!s_down) return 0;
            s_down = false;
            KillTimer(h, TMR_HOLD);
            if (!s_holdFired) OnClick();
        }
        return 0;
    case WM_APP_ESC:
        Radial_Close(true);
        return 0;
    case WM_INPUT:
        OnRawInput(l);
        return DefWindowProcW(h, m, w, l);
    case WM_TIMER:
        if (w == TMR_HOLD) {
            KillTimer(h, TMR_HOLD);
            if (s_down) { s_holdFired = true; OnHold(); }
        } else if (w == TMR_DBL) {
            KillTimer(h, TMR_DBL);
            s_waitDbl = false;
            ToggleMuteWithOsd();
        }
        return 0;
    case WM_APP_TRAY:
        if (LOWORD(l) == WM_LBUTTONDBLCLK) Settings_Show();
        else if (LOWORD(l) == WM_RBUTTONUP || LOWORD(l) == WM_CONTEXTMENU) TrayMenu();
        return 0;
    case WM_APP_SHOWSETTINGS:
        Settings_Show();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case ID_TRAY_OPEN:     Radial_Open(); break;
        case ID_TRAY_SETTINGS: Settings_Show(); break;
        case ID_TRAY_PAUSE:    g_paused = !g_paused; if (g_paused) Radial_Close(false); break;
        case ID_TRAY_EXIT:     DestroyWindow(h); break;
        case ID_TRAY_MIC: {
            bool mm = !Audio_GetMicMute();
            Audio_SetMicMute(mm);
            Osd_Show(Audio_GetMic(), mm, true);
            break;
        }
        case ID_TRAY_LOG:      ShellExecuteW(nullptr, L"open", LogPath().c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
        }
        return 0;
    case WM_DESTROY:
        TrayRemove();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR cmdLine, int) {
    g_hInst = hInst;

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"RuedaSuprema_byalvaro_mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(MAIN_CLASS, nullptr);
        if (other) PostMessageW(other, WM_APP_SHOWSETTINGS, 0, 0);
        return 0;
    }

    Media_InitApartment();   // COM/WinRT multihilo

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartup(&gdipToken, &gsi, nullptr);

    Config_Load();
    Startup_Apply(g_cfg.startup);
    InitSounds();

    g_iconBig = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                  GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    g_iconSmall = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    if (!g_iconBig) g_iconBig = LoadIcon(nullptr, IDI_APPLICATION);
    if (!g_iconSmall) g_iconSmall = g_iconBig;

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInst;
    wc.lpszClassName = MAIN_CLASS;
    wc.hIcon = g_iconBig;
    RegisterClassExW(&wc);
    g_hMain = CreateWindowExW(WS_EX_TOOLWINDOW, MAIN_CLASS, APP_NAME, WS_POPUP, 0, 0, 0, 0,
                              nullptr, nullptr, hInst, nullptr);
    s_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    Radial_Init();

    bool silent = cmdLine && wcsstr(cmdLine, L"--tray") != nullptr;
    TrayAdd(!silent);

    s_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHook, hInst, 0);
    if (!s_hook)
        MessageBoxW(nullptr, L"No se pudo instalar el detector de la rueda.", APP_NAME, MB_ICONERROR);

    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = 0x0C;   // Consumer Control (teclas multimedia y rueda)
    rid.usUsage = 0x01;
    rid.dwFlags = RIDEV_INPUTSINK;
    rid.hwndTarget = g_hMain;
    BOOL rawOk = RegisterRawInputDevices(&rid, 1, sizeof(rid));

    Log(L"===== Rueda Suprema %s iniciada. Hook: %s. Entrada multimedia: %s. Tecla del botón: %s =====",
        APP_VERSION, s_hook ? L"OK" : L"FALLO", rawOk ? L"OK" : L"FALLO",
        g_cfg.pressKey == 1 ? L"Play/Pausa" : L"Mute");

    TrimMemory();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {   // espera sin gastar CPU
        HWND active = GetActiveWindow();
        if (!active || !IsDialogMessageW(active, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (s_hook) UnhookWindowsHookEx(s_hook);
    Gdiplus::GdiplusShutdown(gdipToken);
    if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); }
    return 0;
}
