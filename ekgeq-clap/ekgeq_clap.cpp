/**
 * EKG·EQ — CLAP Plugin
 * Lumina Aerospace · AuraTone Technology
 *
 * CLAP (CLever Audio Plugin) target. Shares the biquad DSP core
 * and WebView2 UI with the VST3 build. Single DLL, .clap extension.
 *
 * Parameters (mirror VST3 IDs exactly):
 *   Band 0 SUB    : gain=0,  freq=1,  Q=2
 *   Band 1 BASS   : gain=10, freq=11, Q=12
 *   Band 2 LO·MID : gain=20, freq=21, Q=22
 *   Band 3 MID    : gain=30, freq=31, Q=32
 *   Band 4 HI·MID : gain=40, freq=41, Q=42
 *   Band 5 AIR    : gain=50, freq=51, Q=52
 *   Bypass        : 100
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>

#include <clap/clap.h>
#include <clap/ext/params.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/state.h>
#include <clap/factory/plugin-factory.h>

#include "../ekgeq-plugin/source/biquad.h"
#include "../ekgeq-plugin/webview2/include/WebView2.h"

#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>
#include <atomic>
#include <algorithm>
#include <cwchar>

// ── Parameter table ───────────────────────────────────────────────────────────
struct ParamDef {
    clap_id id;
    const char* name;
    const char* module;
    double min, max, def;
};

static const ParamDef PARAMS[] = {
    // Band 0 SUB
    {  0, "SUB Gain",    "SUB",    -24, 24,   0 },
    {  1, "SUB Freq",    "SUB",     20, 20000, 60 },
    {  2, "SUB Q",       "SUB",    0.1, 10,  0.71 },
    // Band 1 BASS
    { 10, "BASS Gain",   "BASS",   -24, 24,   0 },
    { 11, "BASS Freq",   "BASS",    20, 20000, 150 },
    { 12, "BASS Q",      "BASS",   0.1, 10,   1.0 },
    // Band 2 LO·MID
    { 20, "LO·MID Gain", "LO·MID", -24, 24,   0 },
    { 21, "LO·MID Freq", "LO·MID",  20, 20000, 500 },
    { 22, "LO·MID Q",    "LO·MID", 0.1, 10,   1.0 },
    // Band 3 MID
    { 30, "MID Gain",    "MID",    -24, 24,   0 },
    { 31, "MID Freq",    "MID",     20, 20000, 1500 },
    { 32, "MID Q",       "MID",    0.1, 10,   1.0 },
    // Band 4 HI·MID
    { 40, "HI·MID Gain", "HI·MID", -24, 24,   0 },
    { 41, "HI·MID Freq", "HI·MID",  20, 20000, 4500 },
    { 42, "HI·MID Q",    "HI·MID", 0.1, 10,   1.0 },
    // Band 5 AIR
    { 50, "AIR Gain",    "AIR",    -24, 24,   0 },
    { 51, "AIR Freq",    "AIR",     20, 20000, 12000 },
    { 52, "AIR Q",       "AIR",    0.1, 10,  0.71 },
    // Global
    {100, "Bypass",      "",        0,   1,   0 },
};
static const int PARAM_COUNT = (int)(sizeof(PARAMS) / sizeof(PARAMS[0]));
static const int BAND_COUNT  = 6;

// ── Normalize helpers ─────────────────────────────────────────────────────────
static double normToGain(double n) { return -24.0 + n * 48.0; }
static double normToFreq(double n) { return 20.0 * std::pow(1000.0, n); }
static double normToQ   (double n) { return 0.1  * std::pow(100.0,  n); }
static double gainToNorm(double g) { return (g + 24.0) / 48.0; }
static double freqToNorm(double f) { return std::log(f / 20.0) / std::log(1000.0); }
static double qToNorm   (double q) { return std::log(q / 0.1)  / std::log(100.0);  }

// Band type by index
static const char* BAND_TYPES[BAND_COUNT] = {
    "lowshelf", "peaking", "peaking", "peaking", "peaking", "highshelf"
};

// ── COM callback templates (same pattern as VST3 editor) ─────────────────────
template<class I, class T, class Fn>
class CB1 final : public I {
    volatile LONG _ref = 1;
    Fn _fn;
public:
    explicit CB1(Fn fn) : _fn(std::move(fn)) {}
    ULONG STDMETHODCALLTYPE AddRef()  override { return (ULONG)InterlockedIncrement(&_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG n = InterlockedDecrement(&_ref);
        if (n == 0) delete this; return (ULONG)n;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** p) override { *p = nullptr; return E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, T* r) override { return _fn(hr, r); }
};
template<class I, class S, class A, class Fn>
class CB2 final : public I {
    volatile LONG _ref = 1;
    Fn _fn;
public:
    explicit CB2(Fn fn) : _fn(std::move(fn)) {}
    ULONG STDMETHODCALLTYPE AddRef()  override { return (ULONG)InterlockedIncrement(&_ref); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG n = InterlockedDecrement(&_ref);
        if (n == 0) delete this; return (ULONG)n;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** p) override { *p = nullptr; return E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE Invoke(S* s, A* a) override { return _fn(s, a); }
};
template<class I, class T, class Fn> CB1<I,T,Fn>* makeCB1(Fn fn) { return new CB1<I,T,Fn>(std::move(fn)); }
template<class I, class S, class A, class Fn> CB2<I,S,A,Fn>* makeCB2(Fn fn) { return new CB2<I,S,A,Fn>(std::move(fn)); }

// ── EKGEQClap — main plugin struct ───────────────────────────────────────────
struct EKGEQClap {
    clap_plugin_t         plugin;
    const clap_host_t*    host;

    // DSP state
    double sampleRate  = 44100.0;
    bool   bypass      = false;
    double normVal[19] = {};  // indexed by PARAMS[] position

    // Per-band per-channel biquad filters
    BiquadFilter filters[BAND_COUNT][2]; // [band][ch]

    // GUI state
    HWND                    guiHwnd   = nullptr;
    HWND                    parentHwnd= nullptr;
    ICoreWebView2Controller* wvCtrl   = nullptr;
    ICoreWebView2*           wv       = nullptr;
    bool                     wvReady  = false;
    HMODULE                  wvDLL    = nullptr;
    EventRegistrationToken   msgToken = {};
    EventRegistrationToken   accelToken = {};

    // ── Param helpers ────────────────────────────────────────────────────────
    int paramIndex(clap_id id) const {
        for (int i = 0; i < PARAM_COUNT; ++i)
            if (PARAMS[i].id == id) return i;
        return -1;
    }
    double paramPlain(int idx) const {
        const auto& p = PARAMS[idx];
        return p.min + normVal[idx] * (p.max - p.min);
    }
    double paramNorm(int idx) const { return normVal[idx]; }

    void rebuildFilter(int band) {
        double fs   = sampleRate;
        int gi = paramIndex(band * 10);
        int fi = paramIndex(band * 10 + 1);
        int qi = paramIndex(band * 10 + 2);
        double g = paramPlain(gi);
        double f = paramPlain(fi);
        double q = paramPlain(qi);

        BiquadCoeffs c;
        if (band == 0) c = makeLowShelf (fs, f, g, q);
        else if (band == 5) c = makeHighShelf(fs, f, g, q);
        else c = makePeaking  (fs, f, g, q);

        filters[band][0].setCoeffs(c);
        filters[band][1].setCoeffs(c);
    }

    // ── WebView2 file URI ────────────────────────────────────────────────────
    std::wstring buildHtmlUri() {
        // Use a static data-pointer to locate this DLL — avoids function-pointer cast warning
        static const char s_anchor = 0;
        HMODULE hMod = nullptr;
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&s_anchor),
            &hMod);
        wchar_t p[MAX_PATH] = {};
        GetModuleFileNameW(hMod, p, MAX_PATH);
        // .clap file is at: ...\CLAP\EKG-EQ.clap
        // HTML is at:       ...\CLAP\EKG-EQ-ui\index.html
        std::wstring path(p);
        size_t sep = path.rfind(L'\\');
        if (sep != std::wstring::npos) path.resize(sep);
        path += L"\\EKG-EQ-ui\\index.html";
        std::wstring uri = L"file:///";
        for (wchar_t c : path) {
            if (c == L'\\') uri += L'/';
            else if (c == L' ') uri += L"%20";
            else uri += c;
        }
        return uri;
    }

    // ── Send params to JS ────────────────────────────────────────────────────
    void sendParamsToJS() {
        if (!wvReady || !wv) return;
        wchar_t json[2048];
        int pos = 0;
        pos += swprintf(json + pos, 2048 - pos, L"{\"type\":\"setParams\",\"bands\":[");
        static const double DEF_FREQ[6] = {60,150,500,1500,4500,12000};
        for (int b = 0; b < 6; ++b) {
            int gi = paramIndex(b*10), fi = paramIndex(b*10+1), qi = paramIndex(b*10+2);
            double gain = paramPlain(gi);
            double freq = paramPlain(fi);
            double q    = paramPlain(qi);
            pos += swprintf(json + pos, 2048 - pos,
                L"%s{\"id\":%d,\"gain\":%.3f,\"freq\":%.2f,\"q\":%.4f}",
                b == 0 ? L"" : L",", b, gain, freq, q);
        }
        swprintf(json + pos, 2048 - pos, L"]}");
        wv->PostWebMessageAsJson(json);
    }

    // ── Handle JS → C++ ──────────────────────────────────────────────────────
    void onJSMessage(LPCWSTR jsonW) {
        std::wstring json(jsonW);
        // Find "type"
        size_t tk = json.find(L"\"type\"");
        if (tk == std::wstring::npos) return;
        size_t q1 = json.find(L'"', json.find(L':', tk) + 1);
        size_t q2 = json.find(L'"', q1 + 1);
        if (q1 == std::wstring::npos || q2 == std::wstring::npos) return;
        std::wstring type = json.substr(q1 + 1, q2 - q1 - 1);

        if (type == L"ready") { sendParamsToJS(); return; }
        if (type != L"allParams") return;

        size_t arrStart = json.find(L'[');
        if (arrStart == std::wstring::npos) return;
        size_t arrEnd = json.find(L']', arrStart);
        if (arrEnd == std::wstring::npos) return;
        std::wstring arr = json.substr(arrStart, arrEnd - arrStart + 1);

        auto parseNum = [&](const std::wstring& s, const wchar_t* key, double def) -> double {
            std::wstring needle = std::wstring(L"\"") + key + L"\"";
            size_t k = s.find(needle);
            if (k == std::wstring::npos) return def;
            size_t c = s.find(L':', k);
            if (c == std::wstring::npos) return def;
            size_t p = c + 1;
            while (p < s.size() && s[p] == L' ') ++p;
            std::wstring num;
            while (p < s.size() && (s[p] == L'-' || (s[p] >= L'0' && s[p] <= L'9') || s[p] == L'.'))
                num += s[p++];
            if (num.empty()) return def;
            try { return std::stod(num); } catch (...) { return def; }
        };

        size_t pos = 0;
        const clap_host_params_t* hostParams =
            (const clap_host_params_t*)host->get_extension(host, CLAP_EXT_PARAMS);

        while (true) {
            size_t oStart = arr.find(L'{', pos);
            if (oStart == std::wstring::npos) break;
            size_t oEnd = arr.find(L'}', oStart);
            if (oEnd == std::wstring::npos) break;
            std::wstring obj = arr.substr(oStart, oEnd - oStart + 1);

            int id = (int)parseNum(obj, L"id", -1);
            if (id >= 0 && id < 6) {
                static const double DFREQ[6] = {60,150,500,1500,4500,12000};
                double gain = std::max(-24.0, std::min(24.0, parseNum(obj, L"gain", 0.0)));
                double freq = std::max(20.0, std::min(20000.0, parseNum(obj, L"freq", DFREQ[id])));
                double q    = std::max(0.1,  std::min(10.0,   parseNum(obj, L"q",    1.0)));

                auto update = [&](clap_id pid, double plain, double minV, double maxV) {
                    int idx = paramIndex(pid);
                    if (idx < 0) return;
                    double newNorm = (plain - minV) / (maxV - minV);
                    newNorm = std::max(0.0, std::min(1.0, newNorm));
                    if (std::abs(newNorm - normVal[idx]) > 0.0001) {
                        normVal[idx] = newNorm;
                        if (hostParams)
                            hostParams->request_flush(host);
                    }
                };
                update(id * 10,     gain, -24,   24);
                update(id * 10 + 1, freq,  20, 20000);
                update(id * 10 + 2, q,    0.1,   10);
                rebuildFilter(id);
            }
            pos = oEnd + 1;
        }
    }
};

// ── CLAP plugin callbacks ─────────────────────────────────────────────────────

static bool ekgeq_init(const clap_plugin_t* p) {
    auto* self = (EKGEQClap*)p->plugin_data;
    // Initialize normVal to defaults
    for (int i = 0; i < PARAM_COUNT; ++i) {
        const auto& pd = PARAMS[i];
        self->normVal[i] = (pd.def - pd.min) / (pd.max - pd.min);
    }
    for (int b = 0; b < BAND_COUNT; ++b)
        self->rebuildFilter(b);
    return true;
}

static void ekgeq_destroy(const clap_plugin_t* p) {
    auto* self = (EKGEQClap*)p->plugin_data;
    delete self;
}

static bool ekgeq_activate(const clap_plugin_t* p, double sampleRate,
                            uint32_t /*minFrames*/, uint32_t /*maxFrames*/) {
    auto* self = (EKGEQClap*)p->plugin_data;
    self->sampleRate = sampleRate;
    for (int b = 0; b < BAND_COUNT; ++b)
        self->rebuildFilter(b);
    return true;
}

