// Offline-Render-Test: rendert für jedes Preset einen Akkord (inkl. Hallfahne) als WAV
// und prüft das Ergebnis auf NaN/Inf und Pegel.
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    const double sr = 48000.0;
    const int block = 512;
    bool ok = true;

    StellarStringsProcessor proc;
    proc.setPlayConfigDetails (0, 2, sr, block);

    for (int prog = 0; prog < proc.getNumPrograms(); ++prog)
    {
        proc.setCurrentProgram (prog);
        proc.prepareToPlay (sr, block);

        const int total = (int) (sr * 9.0);
        juce::AudioBuffer<float> out (2, total), buf (2, block);
        const int chord[] = { 41, 53, 57, 60, 64, 69 }; // F-Moll-ish Pad mit Bass
        float peak = 0.0f; double sumSq = 0.0; bool finite = true;

        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            juce::MidiBuffer midi;
            for (int note : chord)
            {
                const int on = 0, off = (int) (sr * 4.0);
                if (on >= pos && on < pos + n)   midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), on - pos);
                if (off >= pos && off < pos + n) midi.addEvent (juce::MidiMessage::noteOff (1, note), off - pos);
            }
            buf.setSize (2, n, false, false, true);
            proc.processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
            {
                out.copyFrom (ch, pos, buf, ch, 0, n);
                for (int i = 0; i < n; ++i)
                {
                    const float v = buf.getSample (ch, i);
                    if (! std::isfinite (v)) finite = false;
                    peak = std::max (peak, std::abs (v));
                    sumSq += (double) v * v;
                }
            }
        }

        const double rms = std::sqrt (sumSq / (2.0 * total));
        // Stereo-Korrelation
        double lr = 0, ll = 0, rr = 0;
        for (int i = 0; i < total; ++i) { auto l = out.getSample (0, i), r = out.getSample (1, i); lr += l * r; ll += l * l; rr += r * r; }
        const double corr = lr / std::sqrt (ll * rr + 1e-20);

        std::printf ("%-24s peak %.3f (%.1f dBFS)  rms %.1f dBFS  L/R-Korrelation %.2f  %s\n",
                     proc.getProgramName (prog).toRawUTF8(), peak, juce::Decibels::gainToDecibels (peak),
                     juce::Decibels::gainToDecibels ((float) rms), corr, finite ? "OK" : "NaN/Inf!");
        if (! finite || peak > 2.0f || peak < 0.01f) ok = false;

        auto file = outDir.getChildFile ("preset_" + juce::String (prog) + ".wav");
        file.deleteFile();
        juce::WavAudioFormat wav;
        if (auto stream = file.createOutputStream())
            if (auto* w = wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0))
            {
                stream.release();
                std::unique_ptr<juce::AudioFormatWriter> writer (w);
                writer->writeFromAudioSampleBuffer (out, 0, total);
            }
    }

    // Zustand speichern/laden
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    proc.setStateInformation (state.getData(), (int) state.getSize());
    std::printf ("State round-trip: %d Bytes\n", (int) state.getSize());

    // Editor-Snapshot (benötigt ein Display, z.B. xvfb-run)
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr)
    {
        proc.setCurrentProgram (0);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        auto png = outDir.getChildFile ("editor.png");
        png.deleteFile();
        juce::PNGImageFormat fmt;
        if (auto s = png.createOutputStream()) fmt.writeImageToStream (img, *s);
        std::printf ("Editor-Snapshot: %s\n", png.getFullPathName().toRawUTF8());
    }

    std::printf (ok ? "ALLE TESTS OK\n" : "TEST FEHLGESCHLAGEN\n");
    return ok ? 0 : 1;
}
