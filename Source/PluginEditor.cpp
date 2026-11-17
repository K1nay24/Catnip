#include "PluginEditor.h"
#include "BinaryData.h"

using namespace catnip;

namespace
{
    const juce::Colour pink { 0xffFB096B };
    const juce::Colour ink { 0xff1E1D1B };
    const juce::Colour muted { 0xff6E6A64 };
    const juce::Colour lcd { 0xffFF5A9A };
    constexpr float renderScale = 2.0f;   // cached images are drawn at 2x for sharp retina screens

    // visual pad order: top-left first; pad 1 sits bottom-left (MPC numbering)
    int chopOfVisual (int vis) { return (3 - vis / 4) * 4 + vis % 4; }
    const char* keyLabels[16] = { "1", "2", "3", "4", "Q", "W", "E", "R", "A", "S", "D", "F", "Y", "X", "C", "V" };

    int visualForChar (juce::juce_wchar c)
    {
        c = juce::CharacterFunctions::toLowerCase (c);
        if (c == 'z') c = 'y';   // German and English keyboards
        const juce::String keys ("1234qwerasdfyxcv");
        return keys.indexOfChar (c);
    }

    bool isAudioFile (const juce::String& path)
    {
        return juce::File (path).hasFileExtension ("wav;aif;aiff;mp3;flac;ogg;m4a;caf;aac");
    }

    juce::Image loadImage (const void* data, int size)
    {
        return juce::ImageFileFormat::loadFrom (data, (size_t) size);
    }

    /** CSS-style linear gradient: angle in degrees (0 = to top, 90 = to right). */
    juce::ColourGradient cssLinear (juce::Rectangle<float> r, float angleDeg, std::initializer_list<std::pair<float, juce::Colour>> stops)
    {
        const float a = juce::degreesToRadians (angleDeg);
        const juce::Point<float> dir (std::sin (a), -std::cos (a));
        const float len = std::abs (r.getWidth() * dir.x) + std::abs (r.getHeight() * dir.y);
        const auto c = r.getCentre();
        juce::ColourGradient g (stops.begin()->second, c - dir * (len * 0.5f), (stops.end() - 1)->second, c + dir * (len * 0.5f), false);
        for (auto& s : stops)
            if (s.first > 0.0f && s.first < 1.0f) g.addColour (s.first, s.second);
        return g;
    }

    juce::Colour lerp (juce::Colour a, juce::Colour b, float t) { return a.interpolatedWith (b, juce::jlimit (0.0f, 1.0f, t)); }

    /** Reads a grey value 0..1 from a tiled texture. */
    inline float texel (const juce::Image::BitmapData& t, int x, int y)
    {
        x %= t.width; if (x < 0) x += t.width;
        y %= t.height; if (y < 0) y += t.height;
        return t.getPixelColour (x, y).getFloatRed();
    }

    juce::String two (int n) { return juce::String (n).paddedLeft ('0', 2); }
    juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }
}

juce::Font CatnipView::font (juce::Typeface::Ptr tf, float size) const
{
    return juce::Font (juce::FontOptions (tf).withPointHeight (size));
}

//==============================================================================
CatnipView::CatnipView (CatnipProcessor& p) : proc (p)
{
    vt    = juce::Typeface::createSystemTypefaceFor (BinaryData::VT323Regular_ttf, BinaryData::VT323Regular_ttfSize);
    reg   = juce::Typeface::createSystemTypefaceFor (BinaryData::ArchivoRegular_ttf, BinaryData::ArchivoRegular_ttfSize);
    semi  = juce::Typeface::createSystemTypefaceFor (BinaryData::ArchivoSemiBold_ttf, BinaryData::ArchivoSemiBold_ttfSize);
    xbold = juce::Typeface::createSystemTypefaceFor (BinaryData::ArchivoExtraBold_ttf, BinaryData::ArchivoExtraBold_ttfSize);

    logo = loadImage (BinaryData::kinai_png, BinaryData::kinai_pngSize);
    texPlastic = loadImage (BinaryData::tex_plastic_jpg, BinaryData::tex_plastic_jpgSize);
    texPad = loadImage (BinaryData::tex_pad_jpg, BinaryData::tex_pad_jpgSize);
    texKey = loadImage (BinaryData::tex_key_jpg, BinaryData::tex_key_jpgSize);

    // keys: chop row (light) and transport row (dark)
    const int chopX[5] = { 540, 642, 744, 847, 950 };
    keys.push_back ({ Key::reverse, "REVERSE",    0, -1, { chopX[0], 424, 94, 44 } });
    keys.push_back ({ Key::gate,    "GATE",       0, -1, { chopX[1], 424, 94, 44 } });
    keys.push_back ({ Key::onBeat,  "AUF SCHLAG", 0, -1, { chopX[2], 424, 94, 44 } });
    keys.push_back ({ Key::sync,    "DAW SYNC",   0, -1, { chopX[3], 424, 94, 44 } });
    keys.push_back ({ Key::toDaw,   "ZUR DAW",    2,  5, { chopX[4], 424, 94, 44 } });
    const int tX[5] = { 540, 644, 748, 852, 956 };
    keys.push_back ({ Key::play,  "PLAY",  1, 0, { tX[0], 524, 88, 56 } });
    keys.push_back ({ Key::stop,  "STOP",  1, 1, { tX[1], 524, 88, 56 } });
    keys.push_back ({ Key::tap,   "TAP",   2, 2, { tX[2], 524, 88, 56 } });
    keys.push_back ({ Key::undo,  "UNDO",  1, 3, { tX[3], 524, 88, 56 } });
    keys.push_back ({ Key::clear, "CLEAR", 1, 4, { tX[4], 524, 88, 56 } });

    for (int i = 0; i < maxChops; ++i) lastHits[(size_t) i] = proc.hitCounters[(size_t) i].load();

    setOpaque (true);
    setWantsKeyboardFocus (false);
    proc.addChangeListener (this);
    startTimerHz (40);
}

CatnipView::~CatnipView()
{
    proc.removeChangeListener (this);
}

void CatnipView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    repaint();
}

void CatnipView::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    bool padsChanged = false;
    for (int i = 0; i < maxChops; ++i)
    {
        const int h = proc.hitCounters[(size_t) i].load();
        if (h != lastHits[(size_t) i])
        {
            lastHits[(size_t) i] = h;
            flashUntil[(size_t) i] = now + 0.16;
            padsChanged = true;
        }
        if (flashUntil[(size_t) i] > 0.0 && flashUntil[(size_t) i] < now)
        {
            flashUntil[(size_t) i] = 0.0;
            padsChanged = true;
        }
    }
    if (messageUntil > 0.0 && messageUntil < now)
    {
        messageUntil = 0.0;
        message = {};
        repaint (displayRect());
    }

    // cheap fingerprint of everything that can change without a change message (host automation, audio thread)
    juce::String sig;
    sig << proc.getPitchSemitones() << "|" << proc.apvts.getRawParameterValue ("snap")->load() << "|"
        << proc.apvts.getRawParameterValue ("volume")->load() << "|" << (int) proc.isSynced() << "|"
        << proc.getHostBpm() << "|" << (int) proc.isTapping() << "|" << (int) proc.isPreviewing() << "|"
        << proc.selectedChop.load();
    if (padsChanged || sig != lastSig)
    {
        lastSig = sig;
        repaint();
    }
    else if (proc.isPreviewing())
    {
        repaint (displayRect());
    }
}

//==============================================================================
// layout

juce::Rectangle<int> CatnipView::padCell (int visual)
{
    const int col = visual % 4, row = visual / 4;
    return { 36 + col * (104 + 12), 88 + row * (124 + 12), 104, 124 };
}

juce::Rectangle<int> CatnipView::padFace (int visual)
{
    auto well = padCell (visual).withTrimmedTop (20);
    return { well.getX() + 3, well.getY() + 3, well.getWidth() - 7, well.getHeight() - 8 };
}

juce::Rectangle<float> CatnipView::knobCentreArea (int knob)
{
    return { 540.0f + 126.0f * (float) knob, 236.0f, 126.0f, 150.0f };
}

int CatnipView::padAt (juce::Point<int> p) const
{
    for (int v = 0; v < 16; ++v)
        if (padCell (v).withTrimmedTop (20).contains (p)) return v;
    return -1;
}

