#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

// EKG·EQ — AuraTone Technology · Lumina Aerospace
static const Steinberg::FUID kEKGEQProcessorUID (0xA3F9C2D1, 0x4B5E6F78, 0x9A0B1C2D, 0x3E4F5A6B);
static const Steinberg::FUID kEKGEQControllerUID(0xB4E0D3E2, 0x5C6F7E89, 0xAB1C2D3E, 0x4F5E6B7C);

// Parameter IDs — one per band × 3 params (gain, freq, Q) + bypass
enum EKGEQParams : Steinberg::Vst::ParamID {
    // Band 0 — SUB
    kSub_Gain = 0,
    kSub_Freq = 1,
    kSub_Q    = 2,
    // Band 1 — BASS
    kBass_Gain = 10,
    kBass_Freq = 11,
    kBass_Q    = 12,
    // Band 2 — LO·MID
    kLoMid_Gain = 20,
    kLoMid_Freq = 21,
    kLoMid_Q    = 22,
    // Band 3 — MID
    kMid_Gain = 30,
    kMid_Freq = 31,
    kMid_Q    = 32,
    // Band 4 — HI·MID
    kHiMid_Gain = 40,
    kHiMid_Freq = 41,
    kHiMid_Q    = 42,
    // Band 5 — AIR
    kAir_Gain = 50,
    kAir_Freq = 51,
    kAir_Q    = 52,
    // Global
    kBypass  = 100,
    kNumParams
};
