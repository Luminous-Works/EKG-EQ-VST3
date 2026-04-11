#include "ekgeq_controller.h"
#include "ekgeq_editor.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "base/source/fstreamer.h"
#include <cmath>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

// ── Normalisation helpers (must mirror ekgeq_processor.cpp) ────────
static double normGain(double db)  { return (db + 24.0) / 48.0; }
static double normFreq(double hz)  { return std::log(hz / 20.0) / std::log(1000.0); }
static double normQ   (double q)   { return (q - 0.1) / 17.9; }
static double normTilt(double db)  { return (db + 12.0) / 24.0; }

// ── Initialize — register all 24 bands + bypass + TLT ──────────────
tresult PLUGIN_API EKGEQController::initialize(FUnknown* context) {
    tresult result = EditController::initialize(context);
    if (result != kResultOk) return result;

    for (int i = 0; i < kNumBands; ++i) {
        const auto& def = kBandDefs[i];

        // Gain  (-24 … +24 dB, default 0 dB)
        parameters.addParameter(
            STR16("Gain"), STR16("dB"), 0, normGain(def.defaultGain),
            ParameterInfo::kCanAutomate, kGain(i));

        // Freq (log-scaled)
        parameters.addParameter(
            STR16("Freq"), STR16("Hz"), 0, normFreq(def.defaultFreq),
            ParameterInfo::kCanAutomate, kFreq(i));

        // Q (0.1 … 18)
        parameters.addParameter(
            STR16("Q"), nullptr, 0, normQ(def.defaultQ),
            ParameterInfo::kCanAutomate, kQ(i));
    }

    // Bypass
    parameters.addParameter(
        STR16("Bypass"), nullptr, 1, 0,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass, kBypass);

    // TLT — spectral tilt (-12 … +12 dB, default 0)
    parameters.addParameter(
        STR16("TLT"), STR16("dB"), 0, normTilt(0.0),
        ParameterInfo::kCanAutomate, kTilt);

    return kResultOk;
}

// ── Sync from processor state ────────────────────────────────────────
tresult PLUGIN_API EKGEQController::setComponentState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    for (int i = 0; i < kNumBands; ++i) {
        double g = 0, f = kBandDefs[i].defaultFreq, q = kBandDefs[i].defaultQ;
        if (!s.readDouble(g) || !s.readDouble(f) || !s.readDouble(q)) break;
        setParamNormalized(kGain(i), normGain(g));
        setParamNormalized(kFreq(i), normFreq(f));
        setParamNormalized(kQ(i),    normQ(q));
    }
    int32 bypass = 0; s.readInt32(bypass);
    setParamNormalized(kBypass, bypass ? 1.0 : 0.0);
    double tilt = 0; s.readDouble(tilt);
    setParamNormalized(kTilt, normTilt(tilt));
    return kResultOk;
}

// ── Editor lifecycle ────────────────────────────────────────────────
IPlugView* PLUGIN_API EKGEQController::createView(FIDString name) {
    if (name && strcmp(name, "editor") == 0) {
        auto* ed = new EKGEQEditor(static_cast<EditController*>(this));
        _editor = ed;
        return ed;
    }
    return nullptr;
}

// ── Receive spectrum messages from processor (IConnectionPoint::notify) ─────
tresult PLUGIN_API EKGEQController::notify(IMessage* msg) {
    if (!msg) return kResultFalse;

    if (!strcmp(msg->getMessageID(), "spectrum")) {
        if (_editor) {
            IAttributeList* attrs = msg->getAttributes();
            if (attrs) {
                float energy[kNumBands] = {};
                for (int i = 0; i < kNumBands; ++i) {
                    char key[4] = { 'b', (char)('0' + i/10), (char)('0' + i%10), 0 };
                    double v = 0;
                    attrs->getFloat(key, v);
                    energy[i] = (float)v;
                }
                _editor->receiveSpectrum(energy);
            }
        }
        return kResultOk;
    }

    // ARIA loopback spectrum — full system mix via PortCls/KS-Direct (C·AUTO reference)
    if (!strcmp(msg->getMessageID(), "loopbackSpectrum")) {
        if (_editor) {
            IAttributeList* attrs = msg->getAttributes();
            if (attrs) {
                float energy[kNumBands] = {};
                for (int i = 0; i < kNumBands; ++i) {
                    char key[4] = { 'b', (char)('0' + i/10), (char)('0' + i%10), 0 };
                    double v = 0;
                    attrs->getFloat(key, v);
                    energy[i] = (float)v;
                }
                double ariaFlag = 0;
                attrs->getFloat("aria", ariaFlag);
                _editor->receiveLoopbackSpectrum(energy, ariaFlag > 0.5);
            }
        }
        return kResultOk;
    }

    return EditController::notify(msg);
}