int CatnipView::knobAt (juce::Point<int> p) const
{
    for (int k = 0; k < 4; ++k)
    {
        const auto a = knobCentreArea (k);
        const juce::Point<float> c (a.getCentreX(), a.getY() + 60.0f);
        if (c.getDistanceFrom (p.toFloat()) <= 40.0f) return k;
    }
    return -1;
}

const CatnipView::KeyInfo* CatnipView::keyAt (juce::Point<int> p) const
{
    for (auto& k : keys)
        if (k.face.expanded (0, 4).contains (p)) return &k;
    return nullptr;
}

//==============================================================================
// state helpers

float CatnipView::knobValue (int knob) const
{
    switch ((Knob) knob)
    {
        case Knob::pitch:  return (proc.getPitchSemitones() + 12.0f) / 24.0f;
        case Knob::snap:   return proc.apvts.getRawParameterValue ("snap")->load();
        case Knob::zoom:   return currentZoom();
        case Knob::volume: return proc.apvts.getParameter ("volume")->getValue();
    }
    return 0.0f;
}

void CatnipView::setKnobValue (int knob, float v, bool fine)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    switch ((Knob) knob)
    {
        case Knob::pitch:
        {
            float st = v * 24.0f - 12.0f;
            if (! fine) st = std::round (st);               // semitones; hold SHIFT for cents
            auto* p = proc.apvts.getParameter ("pitch");
            p->setValueNotifyingHost (p->convertTo0to1 (st));
            break;
        }
        case Knob::snap:   proc.apvts.getParameter ("snap")->setValueNotifyingHost (v); break;
        case Knob::volume: proc.apvts.getParameter ("volume")->setValueNotifyingHost (v); break;
        case Knob::zoom:   setZoom (v); break;
    }
    repaint();
}

juce::String CatnipView::knobText (int knob) const
{
    switch ((Knob) knob)
    {
        case Knob::pitch:
        {
            const float st = proc.getPitchSemitones();
            const bool whole = std::abs (st - std::round (st)) < 0.005f;
            juce::String s = (st > 0.004f ? "+" : "") + (whole ? juce::String ((int) std::round (st)) : juce::String (st, 2)) + " st";
            if (proc.isSynced()) return s + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 nur Tonh\xc3\xb6he"));
            const double bpm = proc.getPlaybackBpm();
            return bpm > 0.0 ? s + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + juce::String (bpm, 1) + " BPM" : s;
        }
        case Knob::snap:   return juce::String (juce::roundToInt (proc.apvts.getRawParameterValue ("snap")->load() * 100.0f)) + " % zum Schlag";
        case Knob::zoom:
        {
            const float z = 1.0f + currentZoom() * 7.0f;
            return z < 1.05f ? juce::String ("ganzes Sample") : juce::String (z, 1) + juce::String (juce::CharPointer_UTF8 ("\xc3\x97"));
        }
        case Knob::volume: return juce::String (proc.apvts.getRawParameterValue ("volume")->load(), 1) + " dB";
    }
    return {};
}

float CatnipView::currentZoom() const
{
    auto s = proc.getSample();
    if (s == nullptr) return 0.0f;
    const auto v = proc.getView();
    if (v.second <= 0) return 0.0f;
    return juce::jlimit (0.0f, 1.0f, (float) (((double) s->buffer.getNumSamples() / (double) v.second - 1.0) / 7.0));
}

void CatnipView::setZoom (float z)
{
    auto s = proc.getSample();
    if (s == nullptr) return;
    const juce::int64 len = s->buffer.getNumSamples();
    const auto view = proc.getView();
    const auto newLen = (juce::int64) std::llround ((double) len / (1.0 + 7.0 * juce::jlimit (0.0f, 1.0f, z)));

    // zoom around the play head, else the selected chop, else the middle of what is shown
    juce::int64 centre = view.first + view.second / 2;
    if (proc.getPreviewPosition() >= 0)
        centre = proc.getPreviewPosition();
    else
    {
        const auto b = proc.getBounds();
        const int sel = proc.selectedChop.load();
        if (sel + 1 < (int) b.size())
            centre = (b[(size_t) sel] + b[(size_t) sel + 1]) / 2;
    }
    proc.setView (centre - newLen / 2, newLen);
}

juce::String CatnipView::bpmLine() const
{
    const double host = proc.getHostBpm();
    const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    if (proc.isSynced())
        return host > 0.0 ? "DAW " + juce::String (host, 1) + dot + juce::String (juce::CharPointer_UTF8 ("SYNC \xe2\x9c\x93"))
                          : juce::String ("SYNC") + dot + "KEIN DAW-TEMPO";
    const double bpm = proc.getPlaybackBpm();
    juce::String s = bpm > 0.0 ? "BPM " + juce::String (bpm, 1) : juce::String ("BPM ?");
    if (host > 0.0) s << dot << "DAW " << juce::String (host, 1);
    return s;
}

bool CatnipView::keyActive (Key k) const
{
    const int sel = proc.selectedChop.load();
    const bool has = sel < proc.getNumChops();
    switch (k)
    {
        case Key::reverse: return has && proc.getChopFlags (sel).reverse.load();
        case Key::gate:    return has && proc.getChopFlags (sel).gate.load();
        case Key::onBeat:  return proc.apvts.getRawParameterValue ("snap")->load() >= 0.999f;
        case Key::sync:    return proc.isSynced();
        case Key::play:    return proc.isPreviewing() && ! proc.isTapping();
        case Key::tap:     return proc.isTapping();
        case Key::toDaw: case Key::stop: case Key::undo: case Key::clear: case Key::count: return false;
    }
    return false;
}


void CatnipView::pressKey (Key k)
{
    const int sel = proc.selectedChop.load();
    switch (k)
    {
        case Key::reverse:
            if (sel < proc.getNumChops()) proc.getChopFlags (sel).reverse = ! proc.getChopFlags (sel).reverse.load();
            break;
        case Key::gate:
            if (sel < proc.getNumChops()) proc.getChopFlags (sel).gate = ! proc.getChopFlags (sel).gate.load();
            break;
        case Key::onBeat: proc.snapAllToHits(); break;
        case Key::sync:
        {
            auto* p = proc.apvts.getParameter ("sync");
            p->beginChangeGesture();
            p->setValueNotifyingHost (proc.isSynced() ? 0.0f : 1.0f);
            p->endChangeGesture();
            if (proc.isSynced() && proc.getHostBpm() <= 0.0)
                showMessage (u8 ("KEIN DAW-TEMPO: NUR TONHÖHE"));
            break;
        }
        case Key::toDaw: break;   // handled on mouse up / drag
        case Key::play:
            if (proc.getSample() == nullptr) { openFileChooser(); break; }
            if (proc.isPreviewing() && ! proc.isTapping()) proc.stopAll(); else proc.play();
            break;
        case Key::stop:  stopEverything(); break;
        case Key::tap:   toggleTap(); break;
        case Key::undo:  undo(); break;
        case Key::clear: proc.clearTaps(); break;
        case Key::count: break;
    }
    repaint();
}

void CatnipView::stopEverything()
{
    proc.stopAll();
    for (int i = 0; i < maxChops; ++i)
        if (held[(size_t) i]) padUp (i);
    proc.keyboardState.allNotesOff (1);
    repaint();
}

void CatnipView::undo()
{
    if (! proc.undo()) showMessage (u8 ("NICHTS ZUM RÜCKGÄNGIG MACHEN"));
}

void CatnipView::showMessage (const juce::String& m)
{
    message = m;
    messageUntil = juce::Time::getMillisecondCounterHiRes() * 0.001 + 2.6;
    repaint (displayRect());
}

void CatnipView::toggleTap()
{
    if (proc.getSample() == nullptr) { openFileChooser(); return; }
    if (proc.isTapping()) proc.stopTapping();
    else proc.startTapping();
    repaint();
}

bool CatnipView::tapKey()
{
    if (! proc.isTapping()) return false;
    proc.tap();
    return true;
}

void CatnipView::padDown (int chop)
{
    if (tapKey()) return;
    if (chop < proc.getNumChops()) proc.selectedChop = chop;
    held[(size_t) chop] = true;
    proc.keyboardState.noteOn (1, firstNote + chop, 0.9f);
    repaint();
}

void CatnipView::padUp (int chop)
{
    if (! held[(size_t) chop]) return;
    held[(size_t) chop] = false;
    proc.keyboardState.noteOff (1, firstNote + chop, 0.0f);
    repaint();
}

