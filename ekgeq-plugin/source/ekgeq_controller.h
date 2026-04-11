#pragma once
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "pluginterfaces/gui/iplugview.h"
#include "ekgeq_ids.h"

class EKGEQEditor;

class EKGEQController : public Steinberg::Vst::EditController {
public:
    static Steinberg::FUnknown* createInstance(void*) {
        return (Steinberg::Vst::IEditController*) new EKGEQController();
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;

    // Receives messages from the processor (IConnectionPoint::notify)
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* msg) SMTG_OVERRIDE;

    // Editor lifecycle — editor registers/unregisters itself
    void setEditor(EKGEQEditor* ed) { _editor = ed; }

private:
    EKGEQEditor* _editor = nullptr;
};
