#include "dsp/Biquad.h"

#include "core/DspMath.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace vs {

namespace {

struct Raw {
    double b0, b1, b2, a0, a1, a2;
};

BiquadCoeffs normalise(const Raw& r) noexcept {
    BiquadCoeffs c;
    c.b0 = static_cast<float>(r.b0 / r.a0);
    c.b1 = static_cast<float>(r.b1 / r.a0);
    c.b2 = static_cast<float>(r.b2 / r.a0);
    c.a1 = static_cast<float>(r.a1 / r.a0);
    c.a2 = static_cast<float>(r.a2 / r.a0);
    return c;
}

double safeOmega(float sampleRate, float frequency) noexcept {
    const double nyquist = 0.5 * sampleRate;
    const double f = std::clamp(static_cast<double>(frequency), 1.0, nyquist * 0.98);
    return 2.0 * 3.14159265358979323846 * f / sampleRate;
}

} // namespace

BiquadCoeffs designHighPass(float sampleRate, float frequency, float q) noexcept {
    const double w0 = safeOmega(sampleRate, frequency);
    const double cosw = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * std::max(0.05f, q));
    return normalise({(1 + cosw) / 2, -(1 + cosw), (1 + cosw) / 2, 1 + alpha, -2 * cosw, 1 - alpha});
}

BiquadCoeffs designLowPass(float sampleRate, float frequency, float q) noexcept {
    const double w0 = safeOmega(sampleRate, frequency);
    const double cosw = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * std::max(0.05f, q));
    return normalise({(1 - cosw) / 2, 1 - cosw, (1 - cosw) / 2, 1 + alpha, -2 * cosw, 1 - alpha});
}

BiquadCoeffs designPeaking(float sampleRate, float frequency, float q, float gainDb) noexcept {
    const double w0 = safeOmega(sampleRate, frequency);
    const double cosw = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * std::max(0.05f, q));
    const double a = std::pow(10.0, gainDb / 40.0);
    return normalise({1 + alpha * a, -2 * cosw, 1 - alpha * a, 1 + alpha / a, -2 * cosw, 1 - alpha / a});
}

BiquadCoeffs designLowShelf(float sampleRate, float frequency, float gainDb) noexcept {
    const double w0 = safeOmega(sampleRate, frequency);
    const double cosw = std::cos(w0);
    const double a = std::pow(10.0, gainDb / 40.0);
    const double alpha = std::sin(w0) / 2.0 * std::sqrt(2.0); // shelf slope S = 1
    const double s = 2.0 * std::sqrt(a) * alpha;
    return normalise({a * ((a + 1) - (a - 1) * cosw + s),
                      2 * a * ((a - 1) - (a + 1) * cosw),
                      a * ((a + 1) - (a - 1) * cosw - s),
                      (a + 1) + (a - 1) * cosw + s,
                      -2 * ((a - 1) + (a + 1) * cosw),
                      (a + 1) + (a - 1) * cosw - s});
}

BiquadCoeffs designHighShelf(float sampleRate, float frequency, float gainDb) noexcept {
    const double w0 = safeOmega(sampleRate, frequency);
    const double cosw = std::cos(w0);
    const double a = std::pow(10.0, gainDb / 40.0);
    const double alpha = std::sin(w0) / 2.0 * std::sqrt(2.0);
    const double s = 2.0 * std::sqrt(a) * alpha;
    return normalise({a * ((a + 1) + (a - 1) * cosw + s),
                      -2 * a * ((a - 1) + (a + 1) * cosw),
                      a * ((a + 1) + (a - 1) * cosw - s),
                      (a + 1) - (a - 1) * cosw + s,
                      2 * ((a - 1) - (a + 1) * cosw),
                      (a + 1) - (a - 1) * cosw - s});
}

float Biquad::magnitudeAt(float sampleRate, float frequency) const noexcept {
    const double w = 2.0 * 3.14159265358979323846 * frequency / sampleRate;
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> z2 = z1 * z1;
    const std::complex<double> num = static_cast<double>(c_.b0) + static_cast<double>(c_.b1) * z1 +
                                     static_cast<double>(c_.b2) * z2;
    const std::complex<double> den = 1.0 + static_cast<double>(c_.a1) * z1 + static_cast<double>(c_.a2) * z2;
    return static_cast<float>(std::abs(num / den));
}

} // namespace vs