void CatnipView::loadFile (const juce::File& f)
{
    juce::String err;
    if (! proc.loadFile (f, err)) showMessage (err);
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

//==============================================================================
// display coordinates

float CatnipView::xForSample (juce::int64 s) const
{
    const auto v = proc.getView();
    const auto w = waveRect().toFloat();
    if (v.second <= 0) return w.getX();
    return w.getX() + (float) ((double) (s - v.first) / (double) v.second) * w.getWidth();
}

juce::int64 CatnipView::sampleAtX (float x) const
{
    const auto v = proc.getView();
    const auto w = waveRect().toFloat();
    const double t = juce::jlimit (0.0, 1.0, (double) ((x - w.getX()) / w.getWidth()));
    return v.first + (juce::int64) std::llround (t * (double) v.second);
}

int CatnipView::chopLineAt (float x) const
{
    const auto b = proc.getBounds();
    int best = -1;
    float dist = 7.0f;
    for (int i = 0; i + 1 < (int) b.size(); ++i)
    {
        const float d = std::abs (xForSample (b[(size_t) i]) - x);
        if (d < dist) { dist = d; best = i; }
    }
    return best;
}

const std::vector<std::pair<float, float>>& CatnipView::viewPeaks (const SampleData& s, juce::int64 start, juce::int64 length)
{
    if (&s == peaksSample && start == peaksStart && length == peaksLength) return peaks;
    peaksSample = &s; peaksStart = start; peaksLength = length;

    constexpr int bars = 150;
    peaks.assign (bars, { 0.0f, 0.0f });
    const auto& buf = s.buffer;
    const float norm = std::max (1.0e-4f, buf.getMagnitude (0, buf.getNumSamples()));
    for (int i = 0; i < bars; ++i)
    {
        const auto a = start + length * i / bars;
        const auto e = std::max (a + 1, start + length * (i + 1) / bars);
        const int step = (int) std::max<juce::int64> (1, (e - a) / 400);   // sparse read for long ranges
        float lo = 0.0f, hi = 0.0f;
        for (int c = 0; c < buf.getNumChannels(); ++c)
        {
            const float* d = buf.getReadPointer (c);
            for (auto k = a; k < e && k < buf.getNumSamples(); k += step)
            {
                lo = std::min (lo, d[k]);
                hi = std::max (hi, d[k]);
            }
        }
        peaks[(size_t) i] = { lo / norm, hi / norm };
    }
    return peaks;
}

//==============================================================================
// cached faces with texture

const juce::Image& CatnipView::faceImage (const juce::String& id, int w, int h,
                                          std::function<void (juce::Graphics&, juce::Rectangle<float>)> fill,
                                          const juce::Image& texture, juce::Point<int> offset, bool overlay, float opacity, float radius)
{
    if (auto it = faceCache.find (id); it != faceCache.end()) return it->second;

    const int W = (int) std::ceil ((float) w * renderScale), H = (int) std::ceil ((float) h * renderScale);
    juce::Image img (juce::Image::ARGB, W, H, true);
    {
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (renderScale));
        juce::Path clip;
        clip.addRoundedRectangle (0.0f, 0.0f, (float) w, (float) h, radius);
        g.reduceClipRegion (clip);
        fill (g, { 0.0f, 0.0f, (float) w, (float) h });
    }

    if (texture.isValid() && opacity > 0.0f)
    {
        juce::Image::BitmapData t (texture, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData d (img, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
            {
                const auto c = d.getPixelColour (x, y);
                if (c.getAlpha() == 0) continue;
                const float tv = texel (t, x + offset.x * 2, y + offset.y * 2);
                auto blend = [&] (float base)
                {
                    const float b = overlay ? (base < 0.5f ? 2.0f * base * tv : 1.0f - 2.0f * (1.0f - base) * (1.0f - tv))
                                            : base * tv;
                    return base + (b - base) * opacity;
                };
                d.setPixelColour (x, y, juce::Colour::fromFloatRGBA (blend (c.getFloatRed()), blend (c.getFloatGreen()),
                                                                     blend (c.getFloatBlue()), c.getFloatAlpha()));
            }
    }
    return faceCache[id] = img;
}

//==============================================================================
// painting

void CatnipView::paint (juce::Graphics& g)
{
    if (! background.isValid()) paintPanel (g);
    g.drawImage (background, getLocalBounds().toFloat());

    for (int v = 0; v < 16; ++v) paintPad (g, v);
    for (int k = 0; k < 4; ++k) paintKnob (g, k);
    for (auto& k : keys) paintKey (g, k);
    paintDisplay (g);

    // status LED
    const bool tapping = proc.isTapping();
    const juce::Rectangle<float> led (1020.0f, 38.0f, 13.0f, 13.0f);
    if (tapping)
    {
        g.setColour (juce::Colour (0xffFF4A3D).withAlpha (0.35f));
        g.fillEllipse (led.expanded (5.0f));
    }
    g.setColour (tapping ? juce::Colour (0xffFF4A3D) : pink);
    g.fillEllipse (led);
    g.setColour (juce::Colours::black.withAlpha (tapping ? 0.0f : 0.35f));
    g.drawEllipse (led.reduced (0.5f), 1.0f);
}

void CatnipView::paintPanel (juce::Graphics&)
{
    const int W = (int) (width * renderScale), H = (int) (height * renderScale);
    background = juce::Image (juce::Image::RGB, W, H, false);

    // warm grey plastic: gradient + soft highlight, multiplied with the plastic texture, dark edges
    {
        juce::Image::BitmapData d (background, juce::Image::BitmapData::writeOnly);
        juce::Image::BitmapData t (texPlastic, juce::Image::BitmapData::readOnly);
        const juce::Colour top (0xffD8D2C8), bottom (0xffC7C1B6);
        for (int y = 0; y < H; ++y)
        {
            const float fy = (float) y / (float) H;
            const auto base = lerp (top, bottom, fy);
            for (int x = 0; x < W; ++x)
            {
                const float fx = (float) x / (float) W;
                const float dx = (fx - 0.30f) / 0.95f, dy = (fy - 0.18f) / 1.15f;
                const float hl = 0.28f * juce::jlimit (0.0f, 1.0f, 1.0f - std::sqrt (dx * dx + dy * dy) / 0.62f);
                float r = base.getFloatRed(), gg = base.getFloatGreen(), b = base.getFloatBlue();
                r += (1.0f - r) * hl; gg += (1.0f - gg) * hl; b += (1.0f - b) * hl;
                const float tv = texel (t, x, y);
                r *= tv; gg *= tv; b *= tv;
                const float e = (float) std::min ({ x, y, W - 1 - x, H - 1 - y }) / renderScale;
                if (e < 60.0f)
                {
                    const float a = 0.22f * std::pow (1.0f - e / 60.0f, 2.0f);
                    r += (70.0f / 255.0f - r) * a; gg += (55.0f / 255.0f - gg) * a; b += (35.0f / 255.0f - b) * a;
                }
                d.setPixelColour (x, y, juce::Colour::fromFloatRGBA (r, gg, b, 1.0f));
            }
        }
    }

    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (renderScale));

    // fine scratches and screws
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    for (auto l : { juce::Line<float> (612, 610, 670, 588), { 880, 92, 840, 122 }, { 140, 650, 210, 642 }, { 960, 640, 988, 600 }, { 520, 30, 564, 36 } })
        g.drawLine (l, 0.7f);
    g.setColour (juce::Colour (0xff3B342C).withAlpha (0.18f));
    for (auto l : { juce::Line<float> (700, 470, 790, 456), { 300, 668, 240, 678 }, { 1010, 330, 980, 376 }, { 60, 40, 100, 58 } })
        g.drawLine (l, 0.7f);
    for (auto c : { juce::Point<float> (24, 24), { 1056, 24 }, { 24, 676 }, { 1056, 676 } })
    {
        g.setColour (juce::Colour (0xffABA498)); g.fillEllipse (juce::Rectangle<float> (10, 10).withCentre (c));
        g.setColour (juce::Colour (0xff8E877B)); g.drawEllipse (juce::Rectangle<float> (10, 10).withCentre (c), 1.0f);
        g.drawLine (c.x - 3, c.y - 3, c.x + 3, c.y + 3, 1.0f);
    }

    // logo
    if (logo.isValid())
    {
        const float lw = 82.0f, lh = lw * (float) logo.getHeight() / (float) logo.getWidth();
        const juce::Rectangle<float> lr (28.0f, 8.0f, lw, lh);
        juce::Image shadowSrc (juce::Image::ARGB, (int) lw + 20, (int) lh + 20, true);
        {
            juce::Graphics sg (shadowSrc);
            sg.drawImage (logo, juce::Rectangle<float> (10.0f, 10.0f, lw, lh));
        }
        juce::Graphics::ScopedSaveState s (g);
        {
            juce::Graphics::ScopedSaveState s2 (g);
            g.setOrigin ({ 18, -2 });
            juce::DropShadow (juce::Colours::black.withAlpha (0.25f), 3, { 0, 2 }).drawForImage (g, shadowSrc);
        }
        g.setOpacity (1.0f);
        g.drawImage (logo, lr);
    }

    // pink rules
    g.setColour (pink);
    g.fillRect (124.0f, 44.0f, 876.0f, 2.0f);
    g.fillRect (512.0f, 84.0f, 2.0f, 536.0f);
    g.fillRect (36.0f, 636.0f, 1008.0f, 2.0f);

    // groove between key rows
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillRect (540.0f, 486.0f, 504.0f, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.fillRect (540.0f, 488.0f, 504.0f, 1.0f);

    // footer
    {
        g.setFont (font (xbold, 13.0f).withExtraKerningFactor (0.06f));
        float x = 38.0f;
        const char* parts[] = { "KINAI \xc2\xb7 TAP SAMPLER", "16 PADS", "SNAP TO TRANSIENT" };
        for (int i = 0; i < 3; ++i)
        {
            const auto t = juce::String::fromUTF8 (parts[i]);
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), t);
            g.setColour (ink);
            g.drawText (t, juce::Rectangle<float> (x, 652.0f, w + 4.0f, 16.0f), juce::Justification::centredLeft, false);
            x += w + 12.0f;
            if (i < 2)
            {
                g.setColour (pink);
                g.fillEllipse (x, 657.5f, 5.0f, 5.0f);
                x += 17.0f;
            }
        }
        g.setColour (pink);
        for (int i = 0; i < 4; ++i)
        {
            const float x0 = 1044.0f - 10.0f - (float) (3 - i) * 17.0f - 6.0f;
            juce::Path p;
            p.startNewSubPath (x0 + 6.4f, 648.0f);
            p.lineTo (x0 + 16.4f, 648.0f);
            p.lineTo (x0 + 3.7f, 670.0f);
            p.lineTo (x0 - 6.3f, 670.0f);
            p.closeSubPath();
            g.fillPath (p);
        }
    }

    // knob ticks, side marks and labels
    for (int k = 0; k < 4; ++k)
    {
        const auto a = knobCentreArea (k);
        const juce::Point<float> c (a.getCentreX(), a.getY() + 60.0f);
        g.setColour (juce::Colour (0xff2B2826));
        for (int i = 0; i < 11; ++i)
        {
            const float ang = juce::degreesToRadians (-135.0f + 27.0f * (float) i);
            const float r1 = 49.0f, r0 = r1 - (i % 5 == 0 ? 9.0f : 6.0f);
            const juce::Point<float> d (std::sin (ang), -std::cos (ang));
            g.drawLine ({ c + d * r0, c + d * r1 }, 2.0f);
        }
        g.setColour (pink);
        g.fillRect (a.getX() + 6.0f, a.getY() + 59.0f, 14.0f, 2.0f);
        g.fillRect (a.getRight() - 20.0f, a.getY() + 59.0f, 14.0f, 2.0f);
        g.setColour (ink);
        g.setFont (font (xbold, 14.0f).withExtraKerningFactor (0.04f));
        const char* names[] = { "PITCH", "SNAP", "ZOOM", "VOLUME" };
        g.drawText (names[k], juce::Rectangle<float> (a.getX(), a.getY() + 110.0f, a.getWidth(), 18.0f), juce::Justification::centred, false);
    }

    // key labels
    g.setFont (font (xbold, 13.0f).withExtraKerningFactor (0.04f));
    g.setColour (ink);
    for (auto& k : keys)
        g.drawText (juce::String (juce::CharPointer_UTF8 (k.label)),
                    juce::Rectangle<int> (k.face.getX() - 6, k.face.getY() - 24, k.face.getWidth() + 12, 16),
                    juce::Justification::centred, false);

    // display bezel with pink spill
    {
        const auto r = displayRect().toFloat();
        juce::Path bez;
        bez.addRoundedRectangle (r, 6.0f);
        juce::DropShadow (pink.withAlpha (0.10f), 60, {}).drawForPath (g, bez);
        juce::DropShadow (pink.withAlpha (0.22f), 22, {}).drawForPath (g, bez);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.fillRoundedRectangle (r.translated (0.0f, 1.0f), 6.0f);
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillRoundedRectangle (r.translated (0.0f, -1.0f), 6.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2B2826), 0.0f, r.getY(), juce::Colour (0xff1A1817), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 4.0f, r.getRight() - 4.0f);
    }

    // knob faces (static: the light does not turn with the knob)
    {
        const float R = 42.0f * 0.82f;
        const int S = (int) std::ceil (R * 2.0f * renderScale) + 4;
        auto conic = [] (juce::Graphics& gg, juce::Point<float> c, float r, float fromDeg, std::initializer_list<std::pair<float, juce::Colour>> stops)
        {
            std::vector<std::pair<float, juce::Colour>> st (stops);
            for (int deg = 0; deg < 360; ++deg)
            {
                const float a = (float) deg + 0.5f;
                juce::Colour col = st.front().second;
                for (size_t i = 0; i + 1 < st.size(); ++i)
                    if (a >= st[i].first && a <= st[i + 1].first)
                        col = lerp (st[i].second, st[i + 1].second, (a - st[i].first) / (st[i + 1].first - st[i].first));
                juce::Path wedge;
                const float a0 = juce::degreesToRadians (fromDeg + (float) deg - 0.6f), a1 = juce::degreesToRadians (fromDeg + (float) deg + 1.6f);
                wedge.startNewSubPath (c);
                wedge.addCentredArc (c.x, c.y, r, r, 0.0f, a0, a1, false);
                wedge.closeSubPath();
                gg.setColour (col);
                gg.fillPath (wedge);
            }
            // brushed rings
            for (float rr = 1.0f; rr < r; rr += 1.5f)
            {
                gg.setColour (((int) (rr * 2.0f) % 2 == 0) ? juce::Colours::white.withAlpha (0.025f) : juce::Colours::black.withAlpha (0.10f));
                gg.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c), 0.5f);
            }
        };

        knobFace = juce::Image (juce::Image::ARGB, S, S, true);
        {
            juce::Graphics kg (knobFace);
            kg.addTransform (juce::AffineTransform::scale (renderScale));
            const juce::Point<float> c ((float) S / renderScale * 0.5f, (float) S / renderScale * 0.5f);
            // outer skirt
            kg.setGradientFill (juce::ColourGradient (juce::Colour (0xff262626), c.x, c.y - R, juce::Colour (0xff151515), c.x, c.y + R, false));
            kg.fillEllipse (juce::Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c));
            kg.setColour (juce::Colour (0xff0C0C0C));
            kg.drawEllipse (juce::Rectangle<float> (R * 2.0f - 1.0f, R * 2.0f - 1.0f).withCentre (c), 1.0f);
            kg.setColour (juce::Colours::white.withAlpha (0.10f));
            kg.drawEllipse (juce::Rectangle<float> (R * 2.0f - 2.0f, R * 2.0f - 2.0f).withCentre (c.translated (0.0f, 0.5f)), 0.6f);
            // brushed face
            const float fr = (42.0f - 5.0f) * 0.82f;
            kg.setColour (juce::Colour (0xff050505));
            kg.fillEllipse (juce::Rectangle<float> (fr * 2.0f + 2.5f, fr * 2.0f + 2.5f).withCentre (c));
            {
                juce::Graphics::ScopedSaveState ss (kg);
                juce::Path clip; clip.addEllipse (juce::Rectangle<float> (fr * 2.0f, fr * 2.0f).withCentre (c));
                kg.reduceClipRegion (clip);
                conic (kg, c, fr + 1.0f, 0.0f, { { 0, juce::Colour (0xff1B1B1B) }, { 40, juce::Colour (0xff1E1E1E) }, { 80, juce::Colour (0xff2A2A2A) },
                                                 { 90, juce::Colour (0xff3C3C3C) }, { 102, juce::Colour (0xff262626) }, { 125, juce::Colour (0xff1A1A1A) },
                                                 { 145, juce::Colour (0xff2C2C2C) }, { 155, juce::Colour (0xff4A4A4A) }, { 168, juce::Colour (0xff2A2A2A) },
                                                 { 200, juce::Colour (0xff181818) }, { 250, juce::Colour (0xff151515) }, { 285, juce::Colour (0xff1C1C1C) },
                                                 { 305, juce::Colour (0xff303030) }, { 316, juce::Colour (0xff4C4C4C) }, { 330, juce::Colour (0xff2B2B2B) },
                                                 { 360, juce::Colour (0xff1B1B1B) } });
            }
            kg.setColour (juce::Colours::white.withAlpha (0.06f));
            kg.drawEllipse (juce::Rectangle<float> (fr * 2.0f - 1.0f, fr * 2.0f - 1.0f).withCentre (c), 1.0f);
        }

        knobCap = juce::Image (juce::Image::ARGB, S, S, true);
        {
            juce::Graphics kg (knobCap);
            kg.addTransform (juce::AffineTransform::scale (renderScale));
            const juce::Point<float> c ((float) S / renderScale * 0.5f, (float) S / renderScale * 0.5f);
            const float cr = 13.0f * 0.82f;
            kg.setColour (juce::Colours::white.withAlpha (0.28f));
            kg.fillEllipse (juce::Rectangle<float> (cr * 2.0f + 3.0f, cr * 2.0f + 3.0f).withCentre (c.translated (0.0f, 1.0f)));
            kg.setColour (juce::Colours::black.withAlpha (0.6f));
            kg.fillEllipse (juce::Rectangle<float> (cr * 2.0f + 3.0f, cr * 2.0f + 3.0f).withCentre (c.translated (0.0f, -1.0f)));
            kg.setColour (juce::Colour (0xff050505));
            kg.fillEllipse (juce::Rectangle<float> (cr * 2.0f + 3.0f, cr * 2.0f + 3.0f).withCentre (c));
            juce::Graphics::ScopedSaveState ss (kg);
            juce::Path clip; clip.addEllipse (juce::Rectangle<float> (cr * 2.0f, cr * 2.0f).withCentre (c));
            kg.reduceClipRegion (clip);
            conic (kg, c, cr + 1.0f, 0.0f, { { 0, juce::Colour (0xff181818) }, { 60, juce::Colour (0xff2C2C2C) }, { 110, juce::Colour (0xff1A1A1A) },
                                             { 150, juce::Colour (0xff383838) }, { 200, juce::Colour (0xff161616) }, { 260, juce::Colour (0xff121212) },
                                             { 315, juce::Colour (0xff333333) }, { 360, juce::Colour (0xff181818) } });
            kg.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.4f), c.x, c.y - cr, juce::Colours::transparentBlack, c.x, c.y - cr + 3.0f, false));
            kg.fillEllipse (juce::Rectangle<float> (cr * 2.0f, cr * 2.0f).withCentre (c));
        }
    }
}