static void ekgeq_deactivate(const clap_plugin_t* /*p*/) {}
static bool ekgeq_start_processing(const clap_plugin_t* /*p*/) { return true; }
static void ekgeq_stop_processing(const clap_plugin_t* /*p*/) {}
static void ekgeq_reset(const clap_plugin_t* p) {
    auto* self = (EKGEQClap*)p->plugin_data;
    for (int b = 0; b < BAND_COUNT; ++b)
        for (int ch = 0; ch < 2; ++ch)
            self->filters[b][ch].reset();
}

static clap_process_status ekgeq_process(const clap_plugin_t* p, const clap_process_t* proc) {
    auto* self = (EKGEQClap*)p->plugin_data;

    // Handle parameter events from host
    uint32_t ev = 0;
    uint32_t numEvents = proc->in_events->size(proc->in_events);
    uint32_t frame = 0;

    for (; frame < proc->frames_count; ) {
        // Process events up to current frame
        while (ev < numEvents) {
            const clap_event_header_t* hdr = proc->in_events->get(proc->in_events, ev);
            if (hdr->time > frame) break;
            if (hdr->type == CLAP_EVENT_PARAM_VALUE) {
                const auto* evt = (const clap_event_param_value_t*)hdr;
                int idx = self->paramIndex((clap_id)evt->param_id);
                if (idx >= 0) {
                    self->normVal[idx] = (evt->value - PARAMS[idx].min) /
                                         (PARAMS[idx].max - PARAMS[idx].min);
                    if ((clap_id)evt->param_id == 100) {
                        self->bypass = (evt->value >= 0.5);
                    } else {
                        int band = (int)evt->param_id / 10;
                        if (band >= 0 && band < BAND_COUNT)
                            self->rebuildFilter(band);
                    }
                }
            }
            ++ev;
        }

        // Process audio for this frame
        if (proc->audio_inputs_count > 0 && proc->audio_outputs_count > 0) {
            const float* in0  = proc->audio_inputs[0].data32[0];
            const float* in1  = proc->audio_inputs[0].data32[1 < (int)proc->audio_inputs[0].channel_count ? 1 : 0];
            float*       out0 = proc->audio_outputs[0].data32[0];
            float*       out1 = proc->audio_outputs[0].data32[1 < (int)proc->audio_outputs[0].channel_count ? 1 : 0];

            if (self->bypass || proc->audio_inputs[0].constant_mask & 1) {
                out0[frame] = in0[frame];
                out1[frame] = in1[frame];
            } else {
                double l = in0[frame], r = in1[frame];
                for (int b = 0; b < BAND_COUNT; ++b) {
                    l = self->filters[b][0].process(l);
                    r = self->filters[b][1].process(r);
                }
                out0[frame] = (float)l;
                out1[frame] = (float)r;
            }
        }
        ++frame;
    }

    return CLAP_PROCESS_CONTINUE;
}

