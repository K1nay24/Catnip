#include "PluginEditor.h"
#include "BinaryData.h"

using namespace catnip;

namespace
{
    juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }

    // visual pad index (0 = top-left, row by row) -> chop index (pad 1 is bottom-left, like an MPC)
    int chopOfVisual (int vis) { return (3 - vis / 4) * 4 + (vis % 4); }

    const char* const keyLabelsQwertz[16] = { "1","2","3","4","Q","W","E","R","A","S","D","F","Y","X","C","V" };

    int visualForChar (juce::juce_wchar c)
    {
        static const juce::String rows ("1234qwerasdf");
        c = juce::CharacterFunctions::toLowerCase (c);
        const int i = rows.indexOfChar (c);
        if (i >= 0) return i;
        switch (c)
        {
            case 'y': case 'z': return 12;   // same physical key on QWERTZ and QWERTY
            case 'x': return 13;
            case 'c': return 14;
            case 'v': return 15;
            default:  return -1;
        }
    }

    juce::String formatTime (double seconds)
    {
        const int m = (int) (seconds / 60.0);
        return juce::String (m) + ":" + juce::String (seconds - m * 60.0, 2).paddedLeft ('0', 5);
    }

    void drawText (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, const juce::Font& f,
                   juce::Colour c, juce::Justification j = juce::Justification::centredLeft)
    {
        g.setFont (f);
        g.setColour (c);
        g.drawText (t, r, j, false);
    }

    bool isAudioFile (const juce::String& path)
    {
        return juce::File (path).hasFileExtension ("wav;aif;aiff;mp3;flac;ogg;m4a;caf");
    }
}

//==============================================================================
Fonts& Fonts::get() { static Fonts f; return f; }

Fonts::Fonts()
{
    vt    = juce::Typeface::createSystemTypefaceFor (BinaryData::VT323Regular_ttf, BinaryData::VT323Regular_ttfSize);
    reg   = juce::Typeface::createSystemTypefaceFor (BinaryData::ArchivoRegular_ttf, BinaryData::ArchivoRegular_ttfSize);
    semi  = juce::Typeface::createSystemTypefaceFor (BinaryData::ArchivoSemiBold_ttf, BinaryData::ArchivoSemiBold_ttfSize);
    xbold = juce::Typeface::createSystemTypefaceFor (BinaryData::ArchivoExtraBold_ttf, BinaryData::ArchivoExtraBold_ttfSize);
}

juce::Font Fonts::lcd (float h) const      { return juce::Font (juce::FontOptions (vt).withHeight (h)); }
juce::Font Fonts::regular (float h) const  { return juce::Font (juce::FontOptions (reg).withHeight (h)); }
juce::Font Fonts::semibold (float h) const { return juce::Font (juce::FontOptions (semi).withHeight (h)); }
juce::Font Fonts::bold (float h, float tracking) const
{
    return juce::Font (juce::FontOptions (xbold).withHeight (h).withKerningFactor (tracking));
}

void KnobLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float startAngle, float endAngle, juce::Slider&)
{
    const auto size = (float) juce::jmin (w, h) - 4.0f;
    const auto r = juce::Rectangle<float> (size, size).withCentre ({ x + w * 0.5f, y + h * 0.5f - 1.5f });

    g.setColour (colours::darkLip);
    g.fillEllipse (r.translated (0.0f, 3.0f));
    g.setColour (colours::dark);
    g.fillEllipse (r);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawEllipse (r.reduced (0.5f), 1.0f);

    const auto angle = startAngle + pos * (endAngle - startAngle);
    const auto c = r.getCentre();
    const auto rad = size * 0.5f;
    juce::Path pointer;
    pointer.addRoundedRectangle (-1.0f, -rad + 6.0f, 2.0f, rad * 0.5f, 1.0f);
    g.setColour (colours::accent);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c));
}

void WheelLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float, float, juce::Slider&)
{
    const auto size = (float) juce::jmin (w, h) - 6.0f;
    const auto r = juce::Rectangle<float> (size, size).withCentre ({ x + w * 0.5f, y + h * 0.5f - 2.0f });
    const auto c = r.getCentre();
    const float rad = size * 0.5f;

    // drop shadow and ridged rim, like the data wheel on old samplers
    g.setColour (colours::darkLip);
    g.fillEllipse (r.translated (0.0f, 4.0f));
    g.setColour (colours::dark);
    g.fillEllipse (r);

    const float spin = pos * juce::MathConstants<float>::twoPi * 2.0f;   // two full turns over the range
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    for (int i = 0; i < 48; ++i)
    {
        const float a = spin + (float) i * juce::MathConstants<float>::twoPi / 48.0f;
        g.drawLine (juce::Line<float> (c.getPointOnCircumference (rad - 1.0f, a), c.getPointOnCircumference (rad - 7.0f, a)), 2.0f);
    }

    // face
    const auto face = r.reduced (9.0f);
    g.setColour (juce::Colour (0xff2F2E2B));
    g.fillEllipse (face);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawEllipse (face.reduced (0.5f), 1.0f);

    // finger dimple
    const auto dimpleCentre = c.getPointOnCircumference (rad * 0.55f, spin);
    const auto dimple = juce::Rectangle<float> (rad * 0.42f, rad * 0.42f).withCentre (dimpleCentre);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (dimple);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawEllipse (dimple.translated (0.0f, 1.0f), 1.0f);

    // small orange index mark on the rim
    g.setColour (colours::accent);
    g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (c.getPointOnCircumference (rad - 13.0f, spin)));
}

