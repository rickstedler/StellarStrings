#pragma once

#include "DspUtils.h"
#include <array>

namespace stellar
{
/**
    Vollpolyphone "Divider"-String-Engine im Stil der klassischen 70er-String-Ensembles.

    Jede Stimme erzeugt bandbegrenzte (PolyBLEP) Sägezähne in 16', 8' und 4' Lage sowie
    eine 8'-Rechteckwelle. Wie beim Original werden die Fußlagen auf gemeinsame Busse
    summiert und erst danach über feste Formant-Filter zu den Registern geformt
    (Violin, Viola, Cello, Contrabass, Horn, Trumpet). Die Oszillatoren laufen mit
    zufälliger Startphase frei – wie bei einem Top-Octave-Divider.
*/
class StringSynth
{
public:
    static constexpr int kMaxVoices = 32;

    struct Params
    {
        float violin = 0.8f, viola = 0.6f, cello = 0.4f, bass = 0.0f, horn = 0.0f, trumpet = 0.0f;
        int   bassSplit = 48;          // Contrabass klingt nur unterhalb dieser Note
        float attack = 0.25f;          // Sekunden (Crescendo)
        float release = 1.5f;          // Sekunden (Sustain)
        float velocitySens = 0.0f;     // 0 = wie das Original (keine Anschlagdynamik)
        float vintage = 0.35f;         // Verstimmung / Drift der Stimmen
        float tone = 0.7f;             // Gesamt-Helligkeit
    };

    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        for (auto& b : bus) b.assign ((size_t) maxBlock, 0.0f);
        for (auto& v : voices) v = Voice();
        uint32_t seed = 0x9E3779B9u;
        for (auto& v : voices) { v.rng = Random (seed); seed = seed * 1664525u + 1013904223u; }

        // Register-Formanten (fest, wie die Filterbank des Originals)
        violin[0].setHighPass (fs, 190.0, 0.7);
        violin[1].setPeak     (fs, 2300.0, 1.1, 5.0);
        violin[2].setLowPass  (fs, 8000.0, 0.7);

        viola[0].setHighPass  (fs, 120.0, 0.7);
        viola[1].setPeak      (fs, 1000.0, 0.9, 3.5);
        viola[2].setLowPass   (fs, 3800.0, 0.8);

        cello[0].setHighPass  (fs, 45.0, 0.7);
        cello[1].setPeak      (fs, 450.0, 1.0, 3.0);
        cello[2].setLowPass   (fs, 2200.0, 0.8);

        bassF[0].setHighPass  (fs, 28.0, 0.7);
        bassF[1].setPeak      (fs, 110.0, 0.9, 2.0);
        bassF[2].setLowPass   (fs, 750.0, 0.8);

        horn[0].setHighPass   (fs, 80.0, 0.7);
        horn[1].setPeak       (fs, 650.0, 1.4, 4.0);
        horn[2].setLowPass    (fs, 1350.0, 1.3);

        trumpet[0].setHighPass (fs, 320.0, 0.7);
        trumpet[1].setPeak     (fs, 1500.0, 2.0, 7.0);
        trumpet[2].setLowPass  (fs, 5200.0, 0.9);

        dcBlock.setHighPass (fs, 18.0, 0.7);
        toneFilter.reset();
        for (auto* fArr : { violin.data(), viola.data(), cello.data(), bassF.data(), horn.data(), trumpet.data() })
            for (int i = 0; i < 3; ++i) fArr[i].reset();
        bendRatio = 1.0f;
        sustainPedal = false;
    }

    void setParams (const Params& p)
    {
        params = p;
        attackInc = 1.0f / (float) std::max (1.0, (double) p.attack * fs);
        releaseCoef = (float) std::exp (std::log (1.0e-4) / std::max (1.0, (double) p.release * fs));
        const double toneHz = 1200.0 * std::pow (16.0, (double) p.tone); // 1.2 kHz … 19 kHz
        toneFilter.setCutoff (fs, toneHz);
    }

