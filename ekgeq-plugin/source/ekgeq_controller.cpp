#include "ekgeq_controller.h"
#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"
#include <cmath>

// ── Param registration helpers ───────────────────────────────────────
// Normalized ↔ real conversions (must mirror ekgeq_processor.cpp)
//   gain:  [0,1] → [-24, +24] dB   default 0 dB  → norm 0.5
//   freq:  [0,1] → log [20, 20000] Hz
//   Q:     [0,1] → [0.1, 18]       default 0.71  → norm ≈ 0.034

static double normFreq(double hz) {
    // freqOf(v) = 20 * pow(1000, v)  →  v = log(hz/20) / log(1000)
    return std::log(hz / 20.0) / std::log(1000.0);
}

static double normQ(double q) {
    return (q - 0.1) / 17.9;
}

// ── Band metadata for registration ──────────────────────────────────
struct BandReg {
    const char16_t* label;
    ParamID         gainId, freqId, qId;
    double          defaultHz;
};

static const BandReg kBands[6] = {
    { u"SUB",    kSub_Gain,   kSub_Freq,   kSub_Q,     60.0   },
    { u"BASS",   kBass_Gain,  kBass_Freq,  kBass_Q,    160.0  },
    { u"LO-MID", kLoMid_Gain, kLoMid_Freq, kLoMid_Q,   500.0  },
    { u"MID",    kMid_Gain,   kMid_Freq,   kMid_Q,     2000.0 },
    { u"HI-MID", kHiMid_Gain, kHiMid_Freq, kHiMid_Q,  6000.0 },
    { u"AIR",    kAir_Gain,   kAir_Freq,   kAir_Q,     12000.0},
};

// ── Initialize ───────────────────────────────────────────────────────
tresult PLUGIN_API EKGEQController::initialize(FUnknown* context) {
    tresult result = EditController::initialize(context);
    if (result != kResultOk) return result;

    for (int i = 0; i < 6; ++i) {
        const auto& b = kBands[i];

        // Gain  (−24 … +24 dB, default 0 dB = 0.5 normalised)
        parameters.addParameter(b.label,
            STR16("dB"), 0, 0.5,
            ParameterInfo::kCanAutomate, b.gainId);

        // Freq  (log-scale, default from kBandDefs)
        parameters.addParameter(b.label,
            STR16("Hz"), 0, normFreq(b.defaultHz),
            ParameterInfo::kCanAutomate, b.freqId);

        // Q     (0.1 … 18, default 0.71)
        parameters.addParameter(b.label,
            STR16("Q"), 0, normQ(0.71),
            ParameterInfo::kCanAutomate, b.qId);
    }

    // Bypass (binary toggle)
    parameters.addParameter(STR16("Bypass"), nullptr, 1, 0,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass, kBypass);

    return kResultOk;
}

// ── Sync from processor state ────────────────────────────────────────
// Read the same byte layout that EKGEQProcessor::getState() writes:
//   6 × { double gain, double freq, double Q }  then int32 bypass
tresult PLUGIN_API EKGEQController::setComponentState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);

    for (int i = 0; i < 6; ++i) {
        double g, f, q;
        if (!s.readDouble(g) || !s.readDouble(f) || !s.readDouble(q))
            return kResultFalse;

        double normG = (g + 24.0) / 48.0;
        double normF = normFreq(f);
        double normQ_ = normQ(q);

        setParamNormalized(kBands[i].gainId, normG);
        setParamNormalized(kBands[i].freqId, normF);
        setParamNormalized(kBands[i].qId,    normQ_);
    }

    int32 bypass = 0;
    s.readInt32(bypass);
    setParamNormalized(kBypass, bypass ? 1.0 : 0.0);

    return kResultOk;
}
