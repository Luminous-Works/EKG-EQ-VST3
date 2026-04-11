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

// 24-band parametric EQ — full spectrum 20Hz–20kHz
// Color gradient: red (sub) → orange → gold → green → cyan → blue → violet (air)
const EKG_BAND_DEFS = [
  { id:0,  name:'SUB·ω',   type:'lowshelf',  freq:20,    gain:0, Q:0.71, fLow:20,    fHigh:28,    color:'#4A0010', colorHi:'#FF2D55', tc:0.985, pcg:true  },
  { id:1,  name:'INFRA',   type:'peaking',   freq:32,    gain:0, Q:1.0,  fLow:22,    fHigh:45,    color:'#6A1020', colorHi:'#FF4433', tc:0.980, pcg:true  },
  { id:2,  name:'SUB',     type:'peaking',   freq:50,    gain:0, Q:1.0,  fLow:36,    fHigh:68,    color:'#7A2010', colorHi:'#FF6633', tc:0.975, pcg:false },
  { id:3,  name:'DEEP',    type:'peaking',   freq:80,    gain:0, Q:1.0,  fLow:58,    fHigh:105,   color:'#7A3808', colorHi:'#FF8C00', tc:0.970, pcg:false },
  { id:4,  name:'KICK',    type:'peaking',   freq:120,   gain:0, Q:1.0,  fLow:95,    fHigh:145,   color:'#7A5008', colorHi:'#FFAA00', tc:0.965, pcg:false },
  { id:5,  name:'BASS',    type:'peaking',   freq:160,   gain:0, Q:1.0,  fLow:130,   fHigh:210,   color:'#7A6010', colorHi:'#D4A843', tc:0.960, pcg:false },
  { id:6,  name:'WARMTH',  type:'peaking',   freq:250,   gain:0, Q:1.0,  fLow:200,   fHigh:310,   color:'#506010', colorHi:'#BBBB00', tc:0.954, pcg:false },
  { id:7,  name:'BOX',     type:'peaking',   freq:350,   gain:0, Q:1.0,  fLow:295,   fHigh:420,   color:'#226020', colorHi:'#55CC44', tc:0.948, pcg:false },
  { id:8,  name:'LO·MID',  type:'peaking',   freq:500,   gain:0, Q:1.0,  fLow:415,   fHigh:620,   color:'#006030', colorHi:'#00EE88', tc:0.942, pcg:false },
  { id:9,  name:'BODY',    type:'peaking',   freq:700,   gain:0, Q:1.0,  fLow:590,   fHigh:870,   color:'#006050', colorHi:'#00DDC0', tc:0.936, pcg:false },
  { id:10, name:'MID',     type:'peaking',   freq:1000,  gain:0, Q:1.0,  fLow:840,   fHigh:1250,  color:'#005568', colorHi:'#00C8E8', tc:0.930, pcg:false },
  { id:11, name:'NASAL',   type:'peaking',   freq:1400,  gain:0, Q:1.0,  fLow:1180,  fHigh:1700,  color:'#004578', colorHi:'#00AAFF', tc:0.922, pcg:false },
  { id:12, name:'HI·MID',  type:'peaking',   freq:2000,  gain:0, Q:1.0,  fLow:1650,  fHigh:2500,  color:'#003588', colorHi:'#3399FF', tc:0.914, pcg:false },
  { id:13, name:'UPPER',   type:'peaking',   freq:2800,  gain:0, Q:1.0,  fLow:2300,  fHigh:3500,  color:'#253090', colorHi:'#5577FF', tc:0.906, pcg:false },
  { id:14, name:'BITE',    type:'peaking',   freq:4000,  gain:0, Q:1.0,  fLow:3200,  fHigh:5000,  color:'#352085', colorHi:'#7755EE', tc:0.897, pcg:false },
  { id:15, name:'ATTACK',  type:'peaking',   freq:5500,  gain:0, Q:1.0,  fLow:4500,  fHigh:6700,  color:'#401878', colorHi:'#9944DD', tc:0.887, pcg:false },
  { id:16, name:'SILK',    type:'peaking',   freq:7000,  gain:0, Q:1.0,  fLow:6000,  fHigh:8200,  color:'#460f6c', colorHi:'#AA33CC', tc:0.876, pcg:false },
  { id:17, name:'CRYSTAL', type:'peaking',   freq:8500,  gain:0, Q:1.0,  fLow:7500,  fHigh:9700,  color:'#420860', colorHi:'#BB44BB', tc:0.864, pcg:false },
  { id:18, name:'SHEEN',   type:'peaking',   freq:10000, gain:0, Q:1.0,  fLow:9000,  fHigh:11200, color:'#3E0058', colorHi:'#CC55AA', tc:0.851, pcg:false },
  { id:19, name:'AIR·LO',  type:'peaking',   freq:12000, gain:0, Q:1.0,  fLow:10800, fHigh:13500, color:'#3A0050', colorHi:'#DD66AA', tc:0.837, pcg:false },
  { id:20, name:'AIR',     type:'peaking',   freq:14000, gain:0, Q:1.0,  fLow:12800, fHigh:15500, color:'#360048', colorHi:'#EE77BB', tc:0.822, pcg:false },
  { id:21, name:'ULTRA',   type:'peaking',   freq:16000, gain:0, Q:1.0,  fLow:14800, fHigh:17500, color:'#2C0040', colorHi:'#FF88BB', tc:0.806, pcg:false },
  { id:22, name:'PRES.',   type:'peaking',   freq:18000, gain:0, Q:1.0,  fLow:16500, fHigh:19500, color:'#220038', colorHi:'#FF99CC', tc:0.788, pcg:false },
  { id:23, name:'AIR·ω',   type:'highshelf', freq:20000, gain:0, Q:0.71, fLow:18500, fHigh:20000, color:'#180030', colorHi:'#FFAADD', tc:0.770, pcg:false },
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
