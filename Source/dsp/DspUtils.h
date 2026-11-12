#pragma once

#include <cmath>
#include <vector>
#include <cstdint>
#include <algorithm>

namespace stellar
{
constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

//==============================================================================
/** Ringpuffer-Delay mit kubischer (Hermite-)Interpolation für modulierte Lesezugriffe. */
class DelayLine
{
public:
    void setMaxSamples (int maxSamples)
    {
        int size = 1;
        while (size < maxSamples + 8)
            size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
    }

    void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); }

    void push (float x) noexcept
    {
        buffer[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    /** Liest das Signal von vor `delay` Samples (nach push(); delay >= 2). */
    float read (float delay) const noexcept
    {
        delay = std::max (2.0f, delay);
        const float pos = (float) writePos - delay;
        const float fl  = std::floor (pos);
        const int   i   = (int) fl;
        const float f   = pos - fl;

        const float y0 = buffer[(size_t) ((i - 1) & mask)];
        const float y1 = buffer[(size_t) ( i      & mask)];
        const float y2 = buffer[(size_t) ((i + 1) & mask)];
        const float y3 = buffer[(size_t) ((i + 2) & mask)];

        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    /** Ganzzahliger Lesezugriff (delay >= 1). */
    float readInt (int delay) const noexcept
    {
        return buffer[(size_t) ((writePos - delay) & mask)];
    }

private:
    std::vector<float> buffer { std::vector<float> (16, 0.0f) };
    int mask = 15;
    int writePos = 0;
};

//==============================================================================
/** RBJ-Biquad (Transposed Direct Form II). */
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void reset() noexcept { z1 = z2 = 0.0f; }

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setLowPass (double fs, double f, double q)
    {
        const double w = 2.0 * 3.141592653589793 * clampFreq (fs, f) / fs;
        const double c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        set ((1.0 - c) * 0.5, 1.0 - c, (1.0 - c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    void setHighPass (double fs, double f, double q)
    {
        const double w = 2.0 * 3.141592653589793 * clampFreq (fs, f) / fs;
        const double c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        set ((1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    void setPeak (double fs, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * 3.141592653589793 * clampFreq (fs, f) / fs;
        const double c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        set (1.0 + alpha * A, -2.0 * c, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * c, 1.0 - alpha / A);
    }

private:
    static double clampFreq (double fs, double f) { return std::min (std::max (f, 10.0), fs * 0.45); }

    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
    {
        b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0);
        a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
    }
};

//==============================================================================
/** Einpoliger Tiefpass. */
struct OnePoleLP
{
    float a = 0.0f, z = 0.0f;
    void setCutoff (double fs, double f)
    {
        f = std::min (std::max (f, 10.0), fs * 0.45);
        a = (float) std::exp (-2.0 * 3.141592653589793 * f / fs);
    }
    float process (float x) noexcept { z = x + a * (z - x); return z; }
    void reset() noexcept { z = 0.0f; }
};

/** Einfacher Rauschgenerator (xorshift). */
struct Random
{
    uint32_t state = 0x12345678u;
    explicit Random (uint32_t seed = 0x12345678u) : state (seed ? seed : 1u) {}
    uint32_t nextInt() noexcept
    {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return state;
    }
    float nextFloat() noexcept { return (float) (nextInt() >> 8) * (1.0f / 16777216.0f); } // [0,1)
    float nextBipolar() noexcept { return nextFloat() * 2.0f - 1.0f; }
};

/** Linearer Werte-Glätter pro Sample. */
struct Smoother
{
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int remaining = 0, rampLen = 1;

    void reset (double fs, double seconds, float value)
    {
        rampLen = std::max (1, (int) (fs * seconds));
        current = target = value; remaining = 0; step = 0.0f;
    }
    void setTarget (float v) noexcept
    {
        if (std::abs (v - target) < 1.0e-9f) return;
        target = v;
        remaining = rampLen;
        step = (target - current) / (float) rampLen;
    }
    float next() noexcept
    {
        if (remaining > 0) { current += step; if (--remaining == 0) current = target; }
        return current;
    }
};
} // namespace stellar