//==============================================================================
CatnipView::CatnipView (CatnipProcessor& p) : proc (p)
{
    setSize (width, height);
    setOpaque (false);
    mascot = juce::ImageCache::getFromMemory (BinaryData::kinai_png, BinaryData::kinai_pngSize);

    auto setupKnob = [this] (juce::Slider& s, double lo, double hi, double step, double def, int x)
    {
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        s.setRange (lo, hi, step);
        s.setDoubleClickReturnValue (true, def);
        s.setMouseDragSensitivity (180);
        s.setLookAndFeel (&knobLook);
        s.setBounds (x, 468, 60, 62);
        s.setWantsKeyboardFocus (false);
        s.setMouseClickGrabsKeyboardFocus (false);
        s.onDragEnd = [this] { if (auto* top = getParentComponent()) top->grabKeyboardFocus(); };
        addAndMakeVisible (s);
    };
    setupKnob (pitch, -12.0, 12.0, 1.0, 0.0, 418);
    setupKnob (decay, 0.05, 1.0, 0.01, 1.0, 502);
    setupKnob (level, 0.0, 1.0, 0.01, 0.8, 586);

    pitch.onValueChange = [this] { proc.getChopParams (proc.selectedChop).pitch = (float) pitch.getValue(); repaint(); };
    decay.onValueChange = [this] { proc.getChopParams (proc.selectedChop).decay = (float) decay.getValue(); repaint(); };
    level.onValueChange = [this] { proc.getChopParams (proc.selectedChop).level = (float) level.getValue(); repaint(); };

    // big zoom wheel: 0 = whole sample, 1 = closest zoom (log scale)
    zoom.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    zoom.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    zoom.setRange (0.0, 1.0, 0.0);
    zoom.setDoubleClickReturnValue (true, 0.0);
    zoom.setMouseDragSensitivity (600);
    zoom.setLookAndFeel (&wheelLook);
    zoom.setBounds (790, 446, 128, 128);
    zoom.setWantsKeyboardFocus (false);
    zoom.setMouseClickGrabsKeyboardFocus (false);
    zoom.onDragStart = [this] { zoomAnchor = selectedChopCentre(); };
    zoom.onDragEnd = [this] { zoomAnchor = -1; if (auto* top = getParentComponent()) top->grabKeyboardFocus(); };
    zoom.onValueChange = [this] { applyZoom(); };
    addAndMakeVisible (zoom);

    for (int i = 0; i < maxChops; ++i)
        seenHits[(size_t) i] = proc.hitCounters[(size_t) i].load();

    syncKnobs();
    proc.addChangeListener (this);
    startTimerHz (40);
}

CatnipView::~CatnipView()
{
    proc.removeChangeListener (this);
    for (auto* s : { &pitch, &decay, &level, &zoom })
        s->setLookAndFeel (nullptr);
}

void CatnipView::resized() {}

void CatnipView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    cachedSample = nullptr;   // sample or zoom window may have changed
    syncKnobs();
    repaint();
}

void CatnipView::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();
    bool dirty = false;
    for (int i = 0; i < maxChops; ++i)
    {
        const int h = proc.hitCounters[(size_t) i].load();
        if (h != seenHits[(size_t) i])
        {
            seenHits[(size_t) i] = h;
            flashUntil[(size_t) i] = now + 120;
            dirty = true;
        }
        else if (flashUntil[(size_t) i] != 0 && now > flashUntil[(size_t) i])
        {
            flashUntil[(size_t) i] = 0;
            dirty = true;
        }
    }
    if (proc.isTapping() && ! proc.isPreviewing())
        proc.stopPreview();               // reached the end of the window: tapping is done
    const bool playing = proc.isPreviewing();
    if (playing || wasPlaying) dirty = true;
    wasPlaying = playing;
    if (dirty) repaint();
}

double CatnipView::maxZoom() const
{
    auto s = proc.getSample();
    if (s == nullptr) return 1.0;
    return juce::jmax (1.0, (double) s->buffer.getNumSamples() / (double) proc.minViewLength());
}

juce::int64 CatnipView::selectedChopCentre() const
{
    const auto b = proc.getBounds();
    const int sel = proc.selectedChop.load();
    if (sel + 1 >= (int) b.size()) return -1;
    return (b[(size_t) sel] + b[(size_t) sel + 1]) / 2;
}

void CatnipView::applyZoom()
{
    auto s = proc.getSample();
    if (s == nullptr) return;
    const double len = (double) s->buffer.getNumSamples();
    const double factor = std::pow (maxZoom(), zoom.getValue());
    const auto newLen = (juce::int64) (len / factor);
    const auto view = proc.getView();
    const auto anchor = zoomAnchor >= 0 ? zoomAnchor : view.first + view.second / 2;
    proc.setView (anchor - newLen / 2, newLen);
}

void CatnipView::syncKnobs()
{
    if (auto s = proc.getSample())
    {
        const auto view = proc.getView();
        const double factor = (double) s->buffer.getNumSamples() / (double) juce::jmax<juce::int64> (1, view.second);
        const double mz = maxZoom();
        if (! zoom.isMouseButtonDown())
            zoom.setValue (mz > 1.0001 ? std::log (factor) / std::log (mz) : 0.0, juce::dontSendNotification);
    }
    else
        zoom.setValue (0.0, juce::dontSendNotification);

    auto& c = proc.getChopParams (proc.selectedChop);
    pitch.setValue (c.pitch.load(), juce::dontSendNotification);
    decay.setValue (c.decay.load(), juce::dontSendNotification);
    level.setValue (c.level.load(), juce::dontSendNotification);
}

void CatnipView::selectChop (int chop)
{
    proc.selectedChop = juce::jlimit (0, juce::jmax (0, proc.getNumChops() - 1), chop);
    syncKnobs();
    repaint();
}

void CatnipView::padDown (int chop)
{
    if (chop < 0 || chop >= proc.getNumChops()) return;
    held[(size_t) chop] = true;          // pad shows pressed right away, whatever the audio thread does
    selectChop (chop);
    proc.keyboardState.noteOn (1, firstNote + chop, 1.0f);
}

void CatnipView::padUp (int chop)
{
    if (chop < 0 || chop >= maxChops) return;
    held[(size_t) chop] = false;
    repaint();
    proc.keyboardState.noteOff (1, firstNote + chop, 0.0f);
}

void CatnipView::loadFile (const juce::File& f)
{
    juce::String err;
    error = proc.loadFile (f, err) ? juce::String() : err;
    dragOver = false;
    repaint();
}

void CatnipView::openFileChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Sample laden", juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg;*.m4a;*.caf");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<CatnipView> (this)] (const juce::FileChooser& fc)
                          {
                              if (safe != nullptr && fc.getResult().existsAsFile())
                                  safe->loadFile (fc.getResult());
                          });
}

void CatnipView::setDragOver (bool b)
{
    dragOver = b;
    repaint();
}

