// Menú radial (estilo Surface Dial) y OSD de volumen con GDI+ - HECHO POR @BYALVARO__
#include "common.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#include <memory>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

using namespace Gdiplus;

bool g_menuOpen = false;

static const float PI = 3.14159265f;

// ------------------------------------------------------------------ estado
enum Mode { M_CLOSED, M_ROOT, M_ADJUST, M_LIST };
enum Adj  { A_MASTER, A_MIC, A_SESSION, A_SEEK };
enum Lst  { L_MIXER, L_OUTPUT };

struct Entry {
    int id = -1;               // ItemId en la raíz
    int idx = -1;              // índice en sesiones / dispositivos
    bool back = false;         // "Volver"
    bool textIcon = false;     // icono = iniciales de la app
    bool checked = false;
    bool slash = false;        // icono tachado (micro silenciado)
    std::wstring glyph, label, sub;
    std::shared_ptr<Gdiplus::Bitmap> img;   // icono real de la app
};

#define TMR_ANIM      1
#define TMR_AUTOCLOSE 2
#define TMR_MEDIA     3
#define TMR_ACTION    4
#define TMR_OSD_HOLD  10
#define TMR_OSD_FADE  11

static HWND  s_wnd = nullptr, s_osd = nullptr;
static Mode  s_mode = M_CLOSED;
static Adj   s_adj = A_MASTER;
static Lst   s_lst = L_MIXER;
static std::vector<Entry>       s_entries;
static std::vector<AppSession>  s_sessions;
static std::vector<AudioDevice> s_devices;
static int   s_sel = 0, s_rootSel = 0, s_acc = 0, s_sessionIdx = -1;
static float s_value = 0;          // valor real (0..1) en modo ajuste
static bool  s_adjMuted = false;
static std::shared_ptr<Bitmap> s_adjImg;
static std::wstring s_adjTitle, s_adjSub;
static MediaInfo s_media;
static DWORD s_lastSeekTick = 0;

// animación
static float s_open = 0, s_angle = 0, s_angleTarget = 0, s_valAnim = 0, s_pulse = 0;
static bool  s_closing = false;
static DWORD s_lastTick = 0;

// superficie
struct Surface { HDC dc = nullptr; HBITMAP bmp = nullptr; HGDIOBJ old = nullptr; void* bits = nullptr; int w = 0, h = 0; };
static Surface s_surf, s_osdSurf;
static int   s_px = 360;
static float s_scale = 1.f;
static POINT s_center = { 0, 0 };

// OSD
static float s_osdVol = 0; static bool s_osdMute = false; static bool s_osdMic = false; static float s_osdAlpha = 0;
static float s_osdScale = 1.f; static POINT s_osdPos = { 0, 0 }; static int s_osdW = 0, s_osdH = 0;

// ------------------------------------------------------------------ utilidades
static void FreeSurface(Surface& s) {
    if (s.dc) {
        if (s.old) SelectObject(s.dc, s.old);
        if (s.bmp) DeleteObject(s.bmp);
        DeleteDC(s.dc);
    }
    s = Surface();
}

static bool EnsureSurface(Surface& s, int w, int h) {
    if (s.dc && s.w == w && s.h == h) return true;
    FreeSurface(s);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;      // de arriba a abajo
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    s.dc = CreateCompatibleDC(screen);
    s.bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &s.bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!s.dc || !s.bmp || !s.bits) { FreeSurface(s); return false; }
    s.old = SelectObject(s.dc, s.bmp);
    s.w = w; s.h = h;
    return true;
}

static void Present(HWND wnd, Surface& s, POINT dst, BYTE alpha) {
    POINT src = { 0, 0 };
    SIZE sz = { s.w, s.h };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA };
    HDC screen = GetDC(nullptr);
    UpdateLayeredWindow(wnd, screen, &dst, &sz, s.dc, &src, 0, &bf, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
}

const wchar_t* Radial_IconFont() {
    static std::wstring name;
    if (name.empty()) {
        FontFamily f(L"Segoe Fluent Icons");
        name = (f.GetLastStatus() == Ok && f.IsAvailable()) ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    }
    return name.c_str();
}

static const wchar_t* IconFont() { return Radial_IconFont(); }

static const wchar_t* TextFont() {
    static std::wstring name;
    if (name.empty()) {
        FontFamily f(L"Segoe UI Variable Display");
        name = (f.GetLastStatus() == Ok && f.IsAvailable()) ? L"Segoe UI Variable Display" : L"Segoe UI";
    }
    return name.c_str();
}

static void Txt(Graphics& g, const std::wstring& s, const wchar_t* family, float size, int style,
                     const Color& c, const RectF& rc, StringAlignment align = StringAlignmentCenter) {
    if (s.empty()) return;
    FontFamily ff(family);
    Font f(&ff, size, style, UnitPixel);
    SolidBrush b(c);
    StringFormat sf;
    sf.SetAlignment(align);
    sf.SetLineAlignment(StringAlignmentCenter);
    sf.SetTrimming(StringTrimmingEllipsisCharacter);
    sf.SetFormatFlags(StringFormatFlagsNoWrap);
    g.DrawString(s.c_str(), -1, &f, rc, &sf, &b);
}

static Color Accent(BYTE a = 255) {
    return Color(a, GetRValue(g_cfg.accent), GetGValue(g_cfg.accent), GetBValue(g_cfg.accent));
}

static std::wstring Pct(float v) { return std::to_wstring((int)lroundf(v * 100.f)) + L" %"; }

