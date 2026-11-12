// Core Audio: volumen general, micro, mezclador por app y salida - HECHO POR @BYALVARO__
#include "common.h"
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <audiopolicy.h>
#include <wrl/client.h>
#include <cwctype>
#include <cwchar>
#include <cstdio>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "version.lib")

using Microsoft::WRL::ComPtr;

// PKEY_Device_FriendlyName (definida aquí para no depender de INITGUID)
static const PROPERTYKEY kFriendlyName = {
    { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

// Interfaz no documentada (la usan todos los cambiadores de salida de audio)
MIDL_INTERFACE("f8679f50-850a-41cf-9c72-430f290290c8")
IPolicyConfig : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR, ERole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};
static const CLSID kPolicyConfigClient = {
    0x870af99c, 0x171d, 0x4f9e, { 0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9 } };

static ComPtr<IMMDeviceEnumerator> Enumerator() {
    ComPtr<IMMDeviceEnumerator> e;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&e));
    return e;
}

static ComPtr<IMMDevice> DefaultDevice(EDataFlow flow) {
    ComPtr<IMMDevice> d;
    ComPtr<IMMDeviceEnumerator> e = Enumerator();
    if (e) e->GetDefaultAudioEndpoint(flow, eConsole, &d);
    return d;
}

static ComPtr<IAudioEndpointVolume> EndpointVolume(EDataFlow flow) {
    ComPtr<IAudioEndpointVolume> v;
    ComPtr<IMMDevice> d = DefaultDevice(flow);
    if (d) d->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, (void**)v.GetAddressOf());
    return v;
}

static float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// ---------- Volumen general ----------
float Audio_GetMaster() {
    float v = 0; auto ep = EndpointVolume(eRender);
    if (ep) ep->GetMasterVolumeLevelScalar(&v);
    return v;
}
void Audio_SetMaster(float v) {
    auto ep = EndpointVolume(eRender);
    if (ep) ep->SetMasterVolumeLevelScalar(Clamp01(v), nullptr);
}
bool Audio_GetMute() {
    BOOL m = FALSE; auto ep = EndpointVolume(eRender);
    if (ep) ep->GetMute(&m);
    return m != FALSE;
}
void Audio_SetMute(bool m) {
    auto ep = EndpointVolume(eRender);
    if (ep) ep->SetMute(m ? TRUE : FALSE, nullptr);
}

// ---------- Micrófono ----------
float Audio_GetMic() {
    float v = 0; auto ep = EndpointVolume(eCapture);
    if (ep) ep->GetMasterVolumeLevelScalar(&v);
    return v;
}
void Audio_SetMic(float v) {
    auto ep = EndpointVolume(eCapture);
    if (ep) ep->SetMasterVolumeLevelScalar(Clamp01(v), nullptr);
}
bool Audio_GetMicMute() {
    BOOL m = FALSE; auto ep = EndpointVolume(eCapture);
    if (ep) ep->GetMute(&m);
    return m != FALSE;
}
void Audio_SetMicMute(bool m) {
    auto ep = EndpointVolume(eCapture);
    if (ep) ep->SetMute(m ? TRUE : FALSE, nullptr);
}

// ---------- Nombre bonito de un proceso ("Spotify", "Discord"...) ----------
static std::wstring FileDescription(const std::wstring& path) {
    DWORD dummy = 0;
    DWORD size = GetFileVersionInfoSizeW(path.c_str(), &dummy);
    if (!size) return L"";
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return L"";
    struct LangCp { WORD lang, cp; } *tr = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", (void**)&tr, &len) || len < sizeof(LangCp))
        return L"";
    wchar_t q[128];
    swprintf(q, 128, L"\\StringFileInfo\\%04x%04x\\FileDescription", tr[0].lang, tr[0].cp);
    wchar_t* desc = nullptr;
    if (VerQueryValueW(data.data(), q, (void**)&desc, &len) && desc && len > 1) return desc;
    return L"";
}

static std::wstring ProcessName(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return L"";
    wchar_t buf[MAX_PATH] = {};
    DWORD sz = MAX_PATH;
    std::wstring result;
    if (QueryFullProcessImageNameW(h, 0, buf, &sz)) {
        std::wstring path(buf);
        result = FileDescription(path);
        if (result.empty() || result.size() > 28) {
            size_t s = path.find_last_of(L"\\/");
            std::wstring file = (s == std::wstring::npos) ? path : path.substr(s + 1);
            size_t dot = file.find_last_of(L'.');
            if (dot != std::wstring::npos) file = file.substr(0, dot);
            if (!file.empty()) file[0] = (wchar_t)towupper(file[0]);
            result = file;
        }
    }
    CloseHandle(h);
    return result;
}

