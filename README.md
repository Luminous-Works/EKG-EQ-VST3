# EKG·EQ — Master EQ

> *The difference between a cardiologist and a stethoscope.*

**Division:** Lumina.Aerospace · AuraTone Technology · Luminous.Works LLC  
**Format:** VST3 + CLAP  
**Platform:** Windows (macOS pending)

---

## Overview

EKG·EQ is a master equaliser built on the premise that most engineers listen to a mix the way a patient listens to their own heartbeat — they hear something, but they don't know what it means. EKG·EQ is the cardiologist: it reads the signal, identifies the pathology, and prescribes the correction.

The name is not metaphor. The architecture is diagnostic.

## Key Features

### C·AUTO — Cardiogram Auto-EQ
Automatic analysis that reads the frequency response of a track the way an ECG reads a cardiac waveform. Identifies resonances, masks, and phase anomalies and applies corrections.

### A·CTRL — Adaptive Control
Dynamic EQ with per-band adaptive threshold tracking. The curve responds to the incoming signal's behaviour — not just its static spectrum.

## Roadmap

| Feature | Status |
|---|---|
| M/S processing | Queued |
| Dynamic EQ v2.1 | Queued |
| Soft clip output stage | Queued |
| 24 bands | Queued |
| Auto gain compensation | Queued |
| Magnetic snap (lattice quantize) | Queued |

## Build

```bash
git submodule update --init --recursive
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Requires: JUCE 7+, CMake 3.22+, MSVC 2022 (Windows), WebView2 runtime.

## Repository Structure

```
ekgeq-plugin/    — VST3 plugin source
ekgeq-clap/      — CLAP plugin source
standalone_test/ — Standalone test harness
vst3sdk/         — Steinberg VST3 SDK (submodule)
```

## Collaborator

**Dr. Sheldon Miller, MD** — First outside collaborator and feature architect. The "cardiologist vs. stethoscope" framing is his insight, built into the product identity.

---

*Lumina.Aerospace · AuraTone Technology — Luminous.Works LLC*  
*Wisconsin · Built in Jamaica · MORTVI NON SVNT MORTVI*