void CatnipView::paintPad (juce::Graphics& g, int vis)
{
    const int chop = chopOfVisual (vis);
    const int n = proc.getNumChops();
    const int sel = proc.selectedChop.load();
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const bool hit = held[(size_t) chop] || flashUntil[(size_t) chop] > now;
    const bool empty = chop >= n, selected = ! empty && chop == sel;

    const auto cell = padCell (vis);
    // label row
    g.setFont (font (xbold, 12.0f).withExtraKerningFactor (0.02f));
    g.setColour (selected || hit ? pink : (empty ? muted : ink));
    g.drawText ("PAD " + juce::String (chop + 1), cell.getX() + 2, cell.getY(), 70, 16, juce::Justification::bottomLeft, false);
    g.setFont (font (semi, 10.0f).withExtraKerningFactor (0.1f));
    g.setColour (selected || hit ? pink : muted);
    g.drawText (keyLabels[vis], cell.getRight() - 32, cell.getY(), 30, 16, juce::Justification::bottomRight, false);

    // well
    const auto well = cell.withTrimmedTop (20).toFloat();
    if (selected || hit)
    {
        g.setColour (pink);
        g.fillRoundedRectangle (well.expanded (2.0f), 7.0f);
    }
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.fillRoundedRectangle (well.translated (0.0f, 1.0f), 5.0f);
    g.setColour (juce::Colour (0xff1B1B19));
    g.fillRoundedRectangle (well, 5.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.7f), 0.0f, well.getY(), juce::Colours::transparentBlack, 0.0f, well.getY() + 4.0f, false));
    g.fillRoundedRectangle (well, 5.0f);

    // rubber face
    auto face = padFace (vis).toFloat();
    if (! hit)
    {
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (face.translated (0.0f, 2.0f), 3.0f);
    }
    face = face.translated (0.0f, hit ? 2.0f : 0.0f);

    const int look = hit ? 2 : (empty ? 3 : 1);
    const juce::String id = "pad" + juce::String (look) + "_" + juce::String (chop);
    const juce::Point<int> off ((chop + 1) * 47 % 200, (chop + 1) * 83 % 300);
    const auto& img = faceImage (id, (int) face.getWidth(), (int) face.getHeight(), [look] (juce::Graphics& fg, juce::Rectangle<float> r)
    {
        if (look == 2)
            fg.setGradientFill (cssLinear (r, 160.0f, { { 0.0f, juce::Colour (0xff6E2444) }, { 0.7f, juce::Colour (0xff4E1A31) }, { 1.0f, juce::Colour (0xff4E1A31) } }));
        else if (look == 3)
            fg.setGradientFill (cssLinear (r, 160.0f, { { 0.0f, juce::Colour (0xff4C4946) }, { 0.7f, juce::Colour (0xff403D3A) }, { 1.0f, juce::Colour (0xff403D3A) } }));
        else
            fg.setGradientFill (cssLinear (r, 160.0f, { { 0.0f, juce::Colour (0xff57534F) }, { 0.55f, juce::Colour (0xff44413D) }, { 1.0f, juce::Colour (0xff3A3734) } }));
        fg.fillRect (r);
        if (look == 2)
        {
            fg.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, juce::Colours::transparentBlack, 0.0f, 9.0f, false));
            fg.fillRect (r);
        }
        else
        {
            fg.setColour (juce::Colours::white.withAlpha (0.14f));
            fg.fillRect (r.getX(), r.getY(), r.getWidth(), 1.0f);
            fg.fillRect (r.getX(), r.getY(), 1.0f, r.getHeight());
            fg.setColour (juce::Colours::black.withAlpha (0.38f));
            fg.fillRect (r.getX(), r.getBottom() - 3.0f, r.getWidth(), 3.0f);
            fg.fillRect (r.getRight() - 2.0f, r.getY(), 2.0f, r.getHeight());
        }
    }, texPad, off, true, 0.6f, 3.0f);
    g.setOpacity (1.0f);
    g.drawImage (img, face);
}

