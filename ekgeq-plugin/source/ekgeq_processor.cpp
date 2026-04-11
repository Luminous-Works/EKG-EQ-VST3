// ═══════════════════════════════════════════════════════════════════════════
// EKG·EQ — Processor
// AuraTone Technology · Luminous.Works LLC
//
// ARIA integration: C·AUTO reads the system mix via PortCls/KS-Direct loopback
// (no WASAPI, no AudioSrv) and applies real-time spectral balance correction.
// The ARIA singleton is shared across all plugin instances in the process.
// ═══════════════════════════════════════════════════════════════════════════

#include "ekgeq_processor.h"
#include "biquad.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "base/source/fstreamer.h"

#include <aria/aria.h>

#include <cstring>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <mutex>

// ════════════════════════════════════════════════════════════════════════════
// AriaAnalyzer — shared singleton for loopback spectrum analysis
//
// All plugin instances in the process share one ARIA loopback stream.
// The singleton tracks ref counts: init on first acquire, shutdown on last
// release. All spectrum reads from process() are lock-free via atomics.
// ════════════════════════════════════════════════════════════════════════════
namespace {

struct AriaAnalyzer {
    // Per-band RMS from loopback — written by ARIA capture thread,
    // read by VST3 process() thread. Lock-free.
    std::atomic<float> loopbandEnv[kNumBands];

    // Lifetime management
    std::atomic<int>  refCount{0};
    std::mutex        initMtx;
    bool              ariaRunning = false;

    // Detection filters — only touched by the ARIA callback thread
    BiquadFilter detL[kNumBands], detR[kNumBands];
    float        detEnv[kNumBands] = {};
    bool         filtersBuilt = false;

    AriaAnalyzer() {
        for (int i = 0; i < kNumBands; ++i)
            loopbandEnv[i].store(0.0f, std::memory_order_relaxed);
    }

    void buildFilters(double sr) {
        for (int i = 0; i < kNumBands; ++i) {
            BiquadCoeffs c = makeBandpass(sr, kBandDefs[i].defaultFreq, 2.0);
            detL[i].setCoeffs(c); detL[i].reset();
            detR[i].setCoeffs(c); detR[i].reset();
        }
        filtersBuilt = true;
    }

    void acquire() {
        if (refCount.fetch_add(1) == 0) {
            std::lock_guard<std::mutex> lk(initMtx);
            start();
        }
    }

    void release() {
        if (refCount.fetch_sub(1) == 1) {
            std::lock_guard<std::mutex> lk(initMtx);
            stop();
        }
    }

    void start() {
        if (ariaRunning) return;
        if (aria::init() != aria::Error::None) return;

        aria::StreamConfig cfg{};
        cfg.sample_rate   = 48000;
        cfg.channels      = 2;
        cfg.buffer_frames = 480;  // 10 ms @ 48 kHz
        cfg.loopback      = true;

        if (aria::capture::open_loopback(cfg) != aria::Error::None) {
            aria::shutdown();
            return;
        }

        buildFilters(48000.0);

        aria::Error err = aria::capture::start([this](const aria::AudioBuffer& buf) {
            onBuffer(buf);
        });

        if (err != aria::Error::None) {
            aria::capture::close();
            aria::shutdown();
            return;
        }

        ariaRunning = true;
    }

    void stop() {
        if (!ariaRunning) return;
        aria::capture::stop();
        aria::capture::close();
        aria::shutdown();
        ariaRunning = false;
    }

    // Called from ARIA capture thread — must be real-time safe
    void onBuffer(const aria::AudioBuffer& buf) noexcept {
        if (!filtersBuilt) return;
        if (buf.format != aria::SampleFormat::Float32) return;
        if (buf.frame_count == 0) return;

        const float* data  = static_cast<const float*>(buf.data);
        const uint32_t ch  = buf.channels;
        const uint32_t n   = buf.frame_count;

        // Per-band bandpass RMS (stereo)
        double accL[kNumBands] = {}, accR[kNumBands] = {};
        for (uint32_t s = 0; s < n; ++s) {
            double xL = data[s * ch + 0];
            double xR = (ch > 1) ? data[s * ch + 1] : xL;
            for (int i = 0; i < kNumBands; ++i) {
                double yL = detL[i].process(xL);
                double yR = detR[i].process(xR);
                accL[i] += yL * yL;
                accR[i] += yR * yR;
            }
        }

        // Smooth envelope
        const double kAlpha = 0.96;
        const double inv2n  = 1.0 / (2.0 * n);
        for (int i = 0; i < kNumBands; ++i) {
            float rms = std::sqrt((float)((accL[i] + accR[i]) * inv2n));
            detEnv[i] = (float)(kAlpha * detEnv[i] + (1.0 - kAlpha) * rms);
            loopbandEnv[i].store(detEnv[i], std::memory_order_relaxed);
        }
    }

