// Drives the real processor offline: load a file, play pads via MIDI, check the output,
// save/restore state, and render the editor to PNGs for a visual check.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <iostream>

static int failures = 0;
static void check (bool ok, const char* what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (! ok) ++failures;
}

static juce::File writeTestLoop()
{
    const double sr = 44100.0;
    const int len = (int) (sr * 2.0);
    juce::AudioBuffer<float> buf (2, len);
    buf.clear();
    juce::Random rnd (3);
    for (int h = 0; h < 8; ++h)
    {
        const int start = h * len / 8 + (h % 3) * 900;
        for (int n = 0; n < 5000 && start + n < len; ++n)
        {
            const float v = (float) std::exp (-n / 1500.0) * (0.5f * std::sin (n * 0.02f * (1 + h % 4)) + 0.3f * (rnd.nextFloat() - 0.5f));
            buf.addSample (0, start + n, v);
            buf.addSample (1, start + n, v);
        }
    }
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("catnip_test_loop.wav");
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (f);
    auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
    w->writeFromAudioSampleBuffer (buf, 0, len);
    return f;
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

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    CatnipProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    std::cout << "empty state\n";
    check (renderPeak (proc, {}, 4) == 0.0f, "silent without a sample");
    snapshot (proc, "01_empty.png");

    std::cout << "load\n";
    juce::String err;
    const auto file = writeTestLoop();
    check (proc.loadFile (file, err), "loads a WAV");
    check (proc.getBounds().size() == 17, "16 chops created");
    proc.setChopCount (8);
    check (proc.getBounds().size() == 9, "switching to 8 chops re-chops");
    proc.setChopCount (16);

    std::cout << "playback\n";
    juce::MidiBuffer on;
    on.addEvent (juce::MidiMessage::noteOn (1, catnip::firstNote + 0, 1.0f), 0);
    const float p1 = renderPeak (proc, on, 20);
    check (p1 > 0.05f, "pad 1 makes sound");
    check (proc.hitCounters[0].load() == 1, "hit counter for the GUI increments");

    juce::MidiBuffer outOfRange;
    outOfRange.addEvent (juce::MidiMessage::noteOn (1, 20, 1.0f), 0);
    check (renderPeak (proc, outOfRange, 4) == 0.0f, "notes outside the pads are ignored");

    proc.getChopParams (0).level = 0.0f;
    check (renderPeak (proc, on, 10) == 0.0f, "level 0 is silent");
    proc.getChopParams (0).level = 0.8f;

    proc.getChopParams (0).reverse = true;
    check (renderPeak (proc, on, 20) > 0.01f, "reverse plays");
    proc.getChopParams (0).reverse = false;

    proc.getChopParams (0).gate = true;
    {
        juce::AudioBuffer<float> out (2, 512);
        juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, catnip::firstNote, 1.0f), 0);
        proc.processBlock (out, m);
        juce::MidiBuffer off; off.addEvent (juce::MidiMessage::noteOff (1, catnip::firstNote), 0);
        proc.processBlock (out, off);
        { juce::MidiBuffer none; proc.processBlock (out, none); }   // 15 ms release
        float tail = 0.0f;
        for (int i = 0; i < 4; ++i) { juce::MidiBuffer none; proc.processBlock (out, none); tail = std::max (tail, out.getMagnitude (0, 512)); }
        check (tail == 0.0f, "gate stops the chop on note-off");
    }
    proc.getChopParams (0).gate = false;

    proc.keyboardState.noteOn (1, catnip::firstNote + 0, 1.0f);
    check (renderPeak (proc, {}, 20) > 0.01f, "on-screen pads / computer keys trigger via keyboard state");
    proc.keyboardState.noteOff (1, catnip::firstNote + 0, 0.0f);

    std::cout << "choke\n";
    {
        juce::AudioBuffer<float> out (2, 512);
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (1, catnip::firstNote + 0, 1.0f), 0);
        m.addEvent (juce::MidiMessage::noteOn (1, catnip::firstNote + 2, 1.0f), 100);
        m.addEvent (juce::MidiMessage::noteOn (1, catnip::firstNote + 4, 1.0f), 200);
        proc.processBlock (out, m);
        check (proc.getActiveVoiceCount() == 1, "a new chop cuts the previous ones (no overlap)");
        juce::MidiBuffer again; again.addEvent (juce::MidiMessage::noteOn (1, catnip::firstNote + 4, 1.0f), 0);
        proc.processBlock (out, again);
        check (proc.getActiveVoiceCount() == 1, "retriggering the same chop does not stack");
        for (int i = 0; i < 40; ++i) { juce::MidiBuffer none; proc.processBlock (out, none); }
    }

    std::cout << "zoom\n";
    {
        const auto len = proc.getSample()->buffer.getNumSamples();
        proc.setView (len / 4, len / 4);
        const auto v = proc.getView();
        const auto zb = proc.getBounds();
        check (v.first == len / 4 && v.second == len / 4, "zoom window is set");
        check (zb.size() == 17 && zb.front() == v.first && zb.back() == v.first + v.second, "16 new chops fill only the zoomed window");
        bool inside = true;
        for (auto x : zb) inside = inside && x >= v.first && x <= v.first + v.second;
        check (inside, "all chop points lie inside the window");
        proc.setView (-5000, 10);
        check (proc.getView().first == 0 && proc.getView().second == proc.minViewLength(), "zoom is clamped to the file and a minimum length");
        juce::MidiBuffer zm; zm.addEvent (juce::MidiMessage::noteOn (1, catnip::firstNote, 1.0f), 0);
        check (renderPeak (proc, zm, 10) > 0.0f, "zoomed chops play");
        proc.setView (0, len);
        check (proc.getBounds().size() == 17 && proc.getBounds().back() == len, "zooming out re-chops the whole file");
    }

    std::cout << "editing + export\n";
    auto before = proc.getBounds();
    proc.setBound (3, before[3] + 2000);
    check (proc.getBounds()[3] == before[3] + 2000, "chop boundary can be moved");
    proc.setBound (3, before[4] + 999999);
    check (proc.getBounds()[3] < proc.getBounds()[4], "boundary cannot pass its neighbour");
    const auto exported = proc.exportChopsToTempFiles();
    check (exported.size() == 16 && juce::File (exported[0]).getSize() > 100, "exports 16 WAV chops for drag to DAW");

    std::cout << "state\n";
    proc.getChopParams (5).pitch = 7.0f;
    proc.getChopParams (5).reverse = true;
    proc.selectedChop = 5;
    {
        const auto len = proc.getSample()->buffer.getNumSamples();
        proc.setView (len / 2, len / 3);
    }
    const auto savedView = proc.getView();
    const auto edited = proc.getBounds();
    juce::MemoryBlock state;
    proc.getStateInformation (state);

    CatnipProcessor restored;
    restored.prepareToPlay (48000.0, 512);
    restored.setStateInformation (state.getData(), (int) state.getSize());
    check (restored.getSample() != nullptr, "sample reloads from saved project");
    check (restored.getBounds() == edited, "hand-edited chop points survive save/load");
    check (restored.getChopParams (5).pitch.load() == 7.0f && restored.getChopParams (5).reverse.load(), "per-chop settings survive");
    check (restored.selectedChop.load() == 5, "selected chop survives");
    check (restored.getView() == savedView, "zoom window survives save/load");

    std::cout << "takt mode (92 BPM drum loop)\n";
    CatnipProcessor beatProc;
    beatProc.prepareToPlay (44100.0, 512);
    juce::Array<juce::int64> beats;
    {
        juce::String err2;
        check (beatProc.loadFile (writeDrumLoop (92.0, beats), err2), "loads the drum loop");
        std::cout << "  (tempo " << beatProc.getBpm() << " BPM)\n";
        check (std::abs (beatProc.getBpm() - 92.0) < 0.2, "tempo detected as 92 BPM");
        beatProc.setMode (catnip::ChopMode::bars);
        beatProc.setDivision (catnip::Division::bar);
        check (beatProc.getNumChops() == 4, "1 TAKT: 4 chops for 4 bars");
        check (std::llabs (beatProc.getBounds()[0] - beats[0]) < 900, "first chop starts on bar 1");
        beatProc.setDivision (catnip::Division::half);
        check (beatProc.getNumChops() == 8, "1/2 TAKT: 8 chops");
        beatProc.setDivision (catnip::Division::beat);
        check (beatProc.getNumChops() == 16, "SCHLAG: 16 chops");
        bool onBeats = true;
        const auto bb = beatProc.getBounds();
        for (int k = 0; k < 16; ++k) onBeats = onBeats && std::llabs (bb[(size_t) k] - beats[k]) < 900;
        check (onBeats, "every beat chop starts on its beat (within 20 ms)");
        beatProc.setChopCount (8);
        check (beatProc.getNumChops() == 8, "MAX. CHOPS 8 caps the beat chops");
        beatProc.setChopCount (16);

        const auto before = beatProc.getBounds()[1];
        beatProc.shiftGrid (2000);
        check (beatProc.getBounds()[1] == before + 2000, "dragging moves the whole grid");
        beatProc.resetBpm();
        check (std::llabs (beatProc.getBounds()[0] - beats[0]) < 900, "reset brings the grid back to bar 1");
        beatProc.setDivision (catnip::Division::bar);
        beatProc.setBpm (beatProc.getBpm() * 0.5);
        check (beatProc.getNumChops() == 2, "half the tempo = half the bars");
        beatProc.resetBpm();
    }

    std::cout << "preview + tap mode\n";
    {
        beatProc.startPreview();
        juce::AudioBuffer<float> out (2, 512);
        float peak = 0.0f;
        for (int i = 0; i < 20; ++i) { juce::MidiBuffer none; beatProc.processBlock (out, none); peak = juce::jmax (peak, out.getMagnitude (0, 512)); }
        check (peak > 0.05f && beatProc.getPreviewPosition() > 9000, "space plays the sample, play head moves");
        juce::MidiBuffer padHit; padHit.addEvent (juce::MidiMessage::noteOn (1, catnip::firstNote, 1.0f), 0);
        beatProc.processBlock (out, padHit);
        for (int i = 0; i < 2; ++i) { juce::MidiBuffer none; beatProc.processBlock (out, none); }
        check (beatProc.getPreviewPosition() < 0, "hitting a pad stops the preview (no overlap)");

        beatProc.setMode (catnip::ChopMode::tap);
        beatProc.startTapping();
        check (beatProc.isTapping(), "tapping starts");
        // taps come ~60 ms late, like a human reacting: they must land on the hits
        for (int k : { 0, 2, 4, 6, 9 })
            beatProc.addTapAt (beats[k] + 2650);
        const auto tb = beatProc.getBounds();
        check (beatProc.getNumChops() == 5, "5 taps = 5 chops");
        bool snapped = tb.size() == 6;
        const int idx[] = { 0, 2, 4, 6, 9 };
        for (int k = 0; k < 5 && snapped; ++k) snapped = std::llabs (tb[(size_t) k] - beats[idx[k]]) < 300;
        check (snapped, "late taps snap back onto the hits");
        check (beatProc.addTapAt (beats[9] + 100) < 0, "a double tap on the same hit is ignored");
        beatProc.stopPreview();
        check (! beatProc.isTapping(), "space stops tapping");

        juce::MemoryBlock st;
        beatProc.getStateInformation (st);
        CatnipProcessor again;
        again.prepareToPlay (44100.0, 512);
        again.setStateInformation (st.getData(), (int) st.getSize());
        check (again.getMode() == catnip::ChopMode::tap && again.getBounds() == tb, "tapped chops survive save/load");
    }

    std::cout << "screens\n";
    snapshot (proc, "02_loaded.png", [] (CatnipEditor& e) { e.keyPressed (juce::KeyPress ('a')); });
    beatProc.setMode (catnip::ChopMode::bars);
    beatProc.setDivision (catnip::Division::half);
    beatProc.selectedChop = 2;
    snapshot (beatProc, "04_takt.png");
    beatProc.setMode (catnip::ChopMode::tap);
    for (int k : { 0, 3, 4, 8, 10, 12 }) beatProc.addTapAt (beats[k] + 2000);
    snapshot (beatProc, "05_tap.png");
    beatProc.setMode (catnip::ChopMode::transient);
    snapshot (beatProc, "06_transient.png");
    snapshot (proc, "03_dragover.png", [] (CatnipEditor& e) { e.fileDragEnter ({}, 0, 0); });

    std::cout << (failures == 0 ? "ALL PASSED\n" : "FAILURES: ") << (failures ? juce::String (failures) : juce::String()) << "\n";
    return failures == 0 ? 0 : 1;
}
