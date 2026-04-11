#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "ekgeq_band_defs.h"

// EKG·EQ — AuraTone Technology · Lumina Aerospace
static const Steinberg::FUID kEKGEQProcessorUID (0xA3F9C2D1, 0x4B5E6F78, 0x9A0B1C2D, 0x3E4F5A6B);
static const Steinberg::FUID kEKGEQControllerUID(0xB4E0D3E2, 0x5C6F7E89, 0xAB1C2D3E, 0x4F5E6B7C);

// ── Parameter layout ───────────────────────────────────────────────
//   Band N gain  = N * 10 + 0   (N = 0..23)
//   Band N freq  = N * 10 + 1
//   Band N Q     = N * 10 + 2
//   Bypass       = 1000
//   TLT          = 1001

enum EKGEQParams : Steinberg::Vst::ParamID {
    kBypass = kBypassID,
    kTilt   = kTiltID,
    kNumParams
};

inline Steinberg::Vst::ParamID kGain(int b) { return (Steinberg::Vst::ParamID)(b * 10 + 0); }
inline Steinberg::Vst::ParamID kFreq(int b) { return (Steinberg::Vst::ParamID)(b * 10 + 1); }
inline Steinberg::Vst::ParamID kQ   (int b) { return (Steinberg::Vst::ParamID)(b * 10 + 2); }
