// Información de lo que suena y salto en la canción (WinRT, Windows 10 1809+) - HECHO POR @BYALVARO__
#include "common.h"
#include <unknwn.h>
#include <chrono>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

#pragma comment(lib, "windowsapp.lib")

using namespace winrt::Windows::Media::Control;

static GlobalSystemMediaTransportControlsSessionManager s_mgr{ nullptr };
static double s_seekTarget = -1;
static DWORD  s_seekTime = 0;

void Media_InitApartment() {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
}

static GlobalSystemMediaTransportControlsSession CurrentSession() {
    try {
        if (!s_mgr) s_mgr = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        if (s_mgr) return s_mgr.GetCurrentSession();
    } catch (...) {
    }
    return nullptr;
}

static double Seconds(winrt::Windows::Foundation::TimeSpan t) {
    return std::chrono::duration<double>(t).count();
}

MediaInfo Media_Get(bool withProps) {
    MediaInfo mi;
    try {
        auto s = CurrentSession();
        if (!s) return mi;
        if (withProps) {
            auto p = s.TryGetMediaPropertiesAsync().get();
            if (p) {
                mi.title = p.Title().c_str();
                mi.artist = p.Artist().c_str();
            }
        }
        auto pb = s.GetPlaybackInfo();
        mi.playing = pb.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
        mi.canSeek = pb.Controls().IsPlaybackPositionEnabled();

        auto tl = s.GetTimelineProperties();
        auto start = tl.StartTime();
        auto pos = tl.Position();
        auto end = tl.EndTime();
        if (mi.playing) {
            auto elapsed = winrt::clock::now() - tl.LastUpdatedTime();
            if (elapsed.count() > 0 && elapsed < std::chrono::hours(6)) pos += elapsed;
        }
        mi.dur = Seconds(end - start);
        mi.pos = Seconds(pos - start);
        if (mi.pos < 0) mi.pos = 0;
        if (mi.dur > 0 && mi.pos > mi.dur) mi.pos = mi.dur;
        mi.has = true;
    } catch (...) {
        mi = MediaInfo();
    }
    return mi;
}

double Media_Seek(int dir, double stepSec, double curPos, double dur) {
    auto s = CurrentSession();
    if (!s) return curPos;
    DWORD now = GetTickCount();
    double base = (s_seekTarget >= 0 && now - s_seekTime < 1500) ? s_seekTarget : curPos;
    double t = base + dir * stepSec;
    if (t < 0) t = 0;
    if (dur > 0 && t > dur - 1) t = dur - 1;
    if (t < 0) t = 0;
    s_seekTarget = t;
    s_seekTime = now;
    try {
        auto start = s.GetTimelineProperties().StartTime();
        int64_t ticks = start.count() + (int64_t)(t * 10000000.0);
        auto op = s.TryChangePlaybackPositionAsync(ticks);   // sin esperar: no bloquea la rueda
        (void)op;
    } catch (...) {
    }
    return t;
}