const std::vector<float>& CatnipView::viewPeaks (const SampleData& s, juce::int64 start, juce::int64 length)
{
    if (&s == cachedSample && start == cachedStart && length == cachedLength)
        return cachedPeaks;

    cachedSample = &s; cachedStart = start; cachedLength = length;
    cachedPeaks.assign ((size_t) numPeaks, 0.0f);
    const auto& buf = s.buffer;
    const int chs = buf.getNumChannels();
    float maxPeak = 0.0f;
    for (int p = 0; p < numPeaks; ++p)
    {
        const auto a = start + (length * p) / numPeaks;
        const auto e = juce::jmin<juce::int64> (buf.getNumSamples(), start + (length * (p + 1)) / numPeaks);
        const auto step = juce::jmax<juce::int64> (1, (e - a) / 512);
        float m = 0.0f;
        for (auto i = a; i < e; i += step)
            for (int c = 0; c < chs; ++c)
                m = juce::jmax (m, std::abs (buf.getSample (c, (int) i)));
        cachedPeaks[(size_t) p] = m;
        maxPeak = juce::jmax (maxPeak, m);
    }
    if (maxPeak > 0.0f)
        for (auto& v : cachedPeaks) v /= maxPeak;
    return cachedPeaks;
}

//==============================================================================
juce::Rectangle<float> CatnipView::padCell (int vis) const
{
    const float cw = 85.0f, ch = 72.5f;
    return { 20.0f + (float) (vis % 4) * (cw + 12.0f), 320.0f + (float) (vis / 4) * (ch + 10.0f), cw, ch };
}

juce::Rectangle<float> CatnipView::modeButton (int i) const
{
    static const float xs[] = { 423.0f, 530.0f, 603.0f }, ws[] = { 104.0f, 70.0f, 64.0f };
    return { xs[i], 343.0f, ws[i], 36.0f };
}

juce::Rectangle<float> CatnipView::countButton (int i) const
{
    return { 693.0f + (float) i * 51.0f, 343.0f, 48.0f, 36.0f };
}

juce::Rectangle<float> CatnipView::divisionButton (int i) const
{
    return { 423.0f + (float) i * 83.0f, 391.0f, 80.0f, 36.0f };
}

int CatnipView::boundaryAt (juce::Point<float> p) const
{
    auto s = proc.getSample();
    const auto wa = waveArea();
    if (s == nullptr || ! wa.expanded (0.0f, 4.0f).contains (p)) return -1;
    const auto b = proc.getBounds();
    const auto view = proc.getView();
    const size_t first = proc.getMode() == ChopMode::bars ? 0 : 1;
    for (size_t k = first; k + 1 < b.size(); ++k)
    {
        const float x = wa.getX() + (float) ((double) (b[k] - view.first) / (double) view.second) * wa.getWidth();
        if (std::abs (x - p.x) <= 5.0f) return (int) k;
    }
    return -1;
}

int CatnipView::chopAt (juce::Point<float> p) const
{
    auto s = proc.getSample();
    const auto wa = waveArea();
    if (s == nullptr || ! wa.withTop (wa.getY() - 28.0f).contains (p)) return -1;
    const auto b = proc.getBounds();
    const auto view = proc.getView();
    const double pos = (double) view.first + (p.x - wa.getX()) / wa.getWidth() * (double) view.second;
    for (size_t k = 0; k + 1 < b.size(); ++k)
        if (pos >= (double) b[k] && pos < (double) b[k + 1]) return (int) k;
    return -1;
}

int CatnipView::padAt (juce::Point<float> p) const
{
    for (int v = 0; v < 16; ++v)
        if (padRect (v).contains (p)) return v;
    return -1;
}

//==============================================================================
void CatnipView::mouseMove (const juce::MouseEvent& e)
{
    const int b = boundaryAt (e.position);
    if (b != hoverBound)
    {
        hoverBound = b;
        repaint (lcdArea().toNearestInt());
    }
    setMouseCursor (b >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void CatnipView::mouseExit (const juce::MouseEvent&)
{
    if (hoverBound >= 0) { hoverBound = -1; repaint(); }
}

void CatnipView::mouseDown (const juce::MouseEvent& e)
{
    if (auto* top = getParentComponent()) top->grabKeyboardFocus();
    const auto p = e.position;
    exportStarted = false;

    if (folderButton().contains (p)) { openFileChooser(); return; }

    if (proc.getSample() == nullptr)
    {
        if (chooseButton().contains (p) || lcdArea().contains (p)) { openFileChooser(); return; }
    }
    else
    {
        if (minimapArea().expanded (0.0f, 3.0f).contains (p)) { draggingMinimap = true; centreViewAt (p.x); return; }
        if ((dragBound = boundaryAt (p)) >= 0)
        {
            const auto b = proc.getBounds();
            dragBoundStart = b[(size_t) dragBound];
            dragOriginStart = proc.getGridOrigin();
            return;
        }
        if ((mouseChop = chopAt (p)) >= 0) { padDown (mouseChop); return; }
    }

    if (const int v = padAt (p); v >= 0)
    {
        mousePad = chopOfVisual (v);
        padDown (mousePad);
        return;
    }

    for (int i = 0; i < 3; ++i)
        if (modeButton (i).contains (p)) { proc.stopPreview(); proc.setMode ((ChopMode) i); return; }
    if (countButton (0).contains (p)) { proc.setChopCount (8); return; }
    if (countButton (1).contains (p)) { proc.setChopCount (16); return; }

    if (proc.getSample() != nullptr && proc.getMode() == ChopMode::bars)
    {
        for (int i = 0; i < 3; ++i)
            if (divisionButton (i).contains (p)) { proc.setDivision ((Division) i); return; }
        const double bpm = proc.getBpm();
        if (bpmMinus().contains (p))  { proc.setBpm (std::round (bpm) - 1.0); return; }
        if (bpmPlus().contains (p))   { proc.setBpm (std::round (bpm) + 1.0); return; }
        if (bpmHalf().contains (p))   { proc.setBpm (bpm * 0.5); return; }
        if (bpmDouble().contains (p)) { proc.setBpm (bpm * 2.0); return; }
        if (bpmBox().contains (p))
        {
            if (e.getNumberOfClicks() >= 2) { proc.resetBpm(); return; }
            draggingBpm = true;
            bpmDragStart = bpm;
            return;
        }
    }
    if (proc.getSample() != nullptr && proc.getMode() == ChopMode::tap)
    {
        if (tapButton().contains (p))  { spacePressed(); return; }
        if (clearButton().contains (p)) { proc.stopPreview(); proc.clearTaps(); return; }
    }

    auto& c = proc.getChopParams (proc.selectedChop);
    if (reverseButton().contains (p)) { c.reverse = ! c.reverse.load(); repaint(); return; }
    if (gateButton().contains (p))    { c.gate = ! c.gate.load(); repaint(); return; }
}

void CatnipView::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingMinimap) { centreViewAt (e.position.x); return; }
    if (draggingBpm)
    {
        // drag up = faster; 0.05 BPM per pixel, so 92.0 -> 92.5 is a short nudge
        proc.setBpm (std::round ((bpmDragStart - e.getDistanceFromDragStartY() * 0.05) * 10.0) / 10.0);
        return;
    }
    if (dragBound >= 0)
    {
        if (auto s = proc.getSample())
        {
            const auto wa = waveArea();
            const double frac = juce::jlimit (0.0, 1.0, (double) ((e.position.x - wa.getX()) / wa.getWidth()));
            const auto view = proc.getView();
            auto target = view.first + (juce::int64) (frac * (double) view.second);

            // lines click onto the nearest hit within ~6 px
            const auto tol = (juce::int64) (6.0 / wa.getWidth() * (double) view.second);
            const auto hit = nearestOnset (s->analysis.onsetsByTime, target, tol, tol);
            if (hit >= 0) target = hit;

            if (proc.getMode() == ChopMode::bars)
                proc.shiftGrid ((dragOriginStart + (target - dragBoundStart)) - proc.getGridOrigin());   // the whole grid moves
            else
                proc.setBound (dragBound, target);
        }
        return;
    }

    if (! exportStarted && exportButton().contains (e.mouseDownPosition) && e.getDistanceFromDragStart() > 4
        && proc.getSample() != nullptr)
    {
        exportStarted = true;
        const auto files = proc.exportChopsToTempFiles();
        if (! files.isEmpty())
            juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, this);
    }
}

