#pragma once

#include "PluginProcessor.h"
#include <map>

namespace catnip
{
    namespace colours
    {
        const juce::Colour cream     { 0xffE7E0D0 };
        const juce::Colour line      { 0xffD3CAB6 };
        const juce::Colour segment   { 0xffDCD4C2 };
        const juce::Colour screw     { 0xffC9C0AC };
        const juce::Colour text      { 0xff2B2A27 };
        const juce::Colour muted     { 0xff5E594F };
        const juce::Colour dark      { 0xff3A3936 };
        const juce::Colour darkLip   { 0xff23221F };
        const juce::Colour light     { 0xffEDE6D6 };
        const juce::Colour lcd       { 0xffC5CDA8 };
        const juce::Colour ink       { 0xff2E3A26 };
        const juce::Colour accent    { 0xffE0592A };
        const juce::Colour padBlack  { 0xff1F1F1E };
        const juce::Colour padHit    { 0xff121211 };
        const juce::Colour padLip    { 0xffCFC6B3 };
        const juce::Colour emptyText { 0xff8C8576 };
    }

    struct Fonts
    {
        static Fonts& get();
        juce::Font lcd (float h) const;
        juce::Font regular (float h) const;
        juce::Font semibold (float h) const;
        juce::Font bold (float h, float tracking = 0.0f) const;

    private:
        Fonts();
        juce::Typeface::Ptr vt, reg, semi, xbold;
    };

    class WheelLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                               float startAngle, float endAngle, juce::Slider&) override;
    };

    class KnobLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                               float startAngle, float endAngle, juce::Slider&) override;
    };
}

/** The whole plugin face, laid out at 960 x 660 and scaled by the editor. */
class CatnipView final : public juce::Component,
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    explicit CatnipView (CatnipProcessor&);
    ~CatnipView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void loadFile (const juce::File&);
    void openFileChooser();
    void setDragOver (bool);
    void padDown (int chop);
    void padUp (int chop);
    void spacePressed();
    bool tapKey();   // true if a tap was taken (TAP mode, running)

    static constexpr int width = 960, height = 660;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void syncKnobs();
    void applyZoom();
    double maxZoom() const;
    juce::int64 selectedChopCentre() const;
    void centreViewAt (float x);
    const std::vector<float>& viewPeaks (const catnip::SampleData&, juce::int64 start, juce::int64 length);
    void selectChop (int chop);

    // layout helpers (all in view coordinates)
    juce::Rectangle<float> lcdArea() const       { return { 20.0f, 72.0f, 920.0f, 232.0f }; }
    juce::Rectangle<float> waveArea() const      { return { 39.0f, 115.0f, 882.0f, 134.0f }; }
    juce::Rectangle<float> minimapArea() const   { return { 39.0f, 255.0f, 882.0f, 10.0f }; }
    juce::Rectangle<float> padCell (int vis) const;
    juce::Rectangle<float> padRect (int vis) const { return padCell (vis).withTrimmedTop (18.0f); }
    juce::Rectangle<float> folderButton() const  { return { 900.0f, 8.0f, 40.0f, 40.0f }; }
    juce::Rectangle<float> chooseButton() const  { return { 390.0f, 248.0f, 180.0f, 32.0f }; }
    juce::Rectangle<float> modeButton (int i) const;
    juce::Rectangle<float> countButton (int i) const;
    juce::Rectangle<float> divisionButton (int i) const;
    juce::Rectangle<float> bpmMinus() const   { return { 690.0f, 391.0f, 30.0f, 36.0f }; }
    juce::Rectangle<float> bpmBox() const     { return { 724.0f, 388.0f, 96.0f, 42.0f }; }
    juce::Rectangle<float> bpmPlus() const    { return { 824.0f, 391.0f, 30.0f, 36.0f }; }
    juce::Rectangle<float> bpmHalf() const    { return { 864.0f, 391.0f, 36.0f, 36.0f }; }
    juce::Rectangle<float> bpmDouble() const  { return { 904.0f, 391.0f, 36.0f, 36.0f }; }
    juce::Rectangle<float> tapButton() const  { return { 420.0f, 388.0f, 170.0f, 42.0f }; }
    juce::Rectangle<float> clearButton() const { return { 598.0f, 388.0f, 72.0f, 42.0f }; }
    juce::Rectangle<float> reverseButton() const { return { 420.0f, 590.0f, 96.0f, 36.0f }; }
    juce::Rectangle<float> gateButton() const    { return { 524.0f, 590.0f, 96.0f, 36.0f }; }
    juce::Rectangle<float> exportButton() const  { return { 700.0f, 600.0f, 240.0f, 40.0f }; }

    void paintHeader (juce::Graphics&);
    void paintLcd (juce::Graphics&);
    void paintPads (juce::Graphics&);
    void paintControls (juce::Graphics&);
    void paintOverlay (juce::Graphics&);

    int boundaryAt (juce::Point<float>) const;
    int chopAt (juce::Point<float>) const;
    int padAt (juce::Point<float>) const;

    CatnipProcessor& proc;
    catnip::KnobLookAndFeel knobLook;
    catnip::WheelLookAndFeel wheelLook;
    juce::Slider pitch, decay, level, zoom;
    juce::int64 zoomAnchor = -1;
    std::array<bool, catnip::maxChops> held {};
    const catnip::SampleData* cachedSample = nullptr;
    juce::int64 cachedStart = -1, cachedLength = -1;
    std::vector<float> cachedPeaks;
    bool draggingMinimap = false, draggingBpm = false, wasPlaying = false;
    double bpmDragStart = 120.0;
    juce::int64 dragBoundStart = 0, dragOriginStart = 0;
    juce::Image mascot;
    std::unique_ptr<juce::FileChooser> chooser;

    std::array<int, catnip::maxChops> seenHits {};
    std::array<juce::uint32, catnip::maxChops> flashUntil {};
    int mousePad = -1, mouseChop = -1, dragBound = -1, hoverBound = -1;
    bool dragOver = false, exportStarted = false;
    juce::String error;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CatnipView)
};

class CatnipEditor final : public juce::AudioProcessorEditor,
                           public juce::FileDragAndDropTarget
{
public:
    explicit CatnipEditor (CatnipProcessor&);
    ~CatnipEditor() override = default;

    void paint (juce::Graphics&) override {}
    void resized() override;
    void parentHierarchyChanged() override;

    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { view.setDragOver (true); }
    void fileDragExit (const juce::StringArray&) override { view.setDragOver (false); }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    CatnipView view;
    std::map<int, juce::juce_wchar> heldKeys;   // visual pad index -> character

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CatnipEditor)
};
