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

    /** Per-chop switches. Written on the message thread, read on the audio thread. */
    struct ChopFlags
    {
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

    /** The tapped chops: raw tap positions plus where the last chop ends. */
    struct TapSet
    {
        std::array<juce::int64, maxChops> raw {};      // where the user tapped / dragged to
        std::array<juce::int64, maxChops> target {};   // where SNAP pulls each one (-1: no hit nearby)
        int count = 0;
        juce::int64 end = 0;                           // end of the last chop
    };

    /** Chop starts after SNAP: raw + (target - raw) * snap, kept in order with a minimum gap.
        bounds gets count + 1 entries (the last one is the end). Allocation-free, safe on the audio thread. */
    int computeBounds (const TapSet& taps, float snap, std::array<juce::int64, maxChops + 1>& bounds);

    /** Plays one chop (or the preview) by resampling or, with DAW SYNC, by granular time-stretch. */
    struct Voice
    {
        bool active = false, releasing = false, reverse = false;
        int chop = -1;
        juce::int64 start = 0, length = 0;            // source region
        double pos = 0.0;                             // analysis position in the region (source samples)
        double tempoStep = 1.0, pitchStep = 1.0;      // source samples per output sample
        bool stretch = false;
        double grainLen = 2048.0;                     // output samples
        double grainPos[2] {}, grainPhase[2] {};
        float gain = 1.0f, attack = 0.0f, attackStep = 1.0f, release = 1.0f, releaseStep = 0.0f;
        double fadeLen = 64.0;                        // source samples faded at the end

        void begin (juce::int64 regionStart, juce::int64 regionLength, double tempo, double pitch,
                    double outRate, bool rev, float g);
        void beginRelease (double outRate, double seconds);
        /** Follows a pitch / sync change while the voice plays. */
        void setRates (double tempo, double pitch);
        /** Adds numSamples to out (from startSample). Returns false once the voice has finished. */
        bool render (const juce::AudioBuffer<float>& src, juce::AudioBuffer<float>& out, int startSample, int numSamples);
    private:
        float read (const juce::AudioBuffer<float>& src, int channel, double p) const;
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

    // ---- sample --------------------------------------------------------------------------
    bool loadFile (const juce::File& file, juce::String& error);
    std::shared_ptr<const catnip::SampleData> getSample() const;
    juce::String getMissingFileName() const { return missingFile; }

    // ---- chops (all tapped) --------------------------------------------------------------
    catnip::TapSet getTaps() const;
    std::vector<juce::int64> getBounds() const;          // chop starts after SNAP + end
    int getNumChops() const;
    void beginEdit();                                    // remember the chops for UNDO (call once per drag)
    void moveTap (int index, juce::int64 position);      // drag a chop on the screen
    void clearTaps();
    bool undo();
    bool canUndo() const;
    void snapAllToHits();                                // AUF SCHLAG: SNAP to 100 % (again: back)

    // ---- transport -----------------------------------------------------------------------
    void play();                                         // listen to the visible part
    void stopAll();
    void startTapping();                                 // silences everything, plays, pads set chops
    void stopTapping();
    bool isTapping() const noexcept { return tapping.load(); }
    bool isPreviewing() const noexcept { return previewPosition.load() >= 0 || previewRequest.load() >= 0; }
    juce::int64 getPreviewPosition() const noexcept { return previewPosition.load(); }
    bool isFreshPass() const;                            // tapping, but nothing tapped yet in this pass
    int tap();                                           // sets a chop at the play position, returns its index or -1
    int addTapAt (juce::int64 position);

    // ---- view (zoom) ---------------------------------------------------------------------
    std::pair<juce::int64, juce::int64> getView() const;
    void setView (juce::int64 start, juce::int64 length);
    juce::int64 minViewLength() const;

    // ---- tempo ---------------------------------------------------------------------------
    double getSampleBpm() const;                         // detected tempo of the file, 0 if unknown
    double getHostBpm() const noexcept { return hostBpm.load(); }   // 0 if the host gives none
    double getPitchSemitones() const noexcept { return pitchParam->load(); }
    bool isSynced() const noexcept { return syncParam->load() > 0.5f; }
    /** Tempo the sample plays at right now (what to set / record in the DAW). */
    double getPlaybackBpm() const;
    /** Source-samples-per-sample factors for tempo and pitch (1 = original). */
    std::pair<double, double> getRatios() const;

    catnip::ChopFlags& getChopFlags (int index) { return flags[(size_t) juce::jlimit (0, catnip::maxChops - 1, index)]; }

    /** Renders every chop with pitch, sync and reverse applied to WAV files (for dragging into the DAW). */
    juce::StringArray exportChopsToTempFiles();

    /** Voices that are sounding and not fading out (for tests). Only call while no audio runs. */
    int getActiveVoiceCount() const noexcept;

    juce::MidiKeyboardState keyboardState;
    std::array<std::atomic<int>, catnip::maxChops> hitCounters {};
    std::atomic<int> selectedChop { 0 };
    juce::AudioProcessorValueTreeState apvts;

private:
    void startVoice (int chop, float velocity);
    void stopVoice (int chop);
    void killAll();                                      // audio thread: everything fades out (4 ms)
    void refreshTargets();                               // lock held
    void pushUndo();                                     // lock held
    void changed() { sendChangeMessage(); }
    std::pair<double, double> ratiosLocked() const;    // lock held

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    mutable juce::SpinLock lock;
    std::shared_ptr<catnip::SampleData> sample;          // guarded by lock
    catnip::TapSet taps;                                 // guarded by lock
    std::vector<catnip::TapSet> undoStack;               // guarded by lock
    juce::int64 viewStart = 0, viewLength = 0;           // guarded by lock
    int tapsThisPass = 0;                                // guarded by lock
    float snapBeforeFull = 0.5f;

    std::array<catnip::ChopFlags, catnip::maxChops> flags;
    std::array<catnip::Voice, 8> voices;                 // audio thread: one sounding chop, the rest fade out
    catnip::Voice preview;                               // audio thread

    std::atomic<juce::int64> previewRequest { -1 }, previewEnd { 0 }, previewPosition { -1 };
    std::atomic<bool> stopRequest { false }, tapping { false };
    std::atomic<double> hostBpm { 0.0 };
    double hostRate = 44100.0;
    juce::String missingFile;
    float lastGain = 1.0f;
    std::atomic<float>* volumeParam = nullptr;
    std::atomic<float>* pitchParam = nullptr;
    std::atomic<float>* snapParam = nullptr;
    std::atomic<float>* syncParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CatnipProcessor)
};