    bool isRunning() const noexcept { return ariaRunning; }
};

static AriaAnalyzer s_aria;

} // anonymous namespace


// ════════════════════════════════════════════════════════════════════════════
// EKGEQProcessor
// ════════════════════════════════════════════════════════════════════════════

EKGEQProcessor::EKGEQProcessor() {
    setControllerClass(kEKGEQControllerUID);
    for (int i = 0; i < kNumBands; ++i) {
        _freq[i] = kBandDefs[i].defaultFreq;
        _q[i]    = kBandDefs[i].defaultQ;
        _gain[i] = 0.0;
    }
}

EKGEQProcessor::~EKGEQProcessor() {
    s_aria.release();
}

tresult PLUGIN_API EKGEQProcessor::initialize(FUnknown* context) {
    tresult result = AudioEffect::initialize(context);
    if (result != kResultOk) return result;
    addAudioInput (STR16("Stereo In"),  SpeakerArr::kStereo);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
    return kResultOk;
}

tresult PLUGIN_API EKGEQProcessor::setupProcessing(ProcessSetup& newSetup) {
    _sampleRate = newSetup.sampleRate;
    return AudioEffect::setupProcessing(newSetup);
}

tresult PLUGIN_API EKGEQProcessor::setBusArrangements(
    SpeakerArrangement* in, int32 numIn,
    SpeakerArrangement* out, int32 numOut)
{
    if (numIn == 1 && numOut == 1 &&
        in[0] == SpeakerArr::kStereo && out[0] == SpeakerArr::kStereo)
        return AudioEffect::setBusArrangements(in, numIn, out, numOut);
    return kResultFalse;
}

// ── Biquad rebuild ──────────────────────────────────────────────────
void EKGEQProcessor::rebuildBand(int i) {
    BiquadCoeffs c;
    if (kBandDefs[i].isShelf) {
        if (kBandDefs[i].isHigh)
            c = makeHighShelf(_sampleRate, _freq[i], _gain[i] + _cautoGain[i], _q[i]);
        else
            c = makeLowShelf (_sampleRate, _freq[i], _gain[i] + _cautoGain[i], _q[i]);
    } else {
        c = makePeaking(_sampleRate, _freq[i], _gain[i] + _cautoGain[i], _q[i]);
    }
    _filL[i].setCoeffs(c);
    _filR[i].setCoeffs(c);
}

void EKGEQProcessor::updateFilters() {
    for (int i = 0; i < kNumBands; ++i) rebuildBand(i);
    rebuildTilt();
}

// ── TLT (spectral tilt) ─────────────────────────────────────────────
void EKGEQProcessor::rebuildTilt() {
    if (std::fabs(_tilt) < 0.001) {
        _tiltL.reset(); _tiltR.reset();
        return;
    }
    BiquadCoeffs c = makeHighShelf(_sampleRate, 1000.0, _tilt, 0.71);
    _tiltL.setCoeffs(c);
    _tiltR.setCoeffs(c);
}

tresult PLUGIN_API EKGEQProcessor::setActive(TBool state) {
    if (state) {
        // Reset signal chain
        for (int i = 0; i < kNumBands; ++i) {
            _filL[i].reset(); _filR[i].reset();
            BiquadCoeffs dc = makeBandpass(_sampleRate, kBandDefs[i].defaultFreq, 2.0);
            _detL[i].setCoeffs(dc); _detL[i].reset();
            _detR[i].setCoeffs(dc); _detR[i].reset();
            _detEnv[i] = 0.0f;
        }
        _tiltL.reset(); _tiltR.reset();
        _detThrottle = 0;
        updateFilters();

        // Acquire ARIA loopback (ref-counted, no-op if already running)
        s_aria.acquire();
    } else {
        s_aria.release();
    }
    return AudioEffect::setActive(state);
}

