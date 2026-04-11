/**
 * EKG·EQ — CLAP Plugin  (24 bands + ARIA C·AUTO)
 * Lumina Aerospace · AuraTone Technology
 *
 * CLAP (CLever Audio Plugin) target. Shares the biquad DSP core,
 * 24-band topology (ekgeq_ids.h), and WebView2 UI with the VST3 build.
 * Single DLL, .clap extension.
 *
 * C·AUTO: ARIA PortCls/KS-Direct loopback analysis drives per-band
 * spectral balance correction in real time.
 *
 * Parameters (mirror VST3 IDs exactly):
 *   Band N gain  = N * 10 + 0   (N = 0..23)
 *   Band N freq  = N * 10 + 1
 *   Band N Q     = N * 10 + 2
 *   Bypass       = 1000
 *   TLT          = 1001
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
#include "../ekgeq-plugin/source/ekgeq_band_defs.h"
#include "../ekgeq-plugin/webview2/include/WebView2.h"

#include <aria/aria.h>

#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <cwchar>

// ════════════════════════════════════════════════════════════════════════════
// AriaAnalyzer — shared singleton, same pattern as VST3 processor
// ════════════════════════════════════════════════════════════════════════════
namespace {

struct AriaAnalyzerClap {
    std::atomic<float> loopbandEnv[kNumBands];
    std::atomic<int>   refCount{0};
    std::mutex         initMtx;
    bool               ariaRunning = false;
    BiquadFilter       detL[kNumBands], detR[kNumBands];
    float              detEnv[kNumBands] = {};
    bool               filtersBuilt = false;

    AriaAnalyzerClap() {
        for (int i = 0; i < kNumBands; ++i)
            loopbandEnv[i].store(0.0f, std::memory_order_relaxed);
    }

    void buildFilters(double sr) {
        for (int i = 0; i < kNumBands; ++i) {
            BiquadCoeffs c = makeBandpass(sr, kBandDefs[i].defaultFreq, 2.0);
            detL[i].setCoeffs(c); detL[i].reset();
            detR[i].setCoeffs(c); detR[i].reset();
        }
        filtersBuilt = true;
    }

    void acquire() {
        if (refCount.fetch_add(1) == 0) {
            std::lock_guard<std::mutex> lk(initMtx);
            start();
        }
    }

    void release() {
        if (refCount.fetch_sub(1) == 1) {
            std::lock_guard<std::mutex> lk(initMtx);
            stop();
        }
    }

    void start() {
        if (ariaRunning) return;
        if (aria::init() != aria::Error::None) return;
        aria::StreamConfig cfg{};
        cfg.sample_rate = 48000; cfg.channels = 2;
        cfg.buffer_frames = 480; cfg.loopback = true;
        if (aria::capture::open_loopback(cfg) != aria::Error::None) {
            aria::shutdown(); return;
        }
        buildFilters(48000.0);
        if (aria::capture::start([this](const aria::AudioBuffer& buf) {
                onBuffer(buf); }) != aria::Error::None) {
            aria::capture::close(); aria::shutdown(); return;
        }
        ariaRunning = true;
    }

    void stop() {
        if (!ariaRunning) return;
        aria::capture::stop(); aria::capture::close(); aria::shutdown();
        ariaRunning = false;
    }

    void onBuffer(const aria::AudioBuffer& buf) noexcept {
        if (!filtersBuilt || buf.format != aria::SampleFormat::Float32 || !buf.frame_count) return;
        const float* data = static_cast<const float*>(buf.data);
        const uint32_t ch = buf.channels, n = buf.frame_count;
        double accL[kNumBands] = {}, accR[kNumBands] = {};
        for (uint32_t s = 0; s < n; ++s) {
            double xL = data[s * ch], xR = ch > 1 ? data[s * ch + 1] : xL;
            for (int i = 0; i < kNumBands; ++i) {
                double yL = detL[i].process(xL), yR = detR[i].process(xR);
                accL[i] += yL * yL; accR[i] += yR * yR;
            }
        }
        const double kA = 0.96, inv2n = 1.0 / (2.0 * n);
        for (int i = 0; i < kNumBands; ++i) {
            float rms = std::sqrt((float)((accL[i] + accR[i]) * inv2n));
            detEnv[i] = (float)(kA * detEnv[i] + (1.0 - kA) * rms);
            loopbandEnv[i].store(detEnv[i], std::memory_order_relaxed);
        }
    }

    bool isRunning() const noexcept { return ariaRunning; }
};

static AriaAnalyzerClap s_aria;

} // anonymous namespace

// ════════════════════════════════════════════════════════════════════════════
// Parameter table — built at runtime from kBandDefs
// ════════════════════════════════════════════════════════════════════════════
static constexpr int CLAP_NUM_BANDS  = kNumBands;        // 24
static constexpr int CLAP_NUM_PARAMS = kNumBands * 3 + 2; // 74

struct ClapParamDef {
    clap_id id;
    char    name  [CLAP_NAME_SIZE];
    char    module[CLAP_PATH_SIZE];
    double  minV, maxV, defV;
    bool    is_bypass;
};

static ClapParamDef PARAMS[CLAP_NUM_PARAMS];
static bool PARAMS_INITIALIZED = false;

static void initParams() {
    if (PARAMS_INITIALIZED) return;
    int idx = 0;
    for (int b = 0; b < kNumBands; ++b) {
        const auto& bd = kBandDefs[b];
        // Gain
        PARAMS[idx].id = (clap_id)(b * 10);
        snprintf(PARAMS[idx].name,   sizeof(PARAMS[idx].name),   "%s Gain",  bd.label);
        snprintf(PARAMS[idx].module, sizeof(PARAMS[idx].module),  "%s",       bd.label);
        PARAMS[idx].minV = -24; PARAMS[idx].maxV = 24; PARAMS[idx].defV = 0;
        PARAMS[idx].is_bypass = false; ++idx;
        // Freq
        PARAMS[idx].id = (clap_id)(b * 10 + 1);
        snprintf(PARAMS[idx].name,   sizeof(PARAMS[idx].name),   "%s Freq",  bd.label);
        snprintf(PARAMS[idx].module, sizeof(PARAMS[idx].module),  "%s",       bd.label);
        PARAMS[idx].minV = 20; PARAMS[idx].maxV = 20000; PARAMS[idx].defV = bd.defaultFreq;
        PARAMS[idx].is_bypass = false; ++idx;
        // Q
        PARAMS[idx].id = (clap_id)(b * 10 + 2);
        snprintf(PARAMS[idx].name,   sizeof(PARAMS[idx].name),   "%s Q",     bd.label);
        snprintf(PARAMS[idx].module, sizeof(PARAMS[idx].module),  "%s",       bd.label);
        PARAMS[idx].minV = 0.1; PARAMS[idx].maxV = 18; PARAMS[idx].defV = bd.defaultQ;
        PARAMS[idx].is_bypass = false; ++idx;
    }
    // Bypass (ID 1000)
    PARAMS[idx].id = 1000;
    snprintf(PARAMS[idx].name,   sizeof(PARAMS[idx].name),   "Bypass");
    snprintf(PARAMS[idx].module, sizeof(PARAMS[idx].module),  "");
    PARAMS[idx].minV = 0; PARAMS[idx].maxV = 1; PARAMS[idx].defV = 0;
    PARAMS[idx].is_bypass = true; ++idx;
    // Spectral Tilt (ID 1001)
    PARAMS[idx].id = 1001;
    snprintf(PARAMS[idx].name,   sizeof(PARAMS[idx].name),   "TLT");
    snprintf(PARAMS[idx].module, sizeof(PARAMS[idx].module),  "");
    PARAMS[idx].minV = -12; PARAMS[idx].maxV = 12; PARAMS[idx].defV = 0;
    PARAMS[idx].is_bypass = false; ++idx;

    PARAMS_INITIALIZED = true;
}

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

// ════════════════════════════════════════════════════════════════════════════
// EKGEQClap — main plugin struct
// ════════════════════════════════════════════════════════════════════════════
struct EKGEQClap {
    clap_plugin_t         plugin;
    const clap_host_t*    host;

    // DSP state
    double sampleRate = 44100.0;
    bool   bypass     = false;
    double tilt       = 0.0;
    double normVal[CLAP_NUM_PARAMS] = {};

    // Per-band per-channel biquad chains (24 bands, stereo)
    BiquadFilter filters[kNumBands][2];  // [band][ch]
    BiquadFilter tiltL, tiltR;

    // Per-band detection filters (pre-EQ RMS analysis)
    BiquadFilter detL[kNumBands], detR[kNumBands];
    float        detEnv[kNumBands] = {};
    int          detThrottle = 0;
    static constexpr int kDetInterval = 8;

    // C·AUTO correction (ARIA loopback reference)
    float cautoGain[kNumBands] = {};
    bool  cautoEnabled = true;
    static constexpr float kCautoRate   = 0.00015f;
    static constexpr float kCautoMaxG   = 6.0f;
    static constexpr float kCautoMinSig = 1e-5f;

    // GUI state
    HWND                     guiHwnd    = nullptr;
    ICoreWebView2Controller* wvCtrl     = nullptr;
    ICoreWebView2*           wv         = nullptr;
    bool                     wvReady    = false;
    HMODULE                  wvDLL      = nullptr;
    EventRegistrationToken   msgToken   = {};
    EventRegistrationToken   accelToken = {};

    // ── Param helpers ─────────────────────────────────────────────────────────
    int paramIndex(clap_id id) const {
        for (int i = 0; i < CLAP_NUM_PARAMS; ++i)
            if (PARAMS[i].id == id) return i;
        return -1;
    }

    double paramPlain(int idx) const {
        return PARAMS[idx].minV + normVal[idx] * (PARAMS[idx].maxV - PARAMS[idx].minV);
    }

    void rebuildFilter(int band) {
        if (band < 0 || band >= kNumBands) return;
        int gi = paramIndex((clap_id)(band * 10));
        int fi = paramIndex((clap_id)(band * 10 + 1));
        int qi = paramIndex((clap_id)(band * 10 + 2));
        if (gi < 0 || fi < 0 || qi < 0) return;

        double g = paramPlain(gi) + (double)cautoGain[band];
        double f = paramPlain(fi);
        double q = paramPlain(qi);

        BiquadCoeffs c;
        if (kBandDefs[band].isShelf) {
            if (kBandDefs[band].isHigh)
                c = makeHighShelf(sampleRate, f, g, q);
            else
                c = makeLowShelf (sampleRate, f, g, q);
        } else {
            c = makePeaking(sampleRate, f, g, q);
        }
        filters[band][0].setCoeffs(c);
        filters[band][1].setCoeffs(c);
    }

    void rebuildTilt() {
        if (std::fabs(tilt) < 0.001) { tiltL.reset(); tiltR.reset(); return; }
        BiquadCoeffs c = makeHighShelf(sampleRate, 1000.0, tilt, 0.71);
        tiltL.setCoeffs(c); tiltR.setCoeffs(c);
    }

    void rebuildAll() {
        for (int b = 0; b < kNumBands; ++b) rebuildFilter(b);
        rebuildTilt();
    }

    // ── Isolated WebView2 user data path ────────────────────────────────────
    // Must be separate from FL Studio's own WebView2 (Image-Line/Edge/EBWebView).
    // Passing nullptr to CreateCoreWebView2Environment uses the same default
    // folder as the host, causing an in-process collision and crash.
    static std::wstring buildWV2UserDataPath() {
        wchar_t localApp[MAX_PATH] = {};
        if (!GetEnvironmentVariableW(L"LOCALAPPDATA", localApp, MAX_PATH))
            ExpandEnvironmentStringsW(L"%LOCALAPPDATA%", localApp, MAX_PATH);
        std::wstring p(localApp);
        p += L"\\EKG-EQ-WebView2";
        // Ensure directory exists
        CreateDirectoryW(p.c_str(), nullptr);
        return p;
    }

    // ── WebView2 file URI ────────────────────────────────────────────────────
    std::wstring buildHtmlUri() {
        static const char s_anchor = 0;
        HMODULE hMod = nullptr;
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&s_anchor), &hMod);
        wchar_t p[MAX_PATH] = {};
        GetModuleFileNameW(hMod, p, MAX_PATH);
        std::wstring path(p);
        size_t sep = path.rfind(L'\\');
        if (sep != std::wstring::npos) path.resize(sep);
        path += L"\\EKG-EQ-ui\\index.html";
        std::wstring uri = L"file:///";
        for (wchar_t ch : path) {
            if (ch == L'\\')      uri += L'/';
            else if (ch == L' ')  uri += L"%20";
            else                  uri += ch;
        }
        return uri;
    }

    // ── Send all 24 bands to JS ──────────────────────────────────────────────
    void sendParamsToJS() {
        if (!wvReady || !wv) return;
        // Build JSON: {"type":"setParams","bands":[{id,gain,freq,q}×24]}
        std::wstring json = L"{\"type\":\"setParams\",\"bands\":[";
        for (int b = 0; b < kNumBands; ++b) {
            int gi = paramIndex((clap_id)(b*10));
            int fi = paramIndex((clap_id)(b*10+1));
            int qi = paramIndex((clap_id)(b*10+2));
            double g = gi >= 0 ? paramPlain(gi) : 0.0;
            double f = fi >= 0 ? paramPlain(fi) : kBandDefs[b].defaultFreq;
            double q = qi >= 0 ? paramPlain(qi) : kBandDefs[b].defaultQ;
            wchar_t buf[128];
            swprintf(buf, 128, L"%s{\"id\":%d,\"gain\":%.3f,\"freq\":%.2f,\"q\":%.4f}",
                     b == 0 ? L"" : L",", b, g, f, q);
            json += buf;
        }
        json += L"]}";
        wv->PostWebMessageAsJson(json.c_str());
    }

    // ── Handle JS → C++ ──────────────────────────────────────────────────────
    void onJSMessage(LPCWSTR jsonW) {
        std::wstring json(jsonW);
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
        size_t arrEnd = json.rfind(L']');
        if (arrEnd == std::wstring::npos || arrEnd <= arrStart) return;
        std::wstring arr = json.substr(arrStart, arrEnd - arrStart + 1);

        auto parseNum = [&](const std::wstring& s, const wchar_t* key, double def) -> double {
            std::wstring needle = std::wstring(L"\"") + key + L"\"";
            size_t k = s.find(needle);
            if (k == std::wstring::npos) return def;
            size_t c = s.find(L':', k);
            if (c == std::wstring::npos) return def;
            size_t pp = c + 1;
            while (pp < s.size() && s[pp] == L' ') ++pp;
            std::wstring num;
            while (pp < s.size() && (s[pp] == L'-' || (s[pp] >= L'0' && s[pp] <= L'9') || s[pp] == L'.'))
                num += s[pp++];
            if (num.empty()) return def;
            try { return std::stod(num); } catch (...) { return def; }
        };

        const clap_host_params_t* hostParams =
            (const clap_host_params_t*)host->get_extension(host, CLAP_EXT_PARAMS);

        size_t pos = 0;
        while (true) {
            size_t oStart = arr.find(L'{', pos);
            if (oStart == std::wstring::npos) break;
            size_t oEnd = arr.find(L'}', oStart);
            if (oEnd == std::wstring::npos) break;
            std::wstring obj = arr.substr(oStart, oEnd - oStart + 1);

            int id = (int)parseNum(obj, L"id", -1);
            if (id >= 0 && id < kNumBands) {
                double gain = std::clamp(parseNum(obj, L"gain", 0.0), -24.0, 24.0);
                double freq = std::clamp(parseNum(obj, L"freq", kBandDefs[id].defaultFreq), 20.0, 20000.0);
                double q    = std::clamp(parseNum(obj, L"q",    kBandDefs[id].defaultQ), 0.1, 18.0);

                auto update = [&](clap_id pid, double plain, double minV, double maxV) {
                    int idx = paramIndex(pid);
                    if (idx < 0) return;
                    double n = std::clamp((plain - minV) / (maxV - minV), 0.0, 1.0);
                    if (std::fabs(n - normVal[idx]) > 0.0001) {
                        normVal[idx] = n;
                        if (hostParams) hostParams->request_flush(host);
                    }
                };
                update((clap_id)(id*10),     gain, -24,    24);
                update((clap_id)(id*10 + 1), freq,  20, 20000);
                update((clap_id)(id*10 + 2), q,    0.1,    18);
                rebuildFilter(id);
            }
            pos = oEnd + 1;
        }
    }

    // ── Send spectrum + loopback to JS ───────────────────────────────────────
    void sendSpectrumToJS(bool sendLoopback) {
        if (!wvReady || !wv) return;
        // Track spectrum
        wchar_t json[600];
        int pos = swprintf(json, 600, L"{\"type\":\"spectrum\",\"bands\":[");
        for (int i = 0; i < kNumBands && pos < 560; ++i)
            pos += swprintf(json + pos, 600 - pos, i > 0 ? L",%.5f" : L"%.5f", (double)detEnv[i]);
        swprintf(json + pos, 600 - pos, L"]}");
        wv->PostWebMessageAsJson(json);

        if (sendLoopback && s_aria.isRunning()) {
            pos = swprintf(json, 600, L"{\"type\":\"loopbackSpectrum\",\"bands\":[");
            for (int i = 0; i < kNumBands && pos < 540; ++i) {
                float v = s_aria.loopbandEnv[i].load(std::memory_order_relaxed);
                pos += swprintf(json + pos, 600 - pos, i > 0 ? L",%.5f" : L"%.5f", (double)v);
            }
            swprintf(json + pos, 600 - pos, L"],\"aria\":true}");
            wv->PostWebMessageAsJson(json);
        }
    }
};

// ════════════════════════════════════════════════════════════════════════════
// CLAP plugin callbacks
// ════════════════════════════════════════════════════════════════════════════

static bool ekgeq_init(const clap_plugin_t* p) {
    auto* self = (EKGEQClap*)p->plugin_data;
    for (int i = 0; i < CLAP_NUM_PARAMS; ++i) {
        const auto& pd = PARAMS[i];
        self->normVal[i] = (pd.defV - pd.minV) / (pd.maxV - pd.minV);
    }
    // Init detection filters
    for (int b = 0; b < kNumBands; ++b) {
        BiquadCoeffs dc = makeBandpass(self->sampleRate, kBandDefs[b].defaultFreq, 2.0);
        self->detL[b].setCoeffs(dc); self->detL[b].reset();
        self->detR[b].setCoeffs(dc); self->detR[b].reset();
        self->detEnv[b] = 0.0f;
    }
    self->rebuildAll();
    return true;
}

static void ekgeq_destroy(const clap_plugin_t* p) {
    auto* self = (EKGEQClap*)p->plugin_data;
    s_aria.release();
    delete self;
}

static bool ekgeq_activate(const clap_plugin_t* p, double sr,
                            uint32_t /*min*/, uint32_t /*max*/) {
    auto* self = (EKGEQClap*)p->plugin_data;
    self->sampleRate = sr;
    // Reset detection filters for new sample rate
    for (int b = 0; b < kNumBands; ++b) {
        BiquadCoeffs dc = makeBandpass(sr, kBandDefs[b].defaultFreq, 2.0);
        self->detL[b].setCoeffs(dc); self->detL[b].reset();
        self->detR[b].setCoeffs(dc); self->detR[b].reset();
    }
    self->rebuildAll();
    s_aria.acquire();
    return true;
}