void CatnipView::paintKnob (juce::Graphics& g, int k)
{
    const auto a = knobCentreArea (k);
    const juce::Point<float> c (a.getCentreX(), a.getY() + 60.0f);
    const float R = 42.0f * 0.82f;

    juce::Path disc;
    disc.addEllipse (juce::Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c));
    juce::DropShadow (juce::Colours::black.withAlpha (0.4f), 8, { 0, 5 }).drawForPath (g, disc);

    const auto imgRect = juce::Rectangle<float> ((float) knobFace.getWidth() / renderScale, (float) knobFace.getHeight() / renderScale).withCentre (c);
    g.setOpacity (1.0f);
    g.drawImage (knobFace, imgRect);

    const float ang = juce::degreesToRadians (-135.0f + 270.0f * knobValue (k));
    const juce::Point<float> d (std::sin (ang), -std::cos (ang));
    g.setColour (juce::Colour (0xffF4F4F4));
    g.drawLine ({ c + d * (11.5f), c + d * ((42.0f - 5.5f) * 0.82f) }, 2.0f * 0.82f + 0.3f);
    g.setOpacity (1.0f);
    g.drawImage (knobCap, imgRect);

    g.setColour (muted);
    g.setFont (font (semi, 11.0f));
    g.drawText (knobText (k), juce::Rectangle<float> (a.getX() - 10.0f, a.getY() + 129.0f, a.getWidth() + 20.0f, 16.0f), juce::Justification::centred, false);
}