// ── Process ─────────────────────────────────────────────────────────
tresult PLUGIN_API EKGEQProcessor::process(ProcessData& data) {
    // 1. Pull parameter changes
    if (data.inputParameterChanges) {
        int32 numQueues = data.inputParameterChanges->getParameterCount();
        for (int32 q = 0; q < numQueues; ++q) {
            IParamValueQueue* queue = data.inputParameterChanges->getParameterData(q);
            if (!queue) continue;
            ParamID  pid = queue->getParameterId();
            int32    np  = queue->getPointCount();
            if (np == 0) continue;
            int32      offset; ParamValue value;
            queue->getPoint(np - 1, offset, value);

            auto gainOf = [](double v){ return v * 48.0 - 24.0; };
            auto freqOf = [](double v){ return 20.0 * std::pow(1000.0, v); };
            auto qOf    = [](double v){ return 0.1 + v * 17.9; };
            auto tiltOf = [](double v){ return v * 24.0 - 12.0; };

            if (pid < (ParamID)(kNumBands * 10)) {
                int band  = (int)(pid / 10);
                int param = (int)(pid % 10);
                if (band < kNumBands && param < 3) {
                    if (param == 0) _gain[band] = gainOf(value);
                    if (param == 1) _freq[band] = freqOf(value);
                    if (param == 2) _q[band]    = qOf(value);
                    rebuildBand(band);
                }
            } else if (pid == kBypass) {
                _bypass = (value >= 0.5);
            } else if (pid == kTilt) {
                _tilt = tiltOf(value);
                rebuildTilt();
            }
        }
    }

    // 2. Process audio
    if (data.numSamples == 0 || data.numInputs == 0 || data.numOutputs == 0)
        return kResultOk;

    AudioBusBuffers& inBus  = data.inputs[0];
    AudioBusBuffers& outBus = data.outputs[0];

    if (inBus.numChannels < 1 || outBus.numChannels < 1) return kResultOk;

    float* inL  = inBus.channelBuffers32[0];
    float* inR  = inBus.numChannels > 1 ? inBus.channelBuffers32[1]  : inBus.channelBuffers32[0];
    float* outL = outBus.channelBuffers32[0];
    float* outR = outBus.numChannels > 1 ? outBus.channelBuffers32[1] : outBus.channelBuffers32[0];

    if (!inL || !outL) return kResultOk;

    // 3. Spectrum detection on PRE-EQ input (runs even in bypass)
    double detAccL[kNumBands] = {}, detAccR[kNumBands] = {};
    for (int32 s = 0; s < data.numSamples; ++s) {
        double xL = inL[s], xR = inR[s];
        for (int i = 0; i < kNumBands; ++i) {
            double yL = _detL[i].process(xL);
            double yR = _detR[i].process(xR);
            detAccL[i] += yL * yL;
            detAccR[i] += yR * yR;
        }
    }
    {
        const double kAlpha = 0.97;
        const int    N      = data.numSamples > 0 ? data.numSamples : 1;
        for (int i = 0; i < kNumBands; ++i) {
            float rms = std::sqrt((float)((detAccL[i] + detAccR[i]) / (2.0 * N)));
            _detEnv[i] = (float)(kAlpha * _detEnv[i] + (1.0 - kAlpha) * rms);
        }
    }

    // 4. C·AUTO — read ARIA loopback spectrum, apply spectral balance correction
    // Reads are lock-free (atomic relaxed). Correction integrates slowly so the
    // filter rebuild is amortised over many process() blocks.
    if (_cautoEnabled && s_aria.isRunning()) {
        // Normalise both spectra (per-band share of total energy)
        float trackSum = 0.0f, mixSum = 0.0f;
        float mixEnv[kNumBands];
        for (int i = 0; i < kNumBands; ++i) {
            mixEnv[i] = s_aria.loopbandEnv[i].load(std::memory_order_relaxed);
            trackSum += _detEnv[i];
            mixSum   += mixEnv[i];
        }

        if (trackSum > kCautoMinSignal && mixSum > kCautoMinSignal) {
            bool filtersDirty = false;
            for (int i = 0; i < kNumBands; ++i) {
                float trackNorm = _detEnv[i]  / trackSum;
                float mixNorm   = mixEnv[i]   / mixSum;

                // delta > 0 → track is underrepresented at this freq → boost
                // delta < 0 → track is dominant at this freq → cut
                float delta = mixNorm - trackNorm;

                // Scale delta to dB range — max correction step per call
                float correction = delta * 12.0f * kCautoRate * 1000.0f;
                _cautoGain[i] += correction;
                _cautoGain[i]  = std::fmax(-kCautoMaxCorr,
                                 std::fmin( kCautoMaxCorr, _cautoGain[i]));
                filtersDirty = true;
            }
            if (filtersDirty) updateFilters();
        }
    }

    // 5. Send spectrum message every kDetInterval calls
    if (++_detThrottle >= kDetInterval) {
        _detThrottle = 0;
        if (IMessage* msg = allocateMessage()) {
            msg->setMessageID("spectrum");
            IAttributeList* attrs = msg->getAttributes();
            if (attrs) {
                for (int i = 0; i < kNumBands; ++i) {
                    char key[4] = { 'b', (char)('0' + i/10), (char)('0' + i%10), 0 };
                    attrs->setFloat(key, (double)_detEnv[i]);
                }
            }
            sendMessage(msg);
            msg->release();
        }

        // Also send ARIA loopback spectrum so UI can display C·AUTO reference
        if (s_aria.isRunning()) {
            if (IMessage* lbmsg = allocateMessage()) {
                lbmsg->setMessageID("loopbackSpectrum");
                IAttributeList* attrs = lbmsg->getAttributes();
                if (attrs) {
                    for (int i = 0; i < kNumBands; ++i) {
                        char key[4] = { 'b', (char)('0' + i/10), (char)('0' + i%10), 0 };
                        float v = s_aria.loopbandEnv[i].load(std::memory_order_relaxed);
                        attrs->setFloat(key, (double)v);
                    }
                    // Report active backend name so UI can show "ARIA PortCls" indicator
                    attrs->setFloat("aria", 1.0);
                }
                sendMessage(lbmsg);
                lbmsg->release();
            }
        }
    }

    // 6. EQ processing
    if (_bypass) {
        std::memcpy(outL, inL, sizeof(float) * data.numSamples);
        std::memcpy(outR, inR, sizeof(float) * data.numSamples);
        return kResultOk;
    }

    const bool applyTilt = (std::fabs(_tilt) >= 0.001);
    for (int32 s = 0; s < data.numSamples; ++s) {
        double l = inL[s], r = inR[s];
        for (int i = 0; i < kNumBands; ++i) {
            l = _filL[i].process(l);
            r = _filR[i].process(r);
        }
        if (applyTilt) {
            l = _tiltL.process(l);
            r = _tiltR.process(r);
        }
        outL[s] = (float)l;
        outR[s] = (float)r;
    }
    return kResultOk;
}

