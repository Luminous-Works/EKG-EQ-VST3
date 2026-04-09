#pragma once
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "biquad.h"
#include "ekgeq_ids.h"
#include <array>

using namespace Steinberg;
using namespace Steinberg::Vst;

// ── Band descriptor ────────────────────────────────────────────────
struct BandDef {
    const char* label;
    double      defaultFreq;
    double      defaultGain;   // dB
    double      defaultQ;
    bool        isShelf;       // true = low/highShelf; false = peaking
    bool        isHigh;        // for shelf: true = high, false = low
};

static const std::array<BandDef, 6> kBandDefs = {{
    { "SUB",    60.0,  0.0, 0.71, true,  false },
    { "BASS",   160.0, 0.0, 0.71, false, false },
    { "LO·MID", 500.0, 0.0, 0.71, false, false },
    { "MID",    2000.0,0.0, 0.71, false, false },
    { "HI·MID", 6000.0,0.0, 0.71, false, false },
    { "AIR",    12000.0,0.0,0.71, true,  true  },
}};

// ── Processor ──────────────────────────────────────────────────────
class EKGEQProcessor : public AudioEffect {
public:
    EKGEQProcessor();

    static FUnknown* createInstance(void*) {
        return (IAudioProcessor*) new EKGEQProcessor();
    }

    // AudioEffect overrides
    tresult PLUGIN_API initialize(FUnknown* context) override;
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* in,  int32 numIn,
                                          SpeakerArrangement* out, int32 numOut) override;
    tresult PLUGIN_API setActive(TBool state) override;
    tresult PLUGIN_API process(ProcessData& data) override;
    tresult PLUGIN_API setState(IBStream* state) override;
    tresult PLUGIN_API getState(IBStream* state) override;

    uint32  PLUGIN_API getLatencySamples() override { return 0; }

private:
    void updateFilters();
    void rebuildBand(int i);

    double _sampleRate = 44100.0;

    // Per-band parameters
    double _gain[6] = {0};
    double _freq[6] = {60, 160, 500, 2000, 6000, 12000};
    double _q[6]    = {0.71, 0.71, 0.71, 0.71, 0.71, 0.71};
    bool   _bypass  = false;

    // Stereo biquad pairs — L and R channel
    BiquadFilter _filL[6], _filR[6];
};