static const void* ekgeq_get_extension(const clap_plugin_t* p, const char* id);
static void ekgeq_on_main_thread(const clap_plugin_t* /*p*/) {}

// ── Audio ports ───────────────────────────────────────────────────────────────
static uint32_t ap_count(const clap_plugin_t*, bool /*isInput*/) { return 1; }
static bool ap_get(const clap_plugin_t*, uint32_t /*idx*/, bool /*isInput*/, clap_audio_port_info_t* info) {
    info->id             = 0;
    strcpy(info->name, "Stereo");
    info->flags          = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count  = 2;
    info->port_type      = CLAP_PORT_STEREO;
    info->in_place_pair  = CLAP_INVALID_ID;
    return true;
}
static const clap_plugin_audio_ports_t EXT_AUDIO_PORTS = { ap_count, ap_get };

// ── Parameters extension ──────────────────────────────────────────────────────
static uint32_t par_count(const clap_plugin_t*) { return (uint32_t)PARAM_COUNT; }

static bool par_get_info(const clap_plugin_t*, uint32_t idx, clap_param_info_t* info) {
    if ((int)idx >= PARAM_COUNT) return false;
    const auto& p = PARAMS[idx];
    info->id    = p.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
    if (p.id == 100) info->flags |= CLAP_PARAM_IS_BYPASS;
    strncpy(info->name,   p.name,   CLAP_NAME_SIZE - 1);
    strncpy(info->module, p.module, CLAP_PATH_SIZE - 1);
    info->min_value     = p.min;
    info->max_value     = p.max;
    info->default_value = p.def;
    info->cookie        = nullptr;
    return true;
}