void CatnipView::mouseUp (const juce::MouseEvent&)
{
    if (mousePad >= 0) { padUp (mousePad); mousePad = -1; }
    if (mouseChop >= 0) { padUp (mouseChop); mouseChop = -1; }
    dragBound = -1;
    draggingMinimap = false;
    draggingBpm = false;
}

void CatnipView::spacePressed()
{
    if (proc.getSample() == nullptr) return;
    if (proc.isTapping() || proc.isPreviewing())
        proc.stopPreview();
    else if (proc.getMode() == ChopMode::tap)
        proc.startTapping();
    else
        proc.startPreview();
    repaint();
}

bool CatnipView::tapKey()
{
    if (! proc.isTapping()) return false;
    proc.tap();
    repaint();
    return true;
}

void CatnipView::centreViewAt (float x)
{
    auto s = proc.getSample();
    if (s == nullptr) return;
    const auto mm = minimapArea();
    const double frac = juce::jlimit (0.0, 1.0, (double) ((x - mm.getX()) / mm.getWidth()));
    const auto view = proc.getView();
    proc.setView ((juce::int64) (frac * (double) s->buffer.getNumSamples()) - view.second / 2, view.second);
}

void CatnipView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (proc.getSample() == nullptr || ! lcdArea().contains (e.position)) return;
    // scrolling over the display moves the zoom window left / right
    const float delta = std::abs (w.deltaX) > std::abs (w.deltaY) ? -w.deltaX : w.deltaY;
    const auto view = proc.getView();
    proc.setView (view.first - (juce::int64) ((double) delta * (double) view.second * 0.5), view.second);
}

//==============================================================================
void CatnipView::paint (juce::Graphics& g)
{
    g.setColour (colours::cream);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    g.setColour (colours::screw);
    for (auto pt : std::initializer_list<juce::Point<float>> { { 11.5f, 11.5f }, { 948.5f, 11.5f }, { 11.5f, 648.5f }, { 948.5f, 648.5f } })
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (pt));

    paintHeader (g);
    paintLcd (g);
    paintPads (g);
    paintControls (g);
    if (dragOver) paintOverlay (g);
}