static void ekgeq_deactivate(const clap_plugin_t* p) {
    s_aria.release();
    (void)p;
}

static bool  ekgeq_start_processing(const clap_plugin_t*) { return true; }
static void  ekgeq_stop_processing (const clap_plugin_t*) {}

static void ekgeq_reset(const clap_plugin_t* p) {
    auto* self = (EKGEQClap*)p->plugin_data;
    for (int b = 0; b < kNumBands; ++b) {
        self->filters[b][0].reset(); self->filters[b][1].reset();
        self->detL[b].reset();       self->detR[b].reset();
        self->detEnv[b] = 0.0f;
    }
    self->tiltL.reset(); self->tiltR.reset();
}

static clap_process_status ekgeq_process(const clap_plugin_t* p, const clap_process_t* proc) {
    auto* self = (EKGEQClap*)p->plugin_data;

    // 1. Consume parameter events
    uint32_t ev = 0, numEv = proc->in_events->size(proc->in_events);
    for (uint32_t frame = 0; frame < proc->frames_count; ) {
        while (ev < numEv) {
            const clap_event_header_t* hdr = proc->in_events->get(proc->in_events, ev);
            if (hdr->time > frame) break;
            if (hdr->type == CLAP_EVENT_PARAM_VALUE) {
                const auto* evt = (const clap_event_param_value_t*)hdr;
                int idx = self->paramIndex((clap_id)evt->param_id);
                if (idx >= 0) {
                    self->normVal[idx] = std::clamp(
                        (evt->value - PARAMS[idx].minV) / (PARAMS[idx].maxV - PARAMS[idx].minV),
                        0.0, 1.0);
                    clap_id pid = (clap_id)evt->param_id;
                    if (pid == 1000) {
                        self->bypass = (evt->value >= 0.5);
                    } else if (pid == 1001) {
                        self->tilt = evt->value;
                        self->rebuildTilt();
                    } else {
                        int band = (int)pid / 10;
                        if (band >= 0 && band < kNumBands) self->rebuildFilter(band);
                    }
                }
            }
            ++ev;
        }

        if (proc->audio_inputs_count > 0 && proc->audio_outputs_count > 0) {
            const float* in0  = proc->audio_inputs[0].data32[0];
            const float* in1  = proc->audio_inputs[0].channel_count > 1
                              ? proc->audio_inputs[0].data32[1] : in0;
            float*       out0 = proc->audio_outputs[0].data32[0];
            float*       out1 = proc->audio_outputs[0].channel_count > 1
                              ? proc->audio_outputs[0].data32[1] : out0;

            if (self->bypass) {
                out0[frame] = in0[frame];
                out1[frame] = in1[frame];
            } else {
                // Detection
                double xL = in0[frame], xR = in1[frame];
                for (int b = 0; b < kNumBands; ++b) {
                    double yL = self->detL[b].process(xL), yR = self->detR[b].process(xR);
                    double rms = (float)std::sqrt((yL*yL + yR*yR) * 0.5);
                    self->detEnv[b] = (float)(0.97 * self->detEnv[b] + 0.03 * rms);
                }
                // EQ
                double l = xL, r = xR;
                for (int b = 0; b < kNumBands; ++b) {
                    l = self->filters[b][0].process(l);
                    r = self->filters[b][1].process(r);
                }
                if (std::fabs(self->tilt) >= 0.001) {
                    l = self->tiltL.process(l);
                    r = self->tiltR.process(r);
                }
                out0[frame] = (float)l;
                out1[frame] = (float)r;
            }
        }
        ++frame;

        // 2. C·AUTO + spectrum report every kDetInterval frames
        if (++self->detThrottle >= self->kDetInterval) {
            self->detThrottle = 0;

            // C·AUTO correction
            if (self->cautoEnabled && s_aria.isRunning()) {
                float trackSum = 0.0f, mixSum = 0.0f;
                float mixEnv[kNumBands];
                for (int i = 0; i < kNumBands; ++i) {
                    mixEnv[i]  = s_aria.loopbandEnv[i].load(std::memory_order_relaxed);
                    trackSum  += self->detEnv[i];
                    mixSum    += mixEnv[i];
                }
                if (trackSum > EKGEQClap::kCautoMinSig && mixSum > EKGEQClap::kCautoMinSig) {
                    bool dirty = false;
                    for (int i = 0; i < kNumBands; ++i) {
                        float delta = mixEnv[i]/mixSum - self->detEnv[i]/trackSum;
                        self->cautoGain[i] += delta * 12.0f * EKGEQClap::kCautoRate * 1000.0f;
                        self->cautoGain[i]  = std::clamp(self->cautoGain[i],
                                                          -EKGEQClap::kCautoMaxG,
                                                           EKGEQClap::kCautoMaxG);
                        dirty = true;
                    }
                    if (dirty) self->rebuildAll();
                }
            }

            self->sendSpectrumToJS(true);
        }
    }
    return CLAP_PROCESS_CONTINUE;
}