static bool par_get_value(const clap_plugin_t* p, clap_id id, double* val) {
    auto* self = (EKGEQClap*)p->plugin_data;
    int idx = self->paramIndex(id);
    if (idx < 0) return false;
    *val = PARAMS[idx].min + self->normVal[idx] * (PARAMS[idx].max - PARAMS[idx].min);
    return true;
}

static bool par_value_to_text(const clap_plugin_t* p, clap_id id, double val,
                               char* buf, uint32_t size) {
    int idx = ((EKGEQClap*)p->plugin_data)->paramIndex(id);
    if (idx < 0) return false;
    const auto& pd = PARAMS[idx];
    if (id == 100) snprintf(buf, size, "%s", val >= 0.5 ? "Bypassed" : "Active");
    else if ((id % 10) == 0) snprintf(buf, size, "%.1f dB", val);
    else if ((id % 10) == 1) snprintf(buf, size, val >= 1000 ? "%.1f kHz" : "%.0f Hz", val >= 1000 ? val/1000 : val);
    else snprintf(buf, size, "%.2f", val);
    return true;
}

static bool par_text_to_value(const clap_plugin_t*, clap_id, const char* txt, double* val) {
    *val = atof(txt);
    return true;
}

static void par_flush(const clap_plugin_t* p, const clap_input_events_t* in,
                      const clap_output_events_t* /*out*/) {
    auto* self = (EKGEQClap*)p->plugin_data;
    uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* hdr = in->get(in, i);
        if (hdr->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* evt = (const clap_event_param_value_t*)hdr;
            int idx = self->paramIndex((clap_id)evt->param_id);
            if (idx >= 0) {
                self->normVal[idx] = (evt->value - PARAMS[idx].min) /
                                     (PARAMS[idx].max - PARAMS[idx].min);
                if ((clap_id)evt->param_id == 100)
                    self->bypass = (evt->value >= 0.5);
                else {
                    int band = (int)evt->param_id / 10;
                    if (band >= 0 && band < BAND_COUNT)
                        self->rebuildFilter(band);
                }
            }
        }
    }
    // Push updated state to WebView2 if ready
    self->sendParamsToJS();
}

