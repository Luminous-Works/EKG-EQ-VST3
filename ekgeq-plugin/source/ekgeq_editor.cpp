/**
 * EKG·EQ — VST3 Editor (WebView2 host)
 * Lumina Aerospace · AuraTone Technology
 *
 * Embeds the EKG·EQ HTML/Canvas UI inside a Win32 child window
 * via Microsoft WebView2 (Edge-based). No GDI+ rendering.
 *
 * Parameter bridge:
 *   C++ → JS  : PostWebMessageAsJson  → { type:"setParams", bands:[...] }
 *   JS → C++  : WebMessageReceived    ← { type:"allParams", bands:[...] }
 *               JS sends { type:"ready" } on first load to request state.
 */

#include "ekgeq_editor.h"
#include "ekgeq_controller.h"
#include "ekgeq_ids.h"

#include <cmath>
#include <string>
#include <algorithm>
#include <cwchar>

using namespace Steinberg;
using namespace Steinberg::Vst;

// ── Band parameter constants ──────────────────────────────────────────────────
static const double BAND_FREQ_DEFAULT[6] = { 60, 150, 500, 1500, 4500, 12000 };
static const double BAND_Q_DEFAULT[6]    = { 0.71, 1.0, 1.0, 1.0, 1.0, 0.71 };
static constexpr double DB_LO   = -24.0;
static constexpr double DB_HI   =  24.0;
static constexpr double FREQ_LO =   20.0;
static constexpr double FREQ_HI = 20000.0;
static constexpr double Q_LO    =   0.1;
static constexpr double Q_HI    =  18.0;  // matches processor: 0.1 + v*17.9

static double normToGain(double n) { return DB_LO + n * (DB_HI - DB_LO); }
static double normToFreq(double n) { return FREQ_LO * std::pow(FREQ_HI / FREQ_LO, n); }
static double normToQ   (double n) { return Q_LO + n * (Q_HI - Q_LO); }  // linear, matches processor/controller
static double gainToNorm(double g) { return (g - DB_LO) / (DB_HI - DB_LO); }
static double freqToNorm(double f) { return std::log(f / FREQ_LO) / std::log(FREQ_HI / FREQ_LO); }
static double qToNorm   (double q) { return (q - Q_LO) / (Q_HI - Q_LO); }  // linear, matches processor/controller

// ── Minimal COM callback helpers (no WRL, no std::function needed) ───────────
// Use template Fn type to avoid MinGW's trouble parsing std::function<R(T*,A*)>
// inside class-template member declarations.

// Completed-handler: Invoke(HRESULT, T*)
template<class I, class T, class Fn>
class CB1 final : public I {
    volatile LONG _ref = 1;
    Fn _fn;
public:
    explicit CB1(Fn fn) : _fn(std::move(fn)) {}
    ULONG STDMETHODCALLTYPE AddRef() override {
        return (ULONG)InterlockedIncrement(&_ref);
    }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG n = InterlockedDecrement(&_ref);
        if (n == 0) delete this;
        return (ULONG)n;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** p) override {
        *p = nullptr; return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, T* result) override {
        return _fn(hr, result);
    }
};

// Event-handler: Invoke(S*, A*)
template<class I, class S, class A, class Fn>
class CB2 final : public I {
    volatile LONG _ref = 1;
    Fn _fn;
public:
    explicit CB2(Fn fn) : _fn(std::move(fn)) {}
    ULONG STDMETHODCALLTYPE AddRef() override {
        return (ULONG)InterlockedIncrement(&_ref);
    }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG n = InterlockedDecrement(&_ref);
        if (n == 0) delete this;
        return (ULONG)n;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** p) override {
        *p = nullptr; return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE Invoke(S* sender, A* args) override {
        return _fn(sender, args);
    }
};

// Factory helpers — deduce Fn from the lambda automatically
template<class I, class T, class Fn>
CB1<I, T, Fn>* makeCB1(Fn fn) { return new CB1<I, T, Fn>(std::move(fn)); }

template<class I, class S, class A, class Fn>
CB2<I, S, A, Fn>* makeCB2(Fn fn) { return new CB2<I, S, A, Fn>(std::move(fn)); }

// ── Window class ──────────────────────────────────────────────────────────────
static const wchar_t* EKGEQ_WND_CLASS = L"EKGEQEditorWV2";
bool EKGEQEditor::s_classRegistered = false;