static const void* ekgeq_get_extension(const clap_plugin_t* p, const char* id);
static void ekgeq_on_main_thread(const clap_plugin_t*) {}

// ── Audio ports ───────────────────────────────────────────────────────────────
static uint32_t ap_count(const clap_plugin_t*, bool) { return 1; }
static bool ap_get(const clap_plugin_t*, uint32_t, bool, clap_audio_port_info_t* info) {
    info->id = 0;
    strcpy(info->name, "Stereo");
    info->flags         = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type     = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
static const clap_plugin_audio_ports_t EXT_AUDIO_PORTS = { ap_count, ap_get };

// ── Parameters extension ──────────────────────────────────────────────────────
static uint32_t par_count(const clap_plugin_t*) { return (uint32_t)CLAP_NUM_PARAMS; }

static bool par_get_info(const clap_plugin_t*, uint32_t idx, clap_param_info_t* info) {
    if ((int)idx >= CLAP_NUM_PARAMS) return false;
    const auto& pd = PARAMS[idx];
    info->id    = pd.id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
    if (pd.is_bypass) info->flags |= CLAP_PARAM_IS_BYPASS;
    strncpy(info->name,   pd.name,   CLAP_NAME_SIZE - 1);
    strncpy(info->module, pd.module, CLAP_PATH_SIZE - 1);
    info->min_value     = pd.minV;
    info->max_value     = pd.maxV;
    info->default_value = pd.defV;
    info->cookie        = nullptr;
    return true;
}

static bool par_get_value(const clap_plugin_t* p, clap_id id, double* val) {
    auto* self = (EKGEQClap*)p->plugin_data;
    int idx = self->paramIndex(id);
    if (idx < 0) return false;
    *val = PARAMS[idx].minV + self->normVal[idx] * (PARAMS[idx].maxV - PARAMS[idx].minV);
    return true;
}

static bool par_value_to_text(const clap_plugin_t* p, clap_id id, double val,
                               char* buf, uint32_t size) {
    (void)p;
    if (id == 1000)           snprintf(buf, size, "%s", val >= 0.5 ? "Bypassed" : "Active");
    else if ((id % 10) == 0)  snprintf(buf, size, "%.1f dB", val);
    else if ((id % 10) == 1)  snprintf(buf, size, val >= 1000 ? "%.1f kHz" : "%.0f Hz",
                                        val >= 1000 ? val/1000.0 : val);
    else                      snprintf(buf, size, "%.2f", val);
    return true;
}

static bool par_text_to_value(const clap_plugin_t*, clap_id, const char* txt, double* val) {
    *val = atof(txt); return true;
}

static void par_flush(const clap_plugin_t* p, const clap_input_events_t* in,
                      const clap_output_events_t*) {
    auto* self = (EKGEQClap*)p->plugin_data;
    uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i) {
        const clap_event_header_t* hdr = in->get(in, i);
        if (hdr->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* evt = (const clap_event_param_value_t*)hdr;
            int idx = self->paramIndex((clap_id)evt->param_id);
            if (idx >= 0) {
                self->normVal[idx] = std::clamp(
                    (evt->value - PARAMS[idx].minV) / (PARAMS[idx].maxV - PARAMS[idx].minV),
                    0.0, 1.0);
                clap_id pid = (clap_id)evt->param_id;
                if (pid == 1000) {
                    self->bypass = (evt->value >= 0.5);
                } else if (pid == 1001) {
                    self->tilt = evt->value; self->rebuildTilt();
                } else {
                    int band = (int)pid / 10;
                    if (band >= 0 && band < kNumBands) self->rebuildFilter(band);
                }
            }
        }
    }
    self->sendParamsToJS();
}