// ── State persistence ─────────────────────────────────────────────
tresult PLUGIN_API EKGEQProcessor::getState(IBStream* state) {
    IBStreamer s(state, kLittleEndian);
    for (int i = 0; i < kNumBands; ++i) {
        s.writeDouble(_gain[i]);
        s.writeDouble(_freq[i]);
        s.writeDouble(_q[i]);
    }
    s.writeInt32(_bypass ? 1 : 0);
    s.writeDouble(_tilt);
    // Persist C·AUTO gain offsets so they survive DAW project save/load
    for (int i = 0; i < kNumBands; ++i)
        s.writeFloat(_cautoGain[i]);
    s.writeInt32(_cautoEnabled ? 1 : 0);
    return kResultOk;
}

tresult PLUGIN_API EKGEQProcessor::setState(IBStream* state) {
    IBStreamer s(state, kLittleEndian);
    for (int i = 0; i < kNumBands; ++i) {
        double g = 0, f = kBandDefs[i].defaultFreq, q = kBandDefs[i].defaultQ;
        s.readDouble(g); s.readDouble(f); s.readDouble(q);
        _gain[i] = g; _freq[i] = f; _q[i] = q;
    }
    int32 bypass = 0; s.readInt32(bypass); _bypass = (bypass != 0);
    double tilt  = 0; s.readDouble(tilt);  _tilt = tilt;
    // Restore C·AUTO state (optional — old sessions won't have these bytes)
    for (int i = 0; i < kNumBands; ++i) {
        float cg = 0.0f;
        if (s.readFloat(cg) == kResultTrue) _cautoGain[i] = cg;
    }
    int32 ce = 1;
    if (s.readInt32(ce) == kResultTrue) _cautoEnabled = (ce != 0);
    updateFilters();
    return kResultOk;
}
