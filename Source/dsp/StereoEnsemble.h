#pragma once

#include "DspUtils.h"

namespace stellar
{
/**
    Stereo-Ensemble-Chorus nach dem klassischen Drei-Phasen-BBD-Prinzip.

    Das Original nutzt drei Eimerketten-Delays, deren Verzögerung von zwei überlagerten
    LFOs (langsam ≈ 0,6 Hz und schnell ≈ 6 Hz) moduliert wird – jeweils um 120° phasen-
    versetzt. Daraus entsteht der typische, "schwebende" Streicherteppich.

    Stereo-Version: Der linke Kanal summiert die Abgriffe mit 0°/120°/240°, der rechte
    die um 60° versetzten Abgriffe (60°/180°/300°). Damit ist die Modulation rechts
    exakt gespiegelt – maximal dekorreliert, aber monokompatibel.
*/
class StereoEnsemble
{
public:
    struct Params
    {
        float depth = 0.7f;   // 0..1
        float rate  = 1.0f;   // Faktor 0.25..2
        float width = 1.0f;   // 0..1.5
        float mix   = 0.85f;  // 0..1 (inkl. Ein/Aus)
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        line.setMaxSamples ((int) (0.04 * fs));
        line.clear();
        for (auto* f : { &bbdL, &bbdR }) { f->setLowPass (fs, 7500.0, 0.65); f->reset(); }
        mix.reset (fs, 0.05, params.mix);
        slowPh = fastPh = 0.0f;
    }

    void setParams (const Params& p) { params = p; mix.setTarget (p.mix); }

    void process (const float* in, float* outL, float* outR, int n)
    {
        const float invFs = (float) (1.0 / fs);
        const float slowInc = kTwoPi * 0.63f * params.rate * invFs;
        const float fastInc = kTwoPi * 5.9f  * params.rate * invFs;
        const float msToSamples = (float) fs * 0.001f;
        const float baseDelay = 6.0f * msToSamples;
        const float slowDepth = params.depth * 2.2f  * msToSamples;
        const float fastDepth = params.depth * 0.32f * msToSamples;

        // Phasenoffsets der 6 Abgriffe: k * 60°
        static const float cosOff[6] = { 1.0f, 0.5f, -0.5f, -1.0f, -0.5f, 0.5f };
        static const float sinOff[6] = { 0.0f, 0.8660254f, 0.8660254f, 0.0f, -0.8660254f, -0.8660254f };

        for (int i = 0; i < n; ++i)
        {
            const float x = in[i];
            line.push (x);

            const float ss = std::sin (slowPh), sc = std::cos (slowPh);
            const float fs_ = std::sin (fastPh), fc = std::cos (fastPh);

            float taps[6];
            for (int k = 0; k < 6; ++k)
            {
                const float slow = ss * cosOff[k] + sc * sinOff[k];  // sin(slowPh + k*60°)
                const float fast = fs_ * cosOff[k] + fc * sinOff[k]; // sin(fastPh + k*60°)
                taps[k] = line.read (baseDelay + slowDepth * slow + fastDepth * fast);
            }

            float wetL = (taps[0] + taps[2] + taps[4]) * (1.0f / 3.0f);
            float wetR = (taps[1] + taps[3] + taps[5]) * (1.0f / 3.0f);
            wetL = bbdL.process (wetL);
            wetR = bbdR.process (wetR);

            const float mid  = 0.5f * (wetL + wetR);
            const float side = 0.5f * (wetL - wetR) * params.width;
            wetL = (mid + side) * 1.6f;
            wetR = (mid - side) * 1.6f;

            const float m = mix.next();
            outL[i] = x * (1.0f - m) + wetL * m;
            outR[i] = x * (1.0f - m) + wetR * m;

            slowPh += slowInc; if (slowPh > kTwoPi) slowPh -= kTwoPi;
            fastPh += fastInc; if (fastPh > kTwoPi) fastPh -= kTwoPi;
        }
    }

private:
    double fs = 44100.0;
    Params params;
    DelayLine line;
    Biquad bbdL, bbdR;
    Smoother mix;
    float slowPh = 0.0f, fastPh = 0.0f;
};
} // namespace stellar
