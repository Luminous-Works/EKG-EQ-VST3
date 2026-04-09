#pragma once
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "ekgeq_ids.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

class EKGEQController : public EditController {
public:
    static FUnknown* createInstance(void*) {
        return (IEditController*) new EKGEQController();
    }

    tresult PLUGIN_API initialize(FUnknown* context) override;
    tresult PLUGIN_API setComponentState(IBStream* state) override;
};
