#pragma once
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "base/source/fobject.h"
#include "ekgeq_ids.h"
#include <windows.h>

using namespace Steinberg;
using namespace Steinberg::Vst;

// ── Editor dimensions ────────────────────────────────────────────────
static constexpr int EDITOR_W = 840;
static constexpr int EDITOR_H = 240;

// ── EKG-EQ Win32 Editor Panel ───────────────────────────────────────
// Pure Win32 — no VSTGUI dependency. 6 band columns × 3 sliders each.
class EKGEQEditor : public FObject, public IPlugView {
public:
    explicit EKGEQEditor(IEditController* controller);
    ~EKGEQEditor();

    // IPlugView
    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) SMTG_OVERRIDE;
    tresult PLUGIN_API attached(void* parent, FIDString type) SMTG_OVERRIDE;
    tresult PLUGIN_API removed() SMTG_OVERRIDE;
    tresult PLUGIN_API getSize(ViewRect* size) SMTG_OVERRIDE;
    tresult PLUGIN_API onSize(ViewRect*) SMTG_OVERRIDE { return kResultOk; }
    tresult PLUGIN_API onFocus(TBool) SMTG_OVERRIDE { return kResultOk; }
    tresult PLUGIN_API setFrame(IPlugFrame* f) SMTG_OVERRIDE { _frame = f; return kResultOk; }
    tresult PLUGIN_API canResize() SMTG_OVERRIDE { return kResultFalse; }
    tresult PLUGIN_API checkSizeConstraint(ViewRect*) SMTG_OVERRIDE { return kResultOk; }
    tresult PLUGIN_API onWheel(float) SMTG_OVERRIDE { return kResultFalse; }
    tresult PLUGIN_API onKeyDown(char16, int16, int16) SMTG_OVERRIDE { return kResultFalse; }
    tresult PLUGIN_API onKeyUp(char16, int16, int16) SMTG_OVERRIDE { return kResultFalse; }

    OBJ_METHODS(EKGEQEditor, FObject)
    REFCOUNT_METHODS(FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(IPlugView)
    END_DEFINE_INTERFACES(FObject)

    // Called by WndProc on slider / button changes
    void onSliderChange(int ctrlId, int pos);
    void onBypassToggle(bool on);

private:
    IEditController* _controller;
    IPlugFrame*      _frame   = nullptr;
    HWND             _hwnd    = nullptr;
    HWND             _sliders[18] = {};   // [band*3 + param]
    HWND             _valLbls[18] = {};
    HWND             _bypassBtn   = nullptr;
    bool             _syncing     = false;

    void createControls(HWND parent);
    void syncFromController();
    void updateLabel(int idx, double norm);

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static bool s_classRegistered;
};