void CatnipView::paintKey (juce::Graphics& g, const KeyInfo& k)
{
    const bool active = keyActive (k.id);
    const bool down = pressedKey == &k;
    const int variant = k.variant;
    const bool lit = active || (variant == 2 && down);
    const auto face = k.face.toFloat().translated (0.0f, down ? 3.0f : 0.0f);
    const float depth = down ? 1.0f : 4.0f;

    if (lit)
    {
        juce::Path p; p.addRoundedRectangle (face, 4.0f);
        juce::DropShadow (pink.withAlpha (0.55f), 14, {}).drawForPath (g, p);
    }
    {
        juce::Path p; p.addRoundedRectangle (face.translated (0.0f, depth + 2.0f), 4.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.35f), 6, {}).drawForPath (g, p);
    }
    g.setColour (juce::Colour (0xff1E1C1B));
    g.fillRoundedRectangle (face.translated (0.0f, depth), 4.0f);
    g.fillRoundedRectangle (face, 4.0f);

    // face colours: light / dark / pink; active light keys glow pink
    int look = variant;
    if (active && variant == 0) look = 2;
    else if (active && variant == 1) look = 3;
    const juce::String id = "key" + juce::String ((int) k.id) + "_" + juce::String (look);
    const auto inner = face.reduced (1.0f);
    const bool multiply = look == 0;
    const auto& img = faceImage (id, (int) inner.getWidth(), (int) inner.getHeight(), [look] (juce::Graphics& fg, juce::Rectangle<float> r)
    {
        juce::Colour t, b;
        switch (look)
        {
            case 0:  t = juce::Colour (0xffDDD8CE); b = juce::Colour (0xffC9C3B8); break;
            case 1:  t = juce::Colour (0xff3D3B39); b = juce::Colour (0xff262524); break;
            case 2:  t = juce::Colour (0xffFF2A80); b = juce::Colour (0xffC9075A); break;
            default: t = juce::Colour (0xff4D2236); b = juce::Colour (0xff2F1620); break;
        }
        fg.setGradientFill (juce::ColourGradient (t, 0.0f, r.getY(), b, 0.0f, r.getBottom(), false));
        fg.fillRect (r);
        fg.setColour (juce::Colours::white.withAlpha (look == 0 ? 0.6f : 0.12f));
        fg.fillRect (r.getX(), r.getY(), r.getWidth(), 1.0f);
    }, texKey, { (int) k.id * 53 % 400, (int) k.id * 131 % 900 }, ! multiply, multiply ? 0.55f : 0.5f, 3.0f);
    g.setOpacity (1.0f);
    g.drawImage (img, inner);

    const juce::Colour inkCol = (variant == 0 && ! active) ? ink : juce::Colour (0xffF2EEE6);
    g.setColour (inkCol);
    const auto c = face.getCentre();

    juce::String caption;
    if (k.id == Key::sync) caption = proc.isSynced() ? "AN" : "AUS";

    juce::Path icon;
    switch (k.icon)
    {
        case 0: icon.addTriangle (c.x - 4.5f, c.y - 7.5f, c.x - 4.5f, c.y + 7.5f, c.x + 8.5f, c.y); g.fillPath (icon); break;
        case 1: g.fillRect (juce::Rectangle<float> (12.0f, 12.0f).withCentre (c)); break;
        case 2: g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (c)); break;
        case 3:
            icon.startNewSubPath (c.x - 3.0f, c.y - 5.0f);
            icon.lineTo (c.x - 7.0f, c.y - 1.0f);
            icon.lineTo (c.x - 3.0f, c.y + 3.0f);
            icon.startNewSubPath (c.x - 7.0f, c.y - 1.0f);
            icon.lineTo (c.x + 2.0f, c.y - 1.0f);
            icon.addCentredArc (c.x + 2.0f, c.y + 4.0f, 5.0f, 5.0f, 0.0f, 0.0f, juce::MathConstants<float>::pi, false);
            icon.lineTo (c.x, c.y + 9.0f);
            g.strokePath (icon, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        case 4:
            g.drawLine (c.x - 6.0f, c.y - 6.0f, c.x + 6.0f, c.y + 6.0f, 2.6f);
            g.drawLine (c.x + 6.0f, c.y - 6.0f, c.x - 6.0f, c.y + 6.0f, 2.6f);
            break;
        case 5:
            icon.startNewSubPath (c.x - 8.0f, c.y);
            icon.lineTo (c.x + 7.0f, c.y);
            icon.startNewSubPath (c.x + 1.0f, c.y - 6.0f);
            icon.lineTo (c.x + 7.0f, c.y);
            icon.lineTo (c.x + 1.0f, c.y + 6.0f);
            g.strokePath (icon, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        default: break;
    }
    if (caption.isNotEmpty())
    {
        g.setFont (font (xbold, 11.0f).withExtraKerningFactor (0.08f));
        g.drawText (caption, face, juce::Justification::centred, false);
    }
}

//==============================================================================
// display

