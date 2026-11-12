// Rueda Suprema - menú radial para la rueda de volumen del AJAZZ AK820
// HECHO POR @BYALVARO__
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>

#define APP_NAME    L"Rueda Suprema"
#define APP_CREDIT  L"HECHO POR @BYALVARO__"
#define APP_VERSION L"1.1"

#define MAIN_CLASS     L"RuedaSupremaMain"
#define RADIAL_CLASS   L"RuedaSupremaRadial"
#define OSD_CLASS      L"RuedaSupremaOSD"
#define SETTINGS_CLASS L"RuedaSupremaSettings"

#define IDI_APP 101

// Mensajes internos
#define WM_APP_KNOB          (WM_APP + 1)   // wParam = +1 / -1
#define WM_APP_PRESS         (WM_APP + 2)   // wParam = 1 pulsado, 0 soltado
#define WM_APP_TRAY          (WM_APP + 3)
#define WM_APP_SHOWSETTINGS  (WM_APP + 4)
#define WM_APP_ESC           (WM_APP + 5)

// Opciones del menú radial
enum ItemId {
    IT_VOLUME = 0,   // Volumen general
    IT_MIXER,        // Mezclador por app
    IT_OUTPUT,       // Cambiar salida de audio
    IT_MIC,          // Volumen del micrófono
    IT_MICMUTE,      // Silenciar micrófono
    IT_MUTE,         // Silenciar sonido
    IT_PLAYPAUSE,
    IT_NEXT,
    IT_PREV,
    IT_SEEK,         // Avanzar / retroceder en la canción
    IT_SETTINGS,
    IT_COUNT
};

// Modos del botón de la rueda
enum PressMode {
    PM_CLICK_MENU = 0,   // clic = menú, mantener = silenciar
    PM_DOUBLE_MENU = 1,  // doble clic = menú, clic = silenciar
    PM_HOLD_MENU = 2     // mantener = menú, clic = silenciar
};

struct Config {
    int  volStep = 2;          // % por muesca
    int  menuClicks = 1;       // muescas necesarias para pasar de opción
    bool invert = false;
    int  pressMode = PM_CLICK_MENU;
    int  pressKey = 0;         // 0 = la rueda manda Mute, 1 = manda Play/Pausa
    int  holdMs = 450;
    int  size = 360;           // diámetro del radial (px a 100 %)
    int  opacity = 235;
    int  autoClose = 6;        // segundos
    COLORREF accent = RGB(0, 170, 255);
    bool closeAfterAction = true;
    bool osd = true;
    bool sounds = true;
    bool startup = true;
    int  order[IT_COUNT];
    bool enabled[IT_COUNT];
};

extern Config    g_cfg;
extern HINSTANCE g_hInst;
extern HWND      g_hMain;
extern HICON     g_iconBig;
extern HICON     g_iconSmall;
extern bool      g_paused;
extern bool      g_menuOpen;   // lo mantiene radial.cpp, lo lee el hook

// ---------- config.cpp ----------
void Config_Defaults(Config& c);
void Config_Load();
void Config_Save();
void Startup_Apply(bool on);
const wchar_t* Item_Name(int id);

// ---------- audio.cpp ----------
struct AppSession {
    std::wstring name;
    DWORD pid = 0;
    bool  system = false;
    float vol = 1.0f;
    bool  mute = false;
};
struct AudioDevice {
    std::wstring id;
    std::wstring name;
    bool isDefault = false;
};
float Audio_GetMaster();
void  Audio_SetMaster(float v);
bool  Audio_GetMute();
void  Audio_SetMute(bool m);
float Audio_GetMic();
void  Audio_SetMic(float v);
bool  Audio_GetMicMute();
void  Audio_SetMicMute(bool m);
std::vector<AppSession>  Audio_ListSessions();
void  Audio_SetSessionVolume(DWORD pid, bool system, float v);
std::vector<AudioDevice> Audio_ListOutputs();
bool  Audio_SetDefaultOutput(const std::wstring& id);
std::wstring Audio_DefaultOutputName();

// ---------- media.cpp (WinRT) ----------
struct MediaInfo {
    bool has = false;
    bool playing = false;
    bool canSeek = false;
    std::wstring title;
    std::wstring artist;
    double pos = 0;   // segundos
    double dur = 0;   // segundos
};
void      Media_InitApartment();
MediaInfo Media_Get(bool withProps);
double    Media_Seek(int dir, double stepSec, double curPos, double dur);

// ---------- radial.cpp ----------
void Radial_Init();
bool Radial_IsOpen();
void Radial_Open();
void Radial_Close(bool animated);
void Radial_Rotate(int dir);
void Radial_Click();
void Radial_Hold();
void Osd_Show(float vol, bool mute);

// ---------- settings.cpp ----------
void Settings_Show();

// ---------- main.cpp ----------
void  Sound_Tick();
void  Sound_Confirm();
void  Media_Key(WORD vk);
void  TrimMemory();
float StepVolume(float cur, int dir);
void  ToggleMuteWithOsd();
