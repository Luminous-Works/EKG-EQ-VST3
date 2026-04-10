/**
 * EKG·EQ DSP Engine
 * AuraTone Technology · Lumina Console · Lumina Aerospace
 *
 * Wavelet-inspired frequency analysis applied to studio audio:
 *   - Octave-band energy extraction with scale-dependent time constants
 *     (high freq = fast time resolution / low freq = high frequency resolution)
 *     — mirrors the mathematical microscope property of the wavelet transform
 *   - Transient detection → PQRST spike trigger per band
 *   - PCG S1/S2 detection on sub-bass (20–100Hz) — the "lub-dub" of the mix
 *
 * Reference: WT heart sound analysis (IntechOpen, Ch.19510)
 * Applied to: music, not medicine. Same math. Different patient.
 */

'use strict';

// ── Band configuration ─────────────────────────────────────────────────────────
// Each band maps to a cardiac-domain concept:
//   SUB     → ventricular wall — the fundamental body resonance
//   BASS    → body cavity — warmth and weight
//   LO·MID  → formant chamber — vowel resonance
//   MID     → conduction node — the presence zone, speech intelligibility
//   HI·MID  → nerve fiber — attack, consonants, brightness
//   AIR     → surface potential — ultra-high shimmer, air
//
// Time constants (tc) implement the wavelet scale tradeoff:
//   Low freq → slow tc → high frequency resolution (wide analysis window)
//   High freq → fast tc → high time resolution (narrow analysis window)

const EKG_BAND_DEFS = [
  {
    id: 0, name: 'SUB',    type: 'lowshelf',
    freq: 60,    gain: 0, Q: 0.71,
    fLow: 20,    fHigh: 120,
    color: '#7A1020', colorHi: '#FF2D55',
    tc: 0.97,   // slowest — wavelet scale 7-8
    pcg: true,  // carries S1/S2 cardiac sound analogues
  },
  {
    id: 1, name: 'BASS',   type: 'peaking',
    freq: 150,   gain: 0, Q: 1.0,
    fLow: 80,    fHigh: 350,
    color: '#7A5810', colorHi: '#D4A843',
    tc: 0.95,   // wavelet scale 5-6
    pcg: false,
  },
  {
    id: 2, name: 'LO·MID', type: 'peaking',
    freq: 500,   gain: 0, Q: 1.0,
    fLow: 280,   fHigh: 950,
    color: '#005A30', colorHi: '#00FF88',
    tc: 0.92,   // wavelet scale 4
    pcg: false,
  },
  {
    id: 3, name: 'MID',    type: 'peaking',
    freq: 1500,  gain: 0, Q: 1.0,
    fLow: 900,   fHigh: 3200,
    color: '#005055', colorHi: '#00D4B8',
    tc: 0.88,   // wavelet scale 3
    pcg: false,
  },
  {
    id: 4, name: 'HI·MID', type: 'peaking',
    freq: 4500,  gain: 0, Q: 1.0,
    fLow: 2800,  fHigh: 8000,
    color: '#004466', colorHi: '#00AAFF',
    tc: 0.82,   // wavelet scale 2
    pcg: false,
  },
  {
    id: 5, name: 'AIR',    type: 'highshelf',
    freq: 12000, gain: 0, Q: 0.71,
    fLow: 7000,  fHigh: 20000,
    color: '#2A1055', colorHi: '#9B59FF',
    tc: 0.74,   // fastest — wavelet scale 1
    pcg: false,
  },
];

class EKGEQEngine {
  constructor(sampleRate) {
    this.sampleRate = sampleRate;

    this.bands = EKG_BAND_DEFS.map(def => ({
      ...def,
      // Live analysis state
      energy:      0,
      bgEnergy:    0.001,
      spikeDecay:  0,
      spikeActive: false,
      // PCG state (SUB band only)
      s1Decay:     0,
      s2Decay:     0,
      s2Timer:     0,
      s1Active:    false,
    }));
  }

  /**
   * Update per-band energies from FFT data.
   * Implements wavelet-inspired time-frequency tradeoff:
   *   - High-frequency bands use fast time constants (short effective window)
   *   - Low-frequency bands use slow time constants (long effective window)
   * This gives better time resolution at high frequencies and better
   * frequency resolution at low frequencies — same property as the WT.
   *
   * @param {Float32Array} freqData  - from analyser.getFloatFrequencyData() (dB)
   * @param {number}       binHz     - Hz per FFT bin
   */
  updateEnergies(freqData, binHz) {
    const N = freqData.length;

    for (const band of this.bands) {
      // Map frequency range to FFT bins
      const lo = Math.max(0,   Math.floor(band.fLow  / binHz));
      const hi = Math.min(N-1, Math.ceil(band.fHigh / binHz));

      // RMS energy in band (linear)
      let sumSq = 0, count = 0;
      for (let i = lo; i <= hi; i++) {
        const lin = Math.pow(10, freqData[i] / 20);
        sumSq += lin * lin;
        count++;
      }
      const rms = count > 0 ? Math.sqrt(sumSq / count) : 0;

      // Wavelet-inspired dual-rate smoothing
      const tc   = band.tc;          // instantaneous (fast for high freq)
      const tcBg = tc + (1-tc)*0.97; // background (always slower)

      band.energy   = band.energy   * tc   + rms * (1 - tc);
      band.bgEnergy = band.bgEnergy * tcBg + rms * (1 - tcBg);

      // Transient detection: energy ratio vs background
      const ratio = band.bgEnergy > 0 ? band.energy / band.bgEnergy : 1;

      if (ratio > 2.5 && band.energy > 0.004) {
        if (!band.spikeActive) {
          band.spikeDecay  = Math.min(1, Math.max(band.spikeDecay, (ratio - 2.5) / 4));
          band.spikeActive = true;
        }
      } else {
        band.spikeActive = false;
      }

      // Decay spike
      band.spikeDecay = Math.max(0, band.spikeDecay - 0.038);

      // ── PCG S1/S2 (sub-bass band only) ──────────────────────────────────────
      // S1 = the "lub" — first major transient in low frequency content
      // S2 = the "dub" — secondary resonance following S1
      // Analogue in mixing: kick drum fundamental + room tail
      if (band.pcg) {
        if (ratio > 3.5 && !band.s1Active) {
          band.s1Decay  = 1.0;
          band.s1Active = true;
          band.s2Timer  = 7; // ~7 frames @ 60fps ≈ 116ms delay (cardiac S1→S2 timing)
        }
        if (band.s1Active && ratio < 1.8) band.s1Active = false;

        if (band.s2Timer > 0) {
          band.s2Timer--;
          if (band.s2Timer === 0) band.s2Decay = 0.55;
        }

        band.s1Decay = Math.max(0, band.s1Decay - 0.045);
        band.s2Decay = Math.max(0, band.s2Decay - 0.038);
      }
    }
  }

  // ── Parameter setters ─────────────────────────────────────────────────────────
  setGain(id, v)  { this.bands[id].gain = Math.max(-24, Math.min(24, v)); }
  setFreq(id, v)  { this.bands[id].freq = Math.max(20, Math.min(20000, v)); }
  setQ(id, v)     { this.bands[id].Q    = Math.max(0.1, Math.min(10, v)); }
  resetBand(id)   { this.setGain(id, 0); }
  resetAll()      { this.bands.forEach((_, i) => this.resetBand(i)); }
}