juce::Image CatnipView::renderScreen()
{
    const auto sr = screenRect();
    const int W = sr.getWidth(), H = sr.getHeight();
    juce::Image fg (juce::Image::ARGB, (int) (W * renderScale), (int) (H * renderScale), true);
    juce::Graphics g (fg);
    g.addTransform (juce::AffineTransform::scale (renderScale));

    auto sample = proc.getSample();
    const auto vt21 = font (vt, 21.0f), vt19 = font (vt, 19.0f), vt13 = font (vt, 13.0f);
    g.setColour (lcd);

    const juce::Rectangle<float> wave (12.0f, 34.0f, 470.0f, 88.0f);
    const auto tapsNow = proc.getTaps();
    const auto b = proc.getBounds();
    const int n = (int) b.size() > 1 ? (int) b.size() - 1 : 0;
    const int sel = juce::jlimit (0, juce::jmax (0, n - 1), proc.selectedChop.load());
    const float snap = proc.apvts.getRawParameterValue ("snap")->load();
    const bool tapping = proc.isTapping();

    if (sample == nullptr)
    {
        g.setFont (vt21);
        const auto missing = proc.getMissingFileName();
        g.drawText (missing.isNotEmpty() ? "DATEI FEHLT: " + juce::File (missing).getFileName().toUpperCase() : juce::String ("KEIN SAMPLE"),
                    juce::Rectangle<float> (12.0f, 6.0f, (float) W - 24.0f, 22.0f), juce::Justification::centredLeft, true);
        g.setFont (font (vt, dragOver ? 34.0f : 30.0f));
        g.drawText (dragOver ? "LOSLASSEN ZUM LADEN" : "SAMPLE HIER REINZIEHEN", juce::Rectangle<float> (0.0f, 50.0f, (float) W, 34.0f), juce::Justification::centred, false);
        g.setFont (vt19);
        g.setColour (lcd.withAlpha (0.7f));
        g.drawText (u8 ("ODER KLICKEN  \xc2\xb7  WAV AIFF MP3 FLAC"), juce::Rectangle<float> (0.0f, 88.0f, (float) W, 20.0f), juce::Justification::centred, false);
        g.setColour (lcd);
        if (message.isNotEmpty())
        {
            g.setFont (vt19);
            g.drawText (message, juce::Rectangle<float> (12.0f, (float) H - 26.0f, (float) W - 24.0f, 20.0f), juce::Justification::centredLeft, true);
        }
        return fg;
    }

    // header
    g.setFont (vt21);
    const auto right = bpmLine();
    const float rw = juce::GlyphArrangement::getStringWidth (vt21, right) + 4.0f;
    g.drawText (right, juce::Rectangle<float> ((float) W - 12.0f - rw, 6.0f, rw, 22.0f), juce::Justification::centredRight, false);
    g.drawText ("SAMPLE: " + sample->file.getFileNameWithoutExtension().toUpperCase(),
                juce::Rectangle<float> (12.0f, 6.0f, (float) W - 36.0f - rw, 22.0f), juce::Justification::centredLeft, true);

    const auto view = proc.getView();
    auto px = [&] (juce::int64 s) { return wave.getX() + (float) ((double) (s - view.first) / (double) std::max<juce::int64> (1, view.second)) * wave.getWidth(); };
    auto visible = [&] (juce::int64 s) { return s >= view.first && s <= view.first + view.second; };

    // selected chop
    if (n > 0 && ! tapping)
    {
        const float x0 = juce::jmax (wave.getX(), px (b[(size_t) sel])), x1 = juce::jmin (wave.getRight(), px (b[(size_t) sel + 1]));
        if (x1 > x0)
        {
            g.setColour (pink.withAlpha (0.16f));
            g.fillRect (x0, wave.getY(), x1 - x0, wave.getHeight());
        }
    }

    // waveform bars
    const auto& pk = viewPeaks (*sample, view.first, view.second);
    const float bw = wave.getWidth() / (float) pk.size();
    const float mid = wave.getY() + (wave.getHeight() - 6.0f) / 2.0f + 4.0f;
    g.setColour (juce::Colour (0xffE8186F));
    for (size_t i = 0; i < pk.size(); ++i)
    {
        const float a = std::max (std::abs (pk[i].first), std::abs (pk[i].second));
        const float hh = std::max (1.5f, a * (wave.getHeight() - 14.0f) * 0.92f);
        g.fillRect (wave.getX() + (float) i * bw, mid - hh / 2.0f, bw * 0.55f, hh);
    }

    // transients (top markers, size = strength)
    float maxStrength = 1.0e-6f;
    for (auto& o : sample->analysis.onsetsByTime) maxStrength = std::max (maxStrength, o.strength);
    g.setColour (juce::Colour (0xffFF3A86).withAlpha (0.7f));
    for (auto& o : sample->analysis.onsetsByTime)
    {
        if (! visible (o.sample)) continue;
        const float x = px (o.sample), sz = 2.0f + 4.0f * juce::jlimit (0.0f, 1.0f, o.strength / maxStrength);
        juce::Path tri;
        tri.addTriangle (x - sz / 2.0f, wave.getY(), x + sz / 2.0f, wave.getY(), x, wave.getY() + sz);
        g.fillPath (tri);
    }

    // chops: raw taps (dashed), snap targets (bottom triangles), chop lines + numbers
    const bool fresh = proc.isFreshPass();   // the old chops are replaced by the first tap: show them faded
    {
        for (int i = 0; i < n; ++i)
        {
            const auto raw = tapsNow.raw[(size_t) i];
            const auto target = tapsNow.target[(size_t) i];
            if (snap > 0.0f && visible (raw) && std::abs (px (raw) - px (b[(size_t) i])) > 1.5f)
            {
                g.setColour (juce::Colour (0xffFF3A86).withAlpha (0.45f * (fresh ? 0.3f : 1.0f)));
                const float x = px (raw);
                for (float y = wave.getY() + 8.0f; y < wave.getBottom() - 8.0f; y += 4.0f)
                    g.fillRect (x - 0.5f, y, 1.0f, 2.0f);
            }
            if (snap > 0.0f && target >= 0 && visible (target))
            {
                const float x = px (target);
                juce::Path tri;
                tri.addTriangle (x - 3.0f, wave.getBottom() - 1.0f, x + 3.0f, wave.getBottom() - 1.0f, x, wave.getBottom() - 6.0f);
                g.setColour (juce::Colour (0xffFFD6E7).withAlpha (fresh ? 0.3f : 1.0f));
                g.fillPath (tri);
            }
            if (visible (b[(size_t) i]))
            {
                const float x = px (b[(size_t) i]);
                const bool hot = i == dragLine || (dragLine < 0 && i == hoverLine);
                g.setColour ((hot ? juce::Colours::white : juce::Colour (0xffFF6FA6)).withAlpha (fresh ? 0.3f : 1.0f));
                g.fillRect (x - (hot ? 1.25f : 0.75f), wave.getY() + (hot ? 0.0f : 8.0f), hot ? 2.5f : 1.5f, wave.getHeight() - (hot ? 0.0f : 8.0f));
                g.setFont (vt13);
                g.setColour (lcd.withAlpha ((i == sel ? 1.0f : 0.6f) * (fresh ? 0.3f : 1.0f)));
                g.drawText (two (i + 1), juce::Rectangle<float> (x + 2.0f, wave.getY() - 2.0f, 20.0f, 12.0f), juce::Justification::centredLeft, false);
            }
        }
    }

    // status line
    g.setColour (lcd);
    g.setFont (vt19);
    juce::String left, rightStatus;
    const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    if (message.isNotEmpty()) left = message;
    else if (tapping) left = "TAP " + juce::String (juce::CharPointer_UTF8 ("L\xc3\x84UFT")) + dot + "ALLES ANDERE STUMM" + dot + juce::String (fresh ? 0 : tapsNow.count) + "/16";
    else if (proc.isPreviewing()) left = juce::String (juce::CharPointer_UTF8 ("> ANH\xc3\x96REN"));
    else if (n == 0) left = u8 ("TAP DRÜCKEN ODER LEERTASTE");
    else left = juce::String (n) + " CHOPS" + dot + "SNAP " + juce::String (juce::roundToInt (snap * 100.0f)) + "%";
    if (n > 0)
        rightStatus = "CHOP " + two (sel + 1) + dot + juce::String ((double) b[(size_t) sel] / sample->sampleRate, 2) + "s";
    else
        rightStatus = "LEERTASTE = TAP";
    const float sw = juce::GlyphArrangement::getStringWidth (vt19, rightStatus) + 4.0f;
    g.drawText (rightStatus, juce::Rectangle<float> ((float) W - 12.0f - sw, (float) H - 26.0f, sw, 20.0f), juce::Justification::centredRight, false);
    g.drawText (left, juce::Rectangle<float> (12.0f, (float) H - 26.0f, (float) W - 36.0f - sw, 20.0f), juce::Justification::centredLeft, true);

    return fg;
}

void CatnipView::paintDisplay (juce::Graphics& g)
{
    const auto sr = screenRect();
    const auto r = sr.toFloat();

    // what the screen shows; re-rendered (with glow) only when something on it changes
    juce::String sig;
    {
        auto s = proc.getSample();
        const auto b = proc.getBounds();
        const auto v = proc.getView();
        sig << (s != nullptr ? s->file.getFullPathName() : proc.getMissingFileName()) << (int) dragOver << "|" << v.first << "," << v.second << "|";
        for (auto x : b) sig << x << ",";
        const auto t = proc.getTaps();
        for (int i = 0; i < t.count; ++i) sig << t.raw[(size_t) i] << ";";
        sig << "|" << proc.selectedChop.load() << "|" << proc.apvts.getRawParameterValue ("snap")->load() << "|" << bpmLine()
            << "|" << (int) proc.isTapping() << (int) proc.isFreshPass() << (int) proc.isPreviewing() << "|" << dragLine << "," << hoverLine << "|" << message;
    }
    if (sig != screenSignature || ! screenImage.isValid())
    {
        screenSignature = sig;
        const auto fg = renderScreen();
        screenImage = juce::Image (juce::Image::ARGB, fg.getWidth(), fg.getHeight(), true);
        juce::Graphics sg (screenImage);
        // the LCD glow: three soft passes under the sharp pixels
        juce::DropShadow (pink.withAlpha (0.40f), (int) (18 * renderScale), {}).drawForImage (sg, fg);
        juce::DropShadow (pink.withAlpha (0.75f), (int) (8 * renderScale), {}).drawForImage (sg, fg);
        juce::DropShadow (juce::Colour (0xffFF78AA).withAlpha (0.9f), (int) (3 * renderScale), {}).drawForImage (sg, fg);
        sg.drawImageAt (fg, 0, 0);
    }

    juce::Graphics::ScopedSaveState state (g);
    juce::Path clip;
    clip.addRoundedRectangle (r, 3.0f);
    g.reduceClipRegion (clip);

    // glass: deep magenta-black, darker at the edges
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2A0E1C), r.getCentreX(), r.getY() + r.getHeight() * 0.45f,
                                             juce::Colour (0xff0B0609), r.getX(), r.getBottom(), true));
    g.fillRect (r);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.9f), 0.0f, r.getY(), juce::Colours::transparentBlack, 0.0f, r.getY() + 10.0f, false));
    g.fillRect (r);
    for (int side = 0; side < 4; ++side)
    {
        const float d = 28.0f;
        juce::Rectangle<float> band;
        juce::Point<float> p0, p1;
        switch (side)
        {
            case 0:  band = r.withHeight (d);                    p0 = { 0, r.getY() };      p1 = { 0, r.getY() + d }; break;
            case 1:  band = r.withTrimmedTop (r.getHeight() - d); p0 = { 0, r.getBottom() }; p1 = { 0, r.getBottom() - d }; break;
            case 2:  band = r.withWidth (d);                     p0 = { r.getX(), 0 };      p1 = { r.getX() + d, 0 }; break;
            default: band = r.withTrimmedLeft (r.getWidth() - d); p0 = { r.getRight(), 0 }; p1 = { r.getRight() - d, 0 }; break;
        }
        g.setGradientFill (juce::ColourGradient (pink.withAlpha (0.16f), p0, juce::Colours::transparentBlack, p1, false));
        g.fillRect (band);
    }

    g.setOpacity (1.0f);
    g.drawImage (screenImage, r);

    // play head (drawn live)
    const auto pos = proc.getPreviewPosition();
    if (pos >= 0)
    {
        const auto v = proc.getView();
        if (pos >= v.first && pos <= v.first + v.second)
        {
            const float x = xForSample (pos);
            const float y0 = (float) waveRect().getY() - 2.0f, y1 = (float) waveRect().getBottom() + 2.0f;
            g.setColour (pink.withAlpha (0.18f)); g.fillRect (x - 4.0f, y0, 8.0f, y1 - y0);
            g.setColour (pink.withAlpha (0.35f)); g.fillRect (x - 2.0f, y0, 4.0f, y1 - y0);
            g.setColour (juce::Colour (0xffFFD6E7)); g.fillRect (x - 1.0f, y0, 2.0f, y1 - y0);
        }
    }

    // scanlines and glass reflection
    g.setColour (juce::Colours::white.withAlpha (0.035f));
    for (float y = r.getY(); y < r.getBottom(); y += 3.0f)
        g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
    g.setGradientFill (cssLinear (r, 165.0f, { { 0.0f, juce::Colours::white.withAlpha (0.07f) }, { 0.38f, juce::Colours::white.withAlpha (0.02f) },
                                               { 0.40f, juce::Colours::transparentWhite }, { 1.0f, juce::Colours::transparentWhite } }));
    g.fillRect (r);

    if (dragOver)
    {
        g.setColour (pink);
        g.drawRoundedRectangle (r.reduced (1.5f), 3.0f, 2.0f);
    }
}