static const clap_plugin_params_t EXT_PARAMS = {
    par_count, par_get_info, par_get_value, par_value_to_text, par_text_to_value, par_flush
};

// ── State (save/load) ─────────────────────────────────────────────────────────
static bool state_save(const clap_plugin_t* p, const clap_ostream_t* stream) {
    auto* self = (EKGEQClap*)p->plugin_data;
    // Write normVal array as raw doubles
    uint32_t written = 0;
    while (written < sizeof(self->normVal)) {
        int64_t n = stream->write(stream, (const char*)self->normVal + written,
                                  sizeof(self->normVal) - written);
        if (n <= 0) return false;
        written += (uint32_t)n;
    }
    return true;
}

static bool state_load(const clap_plugin_t* p, const clap_istream_t* stream) {
    auto* self = (EKGEQClap*)p->plugin_data;
    double buf[PARAM_COUNT] = {};
    uint32_t read = 0;
    while (read < sizeof(buf)) {
        int64_t n = stream->read(stream, (char*)buf + read, sizeof(buf) - read);
        if (n <= 0) break;
        read += (uint32_t)n;
    }
    if (read >= sizeof(self->normVal))
        memcpy(self->normVal, buf, sizeof(self->normVal));
    for (int b = 0; b < BAND_COUNT; ++b) self->rebuildFilter(b);
    self->sendParamsToJS();
    return true;
}