// ── Constructor / Destructor ──────────────────────────────────────────────────
EKGEQEditor::EKGEQEditor(EditController* controller)
    : _controller(controller)
{
    for (int b = 0; b < kNumBands; ++b) {
        _normParams[b][0] = gainToNorm(0.0);
        _normParams[b][1] = freqToNorm(kBandDefs[b].defaultFreq);
        _normParams[b][2] = qToNorm(kBandDefs[b].defaultQ);
    }
}

EKGEQEditor::~EKGEQEditor() {
    if (_wv) {
        _wv->remove_WebMessageReceived(_msgToken);
        _wv->Release();
        _wv = nullptr;
    }
    if (_wvCtrl) {
        _wvCtrl->Close();
        _wvCtrl->Release();
        _wvCtrl = nullptr;
    }
    if (_hwnd) {
        DestroyWindow(_hwnd);
        _hwnd = nullptr;
    }
    if (_wvDLL) {
        FreeLibrary(_wvDLL);
        _wvDLL = nullptr;
    }
}

// ── IPlugView ─────────────────────────────────────────────────────────────────
tresult PLUGIN_API EKGEQEditor::isPlatformTypeSupported(FIDString type) {
    if (strcmp(type, kPlatformTypeHWND) == 0)
        return kResultTrue;
    return kResultFalse;
}

tresult PLUGIN_API EKGEQEditor::getSize(ViewRect* sz) {
    if (!sz) return kInvalidArgument;
    sz->left   = 0;
    sz->top    = 0;
    sz->right  = _currentW;
    sz->bottom = _currentH;
    return kResultOk;
}

tresult PLUGIN_API EKGEQEditor::onSize(ViewRect* newSize) {
    if (!newSize) return kInvalidArgument;

    int w = newSize->right  - newSize->left;
    int h = newSize->bottom - newSize->top;

    if (w < 720) w = 720;
    if (h < 480) h = 480;

    _currentW = w;
    _currentH = h;

    // PostMessage is safe from any thread. The WndProc (window thread) does the
    // actual SetWindowPos + WebView2 put_Bounds. Calling those directly here would
    // violate Win32 cross-thread rules and WebView2's STA requirement.
    if (_hwnd) {
        PostMessage(_hwnd, WM_USER + 1, (WPARAM)w, (LPARAM)h);
    }
    return kResultOk;
}

tresult PLUGIN_API EKGEQEditor::checkSizeConstraint(ViewRect* rect) {
    if (!rect) return kInvalidArgument;

    int w = rect->right  - rect->left;
    int h = rect->bottom - rect->top;

    if (w < 720) w = 720;
    if (h < 480) h = 480;

    rect->right  = rect->left + w;
    rect->bottom = rect->top  + h;
    return kResultOk;
}