void CatnipView::paintHeader (juce::Graphics& g)
{
    auto& F = Fonts::get();
    g.setColour (colours::line);
    g.fillRect (20.0f, 55.0f, 920.0f, 1.0f);

    auto s = proc.getSample();
    const juce::String name = s != nullptr ? s->file.getFileName()
                                           : (proc.getMissingFileName().isNotEmpty() ? "Datei nicht gefunden" : "Kein Sample geladen");
    const juce::String info = s != nullptr ? juce::String ((double) s->buffer.getNumSamples() / s->sampleRate, 2) + juce::String (juce::CharPointer_UTF8 (" s  ·  "))
                                                 + juce::String (s->sampleRate / 1000.0, 1) + " kHz"
                                           : juce::String();

    const auto nameFont = F.semibold (13.0f), infoFont = F.regular (13.0f);
    const float nameW = juce::jmin (380.0f, juce::GlyphArrangement::getStringWidth (nameFont, name) + 2.0f);
    const float infoW = info.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (infoFont, info) + 2.0f;
    const float pillW = 14.0f + 14.0f + 10.0f + nameW + (info.isEmpty() ? 0.0f : 10.0f + infoW) + 14.0f;
    auto pill = juce::Rectangle<float> (pillW, 32.0f).withCentre ({ 480.0f, 28.0f });

    g.setColour (colours::segment);
    g.fillRoundedRectangle (pill, 16.0f);

    auto r = pill.reduced (14.0f, 0.0f);
    const auto icon = r.removeFromLeft (14.0f).withSizeKeepingCentre (13.0f, 13.0f);
    g.setColour (s != nullptr ? colours::text : colours::muted);
    g.drawEllipse (icon, 1.6f);
    g.fillEllipse (icon.withSizeKeepingCentre (3.5f, 3.5f));
    r.removeFromLeft (10.0f);
    drawText (g, name, r.removeFromLeft (nameW), nameFont, s != nullptr ? colours::text : colours::muted);
    if (info.isNotEmpty())
    {
        r.removeFromLeft (10.0f);
        drawText (g, info, r.removeFromLeft (infoW), infoFont, colours::muted);
    }

    // folder button
    const auto fb = folderButton().withSizeKeepingCentre (18.0f, 15.0f);
    juce::Path folder;
    folder.startNewSubPath (fb.getX(), fb.getY() + 2.0f);
    folder.lineTo (fb.getX() + 6.0f, fb.getY() + 2.0f);
    folder.lineTo (fb.getX() + 8.0f, fb.getY() + 4.5f);
    folder.lineTo (fb.getRight(), fb.getY() + 4.5f);
    folder.lineTo (fb.getRight(), fb.getBottom());
    folder.lineTo (fb.getX(), fb.getBottom());
    folder.closeSubPath();
    g.setColour (colours::muted);
    g.strokePath (folder, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void CatnipView::paintLcd (juce::Graphics& g)
{
    auto& F = Fonts::get();
    const auto area = lcdArea();
    g.setColour (colours::dark);
    g.fillRoundedRectangle (area, 8.0f);
    g.setColour (colours::lcd);
    g.fillRoundedRectangle (area.reduced (3.0f), 6.0f);
    // inner shadow along the top edge
    juce::ColourGradient shade (juce::Colour (0x40283220), 0.0f, area.getY() + 3.0f,
                                juce::Colour (0x00283220), 0.0f, area.getY() + 12.0f, false);
    g.setGradientFill (shade);
    g.fillRect (area.reduced (4.0f).withHeight (10.0f));

    const auto ink = colours::ink;
    const auto inkSoft = ink.withAlpha (0.55f);
    auto s = proc.getSample();
    const auto wa = waveArea();

    if (s == nullptr)
    {
        const auto zone = area.reduced (15.0f);
        juce::Path border;
        border.addRoundedRectangle (zone, 6.0f);
        const float dashes[] = { 6.0f, 5.0f };
        juce::Path dashed;
        juce::PathStrokeType (2.0f).createDashedStroke (dashed, border, dashes, 2);
        g.setColour (inkSoft);
        g.fillPath (dashed);

        if (mascot.isValid())
        {
            const float mw = 100.0f, mh = mw * (float) mascot.getHeight() / (float) mascot.getWidth();
            g.drawImage (mascot, juce::Rectangle<float> (mw, mh).withCentre ({ 480.0f, 136.0f }), juce::RectanglePlacement::centred);
        }
        const auto missing = proc.getMissingFileName();
        drawText (g, missing.isNotEmpty() ? "DATEI NICHT GEFUNDEN" : "DROP A SAMPLE",
                  { 20.0f, 182.0f, 920.0f, 32.0f }, F.lcd (34.0f), ink, juce::Justification::centred);
        juce::String sub = error.isNotEmpty() ? error
                         : missing.isNotEmpty() ? juce::File (missing).getFileName() + juce::String (juce::CharPointer_UTF8 (" – BITTE NEU LADEN"))
                                                : juce::String (juce::CharPointer_UTF8 ("WAV · AIFF · MP3 · FLAC — WIRD AUTOMATISCH GECHOPPT"));
        drawText (g, sub, { 20.0f, 214.0f, 920.0f, 24.0f }, F.lcd (20.0f), ink.withAlpha (0.75f), juce::Justification::centred);

        const auto cb = chooseButton();
        g.setColour (ink);
        g.drawRoundedRectangle (cb.reduced (0.75f), 16.0f, 1.5f);
        drawText (g, juce::String (juce::CharPointer_UTF8 ("ODER DATEI WÄHLEN")), cb, F.lcd (20.0f), ink, juce::Justification::centred);
        return;
    }

    const auto b = proc.getBounds();
    const int count = juce::jmax (0, (int) b.size() - 1);
    const int sel = juce::jlimit (0, juce::jmax (0, count - 1), proc.selectedChop.load());
    if (count == 0) return;
    const auto view = proc.getView();
    auto xOf = [&] (juce::int64 pos) { return wa.getX() + (float) ((double) (pos - view.first) / (double) view.second) * wa.getWidth(); };

    // selected chop
    const float selX0 = xOf (b[(size_t) sel]), selX1 = xOf (b[(size_t) sel + 1]);
    g.setColour (ink);
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (selX0, wa.getY(), selX1, wa.getBottom()));

    // bars
    const auto& peaks = viewPeaks (*s, view.first, view.second);
    const float bw = wa.getWidth() / (float) numPeaks;
    for (int i = 0; i < (int) peaks.size(); ++i)
    {
        const float x = wa.getX() + (float) i * bw;
        const float cx = x + bw * 0.31f;
        const float h = juce::jmax (2.0f, peaks[(size_t) i] * wa.getHeight() * 0.92f);
        const bool inside = cx >= selX0 && cx < selX1;
        g.setColour (inside ? colours::lcd : ink);
        g.fillRect (x, wa.getCentreY() - h * 0.5f, bw * 0.62f, h);
    }

    // transient markers: small ticks along the top edge, bigger for harder hits
    {
        const auto& hits = s->analysis.onsetsByTime;
        float maxStrength = 0.0f;
        for (auto& o : hits) maxStrength = juce::jmax (maxStrength, o.strength);
        const auto viewEnd = view.first + view.second;
        auto it = std::lower_bound (hits.begin(), hits.end(), view.first, [] (const Onset& o, juce::int64 v) { return o.sample < v; });
        for (int drawn = 0; it != hits.end() && it->sample < viewEnd && drawn < 600; ++it, ++drawn)
        {
            const float x = xOf (it->sample);
            const float h = 3.0f + 5.0f * (maxStrength > 0.0f ? it->strength / maxStrength : 0.0f);
            const bool inSel = x >= selX0 && x < selX1;
            g.setColour (inSel ? colours::lcd : ink);
            juce::Path tri;
            tri.addTriangle (x - h * 0.5f, wa.getY(), x + h * 0.5f, wa.getY(), x, wa.getY() + h);
            g.fillPath (tri);
        }
    }

    // boundaries + numbers
    const float dashes[] = { 3.0f, 3.0f };
    for (int k = 0; k < count; ++k)
    {
        const float x0 = xOf (b[(size_t) k]), x1 = xOf (b[(size_t) k + 1]);
        if (k > 0 || proc.getMode() == ChopMode::bars)
        {
            const bool hot = (k == hoverBound || k == dragBound);
            if (hot)
            {
                g.setColour (colours::accent);
                g.fillRect (x0 - 1.0f, wa.getY() - 2.0f, 3.0f, wa.getHeight() + 4.0f);
            }
            else
            {
                g.setColour (inkSoft);
                g.drawDashedLine ({ x0, wa.getY(), x0, wa.getBottom() }, dashes, 2, 1.0f);
            }
        }
        if (x1 - x0 >= 14.0f)
            drawText (g, juce::String (k + 1).paddedLeft ('0', 2), { x0, 87.0f, x1 - x0, 22.0f }, F.lcd (20.0f),
                      k == sel ? ink : inkSoft, juce::Justification::centred);
    }

    // play head
    if (const auto ph = proc.getPreviewPosition(); ph >= view.first && ph < view.first + view.second)
    {
        g.setColour (colours::accent);
        g.fillRect (xOf (ph) - 1.0f, wa.getY() - 4.0f, 2.5f, wa.getHeight() + 8.0f);
    }

    // overview strip: whole sample, with the zoom window marked
    const auto mm = minimapArea();
    const auto& all = s->analysis.peaks;
    const double total = (double) s->buffer.getNumSamples();
    const float mbw = mm.getWidth() / (float) juce::jmax<size_t> (1, all.size());
    g.setColour (inkSoft);
    for (size_t i = 0; i < all.size(); ++i)
    {
        const float hh = juce::jmax (1.0f, all[i] * mm.getHeight());
        g.fillRect (mm.getX() + (float) i * mbw, mm.getCentreY() - hh * 0.5f, juce::jmax (1.0f, mbw * 0.6f), hh);
    }
    const float wx0 = mm.getX() + (float) ((double) view.first / total) * mm.getWidth();
    const float wx1 = mm.getX() + (float) ((double) (view.first + view.second) / total) * mm.getWidth();
    const auto win = juce::Rectangle<float>::leftTopRightBottom (wx0, mm.getY() - 2.0f, juce::jmax (wx0 + 3.0f, wx1), mm.getBottom() + 2.0f);
    g.setColour (ink.withAlpha (0.18f));
    g.fillRect (win);
    g.setColour (ink);
    g.drawRect (win, 1.0f);

    // status line
    const double zoomFactor = total / (double) juce::jmax<juce::int64> (1, view.second);
    const juce::String zoomText = zoomFactor > 1.05 ? juce::String (juce::CharPointer_UTF8 (" · ZOOM ")) + juce::String (zoomFactor, zoomFactor < 10 ? 1 : 0) + juce::String (juce::CharPointer_UTF8 ("×")) : juce::String();
    const auto mode = proc.getMode();
    static const char* const divNames[] = { "1 TAKT", "1/2 TAKT", "SCHLAG" };
    juce::String left;
    if (error.isNotEmpty())
        left = error;
    else if (proc.isTapping())
        left = u8 ("TAP LÄUFT · PADS IM TAKT TIPPEN · ") + juce::String (proc.getNumChops()) + "/" + juce::String (proc.getChopCount());
    else if (mode == ChopMode::tap)
        left = u8 ("TAP · LEERTASTE = START · ") + juce::String (count) + " CHOPS";
    else if (mode == ChopMode::bars)
        left = u8 ("TAKT · ") + divNames[(int) proc.getDivision()] + u8 (" · ") + juce::String (count) + " CHOPS" + zoomText;
    else
        left = u8 ("TRANSIENTS · ") + juce::String (count) + " CHOPS" + zoomText;
    if (proc.isPreviewing() && ! proc.isTapping()) left = "> " + left;
    drawText (g, left, { 39.0f, 271.0f, 640.0f, 20.0f }, F.lcd (19.0f), ink);

    const double detected = proc.getDetectedBpm();
    const juce::String bpmText = juce::String (proc.getBpm(), 1) + " BPM" + (detected <= 0.0 ? " ?" : "");
    drawText (g, bpmText, { 700.0f, 268.0f, 221.0f, 24.0f }, F.lcd (24.0f), ink, juce::Justification::centredRight);
}

void CatnipView::paintPads (juce::Graphics& g)
{
    auto& F = Fonts::get();
    const int count = proc.getNumChops();
    const int sel = proc.selectedChop.load();
    const auto now = juce::Time::getMillisecondCounter();
    const bool loaded = proc.getSample() != nullptr;

    for (int v = 0; v < 16; ++v)
    {
        const int chop = chopOfVisual (v);
        const bool empty = ! loaded || chop >= count;
        const bool on = loaded && chop == sel && ! empty;
        const bool hit = (flashUntil[(size_t) chop] != 0 && now <= flashUntil[(size_t) chop]) || held[(size_t) chop];

        const auto cell = padCell (v);
        const auto labelColour = empty ? colours::emptyText : (on ? colours::accent : colours::muted);
        drawText (g, "PAD " + juce::String (chop + 1), cell.withHeight (14.0f).withTrimmedLeft (2.0f), F.bold (10.0f, 0.08f), labelColour);
        drawText (g, keyLabelsQwertz[v], cell.withHeight (14.0f).withTrimmedRight (2.0f), F.bold (11.0f), labelColour,
                  juce::Justification::centredRight);

        auto pad = padRect (v);
        g.setColour (colours::padLip);
        g.fillRoundedRectangle (pad.translated (0.0f, 2.0f), 6.0f);

        if (hit) pad = pad.translated (0.0f, 2.0f);
        g.setColour (hit ? colours::padBlack.interpolatedWith (colours::accent, 0.55f) : colours::padBlack);
        g.fillRoundedRectangle (pad, 6.0f);

        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addRoundedRectangle (pad, 6.0f);
        g.reduceClipRegion (clip);
        if (hit)
        {
            juce::ColourGradient inset (juce::Colours::black.withAlpha (0.6f), 0.0f, pad.getY(),
                                        juce::Colours::transparentBlack, 0.0f, pad.getY() + 8.0f, false);
            g.setGradientFill (inset);
            g.fillRect (pad);
        }
        else
        {
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.fillRect (pad.withHeight (2.0f));
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (pad.withTrimmedTop (pad.getHeight() - 4.0f));
        }
        if (on)
        {
            g.setColour (colours::accent);
            g.drawRoundedRectangle (pad.reduced (1.0f), 5.0f, 2.0f);
        }
    }
}

void CatnipView::paintControls (juce::Graphics& g)
{
    auto& F = Fonts::get();
    auto label = [&] (const juce::String& t, float x, float y)
    {
        drawText (g, t, { x, y, 200.0f, 14.0f }, F.bold (11.0f, 0.16f), colours::muted);
    };
    auto segButton = [&] (juce::Rectangle<float> r, const juce::String& t, bool active, float fontSize)
    {
        if (active)
        {
            g.setColour (colours::text);
            g.fillRoundedRectangle (r, 6.0f);
        }
        drawText (g, t, r, F.bold (fontSize, 0.08f), active ? colours::light : colours::muted, juce::Justification::centred);
    };

    label ("CHOP-MODUS", 420.0f, 322.0f);
    g.setColour (colours::segment);
    g.fillRoundedRectangle ({ 420.0f, 340.0f, 250.0f, 42.0f }, 8.0f);
    const auto mode = proc.getMode();
    segButton (modeButton (0), "TRANSIENT", mode == ChopMode::transient, 12.0f);
    segButton (modeButton (1), "TAKT", mode == ChopMode::bars, 12.0f);
    segButton (modeButton (2), "TAP", mode == ChopMode::tap, 12.0f);

    label ("MAX. CHOPS", 690.0f, 322.0f);
    g.setColour (colours::segment);
    g.fillRoundedRectangle ({ 690.0f, 340.0f, 105.0f, 42.0f }, 8.0f);
    segButton (countButton (0), "8", proc.getChopCount() == 8, 13.0f);
    segButton (countButton (1), "16", proc.getChopCount() == 16, 13.0f);

    // second row: only what the chosen mode needs
    const bool loaded = proc.getSample() != nullptr;
    auto smallButton = [&] (juce::Rectangle<float> r, const juce::String& t)
    {
        g.setColour (colours::segment);
        g.fillRoundedRectangle (r, 6.0f);
        drawText (g, t, r, F.bold (13.0f), colours::text, juce::Justification::centred);
    };
    if (mode == ChopMode::bars)
    {
        g.setColour (colours::segment);
        g.fillRoundedRectangle ({ 420.0f, 388.0f, 252.0f, 42.0f }, 8.0f);
        const auto div = proc.getDivision();
        segButton (divisionButton (0), "1 TAKT", div == Division::bar, 12.0f);
        segButton (divisionButton (1), juce::String (juce::CharPointer_UTF8 ("½ TAKT")), div == Division::half, 12.0f);
        segButton (divisionButton (2), "SCHLAG", div == Division::beat, 12.0f);

        smallButton (bpmMinus(), juce::String (juce::CharPointer_UTF8 ("−")));
        smallButton (bpmPlus(), "+");
        smallButton (bpmHalf(), juce::String (juce::CharPointer_UTF8 ("½×")));
        smallButton (bpmDouble(), juce::String (juce::CharPointer_UTF8 ("2×")));
        const auto box = bpmBox();
        g.setColour (colours::dark);
        g.fillRoundedRectangle (box, 6.0f);
        drawText (g, loaded ? juce::String (proc.getBpm(), 1) : juce::String ("--"), box.withTrimmedBottom (12.0f).translated (0.0f, 4.0f),
                  F.lcd (24.0f), colours::lcd, juce::Justification::centred);
        drawText (g, "BPM", box.withTrimmedTop (28.0f), F.bold (8.0f, 0.2f), colours::lcd.withAlpha (0.7f), juce::Justification::centred);
    }
    else if (mode == ChopMode::tap)
    {
        const bool tapping = proc.isTapping();
        const auto tb = tapButton();
        g.setColour (tapping ? colours::accent : colours::text);
        g.fillRoundedRectangle (tb, 8.0f);
        drawText (g, tapping ? juce::String ("STOPP") : juce::String ("TAPPEN STARTEN"), tb.withTrimmedLeft (22.0f), F.bold (12.0f, 0.08f),
                  tapping ? colours::text : colours::light, juce::Justification::centred);
        g.setColour (tapping ? colours::text : colours::accent);
        if (tapping) g.fillRect (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ tb.getX() + 22.0f, tb.getCentreY() }));
        else         g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ tb.getX() + 22.0f, tb.getCentreY() }));

        smallButton (clearButton(), juce::String (juce::CharPointer_UTF8 ("LÖSCHEN")));
        drawText (g, "Leertaste startet das Sample.", { 690.0f, 392.0f, 250.0f, 16.0f }, F.regular (12.0f), colours::muted);
        drawText (g, "Tippe Pads im Takt: jeder Tipp = ein Chop.", { 690.0f, 408.0f, 250.0f, 16.0f }, F.regular (12.0f), colours::muted);
    }
    else
    {
        drawText (g, juce::String (juce::CharPointer_UTF8 ("Chops sitzen auf den stärksten Schlägen (▼ im Display).")),
                  { 420.0f, 392.0f, 520.0f, 16.0f }, F.regular (12.0f), colours::muted);
        drawText (g, juce::String (juce::CharPointer_UTF8 ("Linie ziehen = verschieben, rastet auf Schläge ein · Leertaste = anhören")),
                  { 420.0f, 408.0f, 520.0f, 16.0f }, F.regular (12.0f), colours::muted);
    }

    g.setColour (colours::line);
    g.fillRect (420.0f, 440.0f, 520.0f, 1.0f);

    const int sel = proc.selectedChop.load();
    int vis = 0;
    for (int v = 0; v < 16; ++v) if (chopOfVisual (v) == sel) vis = v;
    const juce::String chopLabel = "CHOP " + juce::String (sel + 1).paddedLeft ('0', 2);
    label (chopLabel, 420.0f, 452.0f);
    const float lw = juce::GlyphArrangement::getStringWidth (F.bold (11.0f, 0.16f), chopLabel);
    const auto badge = juce::Rectangle<float> (430.0f + lw, 447.0f, 24.0f, 24.0f);
    g.setColour (colours::dark);
    g.fillRoundedRectangle (badge, 4.0f);
    drawText (g, keyLabelsQwertz[vis], badge, F.bold (12.0f), colours::light, juce::Justification::centred);

    // knob captions
    const double pv = pitch.getValue();
    const double lv = level.getValue();
    const juce::String values[3] = {
        (pv > 0 ? "+" : "") + juce::String ((int) pv) + " st",
        juce::String (juce::roundToInt (decay.getValue() * 100.0)) + "%",
        lv <= 0.0005 ? juce::String ("-inf dB") : juce::String (juce::Decibels::gainToDecibels (lv), 1) + " dB"
    };
    const char* names[3] = { "PITCH", "DECAY", "LEVEL" };
    for (int i = 0; i < 3; ++i)
    {
        const float cx = 448.0f + (float) i * 84.0f;
        drawText (g, names[i], { cx - 40.0f, 534.0f, 80.0f, 14.0f }, F.bold (11.0f, 0.14f), colours::muted, juce::Justification::centred);
        drawText (g, values[i], { cx - 40.0f, 550.0f, 80.0f, 20.0f }, F.lcd (19.0f), colours::text, juce::Justification::centred);
    }

    auto& c = proc.getChopParams (sel);
    auto toggle = [&] (juce::Rectangle<float> r, const juce::String& t, bool on)
    {
        g.setColour (on ? colours::text : colours::segment);
        g.fillRoundedRectangle (r, 6.0f);
        drawText (g, t, r, F.bold (12.0f, 0.08f), on ? colours::light : colours::text, juce::Justification::centred);
    };
    toggle (reverseButton(), "REVERSE", c.reverse.load());
    toggle (gateButton(), c.gate.load() ? "GATE" : "ONE-SHOT", c.gate.load());

    // zoom wheel caption
    label ("ZOOM", 690.0f, 452.0f);
    {
        juce::String zt = "GANZ";
        if (auto s = proc.getSample())
        {
            const double f = (double) s->buffer.getNumSamples() / (double) juce::jmax<juce::int64> (1, proc.getView().second);
            if (f > 1.05) zt = juce::String (f, f < 10 ? 1 : 0) + juce::String (juce::CharPointer_UTF8 ("×"));
        }
        drawText (g, zt, { 690.0f, 470.0f, 96.0f, 26.0f }, F.lcd (26.0f), colours::text);
        drawText (g, "Drehen = ins", { 690.0f, 504.0f, 96.0f, 14.0f }, F.regular (11.0f), colours::muted);
        drawText (g, "Sample zoomen,", { 690.0f, 518.0f, 96.0f, 14.0f }, F.regular (11.0f), colours::muted);
        drawText (g, "neu choppen", { 690.0f, 532.0f, 96.0f, 14.0f }, F.regular (11.0f), colours::muted);
    }


    const auto eb = exportButton();
    const bool canExport = proc.getSample() != nullptr;
    g.setColour (canExport ? colours::text : colours::text.withAlpha (0.35f));
    g.fillRoundedRectangle (eb, 20.0f);
    juce::Path arrow;
    const float ax = eb.getX() + 18.0f, ay = eb.getCentreY();
    arrow.startNewSubPath (ax, ay); arrow.lineTo (ax + 13.0f, ay);
    arrow.startNewSubPath (ax + 8.0f, ay - 5.0f); arrow.lineTo (ax + 13.0f, ay); arrow.lineTo (ax + 8.0f, ay + 5.0f);
    g.setColour (colours::light);
    g.strokePath (arrow, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    drawText (g, "CHOPS ZUR DAW ZIEHEN", eb.withTrimmedLeft (38.0f).withTrimmedRight (14.0f), F.bold (12.0f, 0.08f), colours::light,
              juce::Justification::centred);
}