static const clap_plugin_state_t EXT_STATE = { state_save, state_load };

// ── GUI extension ─────────────────────────────────────────────────────────────
static constexpr uint32_t GUI_W = 1024;
static constexpr uint32_t GUI_H =  540;

static const wchar_t* CLAP_WND_CLASS = L"EKGEQCLAPEditor";
static bool s_clapClassRegistered = false;

static LRESULT CALLBACK ClapWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    EKGEQClap* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lp;
        self = (EKGEQClap*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (EKGEQClap*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }

    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (self && !self->wvReady) {
            RECT r; GetClientRect(hwnd, &r);
            HBRUSH br = CreateSolidBrush(RGB(3,8,6));
            FillRect(hdc, &r, br); DeleteObject(br);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SETFOCUS:
        if (self && self->wvCtrl)
            self->wvCtrl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        return 0;
    case WM_MOUSEACTIVATE:
        SetFocus(hwnd); return MA_ACTIVATE;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        SetFocus(hwnd);
        return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_SIZE:
        if (self && self->wvCtrl) {
            RECT r { 0, 0, (LONG)LOWORD(lp), (LONG)HIWORD(lp) };
            self->wvCtrl->put_Bounds(r);
        }
        return 0;
    case WM_DESTROY: return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

typedef HRESULT (*WV2CreateFn)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

static WV2CreateFn loadWV2(HMODULE* outDll) {
    const wchar_t* candidates[] = {
        L"WebView2Loader.dll",
        L"C:\\Program Files\\Image-Line\\FL Studio 2025\\WebView2Loader.dll",
        nullptr
    };
    for (int i = 0; candidates[i]; ++i) {
        *outDll = LoadLibraryW(candidates[i]);
        if (*outDll) break;
    }
    if (!*outDll) return nullptr;
    auto fn = (WV2CreateFn)GetProcAddress(*outDll, "CreateCoreWebView2EnvironmentWithOptions");
    if (!fn) { FreeLibrary(*outDll); *outDll = nullptr; }
    return fn;
}

static bool gui_is_api_supported(const clap_plugin_t*, const char* api, bool is_floating) {
    return !is_floating && strcmp(api, CLAP_WINDOW_API_WIN32) == 0;
}
static bool gui_get_preferred_api(const clap_plugin_t*, const char** api, bool* is_floating) {
    *api = CLAP_WINDOW_API_WIN32; *is_floating = false; return true;
}
static bool gui_create(const clap_plugin_t* p, const char* /*api*/, bool /*floating*/) {
    auto* self = (EKGEQClap*)p->plugin_data;
    if (!s_clapClassRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc); wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = ClapWndProc; wc.hInstance = GetModuleHandle(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = CLAP_WND_CLASS;
        RegisterClassExW(&wc); s_clapClassRegistered = true;
    }
    return true;
}
static void gui_destroy(const clap_plugin_t* p) {
    auto* self = (EKGEQClap*)p->plugin_data;
    if (self->wv) {
        self->wv->remove_WebMessageReceived(self->msgToken);
        self->wv->Release(); self->wv = nullptr;
    }
    if (self->wvCtrl) {
        self->wvCtrl->remove_AcceleratorKeyPressed(self->accelToken);
        self->wvCtrl->Close(); self->wvCtrl->Release(); self->wvCtrl = nullptr;
    }
    if (self->guiHwnd) { DestroyWindow(self->guiHwnd); self->guiHwnd = nullptr; }
    if (self->wvDLL)   { FreeLibrary(self->wvDLL); self->wvDLL = nullptr; }
    self->wvReady = false;
}
static bool gui_set_scale(const clap_plugin_t*, double) { return false; }
static bool gui_get_size(const clap_plugin_t*, uint32_t* w, uint32_t* h) {
    *w = GUI_W; *h = GUI_H; return true;
}
static bool gui_can_resize(const clap_plugin_t*) { return false; }
static bool gui_get_resize_hints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
static bool gui_adjust_size(const clap_plugin_t*, uint32_t* w, uint32_t* h) {
    *w = GUI_W; *h = GUI_H; return true;
}
static bool gui_set_size(const clap_plugin_t*, uint32_t, uint32_t) { return true; }
static bool gui_set_parent(const clap_plugin_t* p, const clap_window_t* win) {
    auto* self = (EKGEQClap*)p->plugin_data;
    HWND parentHwnd = (HWND)win->win32;

    self->guiHwnd = CreateWindowExW(
        0, CLAP_WND_CLASS, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, GUI_W, GUI_H, parentHwnd, nullptr, GetModuleHandle(nullptr), self);
    if (!self->guiHwnd) return false;

    WV2CreateFn createEnv = loadWV2(&self->wvDLL);
    if (!createEnv) return true; // No WebView2 — black window, non-fatal

    createEnv(nullptr, nullptr, nullptr,
        makeCB1<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
                ICoreWebView2Environment>(
            [self](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(hr) || !env || !self->guiHwnd) return hr;
                return env->CreateCoreWebView2Controller(self->guiHwnd,
                    makeCB1<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
                             ICoreWebView2Controller>(
                        [self](HRESULT hr, ICoreWebView2Controller* ctrl) -> HRESULT {
                            if (FAILED(hr) || !ctrl) return hr;
                            self->wvCtrl = ctrl; self->wvCtrl->AddRef();
                            RECT r{0,0,GUI_W,GUI_H};
                            self->wvCtrl->put_Bounds(r);
                            self->wvCtrl->put_IsVisible(TRUE);
                            self->wvCtrl->get_CoreWebView2(&self->wv);
                            if (!self->wv) return E_FAIL;

                            ICoreWebView2Settings* s = nullptr;
                            if (SUCCEEDED(self->wv->get_Settings(&s)) && s) {
                                s->put_IsStatusBarEnabled(FALSE);
                                s->put_AreDevToolsEnabled(FALSE);
                                s->put_IsBuiltInErrorPageEnabled(FALSE);
                                s->Release();
                            }

                            self->wv->add_WebMessageReceived(
                                makeCB2<ICoreWebView2WebMessageReceivedEventHandler,
                                         ICoreWebView2,
                                         ICoreWebView2WebMessageReceivedEventArgs>(
                                    [self](ICoreWebView2*,
                                           ICoreWebView2WebMessageReceivedEventArgs* args)
                                        -> HRESULT {
                                        LPWSTR json = nullptr;
                                        if (SUCCEEDED(args->get_WebMessageAsJson(&json)) && json) {
                                            self->onJSMessage(json);
                                            CoTaskMemFree(json);
                                        }
                                        return S_OK;
                                    }), &self->msgToken);

                            self->wvCtrl->add_AcceleratorKeyPressed(
                                makeCB2<ICoreWebView2AcceleratorKeyPressedEventHandler,
                                         ICoreWebView2Controller,
                                         ICoreWebView2AcceleratorKeyPressedEventArgs>(
                                    [](ICoreWebView2Controller*,
                                       ICoreWebView2AcceleratorKeyPressedEventArgs* args)
                                        -> HRESULT {
                                        args->put_Handled(FALSE);
                                        return S_OK;
                                    }), &self->accelToken);

                            self->wvReady = true;
                            self->wvCtrl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);

                            std::wstring uri = self->buildHtmlUri();
                            self->wv->Navigate(uri.c_str());
                            return S_OK;
                        }));
            }));
    return true;
}
static bool gui_set_transient(const clap_plugin_t*, const clap_window_t*) { return false; }
static void gui_suggest_title(const clap_plugin_t*, const char*) {}
static bool gui_show(const clap_plugin_t* p) {
    auto* self = (EKGEQClap*)p->plugin_data;
    if (self->guiHwnd) ShowWindow(self->guiHwnd, SW_SHOW);
    if (self->wvCtrl)  self->wvCtrl->put_IsVisible(TRUE);
    return true;
}
static bool gui_hide(const clap_plugin_t* p) {
    auto* self = (EKGEQClap*)p->plugin_data;
    if (self->guiHwnd) ShowWindow(self->guiHwnd, SW_HIDE);
    if (self->wvCtrl)  self->wvCtrl->put_IsVisible(FALSE);
    return true;
}

