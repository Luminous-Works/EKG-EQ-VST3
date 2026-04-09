#include "ekgeq_processor.h"
#include "ekgeq_controller.h"
#include "ekgeq_ids.h"
#include "version.h"
#include "public.sdk/source/main/pluginfactory.h"

#define stringPluginName "EKG-EQ"

using namespace Steinberg;
using namespace Steinberg::Vst;

// ── VST3 Plug-in Entry ───────────────────────────────────────────────
// On Windows the linker must export GetPluginFactory.
// This is handled automatically by __declspec(dllexport) in the SDK macros.

BEGIN_FACTORY_DEF(stringCompanyName,
                  stringCompanyWeb,
                  stringCompanyEmail)

    // ── Processor component ─────────────────────────────────────────
    DEF_CLASS2(INLINE_UID_FROM_FUID(kEKGEQProcessorUID),
               PClassInfo::kManyInstances,
               kVstAudioEffectClass,
               stringPluginName,
               Vst::kDistributable,
               "Fx|EQ",
               FULL_VERSION_STR,
               kVstVersionString,
               EKGEQProcessor::createInstance)

    // ── EditController ──────────────────────────────────────────────
    DEF_CLASS2(INLINE_UID_FROM_FUID(kEKGEQControllerUID),
               PClassInfo::kManyInstances,
               kVstComponentControllerClass,
               stringPluginName " Controller",
               0,
               "",
               FULL_VERSION_STR,
               kVstVersionString,
               EKGEQController::createInstance)

END_FACTORY