//==============================================================================
// mouse

void CatnipView::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    int hv = -1;
    if (waveRect().expanded (4, 8).contains (p) && proc.getSample() != nullptr)
        hv = chopLineAt ((float) p.x);
    if (hv != hoverLine) { hoverLine = hv; repaint (displayRect()); }

    if (hv >= 0) setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else if (knobAt (p) >= 0) setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    else if (padAt (p) >= 0 || keyAt (p) != nullptr || (screenRect().contains (p) && proc.getSample() == nullptr))
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    else setMouseCursor (juce::MouseCursor::NormalCursor);
}

void CatnipView::mouseExit (const juce::MouseEvent&)
{
    if (hoverLine >= 0) { hoverLine = -1; repaint (displayRect()); }
}

void CatnipView::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    exportStarted = false;

    if (const int vis = padAt (p); vis >= 0)
    {
        mousePad = chopOfVisual (vis);
        padDown (mousePad);
        return;
    }
    if (const int k = knobAt (p); k >= 0)
    {
        dragKnob = k;
        dragStartValue = knobValue (k);
        const char* ids[] = { "pitch", "snap", nullptr, "volume" };
        if (ids[k] != nullptr) proc.apvts.getParameter (ids[k])->beginChangeGesture();
        e.source.enableUnboundedMouseMovement (true, true);
        return;
    }
    if (auto* key = keyAt (p))
    {
        pressedKey = key;
        if (key->id != Key::toDaw) pressKey (key->id);
        repaint();
        return;
    }
    if (screenRect().contains (p))
    {
        if (proc.getSample() == nullptr) { openFileChooser(); return; }
        if (waveRect().expanded (4, 8).contains (p))
        {
            dragLine = chopLineAt ((float) p.x);
            lineEditStarted = false;
            if (dragLine >= 0)
            {
                proc.selectedChop = dragLine;
            }
            else
            {
                // click inside a chop selects it
                const auto s = sampleAtX ((float) p.x);
                const auto b = proc.getBounds();
                for (int i = 0; i + 1 < (int) b.size(); ++i)
                    if (s >= b[(size_t) i] && s < b[(size_t) i + 1]) { proc.selectedChop = i; break; }
            }
            repaint();
        }
    }
}

void CatnipView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragKnob >= 0)
    {
        if (! e.mods.isLeftButtonDown()) { mouseUp (e); return; }   // missed release: never keep turning
        const bool fine = e.mods.isShiftDown();
        const float delta = (float) -e.getDistanceFromDragStartY() / (fine ? 800.0f : 160.0f);
        setKnobValue (dragKnob, dragStartValue + delta, fine);
        return;
    }
    if (dragLine >= 0)
    {
        if (! lineEditStarted) { proc.beginEdit(); lineEditStarted = true; }
        proc.moveTap (dragLine, sampleAtX ((float) e.x));
        return;
    }
    if (pressedKey != nullptr && pressedKey->id == Key::toDaw && ! exportStarted && e.getDistanceFromDragStart() > 4)
    {
        exportStarted = true;
        pressedKey = nullptr;
        repaint();
        if (proc.getNumChops() == 0) { showMessage ("ERST CHOPS TAPPEN"); return; }
        const auto files = proc.exportChopsToTempFiles();
        if (! files.isEmpty())
            juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, this);
    }
}

void CatnipView::mouseUp (const juce::MouseEvent& e)
{
    if (mousePad >= 0) { padUp (mousePad); mousePad = -1; }
    if (dragKnob >= 0)
    {
        const char* ids[] = { "pitch", "snap", nullptr, "volume" };
        if (ids[dragKnob] != nullptr) proc.apvts.getParameter (ids[dragKnob])->endChangeGesture();
        e.source.enableUnboundedMouseMovement (false);
        dragKnob = -1;
    }
    dragLine = -1;
    if (pressedKey != nullptr)
    {
        if (pressedKey->id == Key::toDaw && ! exportStarted && keyAt (e.getPosition()) == pressedKey)
        {
            if (proc.getNumChops() == 0) showMessage ("ERST CHOPS TAPPEN");
            else
            {
                const auto files = proc.exportChopsToTempFiles();
                if (! files.isEmpty())
                {
                    juce::File (files[0]).revealToUser();
                    showMessage (juce::String (files.size()) + u8 (" CHOPS GESPEICHERT \xc2\xb7 TASTE ZIEHEN = IN DIE DAW"));
                }
            }
        }
        pressedKey = nullptr;
    }
    repaint();
}

void CatnipView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const int k = knobAt (e.getPosition()); k >= 0)
    {
        switch ((Knob) k)
        {
            case Knob::pitch:  proc.apvts.getParameter ("pitch")->setValueNotifyingHost (0.5f); break;
            case Knob::snap:   { auto* p = proc.apvts.getParameter ("snap"); p->setValueNotifyingHost (p->getDefaultValue()); break; }
            case Knob::zoom:   setZoom (0.0f); break;
            case Knob::volume: { auto* p = proc.apvts.getParameter ("volume"); p->setValueNotifyingHost (p->getDefaultValue()); break; }
        }
        repaint();
    }
}

void CatnipView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (screenRect().contains (e.getPosition()) && proc.getSample() != nullptr)
    {
        setZoom (currentZoom() + (w.deltaY > 0 ? 0.06f : -0.06f));
        return;
    }
    if (const int k = knobAt (e.getPosition()); k >= 0)
        setKnobValue (k, knobValue (k) + (w.deltaY > 0 ? 1.0f : -1.0f) / 24.0f, e.mods.isShiftDown());
}

//==============================================================================
CatnipEditor::CatnipEditor (CatnipProcessor& p)
    : AudioProcessorEditor (p), proc (p), view (p)
{
    addAndMakeVisible (view);
    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (CatnipView::width * 6 / 10, CatnipView::height * 6 / 10, CatnipView::width * 3 / 2, CatnipView::height * 3 / 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) CatnipView::width / (double) CatnipView::height);
    setSize (CatnipView::width * 9 / 10, CatnipView::height * 9 / 10);
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
    const auto mods = key.getModifiers();
    if ((mods.isCommandDown() || mods.isCtrlDown()) && key.getKeyCode() == 'Z')
    {
        view.undo();
        return true;
    }
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;

    if (key == juce::KeyPress::spaceKey) { view.toggleTap(); return true; }
    if (key == juce::KeyPress::escapeKey) { view.stopEverything(); return true; }

    auto ch = key.getTextCharacter();
    if (ch == 0) ch = (juce::juce_wchar) key.getKeyCode();
    const int vis = visualForChar (ch);
    if (vis < 0) return false;

    if (heldKeys.count (vis) == 0)   // ignore auto-repeat
    {
        heldKeys[vis] = juce::CharacterFunctions::toLowerCase (ch);
        view.padDown (chopOfVisual (vis));   // while tapping this sets a chop instead
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