static const clap_plugin_params_t EXT_PARAMS = {
    par_count, par_get_info, par_get_value, par_value_to_text, par_text_to_value, par_flush
};

// ── State (save/load) ─────────────────────────────────────────────────────────
static bool state_save(const clap_plugin_t* p, const clap_ostream_t* stream) {
    auto* self = (EKGEQClap*)p->plugin_data;
    uint32_t written = 0;
    while (written < sizeof(self->normVal)) {
        int64_t n = stream->write(stream,
            (const char*)self->normVal + written,
            sizeof(self->normVal) - written);
        if (n <= 0) return false;
        written += (uint32_t)n;
    }
    // Also save C·AUTO gains
    for (int i = 0; i < kNumBands; ++i) {
        float v = self->cautoGain[i];
        stream->write(stream, (const char*)&v, sizeof(v));
    }
    return true;
}

static bool state_load(const clap_plugin_t* p, const clap_istream_t* stream) {
    auto* self = (EKGEQClap*)p->plugin_data;
    double buf[CLAP_NUM_PARAMS] = {};
    uint32_t read = 0;
    while (read < sizeof(buf)) {
        int64_t n = stream->read(stream, (char*)buf + read, sizeof(buf) - read);
        if (n <= 0) break;
        read += (uint32_t)n;
    }
    if (read >= sizeof(self->normVal))
        memcpy(self->normVal, buf, sizeof(self->normVal));
    // Try to restore C·AUTO gains (may not be present in old sessions)
    for (int i = 0; i < kNumBands; ++i) {
        float v = 0.0f;
        if (stream->read(stream, (char*)&v, sizeof(v)) > 0)
            self->cautoGain[i] = v;
    }
    self->rebuildAll();
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
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
        if (self && !self->wvReady) {
            RECT r; GetClientRect(hwnd, &r);
            HBRUSH br = CreateSolidBrush(RGB(3,8,6));
            FillRect(hdc, &r, br); DeleteObject(br);
        }
        EndPaint(hwnd, &ps); return 0;
    }
    case WM_SETFOCUS:
        if (self && self->wvCtrl)
            self->wvCtrl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        return 0;
    case WM_MOUSEACTIVATE: SetFocus(hwnd); return MA_ACTIVATE;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: SetFocus(hwnd);
        return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_SIZE:
        if (self && self->wvCtrl) {
            RECT r{0,0,(LONG)LOWORD(lp),(LONG)HIWORD(lp)};
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

static bool gui_is_api_supported(const clap_plugin_t*, const char* api, bool floating) {
    return !floating && strcmp(api, CLAP_WINDOW_API_WIN32) == 0;
}
static bool gui_get_preferred_api(const clap_plugin_t*, const char** api, bool* floating) {
    *api = CLAP_WINDOW_API_WIN32; *floating = false; return true;
}
static bool gui_create(const clap_plugin_t* p, const char*, bool) {
    (void)p;
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
static bool gui_can_resize    (const clap_plugin_t*) { return false; }
static bool gui_get_resize_hints(const clap_plugin_t*, clap_gui_resize_hints_t*) { return false; }
static bool gui_adjust_size   (const clap_plugin_t*, uint32_t* w, uint32_t* h) {
    *w = GUI_W; *h = GUI_H; return true;
}
static bool gui_set_size(const clap_plugin_t*, uint32_t, uint32_t) { return true; }

static bool gui_set_parent(const clap_plugin_t* p, const clap_window_t* win) {
    auto* self = (EKGEQClap*)p->plugin_data;
    self->guiHwnd = CreateWindowExW(
        0, CLAP_WND_CLASS, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, GUI_W, GUI_H, (HWND)win->win32,
        nullptr, GetModuleHandle(nullptr), self);
    if (!self->guiHwnd) return false;

    WV2CreateFn createEnv = loadWV2(&self->wvDLL);
    if (!createEnv) return true;

    static std::wstring s_wv2UserData = EKGEQClap::buildWV2UserDataPath();
    createEnv(nullptr, s_wv2UserData.c_str(), nullptr,
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
                                        args->put_Handled(FALSE); return S_OK;
                                    }), &self->accelToken);

                            self->wvReady = true;
                            self->wvCtrl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
                            self->wv->Navigate(self->buildHtmlUri().c_str());
                            return S_OK;
                        }));
            }));
    return true;
}

