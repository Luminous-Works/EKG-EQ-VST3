#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"
#include <array>

// EKG·EQ — AuraTone Technology · Lumina Aerospace
static const Steinberg::FUID kEKGEQProcessorUID (0xA3F9C2D1, 0x4B5E6F78, 0x9A0B1C2D, 0x3E4F5A6B);
static const Steinberg::FUID kEKGEQControllerUID(0xB4E0D3E2, 0x5C6F7E89, 0xAB1C2D3E, 0x4F5E6B7C);

// ── Band count ─────────────────────────────────────────────────────
static constexpr int kNumBands = 24;

// ── Band descriptor ────────────────────────────────────────────────
struct BandDef {
    const char* label;
    double      defaultFreq;
    double      defaultGain;
    double      defaultQ;
    bool        isShelf;
    bool        isHigh;
};

static const std::array<BandDef, kNumBands> kBandDefs = {{
    { "SUB-lo",  20.0,   0.0, 0.71, true,  false }, // 0  low shelf
    { "INFRA",   32.0,   0.0, 1.0,  false, false }, // 1
    { "SUB",     50.0,   0.0, 1.0,  false, false }, // 2
    { "DEEP",    80.0,   0.0, 1.0,  false, false }, // 3
    { "KICK",    120.0,  0.0, 1.0,  false, false }, // 4
    { "BASS",    160.0,  0.0, 1.0,  false, false }, // 5
    { "WARMTH",  250.0,  0.0, 1.0,  false, false }, // 6
    { "BOX",     350.0,  0.0, 1.0,  false, false }, // 7
    { "LO-MID",  500.0,  0.0, 1.0,  false, false }, // 8
    { "BODY",    700.0,  0.0, 1.0,  false, false }, // 9
    { "MID",     1000.0, 0.0, 1.0,  false, false }, // 10
    { "NASAL",   1400.0, 0.0, 1.0,  false, false }, // 11
    { "HI-MID",  2000.0, 0.0, 1.0,  false, false }, // 12
    { "UPPER",   2800.0, 0.0, 1.0,  false, false }, // 13
    { "BITE",    4000.0, 0.0, 1.0,  false, false }, // 14
    { "ATTACK",  5500.0, 0.0, 1.0,  false, false }, // 15
    { "SILK",    7000.0, 0.0, 1.0,  false, false }, // 16
    { "CRYSTAL", 8500.0, 0.0, 1.0,  false, false }, // 17
    { "SHEEN",   10000.0,0.0, 1.0,  false, false }, // 18
    { "AIR-LO",  12000.0,0.0, 1.0,  false, false }, // 19
    { "AIR",     14000.0,0.0, 1.0,  false, false }, // 20
    { "ULTRA",   16000.0,0.0, 1.0,  false, false }, // 21
    { "PRES.",   18000.0,0.0, 1.0,  false, false }, // 22
    { "AIR-hi",  20000.0,0.0, 0.71, true,  true  }, // 23 high shelf
}};

// ── Parameter layout ───────────────────────────────────────────────
//   Band N gain  = N * 10 + 0   (N = 0..23)
//   Band N freq  = N * 10 + 1
//   Band N Q     = N * 10 + 2
//   Bypass       = 1000
//   TLT          = 1001

enum EKGEQParams : Steinberg::Vst::ParamID {
    kBypass = 1000,
    kTilt   = 1001,
    kNumParams
};

inline Steinberg::Vst::ParamID kGain(int b) { return (Steinberg::Vst::ParamID)(b * 10 + 0); }
inline Steinberg::Vst::ParamID kFreq(int b) { return (Steinberg::Vst::ParamID)(b * 10 + 1); }
inline Steinberg::Vst::ParamID kQ   (int b) { return (Steinberg::Vst::ParamID)(b * 10 + 2); }