static std::wstring Clock(double sec) {
    if (sec < 0) sec = 0;
    int s = (int)sec;
    wchar_t b[32];
    if (s >= 3600) swprintf(b, 32, L"%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
    else swprintf(b, 32, L"%d:%02d", s / 60, s % 60);
    return b;
}

static std::wstring Initials(const std::wstring& name) {
    std::wstring r;
    bool next = true;
    for (wchar_t ch : name) {
        if (ch == L' ' || ch == L'-' || ch == L'_') { next = true; continue; }
        if (next) { r += (wchar_t)towupper(ch); next = false; if (r.size() == 2) break; }
    }
    if (r.size() == 1 && name.size() > 1) r += (wchar_t)towlower(name[1]);
    return r.empty() ? L"?" : r;
}


static const wchar_t* Glyph(int id) {
    switch (id) {
    case IT_VOLUME:    return L"";
    case IT_MIXER:     return L"";
    case IT_OUTPUT:    return L"";
    case IT_MIC:       return L"";
    case IT_MICMUTE:   return L"";
    case IT_MUTE:      return L"";
    case IT_PLAYPAUSE: return L"";
    case IT_NEXT:      return L"";
    case IT_PREV:      return L"";
    case IT_SEEK:      return L"";
    case IT_SETTINGS:  return L"";
    }
    return L"";
}

static std::wstring MediaLine() {
    if (!s_media.has || s_media.title.empty()) return L"Nada sonando";
    if (s_media.artist.empty()) return s_media.title;
    return s_media.title + L" · " + s_media.artist;
}

static std::wstring SubFor(int id) {
    switch (id) {
    case IT_VOLUME:   return Audio_GetMute() ? L"Silenciado" : Pct(Audio_GetMaster());
    case IT_MIXER:    return L"Volumen de cada app";
    case IT_OUTPUT:   return Audio_DefaultOutputName();
    case IT_MIC:      return Audio_GetMicMute() ? L"Micro silenciado" : Pct(Audio_GetMic());
    case IT_MICMUTE:  return Audio_GetMicMute() ? L"Micro silenciado" : L"Micro activo";
    case IT_MUTE:     return Audio_GetMute() ? L"Sonido silenciado" : L"Sonido activo";
    case IT_PLAYPAUSE:
    case IT_NEXT:
    case IT_PREV:     return MediaLine();
    case IT_SEEK:
        if (!s_media.has || !s_media.canSeek) return L"No disponible ahora";
        return Clock(s_media.pos) + L" / " + Clock(s_media.dur);
    case IT_SETTINGS: return L"Sensibilidad, menú y más";
    }
    return L"";
}

// Icono de un .exe convertido a imagen GDI+ con transparencia
static std::shared_ptr<Bitmap> IconToBitmap(HICON hicon) {
    std::shared_ptr<Bitmap> out;
    ICONINFO ii = {};
    if (!GetIconInfo(hicon, &ii)) return out;
    BITMAP bm = {};
    if (ii.hbmColor && GetObjectW(ii.hbmColor, sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmHeight > 0) {
        int w = bm.bmWidth, h = bm.bmHeight;
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        std::vector<DWORD> px((size_t)w * h);
        HDC dc = GetDC(nullptr);
        int ok = GetDIBits(dc, ii.hbmColor, 0, h, px.data(), &bi, DIB_RGB_COLORS);
        bool hasAlpha = false;
        for (DWORD p : px) if (p >> 24) { hasAlpha = true; break; }
        if (ok && !hasAlpha && ii.hbmMask) {
            std::vector<DWORD> mask((size_t)w * h);
            GetDIBits(dc, ii.hbmMask, 0, h, mask.data(), &bi, DIB_RGB_COLORS);
            for (size_t i = 0; i < px.size(); i++)
                px[i] = (mask[i] & 0xFFFFFF) ? 0 : (px[i] | 0xFF000000);
        }
        ReleaseDC(nullptr, dc);
        if (ok) {
            out.reset(new Bitmap(w, h, PixelFormat32bppARGB));
            BitmapData bd;
            Rect r(0, 0, w, h);
            if (out->LockBits(&r, ImageLockModeWrite, PixelFormat32bppARGB, &bd) == Ok) {
                for (int y = 0; y < h; y++)
                    memcpy((BYTE*)bd.Scan0 + (size_t)y * bd.Stride, &px[(size_t)y * w], (size_t)w * 4);
                out->UnlockBits(&bd);
            } else out.reset();
        }
    }
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    return out;
}

static std::shared_ptr<Bitmap> LoadAppIcon(const std::wstring& exe) {
    if (exe.empty()) return nullptr;
    HICON hi = nullptr;
    if (FAILED(SHDefExtractIconW(exe.c_str(), 0, 0, &hi, nullptr, MAKELONG(64, 16))) || !hi) {
        hi = nullptr;
        ExtractIconExW(exe.c_str(), 0, &hi, nullptr, 1);
    }
    if (!hi) return nullptr;
    auto bmp = IconToBitmap(hi);
    DestroyIcon(hi);
    return bmp;
}

static float Span() { return s_entries.empty() ? 360.f : 360.f / (float)s_entries.size(); }

static void StartAnim() {
    s_lastTick = GetTickCount();
    SetTimer(s_wnd, TMR_ANIM, 15, nullptr);
}

static void TouchAutoClose() {
    SetTimer(s_wnd, TMR_AUTOCLOSE, (UINT)g_cfg.autoClose * 1000, nullptr);
}

static void Render();

static void SetEntries(std::vector<Entry>&& e, int sel) {
    s_entries = std::move(e);
    if (s_entries.empty()) { Entry x; x.back = true; x.glyph = L""; x.label = L"Volver"; s_entries.push_back(x); }
    int n = (int)s_entries.size();
    s_sel = sel < 0 ? 0 : (sel >= n ? n - 1 : sel);
    s_angle = s_angleTarget = s_sel * Span();
    s_acc = 0;
    s_pulse = 1.f;          // pequeño "pop" al cambiar de pantalla
    StartAnim();
}

// ------------------------------------------------------------------ pantallas
static void BuildRoot() {
    std::vector<Entry> v;
    for (int k = 0; k < IT_COUNT; k++) {
        int id = g_cfg.order[k];
        if (!g_cfg.enabled[id]) continue;
        Entry e;
        e.id = id;
        e.glyph = Glyph(id);
        if (id == IT_OUTPUT) {
            for (auto& d : Audio_ListOutputs())
                if (d.isDefault) { e.glyph = DeviceIcon_For(d.id, d.name); break; }
        }
        if (id == IT_PLAYPAUSE && s_media.playing) e.glyph = L"";
        if (id == IT_MUTE && !Audio_GetMute()) e.glyph = L"";
        e.slash = (id == IT_MICMUTE) || (id == IT_MUTE && Audio_GetMute());
        e.label = Item_Name(id);
        e.sub = SubFor(id);
        v.push_back(e);
    }
    if (v.empty()) {
        Entry e; e.id = IT_VOLUME; e.glyph = Glyph(IT_VOLUME); e.label = Item_Name(IT_VOLUME); e.sub = SubFor(IT_VOLUME);
        v.push_back(e);
    }
    s_mode = M_ROOT;
    SetEntries(std::move(v), s_rootSel);
}

static void RefreshRootSubs() {
    if (s_mode != M_ROOT) return;
    for (auto& e : s_entries) {
        e.sub = SubFor(e.id);
        if (e.id == IT_PLAYPAUSE) e.glyph = s_media.playing ? L"" : L"";
        if (e.id == IT_MUTE) { bool m = Audio_GetMute(); e.glyph = m ? L"" : L""; e.slash = m; }
    }
}

static Entry BackEntry() {
    Entry b; b.back = true; b.glyph = L""; b.label = L"Volver"; b.sub = L"Al menú principal";
    return b;
}

static void EnterMixer(int sel) {
    s_sessions = Audio_ListSessions();
    std::vector<Entry> v;
    v.push_back(BackEntry());
    for (size_t i = 0; i < s_sessions.size(); i++) {
        Entry e;
        e.idx = (int)i;
        e.textIcon = true;
        e.glyph = s_sessions[i].system ? L"" : Initials(s_sessions[i].name);
        if (s_sessions[i].system) e.textIcon = false;
        if (!s_sessions[i].system) {
            e.img = LoadAppIcon(s_sessions[i].exePath);
            if (e.img) e.textIcon = false;
        }
        e.label = s_sessions[i].name;
        e.sub = s_sessions[i].mute ? L"Silenciada" : Pct(s_sessions[i].vol);
        v.push_back(e);
    }
    if (s_sessions.empty()) v[0].sub = L"No hay apps sonando";
    s_mode = M_LIST; s_lst = L_MIXER;
    SetEntries(std::move(v), sel);
}

static void EnterOutputs() {
    s_devices = Audio_ListOutputs();
    std::vector<Entry> v;
    v.push_back(BackEntry());
    int sel = 0;
    for (size_t i = 0; i < s_devices.size(); i++) {
        Entry e;
        e.idx = (int)i;
        e.glyph = DeviceIcon_For(s_devices[i].id, s_devices[i].name);
        e.label = s_devices[i].name;
        e.checked = s_devices[i].isDefault;
        e.sub = s_devices[i].isDefault ? L"En uso ahora" : L"Pulsa para usar esta salida";
        if (s_devices[i].isDefault) sel = (int)i + 1;
        v.push_back(e);
    }
    s_mode = M_LIST; s_lst = L_OUTPUT;
    SetEntries(std::move(v), sel);
}

static void UpdateAdjustTexts() {
    switch (s_adj) {
    case A_MASTER:
        s_adjMuted = Audio_GetMute();
        s_adjTitle = L"Volumen general";
        s_adjSub = Audio_DefaultOutputName();
        break;
    case A_MIC:
        s_adjMuted = Audio_GetMicMute();
        s_adjTitle = L"Micrófono";
        s_adjSub = L"Gira para ajustar";
        break;
    case A_SESSION:
        s_adjMuted = false;
        if (s_sessionIdx >= 0 && s_sessionIdx < (int)s_sessions.size())
            s_adjTitle = s_sessions[s_sessionIdx].name;
        s_adjSub = s_adjImg ? s_adjTitle : L"Pulsa para volver";
        break;
    case A_SEEK:
        s_adjMuted = false;
        s_adjTitle = s_media.title.empty() ? L"Canción" : s_media.title;
        s_adjSub = Clock(s_media.pos) + L" / " + Clock(s_media.dur);
        break;
    }
}

static void EnterAdjust(Adj a) {
    s_adj = a;
    s_mode = M_ADJUST;
    switch (a) {
    case A_MASTER:  s_value = Audio_GetMaster(); break;
    case A_MIC:     s_value = Audio_GetMic(); break;
    case A_SESSION:
        s_value = (s_sessionIdx >= 0 && s_sessionIdx < (int)s_sessions.size()) ? s_sessions[s_sessionIdx].vol : 0;
        s_adjImg.reset();
        for (auto& en : s_entries) if (en.idx == s_sessionIdx && !en.back) { s_adjImg = en.img; break; }
        break;
    case A_SEEK:
        s_media = Media_Get(true);
        s_value = s_media.dur > 0 ? (float)(s_media.pos / s_media.dur) : 0.f;
        SetTimer(s_wnd, TMR_MEDIA, 500, nullptr);
        break;
    }
    UpdateAdjustTexts();
    s_valAnim = 0;          // el arco se "dibuja" al entrar
    s_pulse = 1.f;
    s_acc = 0;
    StartAnim();
}

static void BackToRoot() {
    KillTimer(s_wnd, TMR_MEDIA);
    BuildRoot();
}

// ------------------------------------------------------------------ dibujo
static void AnnularSector(GraphicsPath& p, float R, float r, float start, float sweep) {
    p.AddArc(-R, -R, 2 * R, 2 * R, start, sweep);
    p.AddArc(-r, -r, 2 * r, 2 * r, start + sweep, -sweep);
    p.CloseFigure();
}

static void DrawItemIcon(Graphics& g, const Entry& e, float cx, float cy, float size, const Color& c) {
    RectF rc(cx - size, cy - size, size * 2, size * 2);
    if (e.img) {
        float is = size * 0.82f;
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(e.img.get(), RectF(cx - is, cy - is, is * 2, is * 2));
        return;
    }
    if (e.textIcon) Txt(g, e.glyph, TextFont(), size * 0.72f, FontStyleBold, c, rc);
    else            Txt(g, e.glyph, IconFont(), size, FontStyleRegular, c, rc);
    if (e.slash) {
        Pen p(c, size * 0.11f);
        p.SetStartCap(LineCapRound); p.SetEndCap(LineCapRound);
        g.DrawLine(&p, cx - size * 0.5f, cy - size * 0.5f, cx + size * 0.5f, cy + size * 0.5f);
    }
}

static void DrawRing(Graphics& g, float R, float r) {
    int n = (int)s_entries.size();
    float span = Span();
    float S = s_scale;

    // sector seleccionado (animado)
    float selCenter = -90.f + s_angle;
    float gap = n > 1 ? 2.0f : 0.f;
    {
        GraphicsPath p;
        float inset = 5 * S;
        if (n == 1) {
            p.AddEllipse(-(R - inset), -(R - inset), 2 * (R - inset), 2 * (R - inset));
            p.AddEllipse(-(r + inset), -(r + inset), 2 * (r + inset), 2 * (r + inset));
        } else {
            AnnularSector(p, R - inset, r + inset, selCenter - span / 2 + gap / 2, span - gap);
        }
        PathGradientBrush pg(&p);
        pg.SetCenterPoint(PointF(cosf(selCenter * PI / 180) * (R + r) / 2, sinf(selCenter * PI / 180) * (R + r) / 2));
        pg.SetCenterColor(Accent(255));
        Color edge = Accent(200);
        int cnt = 1;
        pg.SetSurroundColors(&edge, &cnt);
        g.FillPath(&pg, &p);
    }

    // separadores
    if (n > 1) {
        Pen sep(Color(34, 255, 255, 255), 1.2f * S);
        for (int i = 0; i < n; i++) {
            float a = (-90.f + i * span - span / 2) * PI / 180;
            g.DrawLine(&sep, cosf(a) * (r + 8 * S), sinf(a) * (r + 8 * S), cosf(a) * (R - 8 * S), sinf(a) * (R - 8 * S));
        }
    }

    // iconos
    float mid = (R + r) / 2;
    float iconSize = std::min((R - r) * 0.30f, mid * sinf(std::min(span, 120.f) * PI / 360.f) * 0.62f);
    for (int i = 0; i < n; i++) {
        float a = (-90.f + i * span) * PI / 180;
        float cx = cosf(a) * mid, cy = sinf(a) * mid;
        bool sel = (i == s_sel);
        Color c = sel ? Color(255, 255, 255, 255) : Color(205, 225, 228, 236);
        float sz = sel ? iconSize * 1.12f : iconSize;
        DrawItemIcon(g, s_entries[i], cx, cy, sz, c);
        if (s_entries[i].checked) {
            float dx = cosf(a) * (R - 14 * S), dy = sinf(a) * (R - 14 * S);
            SolidBrush dot(sel ? Color(255, 255, 255, 255) : Accent(255));
            g.FillEllipse(&dot, dx - 4 * S, dy - 4 * S, 8 * S, 8 * S);
        }
    }
}

static void DrawAdjust(Graphics& g, float R, float r) {
    float S = s_scale;
    float mid = (R + r) / 2;
    float thick = (R - r) * 0.34f;
    RectF arcRc(-mid, -mid, 2 * mid, 2 * mid);

    Pen track(Color(38, 255, 255, 255), thick);
    g.DrawEllipse(&track, arcRc);

    // marcas cada 10 %
    Pen tick(Color(70, 255, 255, 255), 1.4f * S);
    for (int i = 0; i < 20; i++) {
        float a = (-90.f + i * 18.f) * PI / 180;
        float r0 = R - 7 * S, r1 = R - (i % 5 == 0 ? 15 : 11) * S;
        g.DrawLine(&tick, cosf(a) * r0, sinf(a) * r0, cosf(a) * r1, sinf(a) * r1);
    }

    float v = std::max(0.f, std::min(1.f, s_valAnim));
    Color col = s_adjMuted ? Color(255, 120, 120, 130) : Accent(255);
    if (v > 0.002f) {
        Pen arc(col, thick);
        arc.SetStartCap(LineCapRound);
        arc.SetEndCap(LineCapRound);
        g.DrawArc(&arc, arcRc, -90.f, std::max(0.5f, 360.f * v));
    }
    // tirador
    float a = (-90.f + 360.f * v) * PI / 180;
    float kx = cosf(a) * mid, ky = sinf(a) * mid;
    SolidBrush knob(Color(255, 255, 255, 255));
    float kr = thick * 0.62f;
    g.FillEllipse(&knob, kx - kr, ky - kr, kr * 2, kr * 2);
    SolidBrush knobIn(col);
    g.FillEllipse(&knobIn, kx - kr * 0.45f, ky - kr * 0.45f, kr * 0.9f, kr * 0.9f);
}

static void DrawCenter(Graphics& g, float r) {
    float S = s_scale;
    float cr = r - 4 * S;
    SolidBrush cb(Color((BYTE)std::min(255, g_cfg.opacity + 10), 30, 31, 39));
    g.FillEllipse(&cb, -cr, -cr, 2 * cr, 2 * cr);
    Pen rim(Accent(90), 1.5f * S);
    g.DrawEllipse(&rim, -cr, -cr, 2 * cr, 2 * cr);

    float w = cr * 1.62f;
    Color white(255, 255, 255, 255), dim(170, 215, 218, 228), faint(95, 215, 218, 228);

    if (s_mode == M_ADJUST) {
        std::wstring big;
        if (s_adj == A_SEEK) big = Clock(s_media.pos);
        else big = s_adjMuted ? L"" : Pct(s_value);
        if (s_adj == A_SESSION && s_adjImg) {
            float is = cr * 0.13f;
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.DrawImage(s_adjImg.get(), RectF(-is, -cr * 0.49f - is, is * 2, is * 2));
        } else
            Txt(g, s_adjTitle, TextFont(), cr * 0.15f, FontStyleBold, dim, RectF(-w / 2, -cr * 0.62f, w, cr * 0.26f));
        if (s_adjMuted && s_adj != A_SEEK)
            Txt(g, big, IconFont(), cr * 0.40f, FontStyleRegular, Color(255, 235, 110, 110), RectF(-w / 2, -cr * 0.36f, w, cr * 0.62f));
        else
            Txt(g, big, TextFont(), cr * 0.40f, FontStyleBold, white, RectF(-w / 2, -cr * 0.36f, w, cr * 0.62f));
        Txt(g, s_adjSub, TextFont(), cr * 0.115f, FontStyleRegular, dim, RectF(-w / 2, cr * 0.28f, w, cr * 0.22f));
    } else if (!s_entries.empty()) {
        const Entry& e = s_entries[s_sel];
        DrawItemIcon(g, e, 0, -cr * 0.40f, cr * 0.22f, Accent(255));
        Txt(g, e.label, TextFont(), cr * 0.165f, FontStyleBold, white, RectF(-w / 2, -cr * 0.16f, w, cr * 0.30f));
        Txt(g, e.sub, TextFont(), cr * 0.115f, FontStyleRegular, dim, RectF(-w / 2, cr * 0.14f, w, cr * 0.22f));
    }
    Txt(g, APP_CREDIT, TextFont(), cr * 0.072f, FontStyleBold, faint, RectF(-w / 2, cr * 0.58f, w, cr * 0.16f));
}

static float EaseOut(float t) { t = std::max(0.f, std::min(1.f, t)); return 1 - (1 - t) * (1 - t) * (1 - t); }

static void Render() {
    if (!s_surf.bits || s_mode == M_CLOSED) return;
    int W = s_surf.w;
    memset(s_surf.bits, 0, (size_t)W * s_surf.h * 4);
    float e = EaseOut(s_open);
    {
        Bitmap bmp(W, W, W * 4, PixelFormat32bppPARGB, (BYTE*)s_surf.bits);
        Graphics g(&bmp);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.SetCompositingQuality(CompositingQualityHighQuality);

        float sc = (0.86f + 0.14f * e) * (1.f - 0.035f * s_pulse);
        g.TranslateTransform(W / 2.f, W / 2.f);
        g.ScaleTransform(sc, sc);

        float S = s_scale;
        float R = W / 2.f - 14 * S;
        float r = R * 0.48f;

        // sombra suave
        for (int i = 0; i < 7; i++) {
            float rr = R + (7 - i) * 1.6f * S;
            SolidBrush sb(Color(9, 0, 0, 0));
            g.FillEllipse(&sb, -rr, -rr + 3 * S, 2 * rr, 2 * rr);
        }
        SolidBrush base(Color((BYTE)g_cfg.opacity, 20, 21, 27));
        g.FillEllipse(&base, -R, -R, 2 * R, 2 * R);
        Pen rim(Color(50, 255, 255, 255), 1.2f * S);
        g.DrawEllipse(&rim, -R, -R, 2 * R, 2 * R);

        if (s_mode == M_ADJUST) DrawAdjust(g, R, r);
        else DrawRing(g, R, r);
        DrawCenter(g, r);
        g.Flush(FlushIntentionSync);
    }
    POINT dst = { s_center.x - W / 2, s_center.y - W / 2 };
    Present(s_wnd, s_surf, dst, (BYTE)(255 * e));
}

static void FinishClose() {
    KillTimer(s_wnd, TMR_ANIM);
    KillTimer(s_wnd, TMR_AUTOCLOSE);
    KillTimer(s_wnd, TMR_MEDIA);
    KillTimer(s_wnd, TMR_ACTION);
    ShowWindow(s_wnd, SW_HIDE);
    s_mode = M_CLOSED;
    s_closing = false;
    g_menuOpen = false;
    FreeSurface(s_surf);
    std::vector<Entry>().swap(s_entries);
    s_adjImg.reset();
    std::vector<AppSession>().swap(s_sessions);
    std::vector<AudioDevice>().swap(s_devices);
    TrimMemory();
}

// ------------------------------------------------------------------ API
bool Radial_IsOpen() { return s_mode != M_CLOSED && !s_closing; }

void Radial_Open() {
    if (Radial_IsOpen()) return;
    if (s_osd) { KillTimer(s_osd, TMR_OSD_HOLD); KillTimer(s_osd, TMR_OSD_FADE); ShowWindow(s_osd, SW_HIDE); FreeSurface(s_osdSurf); }

    POINT pt; GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    UINT dx = 96, dy = 96;
    if (FAILED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy))) dx = 96;
    s_scale = dx / 96.f;
    s_px = (int)(g_cfg.size * s_scale);
    s_center.x = (mi.rcWork.left + mi.rcWork.right) / 2;
    s_center.y = (mi.rcWork.top + mi.rcWork.bottom) / 2;
    if (!EnsureSurface(s_surf, s_px, s_px)) return;

    s_closing = false;
    g_menuOpen = true;
    s_media = Media_Get(true);
    s_open = 0;
    BuildRoot();
    s_pulse = 0;
    Render();
    SetWindowPos(s_wnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    StartAnim();
    TouchAutoClose();
    Sound_Confirm();
}

void Radial_Close(bool animated) {
    if (s_mode == M_CLOSED) return;
    KillTimer(s_wnd, TMR_AUTOCLOSE);
    KillTimer(s_wnd, TMR_MEDIA);
    g_menuOpen = false;
    if (animated) { s_closing = true; StartAnim(); }
    else FinishClose();
}

static void MoveSelection(int delta) {
    int n = (int)s_entries.size();
    if (n <= 0) return;
    s_sel = ((s_sel + delta) % n + n) % n;
    s_angleTarget += delta * Span();
    Sound_Tick();
    StartAnim();
}

void Radial_Rotate(int dir) {
    if (!Radial_IsOpen()) return;
    TouchAutoClose();
    if (s_mode == M_ADJUST) {
        switch (s_adj) {
        case A_MASTER: {
            float v = StepVolume(Audio_GetMaster(), dir);
            Audio_SetMaster(v);
            if (dir > 0 && Audio_GetMute()) Audio_SetMute(false);
            s_value = v;
            break;
        }
        case A_MIC: {
            float v = StepVolume(Audio_GetMic(), dir);
            Audio_SetMic(v);
            if (dir > 0 && Audio_GetMicMute()) Audio_SetMicMute(false);
            s_value = v;
            break;
        }
        case A_SESSION:
            if (s_sessionIdx >= 0 && s_sessionIdx < (int)s_sessions.size()) {
                AppSession& a = s_sessions[s_sessionIdx];
                a.vol = StepVolume(a.vol, dir);
                if (a.vol > 0) a.mute = false;
                Audio_SetSessionVolume(a.id, a.vol);
                s_value = a.vol;
            }
            break;
        case A_SEEK:
            if (s_media.has && s_media.canSeek) {
                s_media.pos = Media_Seek(dir, 5.0, s_media.pos, s_media.dur);
                s_lastSeekTick = GetTickCount();
                s_value = s_media.dur > 0 ? (float)(s_media.pos / s_media.dur) : 0.f;
            }
            break;
        }
        UpdateAdjustTexts();
        Sound_Tick();
        StartAnim();
        return;
    }
    s_acc += dir;
    if (abs(s_acc) < g_cfg.menuClicks) return;
    int d = s_acc > 0 ? 1 : -1;
    s_acc = 0;
    MoveSelection(d);
}

void Radial_Click() {
    if (!Radial_IsOpen()) return;
    TouchAutoClose();
    if (s_mode == M_ADJUST) {
        Sound_Confirm();
        if (s_adj == A_SESSION) { KillTimer(s_wnd, TMR_MEDIA); EnterMixer(s_sessionIdx + 1); }
        else BackToRoot();
        return;
    }
    if (s_entries.empty()) return;
    Entry e = s_entries[s_sel];

    if (s_mode == M_LIST) {
        Sound_Confirm();
        if (e.back) { BackToRoot(); return; }
        if (s_lst == L_MIXER) { s_sessionIdx = e.idx; EnterAdjust(A_SESSION); }
        else if (e.idx >= 0 && e.idx < (int)s_devices.size()) {
            Audio_SetDefaultOutput(s_devices[e.idx].id);
            BackToRoot();
        }
        return;
    }

    // raíz
    s_rootSel = s_sel;
    switch (e.id) {
    case IT_VOLUME:  Sound_Confirm(); EnterAdjust(A_MASTER); break;
    case IT_MIC:     Sound_Confirm(); EnterAdjust(A_MIC); break;
    case IT_MIXER:   Sound_Confirm(); EnterMixer(1); break;
    case IT_OUTPUT:  Sound_Confirm(); EnterOutputs(); break;
    case IT_SEEK:
        s_media = Media_Get(true);
        if (s_media.has && s_media.canSeek) { Sound_Confirm(); EnterAdjust(A_SEEK); }
        else { RefreshRootSubs(); s_pulse = 1.f; StartAnim(); }
        break;
    case IT_MUTE:
        Sound_Confirm();
        Audio_SetMute(!Audio_GetMute());
        RefreshRootSubs(); s_pulse = 1.f; StartAnim();
        break;
    case IT_MICMUTE:
        Sound_Confirm();
        Audio_SetMicMute(!Audio_GetMicMute());
        RefreshRootSubs(); s_pulse = 1.f; StartAnim();
        break;
    case IT_PLAYPAUSE:
    case IT_NEXT:
    case IT_PREV: {
        Sound_Confirm();
        WORD vk = e.id == IT_PLAYPAUSE ? VK_MEDIA_PLAY_PAUSE : (e.id == IT_NEXT ? VK_MEDIA_NEXT_TRACK : VK_MEDIA_PREV_TRACK);
        Media_Key(vk);
        if (g_cfg.closeAfterAction) Radial_Close(true);
        else { s_pulse = 1.f; StartAnim(); SetTimer(s_wnd, TMR_ACTION, 450, nullptr); }
        break;
    }
    case IT_SETTINGS:
        Sound_Confirm();
        Radial_Close(true);
        Settings_Show();
        break;
    }
}

void Radial_Hold() { Radial_Close(true); }

// ------------------------------------------------------------------ ventana del radial
static int HitTest(int x, int y, bool* inCenter) {
    float sc = 1.f;   // a escala completa una vez abierto
    float W = (float)s_px;
    float R = (W / 2.f - 14 * s_scale) * sc, r = R * 0.48f;
    float dx = x - W / 2.f, dy = y - W / 2.f;
    float d = sqrtf(dx * dx + dy * dy);
    *inCenter = d < r;
    if (d < r || d > R || s_entries.empty()) return -1;
    float a = atan2f(dy, dx) * 180.f / PI + 90.f;
    while (a < 0) a += 360.f;
    float span = Span();
    int idx = (int)floorf((a + span / 2) / span) % (int)s_entries.size();
    return idx;
}

static LRESULT CALLBACK RadialProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_TIMER:
        if (w == TMR_ANIM) {
            DWORD now = GetTickCount();
            float dt = std::min(0.05f, (now - s_lastTick) / 1000.f);
            s_lastTick = now;
            bool busy = false;
            if (s_closing) {
                s_open -= dt / 0.12f;
                if (s_open <= 0) { s_open = 0; FinishClose(); return 0; }
                busy = true;
            } else if (s_open < 1.f) {
                s_open = std::min(1.f, s_open + dt / 0.17f);
                busy = true;
            }
            float k = 1.f - expf(-dt * 20.f);
            float da = s_angleTarget - s_angle;
            if (fabsf(da) > 0.05f) { s_angle += da * k; busy = true; } else s_angle = s_angleTarget;
            float dv = s_value - s_valAnim;
            if (fabsf(dv) > 0.0015f) { s_valAnim += dv * (1.f - expf(-dt * 16.f)); busy = true; } else s_valAnim = s_value;
            if (s_pulse > 0) { s_pulse = std::max(0.f, s_pulse - dt / 0.16f); busy = true; }
            Render();
            if (!busy) KillTimer(h, TMR_ANIM);
        } else if (w == TMR_AUTOCLOSE) {
            Radial_Close(true);
        } else if (w == TMR_MEDIA) {
            if (s_mode == M_ADJUST && s_adj == A_SEEK) {
                MediaInfo mi = Media_Get(false);
                if (mi.has && GetTickCount() - s_lastSeekTick > 1500) {
                    s_media.playing = mi.playing;
                    s_media.dur = mi.dur;
                    s_media.pos = mi.pos;
                    s_value = s_media.dur > 0 ? (float)(s_media.pos / s_media.dur) : 0.f;
                    UpdateAdjustTexts();
                    StartAnim();
                }
            } else KillTimer(h, TMR_MEDIA);
        } else if (w == TMR_ACTION) {
            KillTimer(h, TMR_ACTION);
            s_media = Media_Get(true);
            RefreshRootSubs();
            Render();
        }
        return 0;
    case WM_MOUSEMOVE: {
        if (!Radial_IsOpen() || s_mode == M_ADJUST) return 0;
        bool c;
        int idx = HitTest((short)LOWORD(l), (short)HIWORD(l), &c);
        if (idx >= 0 && idx != s_sel) {
            int n = (int)s_entries.size();
            int delta = idx - s_sel;
            if (delta > n / 2) delta -= n;
            if (delta < -n / 2) delta += n;
            MoveSelection(delta);
            TouchAutoClose();
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!Radial_IsOpen()) return 0;
        bool c;
        int idx = HitTest((short)LOWORD(l), (short)HIWORD(l), &c);
        if (idx >= 0 || c || s_mode == M_ADJUST) Radial_Click();
        return 0;
    }
    case WM_RBUTTONUP:
        Radial_Close(true);
        return 0;
    case WM_MOUSEWHEEL: {
        int z = GET_WHEEL_DELTA_WPARAM(w);
        Radial_Rotate(z > 0 ? -1 : 1);
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

// ------------------------------------------------------------------ OSD de volumen
static void RRect(GraphicsPath& p, float x, float y, float w, float h, float r) {
    float d = r * 2;
    p.AddArc(x, y, d, d, 180.f, 90.f);
    p.AddArc(x + w - d, y, d, d, 270.f, 90.f);
    p.AddArc(x + w - d, y + h - d, d, d, 0.f, 90.f);
    p.AddArc(x, y + h - d, d, d, 90.f, 90.f);
    p.CloseFigure();
}

static void RenderOsd() {
    if (!s_osdSurf.bits) return;
    int W = s_osdSurf.w, H = s_osdSurf.h;
    memset(s_osdSurf.bits, 0, (size_t)W * H * 4);
    {
        Bitmap bmp(W, H, W * 4, PixelFormat32bppPARGB, (BYTE*)s_osdSurf.bits);
        Graphics g(&bmp);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        float S = s_osdScale;
        float m = 8 * S;
        float w = W - 2 * m, h = H - 2 * m;
        for (int i = 0; i < 5; i++) {
            GraphicsPath sp;
            float o = (5 - i) * 1.4f * S;
            RRect(sp, m - o, m - o + 2 * S, w + 2 * o, h + 2 * o, h / 2 + o);
            SolidBrush sb(Color(10, 0, 0, 0));
            g.FillPath(&sb, &sp);
        }
        GraphicsPath p;
        RRect(p, m, m, w, h, h / 2);
        SolidBrush bg(Color((BYTE)g_cfg.opacity, 22, 23, 30));
        g.FillPath(&bg, &p);
        Pen rim(Color(45, 255, 255, 255), 1.f * S);
        g.DrawPath(&rim, &p);

        const wchar_t* icon = s_osdMute ? L"\uE74F" : (s_osdVol <= 0.001f ? L"\uE992" : (s_osdVol < 0.34f ? L"\uE993" : (s_osdVol < 0.67f ? L"\uE994" : L"\uE995")));
        if (s_osdMic) icon = L"\uE720";
        Color ic = s_osdMute ? Color(255, 235, 110, 110) : Color(255, 255, 255, 255);
        Txt(g, icon, IconFont(), h * 0.42f, FontStyleRegular, ic, RectF(m + h * 0.18f, m, h * 0.8f, h));
        if (s_osdMic && s_osdMute) {
            float cx = m + h * 0.58f, cy = m + h / 2, a = h * 0.2f;
            Pen sl(ic, 2.2f * S);
            sl.SetStartCap(LineCapRound); sl.SetEndCap(LineCapRound);
            g.DrawLine(&sl, cx - a, cy - a, cx + a, cy + a);
        }

        float bx = m + h * 1.05f, bw = w - h * 1.05f - h * 1.25f, bh = 5 * S, by = m + h / 2 - bh / 2;
        GraphicsPath tp; RRect(tp, bx, by, bw, bh, bh / 2);
        SolidBrush tb(Color(55, 255, 255, 255)); g.FillPath(&tb, &tp);
        float fw = std::max(bh, bw * (s_osdMute ? 0.f : s_osdVol));
        if (!s_osdMute && s_osdVol > 0.001f) {
            GraphicsPath fp; RRect(fp, bx, by, fw, bh, bh / 2);
            SolidBrush fb(Accent(255)); g.FillPath(&fb, &fp);
            SolidBrush kb(Color(255, 255, 255, 255));
            g.FillEllipse(&kb, bx + fw - 6 * S, by + bh / 2 - 6 * S, 12 * S, 12 * S);
        }
        std::wstring t = s_osdMute ? L"Mute" : std::to_wstring((int)lroundf(s_osdVol * 100));
        Txt(g, t, TextFont(), h * 0.32f, FontStyleBold, Color(255, 255, 255, 255),
                 RectF(m + w - h * 1.2f, m, h * 1.0f, h));
        g.Flush(FlushIntentionSync);
    }
    Present(s_osd, s_osdSurf, s_osdPos, (BYTE)(255 * std::max(0.f, std::min(1.f, s_osdAlpha))));
}

void Osd_Show(float vol, bool mute, bool mic) {
    if (!g_cfg.osd || Radial_IsOpen()) return;
    s_osdVol = vol; s_osdMute = mute; s_osdMic = mic;
    POINT pt; GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    UINT dx = 96, dy = 96;
    if (FAILED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy))) dx = 96;
    s_osdScale = dx / 96.f;
    s_osdW = (int)(300 * s_osdScale);
    s_osdH = (int)(66 * s_osdScale);
    s_osdPos.x = (mi.rcWork.left + mi.rcWork.right) / 2 - s_osdW / 2;
    s_osdPos.y = mi.rcWork.bottom - s_osdH - (int)(60 * s_osdScale);
    if (!EnsureSurface(s_osdSurf, s_osdW, s_osdH)) return;
    s_osdAlpha = 1.f;
    KillTimer(s_osd, TMR_OSD_FADE);
    RenderOsd();
    SetWindowPos(s_osd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetTimer(s_osd, TMR_OSD_HOLD, 1300, nullptr);
}

static LRESULT CALLBACK OsdProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (m == WM_NCHITTEST) return HTTRANSPARENT;
    if (m == WM_TIMER) {
        if (w == TMR_OSD_HOLD) {
            KillTimer(h, TMR_OSD_HOLD);
            SetTimer(h, TMR_OSD_FADE, 15, nullptr);
        } else if (w == TMR_OSD_FADE) {
            s_osdAlpha -= 0.08f;
            if (s_osdAlpha <= 0) {
                KillTimer(h, TMR_OSD_FADE);
                ShowWindow(h, SW_HIDE);
                FreeSurface(s_osdSurf);
                TrimMemory();
            } else RenderOsd();
        }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void Radial_Init() {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = RadialProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = RADIAL_CLASS;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = OsdProc;
    wc.lpszClassName = OSD_CLASS;
    RegisterClassExW(&wc);

    DWORD ex = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    s_wnd = CreateWindowExW(ex, RADIAL_CLASS, L"Rueda Suprema", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, g_hInst, nullptr);
    s_osd = CreateWindowExW(ex | WS_EX_TRANSPARENT, OSD_CLASS, L"Rueda Suprema OSD", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, g_hInst, nullptr);
}
