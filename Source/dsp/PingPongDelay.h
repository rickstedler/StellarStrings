#pragma once

#include "DspUtils.h"

namespace stellar
{
/**
    Stereo-Ping-Pong-Delay. Das Eingangssignal (Mono-Summe) wird in den linken Kanal
    eingespeist, das Echo springt dann abwechselnd links ↔ rechts. Im Feedback-Pfad
    sitzen ein Hochpass (gegen Mulm) und ein regelbarer Tiefpass (Bandecho-Charakter)
    sowie eine leichte "Wow"-Modulation.
*/
class PingPongDelay
{
public:
    static constexpr double kMaxSeconds = 2.5;

    struct Params
    {
        float timeSeconds = 0.375f;
        float feedback = 0.45f;  // 0..0.95
        float damping = 0.4f;    // 0..1
        float modulation = 0.2f; // 0..1
        float mix = 0.3f;        // 0..1 (inkl. Ein/Aus)
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        lineL.setMaxSamples ((int) (kMaxSeconds * fs) + 64);
        lineR.setMaxSamples ((int) (kMaxSeconds * fs) + 64);
        lineL.clear(); lineR.clear();
        hpL.setHighPass (fs, 140.0, 0.7); hpR.setHighPass (fs, 140.0, 0.7);
        hpL.reset(); hpR.reset(); lpL.reset(); lpR.reset();
        mix.reset (fs, 0.05, params.mix);
        currentDelay = (float) (params.timeSeconds * fs);
        modPh = 0.0f;
    }

    void setParams (const Params& p)
    {
        params = p;
        params.feedback = std::min (0.95f, std::max (0.0f, p.feedback));
        const double cutoff = 18000.0 * std::pow (0.1, (double) p.damping); // 18 kHz … 1.8 kHz
        lpL.setCutoff (fs, cutoff); lpR.setCutoff (fs, cutoff);
        mix.setTarget (p.mix);
    }

    void process (float* L, float* R, int n)
    {
        const float target = std::min ((float) (kMaxSeconds * fs), std::max (4.0f, params.timeSeconds * (float) fs));
        const float glide = 1.0f - std::exp (-1.0f / (0.08f * (float) fs)); // ~80 ms Gleitzeit
        const float modInc = kTwoPi * 0.45f / (float) fs;
        const float modDepth = params.modulation * 0.0025f * (float) fs;    // bis ±2,5 ms
        const float fb = params.feedback;

        for (int i = 0; i < n; ++i)
        {
            currentDelay += (target - currentDelay) * glide;
            const float mod = modDepth * std::sin (modPh);
            modPh += modInc; if (modPh > kTwoPi) modPh -= kTwoPi;

            const float d = std::max (4.0f, currentDelay + mod);
            const float dl = lineL.read (d);
            const float dr = lineR.read (d);

            const float in = 0.5f * (L[i] + R[i]);
            const float fbR = lpR.process (hpR.process (dr));
            const float fbL = lpL.process (hpL.process (dl));

            lineL.push (softClip (in + fb * fbR));
            lineR.push (softClip (fb * fbL));

            const float m = mix.next();
            L[i] += dl * m;
            R[i] += dr * m;
        }
    }

private:
    static float softClip (float x) noexcept
    {
        if (x > 1.5f) return 1.0f;
        if (x < -1.5f) return -1.0f;
        return x - (4.0f / 27.0f) * x * x * x;
    }

    double fs = 44100.0;
    Params params;
    DelayLine lineL, lineR;
    Biquad hpL, hpR;
    OnePoleLP lpL, lpR;
    Smoother mix;
    float currentDelay = 0.0f, modPh = 0.0f;
};
} // namespace stellar