// ── attached() ───────────────────────────────────────────────────────────────
tresult PLUGIN_API EKGEQEditor::attached(void* parent, FIDString type) {
    if (strcmp(type, kPlatformTypeHWND) != 0)
        return kResultFalse;

    HWND parentHwnd = (HWND)parent;

    if (!s_classRegistered) {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandle(nullptr);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = EKGEQ_WND_CLASS;
        RegisterClassExW(&wc);
        s_classRegistered = true;
    }

    _hwnd = CreateWindowExW(
        0, EKGEQ_WND_CLASS, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, _currentW, _currentH,
        parentHwnd, nullptr, GetModuleHandle(nullptr), this);

    if (!_hwnd) return kResultFalse;

    syncFromController();

    CreateEnvFn createEnv = loadWebView2Factory();
    if (!createEnv) {
        // No WebView2 runtime found — plugin loads but shows black window
        return kResultOk;
    }

    // Async chain: Environment → Controller → WebView → Navigate
    // Use makeCB1/makeCB2 factory helpers so the lambda type is deduced
    // automatically — avoids MinGW's std::function template parsing bug.
    // Explicit user data folder keeps our WebView2 env separate from FL Studio's own.
    const wchar_t* wv2UserData = L"C:\\Users\\Tyrone Cunningham\\AppData\\Local\\EKG-EQ-WebView2";
    createEnv(nullptr, wv2UserData, nullptr,
        makeCB1<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
                ICoreWebView2Environment>(
            [this](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(hr) || !env || !_hwnd) return hr;

                return env->CreateCoreWebView2Controller(
                    _hwnd,
                    makeCB1<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
                             ICoreWebView2Controller>(
                        [this](HRESULT hr, ICoreWebView2Controller* ctrl) -> HRESULT {
                            if (FAILED(hr) || !ctrl) return hr;

                            _wvCtrl = ctrl;
                            _wvCtrl->AddRef();

                            RECT r { 0, 0, (LONG)_currentW, (LONG)_currentH };
                            _wvCtrl->put_Bounds(r);
                            _wvCtrl->put_IsVisible(TRUE);

                            _wvCtrl->get_CoreWebView2(&_wv);
                            if (!_wv) return E_FAIL;

                            // Configure
                            ICoreWebView2Settings* settings = nullptr;
                            if (SUCCEEDED(_wv->get_Settings(&settings)) && settings) {
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->put_AreDevToolsEnabled(FALSE);
                                settings->put_IsBuiltInErrorPageEnabled(FALSE);
                                settings->Release();
                            }

                            // JS → C++ message handler
                            _wv->add_WebMessageReceived(
                                makeCB2<ICoreWebView2WebMessageReceivedEventHandler,
                                         ICoreWebView2,
                                         ICoreWebView2WebMessageReceivedEventArgs>(
                                    [this](ICoreWebView2*,
                                           ICoreWebView2WebMessageReceivedEventArgs* args)
                                        -> HRESULT {
                                        LPWSTR json = nullptr;
                                        if (SUCCEEDED(args->get_WebMessageAsJson(&json)) && json) {
                                            onJSParamChange(json);
                                            CoTaskMemFree(json);
                                        }
                                        return S_OK;
                                    }),
                                &_msgToken);

                            // Allow ALL accelerator keys through to the page.
                            // Without this FL Studio swallows Escape, Y, Enter, etc.
                            _wvCtrl->add_AcceleratorKeyPressed(
                                makeCB2<ICoreWebView2AcceleratorKeyPressedEventHandler,
                                         ICoreWebView2Controller,
                                         ICoreWebView2AcceleratorKeyPressedEventArgs>(
                                    [](ICoreWebView2Controller*,
                                       ICoreWebView2AcceleratorKeyPressedEventArgs* args)
                                        -> HRESULT {
                                        // FALSE = do not suppress; let WebView2 handle the key
                                        args->put_Handled(FALSE);
                                        return S_OK;
                                    }),
                                &_accelToken);

                            _wvReady = true;

                            // Claim keyboard focus immediately
                            _wvCtrl->MoveFocus(
                                COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);

                            // Navigate to HTML UI
                            std::wstring uri = buildHtmlFileUri();
                            _wv->Navigate(uri.c_str());

                            return S_OK;
                        }));
            }));

    return kResultOk;
}

// ── removed() ────────────────────────────────────────────────────────────────
void EKGEQEditor::receiveSpectrum(const float energy[24]) {
    if (!_wvReady || !_wv) return;
    // Build JSON: {"type":"spectrum","bands":[e0,e1,...,e23]}
    wchar_t json[512];
    int pos = swprintf(json, 512, L"{\"type\":\"spectrum\",\"bands\":[");
    for (int i = 0; i < 24 && pos < 480; ++i) {
        pos += swprintf(json + pos, 512 - pos,
            i > 0 ? L",%.5f" : L"%.5f", (double)energy[i]);
    }
    swprintf(json + pos, 512 - pos, L"]}");
    _wv->PostWebMessageAsJson(json);
}

// ── ARIA loopback spectrum ────────────────────────────────────────────────────
void EKGEQEditor::receiveLoopbackSpectrum(const float energy[24], bool ariaActive) {
    if (!_wvReady || !_wv) return;
    // {"type":"loopbackSpectrum","bands":[...],"aria":true}
    wchar_t json[600];
    int pos = swprintf(json, 600, L"{\"type\":\"loopbackSpectrum\",\"bands\":[");
    for (int i = 0; i < 24 && pos < 560; ++i) {
        pos += swprintf(json + pos, 600 - pos,
            i > 0 ? L",%.5f" : L"%.5f", (double)energy[i]);
    }
    swprintf(json + pos, 600 - pos,
        L"],\"aria\":%s}", ariaActive ? L"true" : L"false");
    _wv->PostWebMessageAsJson(json);
}

tresult PLUGIN_API EKGEQEditor::removed() {
    // Unregister from controller so dangling pointer can't be used
    static_cast<EKGEQController*>(_controller)->setEditor(nullptr);
    _wvReady = false;

    if (_wv) {
        _wv->remove_WebMessageReceived(_msgToken);
        _wv->Release();
        _wv = nullptr;
    }
    if (_wvCtrl) {
        _wvCtrl->remove_AcceleratorKeyPressed(_accelToken);
        _wvCtrl->Close();
        _wvCtrl->Release();
        _wvCtrl = nullptr;
    }
    if (_hwnd) {
        DestroyWindow(_hwnd);
        _hwnd = nullptr;
    }
    return kResultOk;
}