static const clap_plugin_gui_t EXT_GUI = {
    gui_is_api_supported, gui_get_preferred_api, gui_create, gui_destroy,
    gui_set_scale, gui_get_size, gui_can_resize, gui_get_resize_hints,
    gui_adjust_size, gui_set_size, gui_set_parent, gui_set_transient,
    gui_suggest_title, gui_show, gui_hide
};

// ── get_extension dispatch ────────────────────────────────────────────────────
static const void* ekgeq_get_extension(const clap_plugin_t* /*p*/, const char* id) {
    if (!strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &EXT_AUDIO_PORTS;
    if (!strcmp(id, CLAP_EXT_PARAMS))      return &EXT_PARAMS;
    if (!strcmp(id, CLAP_EXT_STATE))       return &EXT_STATE;
    if (!strcmp(id, CLAP_EXT_GUI))         return &EXT_GUI;
    return nullptr;
}

// ── Plugin descriptor ─────────────────────────────────────────────────────────
static const char* const EKGEQ_FEATURES[] = {
    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
    CLAP_PLUGIN_FEATURE_EQUALIZER,
    nullptr
};

static const clap_plugin_descriptor_t EKGEQ_DESC = {
    CLAP_VERSION_INIT,
    "com.luminousworks.ekgeq",
    "EKG\xB7\x45Q",
    "Lumina Aerospace",
    "https://luminousworks.com",
    "",
    "",
    "1.0.0",
    "Master EQ \xE2\x80\x94 ECG band topology. AuraTone Technology.",
    EKGEQ_FEATURES
};

// ── Plugin factory ────────────────────────────────────────────────────────────
static uint32_t factory_count(const clap_plugin_factory_t*) { return 1; }

static const clap_plugin_descriptor_t* factory_get_descriptor(
    const clap_plugin_factory_t*, uint32_t /*idx*/) { return &EKGEQ_DESC; }

static const clap_plugin_t* factory_create(const clap_plugin_factory_t*,
                                            const clap_host_t* host,
                                            const char* plugin_id) {
    if (strcmp(plugin_id, EKGEQ_DESC.id) != 0) return nullptr;
    auto* self = new EKGEQClap();
    self->host = host;
    self->plugin.desc        = &EKGEQ_DESC;
    self->plugin.plugin_data = self;
    self->plugin.init             = ekgeq_init;
    self->plugin.destroy          = ekgeq_destroy;
    self->plugin.activate         = ekgeq_activate;
    self->plugin.deactivate       = ekgeq_deactivate;
    self->plugin.start_processing = ekgeq_start_processing;
    self->plugin.stop_processing  = ekgeq_stop_processing;
    self->plugin.reset            = ekgeq_reset;
    self->plugin.process          = ekgeq_process;
    self->plugin.get_extension    = ekgeq_get_extension;
    self->plugin.on_main_thread   = ekgeq_on_main_thread;
    return &self->plugin;
}

static const clap_plugin_factory_t PLUGIN_FACTORY = {
    factory_count, factory_get_descriptor, factory_create
};

// ── CLAP entry point (exported symbol) ───────────────────────────────────────
static bool entry_init(const char* /*path*/) { CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); return true; }
static void entry_deinit() { CoUninitialize(); }
static const void* entry_get_factory(const char* factory_id) {
    if (!strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID)) return &PLUGIN_FACTORY;
    return nullptr;
}

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT, entry_init, entry_deinit, entry_get_factory
};