    //==========================================================================
    void noteOn (int note, float velocity)
    {
        Voice* v = findVoiceFor (note);
        const bool wasSilent = (v->stage == Stage::Off);

        v->note = note;
        v->velocityGain = 1.0f - params.velocitySens + params.velocitySens * velocity;
        v->stage = Stage::Attack;
        v->sustained = false;
        v->age = ++ageCounter;

        if (wasSilent)
        {
            v->env = 0.0f;
            v->ph16 = v->rng.nextFloat();
            v->ph8  = v->rng.nextFloat();
            v->ph4  = v->rng.nextFloat();
            v->driftPhase = v->rng.nextFloat() * kTwoPi;
        }
        v->staticDetuneCents = v->rng.nextBipolar();
        v->driftRate = 0.08f + 0.25f * v->rng.nextFloat();
        v->baseFreq = 440.0f * std::pow (2.0f, (float) (note - 69) / 12.0f);
    }

    void noteOff (int note)
    {
        for (auto& v : voices)
            if (v.note == note && (v.stage == Stage::Attack || v.stage == Stage::Sustain))
            {
                if (sustainPedal) v.sustained = true;
                else              v.stage = Stage::Release;
            }
    }

    void setSustainPedal (bool down)
    {
        sustainPedal = down;
        if (! down)
            for (auto& v : voices)
                if (v.sustained) { v.sustained = false; v.stage = Stage::Release; }
    }

    void setPitchBend (int value14bit)
    {
        const float semis = 2.0f * ((float) value14bit - 8192.0f) / 8192.0f;
        bendRatio = std::pow (2.0f, semis / 12.0f);
    }

    void allNotesOff()
    {
        sustainPedal = false;
        for (auto& v : voices)
            if (v.stage != Stage::Off) { v.stage = Stage::Release; v.sustained = false; }
    }

    //==========================================================================
    /** Rendert `n` Samples (mono) ab `out`. */
    void render (float* out, int n)
    {
        const int maxChunk = (int) bus[0].size();
        while (n > maxChunk) { render (out, maxChunk); out += maxChunk; n -= maxChunk; }
        if (n <= 0) return;

        for (auto& b : bus) std::fill (b.begin(), b.begin() + n, 0.0f);

        float* b16 = bus[0].data(); float* b8 = bus[1].data(); float* b4 = bus[2].data();
        float* p8  = bus[3].data(); float* bb = bus[4].data();

        const float invFs = (float) (1.0 / fs);
        const float detuneAmt = params.vintage * 7.0f;   // statische Streuung in Cent
        const float driftAmt  = params.vintage * 4.0f;   // langsame Drift in Cent
        constexpr float centToRatio = 0.000577789f;      // ln(2)/1200

        for (auto& v : voices)
        {
            if (v.stage == Stage::Off) continue;

            const bool isBass = v.note < params.bassSplit;
            const float driftInc = kTwoPi * v.driftRate * invFs;
            const float base = v.baseFreq * bendRatio * invFs * (1.0f + v.staticDetuneCents * detuneAmt * centToRatio);

            for (int i = 0; i < n; ++i)
            {
                // Hüllkurve
                switch (v.stage)
                {
                    case Stage::Attack:
                        v.env += attackInc;
                        if (v.env >= 1.0f) { v.env = 1.0f; v.stage = Stage::Sustain; }
                        break;
                    case Stage::Release:
                        v.env *= releaseCoef;
                        if (v.env < 1.0e-4f) { v.env = 0.0f; v.stage = Stage::Off; }
                        break;
                    case Stage::Sustain:
                    case Stage::Off:
                        break;
                }
                if (v.stage == Stage::Off) break;

                v.driftPhase += driftInc;
                if (v.driftPhase > kTwoPi) v.driftPhase -= kTwoPi;
                const float inc8 = base * (1.0f + driftAmt * centToRatio * std::sin (v.driftPhase));
                const float inc16 = inc8 * 0.5f, inc4 = inc8 * 2.0f;

                const float s16 = saw (v.ph16, inc16);
                const float s8  = saw (v.ph8,  inc8);
                const float s4  = inc4 < 0.45f ? saw (v.ph4, inc4) : 0.0f;
                const float sq8 = square (v.ph8, inc8);

                advance (v.ph16, inc16); advance (v.ph8, inc8); advance (v.ph4, inc4);

                const float g = v.env * v.velocityGain * 0.085f;
                b16[i] += s16 * g;
                b8[i]  += s8  * g;
                b4[i]  += s4  * g;
                p8[i]  += sq8 * g;
                if (isBass) bb[i] += (s16 + 0.35f * s8) * g;
            }
        }

        // Register-Filterbank
        for (int i = 0; i < n; ++i)
        {
            float x = 0.0f;
            if (params.violin  > 0.0f) x += params.violin  * chain (violin,  b8[i] + 0.7f  * b4[i]);
            if (params.viola   > 0.0f) x += params.viola   * chain (viola,   b8[i] + 0.3f  * b4[i]) * 1.1f;
            if (params.cello   > 0.0f) x += params.cello   * chain (cello,   b16[i] + 0.5f * b8[i]) * 1.1f;
            if (params.bass    > 0.0f) x += params.bass    * chain (bassF,   bb[i]) * 1.6f;
            if (params.horn    > 0.0f) x += params.horn    * chain (horn,    p8[i] + 0.4f  * b16[i]) * 1.2f;
            if (params.trumpet > 0.0f) x += params.trumpet * chain (trumpet, b8[i]) * 0.9f;

            out[i] = toneFilter.process (dcBlock.process (x));
        }
    }