// ── Load WebView2Loader.dll ───────────────────────────────────────────────────
EKGEQEditor::CreateEnvFn EKGEQEditor::loadWebView2Factory() {
    const wchar_t* candidates[] = {
        L"WebView2Loader.dll",
        L"C:\\Program Files\\Image-Line\\FL Studio 2025\\WebView2Loader.dll",
        L"C:\\Program Files (x86)\\Microsoft\\EdgeWebView\\Application\\WebView2Loader.dll",
        nullptr
    };
    for (int i = 0; candidates[i]; ++i) {
        _wvDLL = LoadLibraryW(candidates[i]);
        if (_wvDLL) break;
    }
    if (!_wvDLL) return nullptr;

    auto fn = (CreateEnvFn)GetProcAddress(_wvDLL, "CreateCoreWebView2EnvironmentWithOptions");
    if (!fn) {
        FreeLibrary(_wvDLL);
        _wvDLL = nullptr;
    }
    return fn;
}

// ── Build file:/// URI for the UI HTML ────────────────────────────────────────
std::wstring EKGEQEditor::buildHtmlFileUri() const {
    // Get this DLL's path
    HMODULE hMod = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)(void*)(&EKGEQEditor::WndProc),
        &hMod);

    wchar_t dllPath[MAX_PATH] = {};
    GetModuleFileNameW(hMod, dllPath, MAX_PATH);

    // Navigate from:  .../EKG-EQ.vst3/Contents/x86_64-win/EKG-EQ.vst3
    // Up to:          .../EKG-EQ.vst3/Contents/
    // Then append:    Resources/ui/index.html
    std::wstring path(dllPath);
    // Strip filename
    size_t sep = path.rfind(L'\\');
    if (sep != std::wstring::npos) path.resize(sep);
    // Strip "x86_64-win"
    sep = path.rfind(L'\\');
    if (sep != std::wstring::npos) path.resize(sep);
    path += L"\\Resources\\ui\\index.html";

    // Convert to file:/// URI
    std::wstring uri;
    uri.reserve(8 + path.size());
    uri = L"file:///";
    for (wchar_t c : path) {
        if (c == L'\\') uri += L'/';
        else if (c == L' ') uri += L"%20";
        else uri += c;
    }
    return uri;
}

// ── Sync params from controller into _normParams cache ───────────────────────
void EKGEQEditor::syncFromController() {
    if (!_controller) return;
    for (int b = 0; b < kNumBands; ++b) {
        _normParams[b][0] = _controller->getParamNormalized(kGain(b));
        _normParams[b][1] = _controller->getParamNormalized(kFreq(b));
        _normParams[b][2] = _controller->getParamNormalized(kQ(b));
    }
}

// ── Push all band params to JS ────────────────────────────────────────────────
void EKGEQEditor::sendAllParamsToJS() {
    if (!_wvReady || !_wv) return;
    syncFromController();

    // 24 bands × ~40 chars each + wrapper ≈ 1100 chars
    wchar_t json[2048];
    int pos = 0;
    pos += swprintf(json + pos, 2048 - pos, L"{\"type\":\"setParams\",\"bands\":[");
    for (int b = 0; b < kNumBands && pos < 1900; ++b) {
        double gain = normToGain(_normParams[b][0]);
        double freq = normToFreq(_normParams[b][1]);
        double q    = normToQ   (_normParams[b][2]);
        pos += swprintf(json + pos, 2048 - pos,
            L"%s{\"id\":%d,\"gain\":%.3f,\"freq\":%.2f,\"q\":%.4f}",
            b == 0 ? L"" : L",", b, gain, freq, q);
    }
    swprintf(json + pos, 2048 - pos, L"]}");
    _wv->PostWebMessageAsJson(json);
}

