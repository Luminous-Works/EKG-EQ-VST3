#pragma once
#include "pluginterfaces/gui/iplugview.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "base/source/fobject.h"
#include "ekgeq_ids.h"
#include <windows.h>
#include "../webview2/include/WebView2.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

static constexpr int EDITOR_W = 1024;
static constexpr int EDITOR_H =  640;

class EKGEQEditor : public FObject, public IPlugView {
public:
    explicit EKGEQEditor(EditController* controller);
    ~EKGEQEditor();

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) SMTG_OVERRIDE;
    tresult PLUGIN_API attached(void* parent, FIDString type) SMTG_OVERRIDE;
    tresult PLUGIN_API removed() SMTG_OVERRIDE;
    tresult PLUGIN_API getSize(ViewRect* size) SMTG_OVERRIDE;
    tresult PLUGIN_API onSize(ViewRect* newSize) SMTG_OVERRIDE;
    tresult PLUGIN_API onFocus(TBool) SMTG_OVERRIDE { return kResultOk; }
    tresult PLUGIN_API setFrame(IPlugFrame* f) SMTG_OVERRIDE { _frame = f; return kResultOk; }
    tresult PLUGIN_API canResize() SMTG_OVERRIDE { return kResultTrue; }
    tresult PLUGIN_API checkSizeConstraint(ViewRect* rect) SMTG_OVERRIDE;
    tresult PLUGIN_API onWheel(float) SMTG_OVERRIDE { return kResultFalse; }
    tresult PLUGIN_API onKeyDown(char16, int16, int16) SMTG_OVERRIDE { return kResultFalse; }
    tresult PLUGIN_API onKeyUp(char16, int16, int16) SMTG_OVERRIDE { return kResultFalse; }

    // Called by EKGEQController when processor sends spectrum updates
    void receiveSpectrum        (const float energy[24]);
    // ARIA loopback spectrum — full system mix via PortCls/KS-Direct
    void receiveLoopbackSpectrum(const float energy[24], bool ariaActive);

    OBJ_METHODS(EKGEQEditor, FObject)
    REFCOUNT_METHODS(FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(IPlugView)
    END_DEFINE_INTERFACES(FObject)

private:
    EditController*        _controller;
    IPlugFrame*            _frame       = nullptr;
    HWND                   _hwnd        = nullptr;
    bool                   _bypass      = false;
    int                    _currentW    = EDITOR_W;
    int                    _currentH    = EDITOR_H;
    double                 _normParams[24][3] = {};

    // WebView2 state
    ICoreWebView2Controller* _wvCtrl    = nullptr;
    ICoreWebView2*            _wv       = nullptr;
    bool                      _wvReady  = false;
    HMODULE                   _wvDLL    = nullptr;
    EventRegistrationToken    _msgToken   = {};
    EventRegistrationToken    _accelToken = {};

    // WebView2 factory typedef
    typedef HRESULT (*CreateEnvFn)(
        PCWSTR browserExecutableFolder,
        PCWSTR userDataFolder,
        ICoreWebView2EnvironmentOptions* environmentOptions,
        ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* environmentCreatedHandler);

    CreateEnvFn loadWebView2Factory();

    void syncFromController();
    void sendAllParamsToJS();
    void onJSParamChange(LPCWSTR json);
    std::wstring buildHtmlFileUri() const;

    static std::wstring parseStringField(const std::wstring& json, const wchar_t* key);
    static double parseDoubleField(const std::wstring& json, const wchar_t* key, double def = 0.0);

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static bool s_classRegistered;
};