static bool gui_set_transient(const clap_plugin_t*, const clap_window_t*) { return false; }
static void gui_suggest_title(const clap_plugin_t*, const char*) {}
static bool gui_show(const clap_plugin_t* p) {
    auto* s = (EKGEQClap*)p->plugin_data;
    if (s->guiHwnd) ShowWindow(s->guiHwnd, SW_SHOW);
    if (s->wvCtrl)  s->wvCtrl->put_IsVisible(TRUE);
    return true;
}
static bool gui_hide(const clap_plugin_t* p) {
    auto* s = (EKGEQClap*)p->plugin_data;
    if (s->guiHwnd) ShowWindow(s->guiHwnd, SW_HIDE);
    if (s->wvCtrl)  s->wvCtrl->put_IsVisible(FALSE);
    return true;
}

static const clap_plugin_gui_t EXT_GUI = {
    gui_is_api_supported, gui_get_preferred_api, gui_create, gui_destroy,
    gui_set_scale, gui_get_size, gui_can_resize, gui_get_resize_hints,
    gui_adjust_size, gui_set_size, gui_set_parent, gui_set_transient,
    gui_suggest_title, gui_show, gui_hide
};

// ── get_extension dispatch ────────────────────────────────────────────────────
static const void* ekgeq_get_extension(const clap_plugin_t*, const char* id) {
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
    "", "",
    "1.0.0",
    "Master EQ \xe2\x80\x94 24 bands. AuraTone Technology. C\xc2\xb7" "AUTO + ARIA.",
    EKGEQ_FEATURES
};

// ── Plugin factory ────────────────────────────────────────────────────────────
static uint32_t factory_count(const clap_plugin_factory_t*) { return 1; }

static const clap_plugin_descriptor_t* factory_get_descriptor(
    const clap_plugin_factory_t*, uint32_t) { return &EKGEQ_DESC; }

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
static bool entry_init(const char*) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    initParams(); // Build parameter table from kBandDefs
    return true;
}
static void entry_deinit() { CoUninitialize(); }
static const void* entry_get_factory(const char* factory_id) {
    if (!strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID)) return &PLUGIN_FACTORY;
    return nullptr;
}

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT, entry_init, entry_deinit, entry_get_factory
};
