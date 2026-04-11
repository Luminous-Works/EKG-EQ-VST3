#pragma once
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "biquad.h"
#include "ekgeq_ids.h"
#include <aria/aria.h>
#include <array>
#include <atomic>
#include <cmath>

using namespace Steinberg;
using namespace Steinberg::Vst;

// ── Processor ──────────────────────────────────────────────────────
class EKGEQProcessor : public AudioEffect {
public:
    EKGEQProcessor();
    ~EKGEQProcessor() override;

    static FUnknown* createInstance(void*) {
        return (IAudioProcessor*) new EKGEQProcessor();
    }

    tresult PLUGIN_API initialize(FUnknown* context) override;
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* in,  int32 numIn,
                                          SpeakerArrangement* out, int32 numOut) override;
    tresult PLUGIN_API setupProcessing(ProcessSetup& newSetup) override;
    tresult PLUGIN_API setActive(TBool state) override;
    tresult PLUGIN_API process(ProcessData& data) override;
    tresult PLUGIN_API setState(IBStream* state) override;
    tresult PLUGIN_API getState(IBStream* state) override;

    uint32  PLUGIN_API getLatencySamples() override { return 0; }
    tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize) override {
        return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
    }

private:
    void rebuildBand(int i);
    void updateFilters();
    void rebuildTilt();

    double _sampleRate = 44100.0;

    // Per-band EQ parameters
    double _gain[kNumBands] = {};
    double _freq[kNumBands];
    double _q[kNumBands];
    bool   _bypass = false;

    // TLT (spectral tilt) — range -12..+12 dB
    double _tilt = 0.0;

    // Stereo EQ biquad chains
    BiquadFilter _filL[kNumBands], _filR[kNumBands];

    // TLT biquad (single one-pole tilt)
    BiquadFilter _tiltL, _tiltR;

    // ── Spectrum detection ─────────────────────────────────────────
    // Fixed-frequency bandpass filters running on the pre-EQ input signal.
    // IIR envelope followers track per-band energy and feed the ECG display.
    BiquadFilter _detL[kNumBands], _detR[kNumBands];
    float        _detEnv[kNumBands] = {};
    int          _detThrottle = 0;
    static constexpr int kDetInterval = 8;  // send spectrum message every N process() calls

    // ── C·AUTO — Cardiac Intelligence (ARIA loopback) ──────────────
    // C·AUTO reads the full mix loopback via ARIA's PortCls/KS-Direct backend.
    // Each process() call applies a slow spectral correction toward mix balance.
    // _cautoGain[i] is additive on top of the operator's manual gain.
    float _cautoGain[kNumBands] = {};   // current C·AUTO correction per band (dB)
    bool  _cautoEnabled = true;
    static constexpr float kCautoRate      = 0.00015f;  // correction speed (dB per call)
    static constexpr float kCautoMaxCorr   = 6.0f;      // max ±6 dB correction
    static constexpr float kCautoMinSignal = 1e-5f;     // ignore bands below noise floor
};