void CatnipView::paintOverlay (juce::Graphics& g)
{
    auto& F = Fonts::get();
    const auto r = getLocalBounds().toFloat();
    g.setColour (colours::ink.withAlpha (0.86f));
    g.fillRoundedRectangle (r, 6.0f);

    juce::Path border, dashed;
    border.addRoundedRectangle (r.reduced (6.0f), 6.0f);
    const float dashes[] = { 10.0f, 7.0f };
    juce::PathStrokeType (3.0f).createDashedStroke (dashed, border, dashes, 2);
    g.setColour (colours::lcd);
    g.fillPath (dashed);

    if (mascot.isValid())
    {
        const float mw = 240.0f, mh = mw * (float) mascot.getHeight() / (float) mascot.getWidth();
        g.drawImage (mascot, juce::Rectangle<float> (mw, mh).withCentre ({ 480.0f, 250.0f }), juce::RectanglePlacement::centred);
    }
    drawText (g, "LOSLASSEN ZUM CHOPPEN", { 0.0f, 372.0f, 960.0f, 44.0f }, F.lcd (44.0f), colours::lcd, juce::Justification::centred);
    drawText (g, juce::String (juce::CharPointer_UTF8 ("WAV · AIFF · MP3 · FLAC")), { 0.0f, 418.0f, 960.0f, 26.0f }, F.lcd (22.0f), colours::lcd.withAlpha (0.8f),
              juce::Justification::centred);
}