// ── Handle JS → C++ messages ─────────────────────────────────────────────────
void EKGEQEditor::onJSParamChange(LPCWSTR jsonW) {
    std::wstring json(jsonW);
    std::wstring type = parseStringField(json, L"type");

    if (type == L"ready") {
        sendAllParamsToJS();
        return;
    }
    if (type != L"allParams") return;

    size_t arrStart = json.find(L'[');
    if (arrStart == std::wstring::npos) return;
    size_t arrEnd = json.find(L']', arrStart);
    if (arrEnd == std::wstring::npos) return;

    std::wstring arr = json.substr(arrStart, arrEnd - arrStart + 1);
    size_t pos = 0;

    while (true) {
        size_t oStart = arr.find(L'{', pos);
        if (oStart == std::wstring::npos) break;
        size_t oEnd = arr.find(L'}', oStart);
        if (oEnd == std::wstring::npos) break;

        std::wstring obj = arr.substr(oStart, oEnd - oStart + 1);
        int id = (int)parseDoubleField(obj, L"id", -1.0);

        if (id >= 0 && id < kNumBands) {
            double gain = std::max(DB_LO, std::min(DB_HI,
                parseDoubleField(obj, L"gain", 0.0)));
            double freq = std::max(FREQ_LO, std::min(FREQ_HI,
                parseDoubleField(obj, L"freq", kBandDefs[id].defaultFreq)));
            double q    = std::max(Q_LO, std::min(Q_HI,
                parseDoubleField(obj, L"q", kBandDefs[id].defaultQ)));

            double ng = gainToNorm(gain);
            double nf = freqToNorm(freq);
            double nq = qToNorm(q);

            auto commit = [&](int p, double nv, ParamID pid) {
                if (std::abs(nv - _normParams[id][p]) > 0.0002) {
                    _normParams[id][p] = nv;
                    if (_controller) {
                        _controller->beginEdit(pid);
                        _controller->performEdit(pid, nv);
                        _controller->endEdit(pid);
                    }
                }
            };
            commit(0, ng, kGain(id));
            commit(1, nf, kFreq(id));
            commit(2, nq, kQ(id));
        }
        pos = oEnd + 1;
    }
}

// ── Simple JSON field parsers (no external library) ───────────────────────────
std::wstring EKGEQEditor::parseStringField(const std::wstring& json, const wchar_t* key) {
    std::wstring needle = std::wstring(L"\"") + key + L"\"";
    size_t k = json.find(needle);
    if (k == std::wstring::npos) return {};
    size_t colon = json.find(L':', k);
    if (colon == std::wstring::npos) return {};
    size_t q1 = json.find(L'"', colon + 1);
    if (q1 == std::wstring::npos) return {};
    size_t q2 = json.find(L'"', q1 + 1);
    if (q2 == std::wstring::npos) return {};
    return json.substr(q1 + 1, q2 - q1 - 1);
}

double EKGEQEditor::parseDoubleField(const std::wstring& json, const wchar_t* key, double def) {
    std::wstring needle = std::wstring(L"\"") + key + L"\"";
    size_t k = json.find(needle);
    if (k == std::wstring::npos) return def;
    size_t colon = json.find(L':', k);
    if (colon == std::wstring::npos) return def;
    size_t p = colon + 1;
    while (p < json.size() && (json[p] == L' ' || json[p] == L'\t')) ++p;
    std::wstring num;
    while (p < json.size() && (json[p] == L'-' || (json[p] >= L'0' && json[p] <= L'9') || json[p] == L'.'))
        num += json[p++];
    if (num.empty()) return def;
    try { return std::stod(num); } catch (...) { return def; }
}

// ── WndProc ───────────────────────────────────────────────────────────────────
LRESULT CALLBACK EKGEQEditor::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    EKGEQEditor* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lp;
        self = static_cast<EKGEQEditor*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (EKGEQEditor*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }

    switch (msg) {
    case WM_ERASEBKGND:
        return 1; // WebView2 covers everything

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (self && !self->_wvReady) {
            RECT r;
            GetClientRect(hwnd, &r);
            HBRUSH br = CreateSolidBrush(RGB(3, 8, 6));
            FillRect(hdc, &r, br);
            DeleteObject(br);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    // ── Focus forwarding — FL Studio steals focus from child HWNDs ──────────
    case WM_SETFOCUS:
        // Route keyboard focus into the WebView2 controller
        if (self && self->_wvCtrl)
            self->_wvCtrl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        return 0;

    case WM_MOUSEACTIVATE:
        // Activate and claim focus when user clicks anywhere in the editor
        SetFocus(hwnd);
        return MA_ACTIVATE;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        // Re-assert focus on every click so FL Studio can't steal it back
        SetFocus(hwnd);
        return DefWindowProcW(hwnd, msg, wp, lp);

    case WM_USER + 1: {
        // Deferred resize posted by onSize() — guaranteed to run on the window thread.
        // SetWindowPos fires WM_SIZE which updates WebView2 bounds below.
        LONG w = (LONG)wp;
        LONG h = (LONG)lp;
        SetWindowPos(hwnd, nullptr, 0, 0, w, h,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_SIZE:
        if (self && self->_wvCtrl) {
            RECT r { 0, 0, (LONG)LOWORD(lp), (LONG)HIWORD(lp) };
            self->_wvCtrl->put_Bounds(r);
        }
        return 0;

    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
