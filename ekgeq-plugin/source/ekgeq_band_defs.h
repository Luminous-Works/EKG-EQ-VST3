#pragma once
// ── EKG·EQ band topology — shared by VST3 and CLAP, no SDK dependencies ──
#include <array>

static constexpr int kNumBands = 24;

// Bypass and Tilt param IDs (mirror VST3 + CLAP both use these literals)
static constexpr int kBypassID = 1000;
static constexpr int kTiltID   = 1001;

struct BandDef {
    const char* label;
    double      defaultFreq;
    double      defaultGain;
    double      defaultQ;
    bool        isShelf;
    bool        isHigh;
};

static const std::array<BandDef, kNumBands> kBandDefs = {{
    { "SUB-lo",  20.0,    0.0, 0.71, true,  false }, // 0  low shelf
    { "INFRA",   32.0,    0.0, 1.0,  false, false }, // 1
    { "SUB",     50.0,    0.0, 1.0,  false, false }, // 2
    { "DEEP",    80.0,    0.0, 1.0,  false, false }, // 3
    { "KICK",    120.0,   0.0, 1.0,  false, false }, // 4
    { "BASS",    160.0,   0.0, 1.0,  false, false }, // 5
    { "WARMTH",  250.0,   0.0, 1.0,  false, false }, // 6
    { "BOX",     350.0,   0.0, 1.0,  false, false }, // 7
    { "LO-MID",  500.0,   0.0, 1.0,  false, false }, // 8
    { "BODY",    700.0,   0.0, 1.0,  false, false }, // 9
    { "MID",     1000.0,  0.0, 1.0,  false, false }, // 10
    { "NASAL",   1400.0,  0.0, 1.0,  false, false }, // 11
    { "HI-MID",  2000.0,  0.0, 1.0,  false, false }, // 12
    { "UPPER",   2800.0,  0.0, 1.0,  false, false }, // 13
    { "BITE",    4000.0,  0.0, 1.0,  false, false }, // 14
    { "ATTACK",  5500.0,  0.0, 1.0,  false, false }, // 15
    { "SILK",    7000.0,  0.0, 1.0,  false, false }, // 16
    { "CRYSTAL", 8500.0,  0.0, 1.0,  false, false }, // 17
    { "SHEEN",   10000.0, 0.0, 1.0,  false, false }, // 18
    { "AIR-LO",  12000.0, 0.0, 1.0,  false, false }, // 19
    { "AIR",     14000.0, 0.0, 1.0,  false, false }, // 20
    { "ULTRA",   16000.0, 0.0, 1.0,  false, false }, // 21
    { "PRES.",   18000.0, 0.0, 1.0,  false, false }, // 22
    { "AIR-hi",  20000.0, 0.0, 0.71, true,  true  }, // 23 high shelf
}};