//==============================================================================
CatnipEditor::CatnipEditor (CatnipProcessor& p)
    : AudioProcessorEditor (p), view (p)
{
    addAndMakeVisible (view);
    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (CatnipView::width * 3 / 4, CatnipView::height * 3 / 4, CatnipView::width * 2, CatnipView::height * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) CatnipView::width / (double) CatnipView::height);
    setSize (CatnipView::width, CatnipView::height);
}

void CatnipEditor::parentHierarchyChanged()
{
    // take the keyboard as soon as the window is on screen, so pad keys work without a click
    juce::Component::SafePointer<CatnipEditor> safe (this);
    juce::MessageManager::callAsync ([safe] { if (safe != nullptr && safe->isShowing()) safe->grabKeyboardFocus(); });
}

void CatnipEditor::resized()
{
    const float scale = (float) getWidth() / (float) CatnipView::width;
    view.setTransform (juce::AffineTransform::scale (scale));
    view.setBounds (0, 0, CatnipView::width, CatnipView::height);
}

bool CatnipEditor::keyPressed (const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown() || key.getModifiers().isAltDown())
        return false;

    if (key == juce::KeyPress::spaceKey)
    {
        view.spacePressed();
        return true;
    }

    auto ch = key.getTextCharacter();
    if (ch == 0) ch = (juce::juce_wchar) key.getKeyCode();
    const int vis = visualForChar (ch);
    if (vis < 0) return false;

    if (heldKeys.count (vis) == 0)   // ignore auto-repeat
    {
        heldKeys[vis] = juce::CharacterFunctions::toLowerCase (ch);
        if (! view.tapKey())             // while tapping, any pad key sets a chop instead of playing one
            view.padDown (chopOfVisual (vis));
    }
    return true;
}

bool CatnipEditor::keyStateChanged (bool)
{
    bool handled = false;
    for (auto it = heldKeys.begin(); it != heldKeys.end();)
    {
        const auto c = it->second;
        const bool down = juce::KeyPress::isKeyCurrentlyDown ((int) c)
                       || juce::KeyPress::isKeyCurrentlyDown ((int) juce::CharacterFunctions::toUpperCase (c))
                       || (it->first == 12 && (juce::KeyPress::isKeyCurrentlyDown ('y') || juce::KeyPress::isKeyCurrentlyDown ('z')
                                               || juce::KeyPress::isKeyCurrentlyDown ('Y') || juce::KeyPress::isKeyCurrentlyDown ('Z')));
        if (! down)
        {
            view.padUp (chopOfVisual (it->first));
            it = heldKeys.erase (it);
            handled = true;
        }
        else
            ++it;
    }
    return handled;
}

bool CatnipEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (isAudioFile (f)) return true;
    return false;
}

void CatnipEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
        if (isAudioFile (f)) { view.loadFile (juce::File (f)); return; }
    view.setDragOver (false);
}
