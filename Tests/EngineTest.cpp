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

    std::cout << "screens\n";
    snapshot (proc, "02_loaded.png");
    snapshot (proc, "03_dragover.png", [] (CatnipEditor& e) { e.fileDragEnter ({}, 0, 0); });

    std::cout << (failures == 0 ? "ALL PASSED\n" : "FAILURES: ") << (failures ? juce::String (failures) : juce::String()) << "\n";
    return failures == 0 ? 0 : 1;
}
