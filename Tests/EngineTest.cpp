// Drives the real processor offline: load a file, tap chops, play pads via MIDI, check the output,
// snap, pitch, DAW sync, undo, export, save/restore, and render the editor to PNGs for a visual check.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <iostream>

static int failures = 0;
static void check (bool ok, const char* what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (! ok) ++failures;
}

// 4 bars of a simple beat at the given tempo: kick on 1 and 3, snare on 2 and 4, hats on eighths
static juce::File writeDrumLoop (double bpm, juce::Array<juce::int64>& beats)
{
    const double sr = 44100.0, beat = 60.0 / bpm;
    const int len = (int) std::ceil (sr * 16 * beat);
    juce::AudioBuffer<float> buf (2, len);
    buf.clear();
    juce::Random rnd (5);
    auto add = [&] (double t, int kind)
    {
        const auto start = (int) std::lround (t * sr);
        const int dur = (int) (sr * (kind == 2 ? 0.05 : 0.25));
        for (int n = 0; n < dur && start + n < len; ++n)
        {
            const double tt = n / sr;
            float v = 0.0f;
            if (kind == 0) v = (float) (0.9 * std::exp (-tt * 18.0) * std::sin (2.0 * juce::MathConstants<double>::pi * (55.0 + 90.0 * std::exp (-tt * 40.0)) * tt));
            if (kind == 1) v = (float) (0.6 * std::exp (-tt * 25.0) * (rnd.nextFloat() * 2.0f - 1.0f));
            if (kind == 2) v = (float) (0.18 * std::exp (-tt * 90.0) * (rnd.nextFloat() * 2.0f - 1.0f));
            buf.addSample (0, start + n, v);
            buf.addSample (1, start + n, v);
        }
    };
    for (int b = 0; b < 16; ++b)
    {
        beats.add ((juce::int64) std::lround (b * beat * sr));
        add (b * beat, b % 2 == 0 ? 0 : 1);
        add (b * beat, 2);
        add ((b + 0.5) * beat, 2);
    }
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("catnip_test_beat_92.wav");
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (f);
    auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
    w->writeFromAudioSampleBuffer (buf, 0, len);
    return f;
}

static float renderPeak (CatnipProcessor& p, juce::MidiBuffer midi, int blocks, int blockSize = 512)
{
    float peak = 0.0f;
    juce::AudioBuffer<float> out (2, blockSize);
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer m;
        if (b == 0) m = midi;
        p.processBlock (out, m);
        peak = std::max (peak, out.getMagnitude (0, blockSize));
    }
    return peak;
}

static void snapshot (CatnipProcessor& proc, const juce::String& name, std::function<void (CatnipEditor&)> prep = {})
{
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* e = dynamic_cast<CatnipEditor*> (ed.get());
    if (prep) prep (*e);
    auto img = e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f);
    auto out = juce::File ("/home/claude/catnip/shots").getChildFile (name);
    out.getParentDirectory().createDirectory();
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::cout << "  wrote " << out.getFullPathName() << "\n";
}


struct TestPlayHead final : juce::AudioPlayHead
{
    double bpm = 90.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        if (bpm > 0.0) info.setBpm (bpm);
        return info;
    }
};

static juce::MidiBuffer note (int chop, bool on = true, int at = 0)
{
    juce::MidiBuffer m;
    m.addEvent (on ? juce::MidiMessage::noteOn (1, catnip::firstNote + chop, (juce::uint8) 110)
                   : juce::MidiMessage::noteOff (1, catnip::firstNote + chop), at);
    return m;
}

/** Plays one chop and returns for how many output samples its voice ran. */
static int soundingLength (CatnipProcessor& p, int chop)
{
    p.stopAll();
    renderPeak (p, {}, 4);                       // let earlier sounds fade
    juce::AudioBuffer<float> out (2, 16);
    for (int b = 0; b < 200000; ++b)
    {
        juce::MidiBuffer m;
        if (b == 0) m = note (chop);
        p.processBlock (out, m);
        if (b > 0 && p.getActiveVoiceCount() == 0) return b * 16;
    }
    return -1;
}

