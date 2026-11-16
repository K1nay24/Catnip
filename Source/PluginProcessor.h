#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <memory>
#include "ChopAnalysis.h"

namespace catnip
{
    constexpr int maxChops = 16;
    constexpr int firstNote = 36;   // MIDI C1 = chop 1 (pad 1)
    constexpr int numPeaks = 160;   // waveform bars in the display

    /** Per-chop settings. Written on the message thread, read on the audio thread. */
    struct ChopParams
    {
        std::atomic<float> pitch { 0.0f };    // semitones, -12..+12
        std::atomic<float> decay { 1.0f };    // fraction of the chop that plays, 0.05..1
        std::atomic<float> level { 0.8f };    // linear gain, 0..1
        std::atomic<bool> reverse { false };
        std::atomic<bool> gate { false };     // true: plays only while held
    };

    struct SampleData
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 44100.0;
        juce::File file;
        Analysis analysis;
    };
}

class CatnipProcessor final : public juce::AudioProcessor,
                              public juce::ChangeBroadcaster
{
public:
    CatnipProcessor();
    ~CatnipProcessor() override = default;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Catnip"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Message-thread API used by the editor
    bool loadFile (const juce::File& file, juce::String& error);
    std::shared_ptr<const catnip::SampleData> getSample() const;
    std::vector<juce::int64> getBounds() const;
    void setBound (int index, juce::int64 position);

    /** The zoom window: chops are only placed inside [start, start + length). */
    std::pair<juce::int64, juce::int64> getView() const;
    void setView (juce::int64 start, juce::int64 length);
    juce::int64 minViewLength() const;

    int getChopCount() const noexcept { return chopCount.load(); }
    void setChopCount (int newCount);
    bool isGridMode() const noexcept { return gridMode.load(); }
    void setGridMode (bool shouldUseGrid);

    catnip::ChopParams& getChopParams (int index) { return chops[(size_t) juce::jlimit (0, catnip::maxChops - 1, index)]; }

    juce::StringArray exportChopsToTempFiles();

    juce::String getMissingFileName() const { return missingFile; }

    /** Voices that are sounding and not fading out (for tests). Audio-thread state, so only call when no audio runs. */
    int getActiveVoiceCount() const noexcept;

    juce::MidiKeyboardState keyboardState;
    std::array<std::atomic<int>, catnip::maxChops> hitCounters {};
    std::atomic<int> selectedChop { 0 };
    juce::AudioProcessorValueTreeState apvts;

private:
    struct Voice
    {
        bool active = false, releasing = false, reverse = false;
        int chop = -1;
        juce::int64 start = 0, end = 0;
        double played = 0.0, total = 0.0, increment = 1.0, fadeLen = 1.0;
        float gain = 0.0f, attack = 0.0f, attackStep = 1.0f, release = 1.0f, releaseStep = 0.0f;
    };

    void rechop();                 // call with lock held or before audio starts
    void startVoice (int chop, float velocity);
    void stopVoice (int chop);
    void renderVoices (juce::AudioBuffer<float>& out, int startSample, int numSamples);

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    mutable juce::SpinLock lock;
    std::shared_ptr<catnip::SampleData> sample;
    std::vector<juce::int64> bounds;
    juce::int64 viewStart = 0, viewLength = 0;
    std::array<catnip::ChopParams, catnip::maxChops> chops;
    std::array<Voice, 8> voices;   // one sounding chop at a time; extra slots only hold fade-outs

    std::atomic<int> chopCount { 16 };
    std::atomic<bool> gridMode { false };
    double hostRate = 44100.0;
    juce::String missingFile;
    std::atomic<float>* volumeParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CatnipProcessor)
};