// ---------- Mezclador ----------
template <class F>
static void ForEachSession(F fn) {
    ComPtr<IMMDevice> d = DefaultDevice(eRender);
    if (!d) return;
    ComPtr<IAudioSessionManager2> mgr;
    if (FAILED(d->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)mgr.GetAddressOf())))
        return;
    ComPtr<IAudioSessionEnumerator> se;
    if (FAILED(mgr->GetSessionEnumerator(&se))) return;
    int n = 0;
    se->GetCount(&n);
    for (int i = 0; i < n; i++) {
        ComPtr<IAudioSessionControl> c;
        if (FAILED(se->GetSession(i, &c))) continue;
        AudioSessionState st;
        if (SUCCEEDED(c->GetState(&st)) && st == AudioSessionStateExpired) continue;
        ComPtr<IAudioSessionControl2> c2;
        ComPtr<ISimpleAudioVolume> sv;
        if (FAILED(c.As(&c2)) || FAILED(c.As(&sv))) continue;
        DWORD pid = 0;
        c2->GetProcessId(&pid);
        bool sys = c2->IsSystemSoundsSession() == S_OK;
        fn(c2.Get(), sv.Get(), pid, sys);
    }
}

std::vector<AppSession> Audio_ListSessions() {
    std::vector<AppSession> out;
    ForEachSession([&](IAudioSessionControl2* c2, ISimpleAudioVolume* sv, DWORD pid, bool sys) {
        for (auto& s : out) if (s.pid == pid && s.system == sys) return;   // misma app, varias sesiones
        AppSession a;
        a.pid = pid;
        a.system = sys;
        float v = 1; BOOL m = FALSE;
        sv->GetMasterVolume(&v);
        sv->GetMute(&m);
        a.vol = v; a.mute = m != FALSE;
        if (sys) a.name = L"Sonidos del sistema";
        else {
            a.name = ProcessName(pid);
            if (a.name.empty()) {
                LPWSTR dn = nullptr;
                if (SUCCEEDED(c2->GetDisplayName(&dn)) && dn) { a.name = dn; CoTaskMemFree(dn); }
            }
            if (a.name.empty()) a.name = L"App " + std::to_wstring(pid);
        }
        out.push_back(a);
    });
    // Sonidos del sistema al final, el resto por orden alfabético
    std::stable_sort(out.begin(), out.end(), [](const AppSession& a, const AppSession& b) {
        if (a.system != b.system) return !a.system;
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return out;
}

void Audio_SetSessionVolume(DWORD pid, bool system, float v) {
    v = Clamp01(v);
    ForEachSession([&](IAudioSessionControl2*, ISimpleAudioVolume* sv, DWORD p, bool sys) {
        if (p == pid && sys == system) {
            sv->SetMasterVolume(v, nullptr);
            if (v > 0) sv->SetMute(FALSE, nullptr);
        }
    });
}

// ---------- Dispositivos de salida ----------
static std::wstring DeviceName(IMMDevice* d) {
    std::wstring name;
    ComPtr<IPropertyStore> ps;
    if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps))) {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        if (SUCCEEDED(ps->GetValue(kFriendlyName, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal) name = pv.pwszVal;
        PropVariantClear(&pv);
    }
    return name;
}

static std::wstring DeviceId(IMMDevice* d) {
    std::wstring id;
    LPWSTR p = nullptr;
    if (SUCCEEDED(d->GetId(&p)) && p) { id = p; CoTaskMemFree(p); }
    return id;
}

std::vector<AudioDevice> Audio_ListOutputs() {
    std::vector<AudioDevice> out;
    ComPtr<IMMDeviceEnumerator> e = Enumerator();
    if (!e) return out;
    std::wstring defId;
    ComPtr<IMMDevice> def;
    if (SUCCEEDED(e->GetDefaultAudioEndpoint(eRender, eConsole, &def))) defId = DeviceId(def.Get());
    ComPtr<IMMDeviceCollection> col;
    if (FAILED(e->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col))) return out;
    UINT n = 0;
    col->GetCount(&n);
    for (UINT i = 0; i < n; i++) {
        ComPtr<IMMDevice> d;
        if (FAILED(col->Item(i, &d))) continue;
        AudioDevice ad;
        ad.id = DeviceId(d.Get());
        ad.name = DeviceName(d.Get());
        if (ad.name.empty()) ad.name = L"Dispositivo " + std::to_wstring(i + 1);
        ad.isDefault = (ad.id == defId);
        out.push_back(ad);
    }
    return out;
}

bool Audio_SetDefaultOutput(const std::wstring& id) {
    ComPtr<IPolicyConfig> pc;
    if (FAILED(CoCreateInstance(kPolicyConfigClient, nullptr, CLSCTX_ALL, __uuidof(IPolicyConfig),
                                (void**)pc.GetAddressOf())))
        return false;
    HRESULT h1 = pc->SetDefaultEndpoint(id.c_str(), eConsole);
    pc->SetDefaultEndpoint(id.c_str(), eMultimedia);
    pc->SetDefaultEndpoint(id.c_str(), eCommunications);
    return SUCCEEDED(h1);
}

std::wstring Audio_DefaultOutputName() {
    ComPtr<IMMDevice> d = DefaultDevice(eRender);
    if (!d) return L"Sin salida de audio";
    return DeviceName(d.Get());
}