    int activeVoiceCount() const
    {
        int c = 0;
        for (auto& v : voices) if (v.stage != Stage::Off) ++c;
        return c;
    }

private:
    enum class Stage { Off, Attack, Sustain, Release };

    struct Voice
    {
        int note = -1;
        Stage stage = Stage::Off;
        bool sustained = false;
        float env = 0.0f, velocityGain = 1.0f, baseFreq = 440.0f;
        float ph16 = 0.0f, ph8 = 0.0f, ph4 = 0.0f;
        float driftPhase = 0.0f, driftRate = 0.2f, staticDetuneCents = 0.0f;
        uint64_t age = 0;
        Random rng;
    };

    static float polyBlep (float t, float dt) noexcept
    {
        if (t < dt)        { t /= dt;          return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }
    static float saw (float ph, float inc) noexcept { return 2.0f * ph - 1.0f - polyBlep (ph, inc); }
    static float square (float ph, float inc) noexcept
    {
        float ph2 = ph + 0.5f; if (ph2 >= 1.0f) ph2 -= 1.0f;
        return (ph < 0.5f ? 1.0f : -1.0f) + polyBlep (ph, inc) - polyBlep (ph2, inc);
    }
    static void advance (float& ph, float inc) noexcept { ph += inc; if (ph >= 1.0f) ph -= 1.0f; }

    static float chain (std::array<Biquad, 3>& f, float x) noexcept
    {
        return f[2].process (f[1].process (f[0].process (x)));
    }

    Voice* findVoiceFor (int note)
    {
        for (auto& v : voices) if (v.note == note && v.stage != Stage::Off) return &v; // Retrigger
        for (auto& v : voices) if (v.stage == Stage::Off) return &v;
        Voice* best = nullptr;                                                     // leiseste Release-Stimme
        for (auto& v : voices)
            if (v.stage == Stage::Release && (best == nullptr || v.env < best->env)) best = &v;
        if (best) return best;
        best = &voices[0];                                                         // älteste Stimme
        for (auto& v : voices) if (v.age < best->age) best = &v;
        return best;
    }

    double fs = 44100.0;
    Params params;
    std::array<Voice, kMaxVoices> voices;
    std::array<std::vector<float>, 5> bus;
    std::array<Biquad, 3> violin, viola, cello, bassF, horn, trumpet;
    Biquad dcBlock;
    OnePoleLP toneFilter;
    float attackInc = 0.001f, releaseCoef = 0.999f, bendRatio = 1.0f;
    bool sustainPedal = false;
    uint64_t ageCounter = 0;
};
} // namespace stellar
