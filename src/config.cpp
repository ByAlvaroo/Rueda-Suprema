// Configuración en %APPDATA%\RuedaSuprema\config.ini - HECHO POR @BYALVARO__
#include "common.h"
#include <cstdio>
#include <cwchar>

Config g_cfg;

static std::wstring IniPath() {
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    std::wstring dir = (n > 0 && n < MAX_PATH) ? std::wstring(buf) : std::wstring(L".");
    dir += L"\\RuedaSuprema";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\config.ini";
}

const wchar_t* Item_Name(int id) {
    switch (id) {
    case IT_VOLUME:    return L"Volumen general";
    case IT_MIXER:     return L"Mezclador de apps";
    case IT_OUTPUT:    return L"Salida de audio";
    case IT_MIC:       return L"Micrófono";
    case IT_MICMUTE:   return L"Silenciar micro";
    case IT_MUTE:      return L"Silenciar sonido";
    case IT_PLAYPAUSE: return L"Play / Pausa";
    case IT_NEXT:      return L"Siguiente";
    case IT_PREV:      return L"Anterior";
    case IT_SEEK:      return L"Avanzar / retroceder";
    case IT_SETTINGS:  return L"Ajustes";
    }
    return L"?";
}

void Config_Defaults(Config& c) {
    c = Config();
    const int def[IT_COUNT] = { IT_VOLUME, IT_MIXER, IT_OUTPUT, IT_MIC, IT_MUTE,
                                IT_PLAYPAUSE, IT_NEXT, IT_PREV, IT_SEEK, IT_MICMUTE, IT_SETTINGS };
    for (int i = 0; i < IT_COUNT; i++) { c.order[i] = def[i]; c.enabled[i] = true; }
    c.enabled[IT_MICMUTE] = false;
}

static int ReadInt(const std::wstring& ini, const wchar_t* key, int def, int lo, int hi) {
    int v = (int)GetPrivateProfileIntW(L"RuedaSuprema", key, def, ini.c_str());
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

static std::wstring ReadStr(const std::wstring& ini, const wchar_t* key) {
    wchar_t buf[256] = {};
    GetPrivateProfileStringW(L"RuedaSuprema", key, L"", buf, 256, ini.c_str());
    return buf;
}

static std::vector<int> ParseList(const std::wstring& s) {
    std::vector<int> out;
    int cur = 0; bool has = false;
    for (wchar_t ch : s) {
        if (ch >= L'0' && ch <= L'9') { cur = cur * 10 + (ch - L'0'); has = true; }
        else if (ch == L',') { if (has) out.push_back(cur); cur = 0; has = false; }
    }
    if (has) out.push_back(cur);
    return out;
}

void Config_Load() {
    Config_Defaults(g_cfg);
    std::wstring ini = IniPath();
    if (GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES) { Config_Save(); return; }

    g_cfg.volStep          = ReadInt(ini, L"VolStep", g_cfg.volStep, 1, 10);
    g_cfg.menuClicks       = ReadInt(ini, L"MenuClicks", g_cfg.menuClicks, 1, 5);
    g_cfg.invert           = ReadInt(ini, L"Invert", g_cfg.invert, 0, 1) != 0;
    g_cfg.pressMode        = ReadInt(ini, L"PressMode", g_cfg.pressMode, 0, 2);
    g_cfg.pressKey         = ReadInt(ini, L"PressKey", g_cfg.pressKey, 0, 1);
    g_cfg.holdMs           = ReadInt(ini, L"HoldMs", g_cfg.holdMs, 250, 1000);
    g_cfg.size             = ReadInt(ini, L"Size", g_cfg.size, 260, 560);
    g_cfg.opacity          = ReadInt(ini, L"Opacity", g_cfg.opacity, 120, 255);
    g_cfg.autoClose        = ReadInt(ini, L"AutoClose", g_cfg.autoClose, 2, 20);
    g_cfg.accent           = (COLORREF)ReadInt(ini, L"Accent", (int)g_cfg.accent, 0, 0xFFFFFF);
    g_cfg.closeAfterAction = ReadInt(ini, L"CloseAfterAction", g_cfg.closeAfterAction, 0, 1) != 0;
    g_cfg.osd              = ReadInt(ini, L"Osd", g_cfg.osd, 0, 1) != 0;
    g_cfg.sounds           = ReadInt(ini, L"Sounds", g_cfg.sounds, 0, 1) != 0;
    g_cfg.startup          = ReadInt(ini, L"Startup", g_cfg.startup, 0, 1) != 0;

    // Orden: tiene que ser una permutación válida de todas las opciones
    std::vector<int> ord = ParseList(ReadStr(ini, L"Order"));
    if ((int)ord.size() == IT_COUNT) {
        bool seen[IT_COUNT] = {}; bool ok = true;
        for (int v : ord) { if (v < 0 || v >= IT_COUNT || seen[v]) { ok = false; break; } seen[v] = true; }
        if (ok) for (int i = 0; i < IT_COUNT; i++) g_cfg.order[i] = ord[i];
    }
    std::vector<int> en = ParseList(ReadStr(ini, L"Enabled"));
    if ((int)en.size() == IT_COUNT) {
        bool any = false;
        for (int i = 0; i < IT_COUNT; i++) { g_cfg.enabled[i] = en[i] != 0; any |= g_cfg.enabled[i]; }
        if (!any) g_cfg.enabled[IT_VOLUME] = true;
    }
}

static void WriteInt(const std::wstring& ini, const wchar_t* key, int v) {
    wchar_t b[32]; swprintf(b, 32, L"%d", v);
    WritePrivateProfileStringW(L"RuedaSuprema", key, b, ini.c_str());
}

void Config_Save() {
    std::wstring ini = IniPath();
    WriteInt(ini, L"VolStep", g_cfg.volStep);
    WriteInt(ini, L"MenuClicks", g_cfg.menuClicks);
    WriteInt(ini, L"Invert", g_cfg.invert);
    WriteInt(ini, L"PressMode", g_cfg.pressMode);
    WriteInt(ini, L"PressKey", g_cfg.pressKey);
    WriteInt(ini, L"HoldMs", g_cfg.holdMs);
    WriteInt(ini, L"Size", g_cfg.size);
    WriteInt(ini, L"Opacity", g_cfg.opacity);
    WriteInt(ini, L"AutoClose", g_cfg.autoClose);
    WriteInt(ini, L"Accent", (int)g_cfg.accent);
    WriteInt(ini, L"CloseAfterAction", g_cfg.closeAfterAction);
    WriteInt(ini, L"Osd", g_cfg.osd);
    WriteInt(ini, L"Sounds", g_cfg.sounds);
    WriteInt(ini, L"Startup", g_cfg.startup);
    std::wstring ord, en;
    for (int i = 0; i < IT_COUNT; i++) {
        if (i) { ord += L","; en += L","; }
        ord += std::to_wstring(g_cfg.order[i]);
        en  += g_cfg.enabled[i] ? L"1" : L"0";
    }
    WritePrivateProfileStringW(L"RuedaSuprema", L"Order", ord.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"RuedaSuprema", L"Enabled", en.c_str(), ini.c_str());
}

void Startup_Apply(bool on) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --tray";
        RegSetValueExW(key, L"RuedaSuprema", 0, REG_SZ, (const BYTE*)cmd.c_str(),
                       (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, L"RuedaSuprema");
    }
    RegCloseKey(key);
}