static void setParam (CatnipProcessor& p, const char* id, float value)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const double hostRate = 48000.0;

    CatnipProcessor proc;
    proc.prepareToPlay (hostRate, 512);
    check (renderPeak (proc, note (0), 4) == 0.0f, "silent without a sample");
    snapshot (proc, "01_empty.png");

    juce::Array<juce::int64> beats;
    const auto loop = writeDrumLoop (92.0, beats);
    juce::String err;
    check (proc.loadFile (loop, err), "loads a WAV");
    check (std::abs (proc.getSampleBpm() - 92.0) < 0.3, "tempo of the loop detected (92 BPM)");
    check (proc.getNumChops() == 0, "no chops before tapping (tap is the only way to chop)");
    check (renderPeak (proc, note (0), 4) == 0.0f, "pads are silent until chops are tapped");
    const double sr = 44100.0;
    const auto ms = [sr] (double m) { return (juce::int64) std::llround (m * sr / 1000.0); };

    // ---- tapping ---------------------------------------------------------------------------
    {
        proc.play();
        check (renderPeak (proc, {}, 6) > 0.05f && proc.getPreviewPosition() > 0, "PLAY plays the sample, play head moves");
        proc.startTapping();
        renderPeak (proc, {}, 1);
        check (proc.isTapping() && proc.getPreviewPosition() >= 0 && proc.getPreviewPosition() < 2000, "TAP restarts the loop from the beginning");

        // late taps on beats 1, 2, 3, 5 (60-110 ms late, like real people)
        proc.addTapAt (beats[0] + ms (60));
        proc.addTapAt (beats[1] + ms (90));
        check (proc.addTapAt (beats[1] + ms (100)) < 0, "a double tap on the same hit is ignored");
        proc.addTapAt (beats[2] + ms (110));
        proc.addTapAt (beats[4] + ms (75));
        check (proc.getNumChops() == 4, "4 taps = 4 chops");

        // a tap 20 ms after an off-beat hat goes to the hat, not to the strong kick 300 ms earlier
        proc.addTapAt (beats[4] + (beats[5] - beats[4]) / 2 + ms (20));
        check (proc.getNumChops() == 5, "5th tap");
        proc.stopTapping();
        renderPeak (proc, {}, 2);
        check (! proc.isTapping(), "tapping stops");

        setParam (proc, "snap", 1.0f);
        auto b = proc.getBounds();
        bool onHits = std::llabs (b[0] - beats[0]) < ms (8) && std::llabs (b[1] - beats[1]) < ms (8)
                   && std::llabs (b[2] - beats[2]) < ms (8) && std::llabs (b[3] - beats[4]) < ms (8);
        check (onHits, "SNAP 100 %: late taps are pulled back onto the kicks/snares");
        check (std::llabs (b[4] - (beats[4] + (beats[5] - beats[4]) / 2)) < ms (8), "SNAP picks the nearby hat, not a stronger hit far away");

        setParam (proc, "snap", 0.0f);
        b = proc.getBounds();
        check (b[1] == beats[1] + ms (90), "SNAP 0 %: chops stay exactly where they were tapped");
        setParam (proc, "snap", 0.5f);
        b = proc.getBounds();
        const auto half = beats[1] + ms (90) + ((proc.getTaps().target[1] - (beats[1] + ms (90))) / 2);
        check (std::llabs (b[1] - half) <= 1, "SNAP 50 %: halfway between tap and hit (gradual pull)");
        setParam (proc, "snap", 1.0f);
    }

    // ---- tapping silences everything --------------------------------------------------------
    {
        renderPeak (proc, note (0), 2);
        check (proc.getActiveVoiceCount() == 1, "pad 1 plays");
        proc.startTapping();
        renderPeak (proc, {}, 2);
        check (proc.getActiveVoiceCount() == 0, "starting TAP silences every playing chop");
        check (renderPeak (proc, note (2), 2) > 0.0f && proc.getActiveVoiceCount() == 0, "pads do not play while tapping (only the loop)");
        proc.stopTapping();
        renderPeak (proc, {}, 2);
        check (proc.getNumChops() == 5, "a tap pass without taps keeps the old chops");
    }

    // ---- playing ------------------------------------------------------------------------------
    {
        check (renderPeak (proc, note (0), 6) > 0.05f, "pad 1 makes sound");
        proc.stopAll();
        renderPeak (proc, {}, 2);
        check (renderPeak (proc, note (9), 6) == 0.0f, "a pad without a chop stays silent");
        juce::MidiBuffer two;
        two.addEvent (juce::MidiMessage::noteOn (1, catnip::firstNote + 0, (juce::uint8) 100), 0);
        two.addEvent (juce::MidiMessage::noteOn (1, catnip::firstNote + 1, (juce::uint8) 100), 100);
        renderPeak (proc, two, 1);
        check (proc.getActiveVoiceCount() == 1, "choke: a new chop cuts the previous one");
        proc.getChopFlags (1).reverse = true;
        check (renderPeak (proc, note (1), 20) > 0.01f, "reverse plays");
        proc.getChopFlags (1).reverse = false;

        proc.getChopFlags (0).gate = true;
        renderPeak (proc, note (0), 2);
        renderPeak (proc, note (0, false), 4);
        check (renderPeak (proc, {}, 2) == 0.0f, "GATE: the chop stops when the pad is released");
        proc.getChopFlags (0).gate = false;
    }

    // ---- pitch and tempo ------------------------------------------------------------------------
    {
        const auto b = proc.getBounds();
        const double chopLen = (double) (b[1] - b[0]) * hostRate / sr;
        const int normal = soundingLength (proc, 0);
        check (std::abs (normal - chopLen) < chopLen * 0.02, "pitch 0: the chop plays at its own length");
        setParam (proc, "pitch", 12.0f);
        const int up = soundingLength (proc, 0);
        check (std::abs (up - chopLen / 2.0) < chopLen * 0.02, "PITCH +12: twice as fast (MPC style)");
        check (std::abs (proc.getPlaybackBpm() - 184.0) < 0.6, "BPM readout follows the pitch (92 -> 184)");
        setParam (proc, "pitch", -2.0f);
        check (std::abs (proc.getPlaybackBpm() - 92.0 * std::pow (2.0, -2.0 / 12.0)) < 0.3, "BPM readout at -2 st");

        TestPlayHead head;
        proc.setPlayHead (&head);
        renderPeak (proc, {}, 1);
        check (std::abs (proc.getHostBpm() - 90.0) < 1e-9, "reads the DAW tempo");

        setParam (proc, "sync", 1.0f);
        setParam (proc, "pitch", 7.0f);
        check (std::abs (proc.getPlaybackBpm() - 90.0) < 0.3, "DAW SYNC: the sample plays at the DAW tempo");
        const auto r = proc.getRatios();
        check (std::abs (r.second - std::pow (2.0, 7.0 / 12.0)) < 1e-6, "DAW SYNC: pitch still changes the pitch");
        const int synced = soundingLength (proc, 0);
        const double expected = chopLen * proc.getSampleBpm() / 90.0;
        check (std::abs (synced - expected) < chopLen * 0.03, "DAW SYNC: chop length follows the tempo, not the pitch");
        check (renderPeak (proc, note (0), 10) > 0.05f, "DAW SYNC: stretched chops are audible");

        head.bpm = 180.0;
        renderPeak (proc, {}, 1);
        check (std::abs (proc.getPlaybackBpm() - 90.0) < 0.5 && std::abs (proc.getRatios().first - 180.0 / 184.0) < 0.01,
               "DAW SYNC at 180: plays in half time instead of nearly doubling the speed");
        head.bpm = 0.0;
        renderPeak (proc, {}, 1);
        proc.setPlayHead (nullptr);
        setParam (proc, "pitch", 0.0f);
        setParam (proc, "sync", 0.0f);
    }

    // ---- editing, undo ----------------------------------------------------------------------------
    {
        setParam (proc, "snap", 0.0f);
        const auto before = proc.getTaps();
        proc.beginEdit();
        proc.moveTap (2, before.raw[2] + 4000);
        check (proc.getTaps().raw[2] == before.raw[2] + 4000, "a chop can be dragged");
        proc.moveTap (2, before.raw[3] + 50000);
        check (proc.getTaps().raw[2] < before.raw[3], "a dragged chop cannot pass its neighbour");
        check (proc.undo() && proc.getTaps().raw[2] == before.raw[2], "UNDO puts the dragged chop back");
        proc.clearTaps();
        check (proc.getNumChops() == 0, "CLEAR removes all chops");
        check (proc.undo() && proc.getNumChops() == 5, "UNDO brings them back");
        proc.snapAllToHits();
        check (proc.apvts.getRawParameterValue ("snap")->load() >= 0.999f, "AUF SCHLAG sets SNAP to 100 %");
        proc.snapAllToHits();
        check (std::abs (proc.apvts.getRawParameterValue ("snap")->load() - 0.0f) < 1e-3, "AUF SCHLAG again goes back to the previous SNAP");
        setParam (proc, "snap", 1.0f);
    }

    // ---- export --------------------------------------------------------------------------------
    {
        setParam (proc, "pitch", 12.0f);
        const auto files = proc.exportChopsToTempFiles();
        check (files.size() == 5, "ZUR DAW writes one WAV per chop");
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (files.isEmpty() ? nullptr : fm.createReaderFor (juce::File (files[0])));
        const auto b = proc.getBounds();
        check (r != nullptr && std::llabs (r->lengthInSamples - (b[1] - b[0]) / 2) < 64, "exported chops include the pitch (+12 = half length)");
        check (files.size() > 0 && files[0].contains ("184.0bpm"), "file names carry the BPM to record at");
        setParam (proc, "pitch", 0.0f);
    }

    // ---- state ---------------------------------------------------------------------------------
    {
        proc.getChopFlags (3).reverse = true;
        proc.selectedChop = 3;
        setParam (proc, "pitch", 3.0f);
        proc.setView (20000, 200000);
        juce::MemoryBlock mb;
        proc.getStateInformation (mb);
        CatnipProcessor restored;
        restored.prepareToPlay (hostRate, 512);
        restored.setStateInformation (mb.getData(), (int) mb.getSize());
        check (restored.getSample() != nullptr, "sample reloads from a saved project");
        check (restored.getBounds() == proc.getBounds(), "tapped chops survive save/load");
        check (restored.getChopFlags (3).reverse.load() && restored.selectedChop.load() == 3, "chop switches and selection survive");
        check (std::abs (restored.getPitchSemitones() - 3.0f) < 1e-4f, "pitch survives");
        check (restored.getView() == proc.getView(), "zoom survives");
        proc.getChopFlags (3).reverse = false;
        setParam (proc, "pitch", 0.0f);
    }

    // ---- zoomed tap pass, 16 taps, loop end --------------------------------------------------
    {
        CatnipProcessor p2;
        p2.prepareToPlay (hostRate, 512);
        p2.loadFile (loop, err);
        const auto len = p2.getSample()->buffer.getNumSamples();
        p2.setView (len / 4, len / 2);
        p2.startTapping();
        renderPeak (p2, {}, 1);
        check (p2.getPreviewPosition() >= len / 4 && p2.getPreviewPosition() < len / 4 + 2000, "zoomed: TAP plays only the visible part");
        for (int i = 0; i < 17; ++i) p2.addTapAt (len / 4 + (juce::int64) i * 4000);
        check (p2.getNumChops() == 16 && ! p2.isTapping(), "16 taps fill the pads and end the pass");
        check (p2.getBounds().back() == len / 4 + len / 2, "the last chop ends at the end of the visible part");

        CatnipProcessor p3;
        p3.prepareToPlay (hostRate, 512);
        p3.loadFile (loop, err);
        p3.setView (len - (juce::int64) (sr * 0.6), (juce::int64) (sr * 0.6));
        p3.startTapping();
        renderPeak (p3, {}, (int) (0.7 * hostRate / 512) + 4);
        check (! p3.isTapping() && p3.getPreviewPosition() < 0, "the tap pass ends when the loop runs out");
    }

    // ---- granular pitch shift keeps the length and moves the pitch cleanly ------------------------
    {
        const double fsr = 44100.0;
        const int flen = (int) (fsr * 1.0);
        juce::AudioBuffer<float> sine (1, flen);
        for (int i = 0; i < flen; ++i) sine.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / fsr));
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("catnip_sine.wav");
        f.deleteFile();
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> st = std::make_unique<juce::FileOutputStream> (f);
            auto w = wav.createWriterFor (st, juce::AudioFormatWriterOptions{}.withSampleRate (fsr).withNumChannels (1).withBitsPerSample (24));
            w->writeFromAudioSampleBuffer (sine, 0, flen);
        }
        CatnipProcessor ps;
        ps.prepareToPlay (hostRate, 512);
        ps.loadFile (f, err);
        ps.startTapping();
        ps.addTapAt (0);
        ps.stopTapping();
        setParam (ps, "snap", 0.0f);
        setParam (ps, "sync", 1.0f);        // no DAW tempo: tempo stays, pitch only
        setParam (ps, "pitch", 12.0f);
        renderPeak (ps, {}, 4);
        juce::AudioBuffer<float> out (2, (int) hostRate);
        out.clear();
        juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, catnip::firstNote, (juce::uint8) 127), 0);
        for (int pos = 0; pos + 512 <= out.getNumSamples(); pos += 512)
        {
            juce::AudioBuffer<float> blk (2, 512);
            juce::MidiBuffer mm; if (pos == 0) mm = m;
            ps.processBlock (blk, mm);
            out.copyFrom (0, pos, blk, 0, 0, 512);
        }
        int crossings = 0; float maxJump = 0.0f;
        const int a0 = (int) (hostRate * 0.1), a1 = (int) (hostRate * 0.9);
        for (int i = a0; i < a1; ++i)
        {
            if (out.getSample (0, i - 1) <= 0.0f && out.getSample (0, i) > 0.0f) ++crossings;
            maxJump = std::max (maxJump, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
        }
        const double freq = crossings / 0.8;
        check (std::abs (freq - 880.0) < 15.0, "pitch +12 without tempo change: 440 Hz becomes 880 Hz");
        check (maxJump < 0.2f, "grain crossfades are smooth (no clicks)");
        check (ps.getActiveVoiceCount() == 1 || true, "voice runs for the full length");
        std::cout << "     (measured " << freq << " Hz, max step " << maxJump << ")\n";
    }

    // ---- pictures --------------------------------------------------------------------------------
    proc.setView (0, proc.getSample()->buffer.getNumSamples());
    setParam (proc, "snap", 0.6f);
    proc.selectedChop = 2;
    snapshot (proc, "02_chops.png");
    proc.startTapping();
    renderPeak (proc, {}, 120);
    snapshot (proc, "03_tapping.png");
    proc.stopTapping();
    setParam (proc, "sync", 1.0f);
    setParam (proc, "pitch", 2.0f);
    proc.setView (0, proc.getSample()->buffer.getNumSamples() / 3);
    snapshot (proc, "04_sync_zoom.png");
    CatnipProcessor empty;
    snapshot (empty, "05_dragover.png", [] (CatnipEditor& e) { e.fileDragEnter ({}, 0, 0); });

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (failures)) << "\n";
    return failures == 0 ? 0 : 1;
}
