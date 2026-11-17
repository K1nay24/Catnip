#pragma once

#include "PluginProcessor.h"
#include <map>

/** The whole hardware panel (1080 x 700 logical pixels), painted by hand. */
class CatnipView final : public juce::Component,
                         private juce::Timer,
                         private juce::ChangeListener
{
public:
    static constexpr int width = 1080, height = 700;

    explicit CatnipView (CatnipProcessor&);
    ~CatnipView() override;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // called by the editor
    void padDown (int chop);
    void padUp (int chop);
    void toggleTap();
    bool tapKey();                 // a pad key while tapping sets a chop; true if it was used for that
    void loadFile (const juce::File&);
    void setDragOver (bool);
    void stopEverything();
    void undo();

private:
    enum class Knob { pitch = 0, snap, zoom, volume };
    enum class Key { reverse = 0, gate, onBeat, sync, toDaw, play, stop, tap, undo, clear, count };

    struct KeyInfo { Key id; const char* label; int variant; int icon; juce::Rectangle<int> face; };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    // layout
    static juce::Rectangle<int> padCell (int visual);          // label + well
    static juce::Rectangle<int> padFace (int visual);
    static juce::Rectangle<float> knobCentreArea (int knob);   // whole knob widget (126 x 150)
    static juce::Rectangle<int> displayRect() { return { 540, 62, 504, 160 }; }
    static juce::Rectangle<int> screenRect() { return displayRect().reduced (5); }
    static juce::Rectangle<int> waveRect() { return { screenRect().getX() + 12, screenRect().getY() + 34, 470, 88 }; }
    const KeyInfo* keyAt (juce::Point<int>) const;
    int knobAt (juce::Point<int>) const;
    int padAt (juce::Point<int>) const;

    // painting
    void paintPanel (juce::Graphics&);
    void paintPad (juce::Graphics&, int visual);
    void paintKnob (juce::Graphics&, int knob);
    void paintKey (juce::Graphics&, const KeyInfo&);
    void paintDisplay (juce::Graphics&);
    juce::Image renderScreen();                                // display contents incl. glow
    const juce::Image& faceImage (const juce::String& id, int w, int h, std::function<void (juce::Graphics&, juce::Rectangle<float>)> fill,
                                  const juce::Image& texture, juce::Point<int> offset, bool overlay, float opacity, float radius);

    // state helpers
    float knobValue (int knob) const;                          // 0..1
    void setKnobValue (int knob, float v, bool fine);
    juce::String knobText (int knob) const;
    bool keyActive (Key) const;
    void pressKey (Key);
    void setZoom (float z);
    float currentZoom() const;
    juce::String bpmLine() const;
    void showMessage (const juce::String&);
    void openFileChooser();
    int chopLineAt (float x) const;                            // index of the chop line near x in the wave, or -1
    juce::int64 sampleAtX (float x) const;
    float xForSample (juce::int64 s) const;
    const std::vector<std::pair<float, float>>& viewPeaks (const catnip::SampleData&, juce::int64 start, juce::int64 length);

    CatnipProcessor& proc;
    juce::Typeface::Ptr vt, reg, semi, xbold;
    juce::Font font (juce::Typeface::Ptr, float size) const;

    juce::Image logo, texPlastic, texPad, texKey, background;
    std::map<juce::String, juce::Image> faceCache;
    juce::Image knobFace, knobCap;

    std::vector<KeyInfo> keys;
    std::array<int, catnip::maxChops> lastHits {};
    std::array<double, catnip::maxChops> flashUntil {};
    std::array<bool, catnip::maxChops> held {};

    // mouse
    int mousePad = -1, dragKnob = -1, dragLine = -1, hoverLine = -1;
    const KeyInfo* pressedKey = nullptr;
    float dragStartValue = 0.0f;
    bool lineEditStarted = false, exportStarted = false, dragOver = false;

    // display cache
    juce::Image screenImage;
    juce::String screenSignature;
    const catnip::SampleData* peaksSample = nullptr;
    juce::int64 peaksStart = -1, peaksLength = -1;
    std::vector<std::pair<float, float>> peaks;

    juce::String message, lastSig;
    double messageUntil = 0.0;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CatnipView)
};

class CatnipEditor final : public juce::AudioProcessorEditor,
                           public juce::FileDragAndDropTarget
{
public:
    explicit CatnipEditor (CatnipProcessor&);
    ~CatnipEditor() override = default;

    void resized() override;
    void parentHierarchyChanged() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { view.setDragOver (true); }
    void fileDragExit (const juce::StringArray&) override { view.setDragOver (false); }
    void filesDropped (const juce::StringArray&, int, int) override;

    CatnipView& getView() { return view; }

private:
    CatnipProcessor& proc;
    CatnipView view;
    std::map<int, juce::juce_wchar> heldKeys;   // visual pad -> key character

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CatnipEditor)
};
