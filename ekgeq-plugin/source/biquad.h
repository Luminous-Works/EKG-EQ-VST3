#pragma once
#include <cmath>

// Direct-form II transposed biquad — identical topology to Web Audio BiquadFilterNode.
// Supports: peaking, lowShelf, highShelf, highpass, lowpass.

struct BiquadCoeffs { double b0,b1,b2,a1,a2; };

enum class BiquadType { Peaking, LowShelf, HighShelf, HighPass, LowPass };

static BiquadCoeffs makePeaking(double fs, double freq, double gainDB, double Q) {
    double A  = std::pow(10.0, gainDB / 40.0);
    double w0 = 2.0 * M_PI * freq / fs;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / (2.0 * Q);
    double b0 =  1.0 + alpha * A;
    double b1 = -2.0 * cw;
    double b2 =  1.0 - alpha * A;
    double a0 =  1.0 + alpha / A;
    double a1 = -2.0 * cw;
    double a2 =  1.0 - alpha / A;
    return { b0/a0, b1/a0, b2/a0, a1/a0, a2/a0 };
}

static BiquadCoeffs makeLowShelf(double fs, double freq, double gainDB, double Q) {
    double A  = std::pow(10.0, gainDB / 40.0);
    double w0 = 2.0 * M_PI * freq / fs;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / 2.0 * std::sqrt((A + 1.0/A) * (1.0/Q - 1.0) + 2.0);
    double b0 =  A * ((A+1) - (A-1)*cw + 2*std::sqrt(A)*alpha);
    double b1 =  2*A * ((A-1) - (A+1)*cw);
    double b2 =  A * ((A+1) - (A-1)*cw - 2*std::sqrt(A)*alpha);
    double a0 =       (A+1) + (A-1)*cw + 2*std::sqrt(A)*alpha;
    double a1 = -2 * ((A-1) + (A+1)*cw);
    double a2 =       (A+1) + (A-1)*cw - 2*std::sqrt(A)*alpha;
    return { b0/a0, b1/a0, b2/a0, a1/a0, a2/a0 };
}

static BiquadCoeffs makeHighShelf(double fs, double freq, double gainDB, double Q) {
    double A  = std::pow(10.0, gainDB / 40.0);
    double w0 = 2.0 * M_PI * freq / fs;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / 2.0 * std::sqrt((A + 1.0/A) * (1.0/Q - 1.0) + 2.0);
    double b0 =  A * ((A+1) + (A-1)*cw + 2*std::sqrt(A)*alpha);
    double b1 = -2*A * ((A-1) + (A+1)*cw);
    double b2 =  A * ((A+1) + (A-1)*cw - 2*std::sqrt(A)*alpha);
    double a0 =       (A+1) - (A-1)*cw + 2*std::sqrt(A)*alpha;
    double a1 =  2 * ((A-1) - (A+1)*cw);
    double a2 =       (A+1) - (A-1)*cw - 2*std::sqrt(A)*alpha;
    return { b0/a0, b1/a0, b2/a0, a1/a0, a2/a0 };
}

static BiquadCoeffs makeBandpass(double fs, double freq, double Q) {
    double w0    = 2.0 * M_PI * freq / fs;
    double alpha = std::sin(w0) / (2.0 * Q);
    double b0 =  alpha;
    double b1 =  0.0;
    double b2 = -alpha;
    double a0 =  1.0 + alpha;
    double a1 = -2.0 * std::cos(w0);
    double a2 =  1.0 - alpha;
    return { b0/a0, b1/a0, b2/a0, a1/a0, a2/a0 };
}

class BiquadFilter {
public:
    BiquadFilter() { reset(); }

    void setCoeffs(const BiquadCoeffs& c) { _c = c; }

    void reset() {
        _z1 = _z2 = 0.0;
        _c = {1,0,0,0,0};
    }

    double process(double x) {
        double y = _c.b0*x + _z1;
        _z1 = _c.b1*x - _c.a1*y + _z2;
        _z2 = _c.b2*x - _c.a2*y;
        return y;
    }

private:
    BiquadCoeffs _c;
    double _z1, _z2;
};
