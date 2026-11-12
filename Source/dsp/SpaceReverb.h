#pragma once

#include "DspUtils.h"
#include <array>

namespace stellar
{
/**
    Modulierte, weite Hallfläche: 8-kanaliges Feedback-Delay-Netzwerk (FDN) mit
    Hadamard-Matrix, frequenzabhängiger Dämpfung, zufällig phasenversetzter Delay-
    Modulation (gegen metallische Resonanzen, für "schwebende" Fahnen) und
    Allpass-Diffusion am Eingang. Pre-Delay und Stereobreite sind einstellbar.
*/
class SpaceReverb
{
public:
    static constexpr int N = 8;

    struct Params
    {
        float size = 0.75f;        // 0..1
        float decaySeconds = 8.0f; // RT60
        float damping = 0.45f;     // 0..1
        float modDepth = 0.5f;     // 0..1
        float modRate = 0.4f;      // Hz
        float preDelayMs = 40.0f;
        float width = 1.0f;        // 0..1.5
        float mix = 0.4f;          // 0..1 (inkl. Ein/Aus)
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& l : lines) { l.setMaxSamples ((int) (0.25 * fs)); l.clear(); }
        preL.setMaxSamples ((int) (0.3 * fs)); preR.setMaxSamples ((int) (0.3 * fs));
        preL.clear(); preR.clear();

        static const float apMs[4] = { 4.71f, 3.59f, 12.73f, 9.31f };
        for (size_t i = 0; i < 4; ++i)
        {
            apL[i].prepare ((int) (apMs[i] * 0.001 * fs));
            apR[i].prepare ((int) (apMs[i] * 1.0711 * 0.001 * fs)); // leicht versetzt → dekorreliert
        }
        for (auto& d : damp) d.reset();
        inHpL.setHighPass (fs, 90.0, 0.7); inHpR.setHighPass (fs, 90.0, 0.7);
        inHpL.reset(); inHpR.reset();

        Random r (0xC0FFEEu);
        for (int i = 0; i < N; ++i) { modPhase[i] = r.nextFloat() * kTwoPi; modRateMul[i] = 0.6f + 0.8f * r.nextFloat(); }

        mix.reset (fs, 0.05, params.mix);
        updateLengths (true);
    }

    void setParams (const Params& p)
    {
        params = p;
        mix.setTarget (p.mix);
        const double cutoff = 16000.0 * std::pow (0.05, (double) p.damping); // 16 kHz … 800 Hz
        for (auto& d : damp) d.setCutoff (fs, cutoff);
        updateLengths (false);
    }

    void process (float* L, float* R, int n)
    {
        const float invFs = 1.0f / (float) fs;
        const float modAmt = params.modDepth * 0.0016f * (float) fs; // bis ±1,6 ms
        const float preDelay = std::max (2.0f, params.preDelayMs * 0.001f * (float) fs);
        const float lenGlide = 1.0f - std::exp (-1.0f / (0.2f * (float) fs));
        constexpr float hadamardNorm = 0.35355339f; // 1/sqrt(8)

        for (int s = 0; s < n; ++s)
        {
            const float dryL = L[s], dryR = R[s];

            preL.push (inHpL.process (dryL));
            preR.push (inHpR.process (dryR));
            float inL = preL.read (preDelay), inR = preR.read (preDelay);
            for (size_t i = 0; i < 4; ++i) { inL = apL[i].process (inL); inR = apR[i].process (inR); }

            float x[N];
            for (size_t i = 0; i < (size_t) N; ++i)
            {
                curLen[i] += (targetLen[i] - curLen[i]) * lenGlide;
                modPhase[i] += kTwoPi * params.modRate * modRateMul[i] * invFs;
                if (modPhase[i] > kTwoPi) modPhase[i] -= kTwoPi;
                const float d = curLen[i] + modAmt * std::sin (modPhase[i]);
                x[i] = damp[i].process (lines[i].read (d)) * gain[i];
            }

            const float outL = x[0] - x[2] + x[4] - x[6] + 0.5f * (x[1] - x[5]);
            const float outR = x[1] - x[3] + x[5] - x[7] + 0.5f * (x[2] - x[6]);

            // Hadamard-Feedback-Matrix (verlustfrei, maximal diffus)
            for (int h = 1; h < N; h <<= 1)
                for (int i = 0; i < N; i += h << 1)
                    for (int j = i; j < i + h; ++j)
                    {
                        const float a = x[j], b = x[j + h];
                        x[j] = a + b; x[j + h] = a - b;
                    }

            for (size_t i = 0; i < (size_t) N; ++i)
                lines[i].push (x[i] * hadamardNorm + ((i & 1) ? inR : inL) * 0.5f);

            float wetL = outL * 0.38f, wetR = outR * 0.38f;
            const float mid = 0.5f * (wetL + wetR), side = 0.5f * (wetL - wetR) * params.width;
            wetL = mid + side; wetR = mid - side;

            const float m = mix.next();
            const float dryG = std::cos (m * 0.5f * kPi), wetG = std::sin (m * 0.5f * kPi);
            L[s] = dryL * dryG + wetL * wetG;
            R[s] = dryR * dryG + wetR * wetG;
        }
    }

private:
    struct Allpass
    {
        std::vector<float> buf; int pos = 0; float g = 0.65f;
        void prepare (int len) { buf.assign ((size_t) std::max (1, len), 0.0f); pos = 0; }
        float process (float x) noexcept
        {
            const float delayed = buf[(size_t) pos];
            const float v = x + g * delayed;
            buf[(size_t) pos] = v;
            if (++pos >= (int) buf.size()) pos = 0;
            return delayed - g * v;
        }
    };

    void updateLengths (bool snap)
    {
        static const float baseMs[N] = { 37.1f, 41.9f, 47.3f, 53.7f, 59.9f, 67.1f, 73.3f, 79.7f };
        const float scale = 0.4f + 1.6f * params.size; // 0,4 … 2,0
        const double rt60 = std::max (0.2, (double) params.decaySeconds);
        for (int i = 0; i < N; ++i)
        {
            targetLen[i] = baseMs[i] * scale * 0.001f * (float) fs;
            if (snap) curLen[i] = targetLen[i];
            const double lenSec = targetLen[i] / fs;
            gain[i] = (float) std::pow (10.0, -3.0 * lenSec / rt60);
        }
    }

    double fs = 44100.0;
    Params params;
    std::array<DelayLine, N> lines;
    std::array<OnePoleLP, N> damp;
    std::array<Allpass, 4> apL, apR;
    DelayLine preL, preR;
    Biquad inHpL, inHpR;
    Smoother mix;
    float curLen[N] {}, targetLen[N] {}, gain[N] {}, modPhase[N] {}, modRateMul[N] {};
};
} // namespace stellar
