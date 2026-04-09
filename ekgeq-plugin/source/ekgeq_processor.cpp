#include "ekgeq_processor.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "base/source/fstreamer.h"
#include <cstring>
#include <algorithm>

EKGEQProcessor::EKGEQProcessor() {
    setControllerClass(kEKGEQControllerUID);
}

tresult PLUGIN_API EKGEQProcessor::initialize(FUnknown* context) {
    tresult result = AudioEffect::initialize(context);
    if (result != kResultOk) return result;
    addAudioInput (STR16("Stereo In"),  SpeakerArr::kStereo);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
    return kResultOk;
}

tresult PLUGIN_API EKGEQProcessor::setBusArrangements(
    SpeakerArrangement* in,  int32 numIn,
    SpeakerArrangement* out, int32 numOut)
{
    if (numIn == 1 && numOut == 1 &&
        in[0] == SpeakerArr::kStereo && out[0] == SpeakerArr::kStereo)
        return AudioEffect::setBusArrangements(in, numIn, out, numOut);
    return kResultFalse;
}

tresult PLUGIN_API EKGEQProcessor::setActive(TBool state) {
    if (state) {
        // Reset filter states on activate
        for (int i = 0; i < 6; ++i) { _filL[i].reset(); _filR[i].reset(); }
        updateFilters();
    }
    return AudioEffect::setActive(state);
}

// ── Biquad rebuild for one band ──────────────────────────────────────
void EKGEQProcessor::rebuildBand(int i) {
    BiquadCoeffs c;
    const auto& def = kBandDefs[i];
    if (def.isShelf) {
        if (def.isHigh)
            c = makeHighShelf(_sampleRate, _freq[i], _gain[i], _q[i]);
        else
            c = makeLowShelf (_sampleRate, _freq[i], _gain[i], _q[i]);
    } else {
        c = makePeaking(_sampleRate, _freq[i], _gain[i], _q[i]);
    }
    _filL[i].setCoeffs(c);
    _filR[i].setCoeffs(c);
}

void EKGEQProcessor::updateFilters() {
    for (int i = 0; i < 6; ++i) rebuildBand(i);
}

// ── Process ─────────────────────────────────────────────────────────
tresult PLUGIN_API EKGEQProcessor::process(ProcessData& data) {
    // 1. Pull parameter changes from host
    if (data.inputParameterChanges) {
        int32 numQueues = data.inputParameterChanges->getParameterCount();
        for (int32 q = 0; q < numQueues; ++q) {
            IParamValueQueue* queue = data.inputParameterChanges->getParameterData(q);
            if (!queue) continue;
            ParamID pid = queue->getParameterId();
            int32   numPoints = queue->getPointCount();
            if (numPoints == 0) continue;

            int32       offset;
            ParamValue  value;
            queue->getPoint(numPoints - 1, offset, value);  // last value wins

            bool dirty = false;
            // Map normalised [0,1] → real units and store
            // Gain:  [0,1] → [-24, +24] dB
            // Freq:  [0,1] → log [20, 20000] Hz
            // Q:     [0,1] → [0.1, 18]
            auto gainOf = [](double v){ return v * 48.0 - 24.0; };
            auto freqOf = [](double v){ return 20.0 * std::pow(1000.0, v); };
            auto qOf    = [](double v){ return 0.1 + v * 17.9; };

            switch (pid) {
                case kSub_Gain:   _gain[0]=gainOf(value); dirty=true; break;
                case kSub_Freq:   _freq[0]=freqOf(value); dirty=true; break;
                case kSub_Q:      _q[0]   =qOf(value);   dirty=true; break;
                case kBass_Gain:  _gain[1]=gainOf(value); dirty=true; break;
                case kBass_Freq:  _freq[1]=freqOf(value); dirty=true; break;
                case kBass_Q:     _q[1]   =qOf(value);   dirty=true; break;
                case kLoMid_Gain: _gain[2]=gainOf(value); dirty=true; break;
                case kLoMid_Freq: _freq[2]=freqOf(value); dirty=true; break;
                case kLoMid_Q:    _q[2]   =qOf(value);   dirty=true; break;
                case kMid_Gain:   _gain[3]=gainOf(value); dirty=true; break;
                case kMid_Freq:   _freq[3]=freqOf(value); dirty=true; break;
                case kMid_Q:      _q[3]   =qOf(value);   dirty=true; break;
                case kHiMid_Gain: _gain[4]=gainOf(value); dirty=true; break;
                case kHiMid_Freq: _freq[4]=freqOf(value); dirty=true; break;
                case kHiMid_Q:    _q[4]   =qOf(value);   dirty=true; break;
                case kAir_Gain:   _gain[5]=gainOf(value); dirty=true; break;
                case kAir_Freq:   _freq[5]=freqOf(value); dirty=true; break;
                case kAir_Q:      _q[5]   =qOf(value);   dirty=true; break;
                case kBypass:     _bypass = (value >= 0.5); break;
                default: break;
            }
            if (dirty) {
                // Rebuild only the band that changed
                int band = (pid / 10);
                if (band < 6) rebuildBand(band);
            }
        }
    }

    // 2. Process audio
    if (data.numSamples == 0 || data.numInputs == 0 || data.numOutputs == 0)
        return kResultOk;

    AudioBusBuffers& inBus  = data.inputs[0];
    AudioBusBuffers& outBus = data.outputs[0];

    float* inL  = inBus.channelBuffers32[0];
    float* inR  = inBus.channelBuffers32[1];
    float* outL = outBus.channelBuffers32[0];
    float* outR = outBus.channelBuffers32[1];

    if (_bypass) {
        std::memcpy(outL, inL, sizeof(float) * data.numSamples);
        std::memcpy(outR, inR, sizeof(float) * data.numSamples);
        return kResultOk;
    }

    for (int32 s = 0; s < data.numSamples; ++s) {
        double l = inL[s];
        double r = inR[s];
        // Serial chain: SUB → BASS → LO·MID → MID → HI·MID → AIR
        for (int i = 0; i < 6; ++i) {
            l = _filL[i].process(l);
            r = _filR[i].process(r);
        }
        outL[s] = static_cast<float>(l);
        outR[s] = static_cast<float>(r);
    }
    return kResultOk;
}

// ── State persistence ─────────────────────────────────────────────
tresult PLUGIN_API EKGEQProcessor::getState(IBStream* state) {
    IBStreamer s(state, kLittleEndian);
    for (int i = 0; i < 6; ++i) {
        s.writeDouble(_gain[i]);
        s.writeDouble(_freq[i]);
        s.writeDouble(_q[i]);
    }
    s.writeInt32(_bypass ? 1 : 0);
    return kResultOk;
}

tresult PLUGIN_API EKGEQProcessor::setState(IBStream* state) {
    IBStreamer s(state, kLittleEndian);
    for (int i = 0; i < 6; ++i) {
        double g, f, q;
        if (!s.readDouble(g) || !s.readDouble(f) || !s.readDouble(q))
            return kResultFalse;
        _gain[i] = g; _freq[i] = f; _q[i] = q;
    }
    int32 bypass = 0;
    s.readInt32(bypass);
    _bypass = (bypass != 0);
    updateFilters();
    return kResultOk;
}
