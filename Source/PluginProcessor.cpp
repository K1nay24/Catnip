#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace catnip;

//==============================================================================
int catnip::computeBounds (const TapSet& t, float snap, std::array<juce::int64, maxChops + 1>& b)
{
    const int n = juce::jlimit (0, maxChops, t.count);
    if (n == 0) return 0;

    constexpr juce::int64 minGap = 256;
    snap = juce::jlimit (0.0f, 1.0f, snap);
    for (int i = 0; i < n; ++i)
    {
        auto x = t.raw[(size_t) i];
        if (t.target[(size_t) i] >= 0)
            x += (juce::int64) std::llround ((double) (t.target[(size_t) i] - x) * (double) snap);
        if (i > 0) x = std::max (x, b[(size_t) i - 1] + minGap);
        b[(size_t) i] = std::max<juce::int64> (0, x);
    }
    b[(size_t) n] = std::max (t.end, b[(size_t) n - 1] + minGap);
    return n;
}

//==============================================================================
void Voice::begin (juce::int64 regionStart, juce::int64 regionLength, double tempo, double pitch,
                   double outRate, bool rev, float g)
{
    start = regionStart;
    length = std::max<juce::int64> (1, regionLength);
    tempoStep = std::max (1.0e-4, tempo);
    pitchStep = std::max (1.0e-4, pitch);
    stretch = std::abs (tempoStep - pitchStep) > 1.0e-6 * pitchStep;
    reverse = rev;
    pos = 0.0;
    grainLen = std::max (64.0, outRate * 0.045);
    // two grains half a grain apart; at the start both read the same spot, so the attack stays intact
    grainPos[0] = grainPos[1] = 0.0;
    grainPhase[0] = grainLen * 0.5;
    grainPhase[1] = 0.0;
    fadeLen = std::min ((double) length * 0.25, outRate * 0.003 * pitchStep);
    gain = g;
    attack = 0.0f;
    attackStep = (float) (1.0 / std::max (1.0, outRate * 0.001));
    release = 1.0f;
    releaseStep = 0.0f;
    releasing = false;
    active = true;
}

void Voice::beginRelease (double outRate, double seconds)
{
    if (! active) return;
    const auto step = (float) (1.0 / std::max (1.0, outRate * seconds));
    if (releasing && releaseStep >= step) return;   // already fading at least as fast
    releasing = true;
    releaseStep = step;
}

void Voice::setRates (double tempo, double pitch)
{
    tempo = std::max (1.0e-4, tempo);
    pitch = std::max (1.0e-4, pitch);
    const bool nowStretch = std::abs (tempo - pitch) > 1.0e-6 * pitch;
    if (nowStretch && ! stretch)
    {
        // switch to grains seamlessly: both read where the voice is, one at full weight
        grainPos[0] = grainPos[1] = pos;
        grainPhase[0] = grainLen * 0.5;
        grainPhase[1] = 0.0;
    }
    stretch = nowStretch;
    tempoStep = tempo;
    pitchStep = pitch;
}

float Voice::read (const juce::AudioBuffer<float>& src, int channel, double p) const
{
    if (p < 0.0 || p >= (double) length) return 0.0f;
    const double absPos = reverse ? (double) (start + length - 1) - p : (double) start + p;
    const int n = src.getNumSamples();
    const auto i0 = (int) juce::jlimit<juce::int64> (0, n - 1, (juce::int64) std::floor (absPos));
    const auto i1 = std::min (i0 + 1, n - 1);
    const auto frac = (float) (absPos - std::floor (absPos));
    const auto* d = src.getReadPointer (std::min (channel, src.getNumChannels() - 1));
    return d[i0] + (d[i1] - d[i0]) * frac;
}

bool Voice::render (const juce::AudioBuffer<float>& src, juce::AudioBuffer<float>& out, int startSample, int numSamples)
{
    if (! active) return false;
    const int outChannels = out.getNumChannels();

    for (int n = 0; n < numSamples; ++n)
    {
        if (pos >= (double) length) { active = false; return false; }

        float amp = gain;
        if (attack < 1.0f) { attack = std::min (1.0f, attack + attackStep); amp *= attack; }
        const double remaining = (double) length - pos;
        if (remaining < fadeLen) amp *= (float) (remaining / fadeLen);
        if (releasing)
        {
            release -= releaseStep;
            if (release <= 0.0f) { active = false; return false; }
            amp *= release;
        }

        if (! stretch)
        {
            for (int c = 0; c < outChannels; ++c)
                out.addSample (c, startSample + n, read (src, c, pos) * amp);
        }
        else
        {
            const auto w0 = (float) std::pow (std::sin (juce::MathConstants<double>::pi * grainPhase[0] / grainLen), 2.0);
            const float w1 = 1.0f - w0;   // the two windows always add up to 1
            for (int c = 0; c < outChannels; ++c)
                out.addSample (c, startSample + n, (read (src, c, grainPos[0]) * w0 + read (src, c, grainPos[1]) * w1) * amp);

            for (int g = 0; g < 2; ++g)
            {
                grainPos[g] += pitchStep;
                grainPhase[g] += 1.0;
                if (grainPhase[g] >= grainLen)
                {
                    grainPhase[g] -= grainLen;
                    grainPos[g] = pos;      // the next grain starts where the music is now
                }
            }
        }
        pos += tempoStep;
    }
    return true;
}

//==============================================================================
CatnipProcessor::CatnipProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Params", createLayout())
{
    volumeParam = apvts.getRawParameterValue ("volume");
    pitchParam = apvts.getRawParameterValue ("pitch");
    snapParam = apvts.getRawParameterValue ("snap");
    syncParam = apvts.getRawParameterValue ("sync");
}

juce::AudioProcessorValueTreeState::ParameterLayout CatnipProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "pitch", 1 }, "Pitch",
                                                             juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("st")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "snap", 1 }, "Snap",
                                                             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "sync", 1 }, "DAW Sync", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "volume", 1 }, "Volume",
                                                             juce::NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("dB")));
    return layout;
}

bool CatnipProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void CatnipProcessor::prepareToPlay (double sampleRate, int)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    hostRate = sampleRate > 0 ? sampleRate : 44100.0;
    for (auto& v : voices) v.active = false;
    preview.active = false;
    previewPosition = -1;
}

//==============================================================================
std::pair<double, double> CatnipProcessor::ratiosLocked() const
{
    const double pitch = std::pow (2.0, (double) pitchParam->load() / 12.0);
    if (! isSynced())
        return { pitch, pitch };              // MPC: pitch and tempo move together

    const double host = hostBpm.load();
    const double own = sample != nullptr ? sample->analysis.bpm : 0.0;
    if (host <= 0.0 || own <= 0.0)
        return { 1.0, pitch };                // no tempo to follow: keep the tempo, pitch only

    // follow the DAW, but in half or double time if that is closer (no octave jumps in speed)
    double best = host / own;
    for (double k : { 0.5, 2.0 })
    {
        const double r = host / (own * k);
        if (std::abs (std::log (r)) < std::abs (std::log (best))) best = r;
    }
    return { best, pitch };
}

std::pair<double, double> CatnipProcessor::getRatios() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return ratiosLocked();
}

double CatnipProcessor::getSampleBpm() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return sample != nullptr ? sample->analysis.bpm : 0.0;
}

double CatnipProcessor::getPlaybackBpm() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    if (sample == nullptr || sample->analysis.bpm <= 0.0) return 0.0;
    return sample->analysis.bpm * ratiosLocked().first;
}

//==============================================================================
void CatnipProcessor::killAll()
{
    for (auto& v : voices) v.beginRelease (hostRate, 0.004);
    preview.beginRelease (hostRate, 0.004);
}

void CatnipProcessor::startVoice (int chop, float velocity)
{
    if (sample == nullptr || chop < 0 || chop >= maxChops) return;

    std::array<juce::int64, maxChops + 1> b;
    const int n = computeBounds (taps, snapParam->load(), b);
    if (chop >= n) return;

    const juce::int64 len = sample->buffer.getNumSamples();
    const auto start = juce::jlimit<juce::int64> (0, len, b[(size_t) chop]);
    const auto end = juce::jlimit<juce::int64> (0, len, b[(size_t) chop + 1]);
    if (end - start < 2) return;

    // choke: a new chop cuts everything still sounding, the preview too (4 ms, no click, no overlap)
    killAll();

    Voice* slot = nullptr;
    for (auto& v : voices)
        if (! v.active) { slot = &v; break; }
    if (slot == nullptr)
    {
        slot = &voices[0];
        for (auto& v : voices)
            if (v.release < slot->release) slot = &v;
    }

    const auto ratios = ratiosLocked();
    const double base = sample->sampleRate / hostRate;
    slot->chop = chop;
    slot->begin (start, end - start, ratios.first * base, ratios.second * base, hostRate,
                 flags[(size_t) chop].reverse.load(), 0.35f + 0.65f * juce::jlimit (0.0f, 1.0f, velocity));

    hitCounters[(size_t) chop].fetch_add (1);
}

void CatnipProcessor::stopVoice (int chop)
{
    if (chop < 0 || chop >= maxChops || ! flags[(size_t) chop].gate.load()) return;
    for (auto& v : voices)
        if (v.active && v.chop == chop && ! v.releasing)
            v.beginRelease (hostRate, 0.015);
}

int CatnipProcessor::getActiveVoiceCount() const noexcept
{
    int n = 0;
    for (auto& v : voices)
        if (v.active && ! v.releasing) ++n;
    return n;
}

void CatnipProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    double dawBpm = 0.0;
    if (auto* ph = getPlayHead())
        if (auto info = ph->getPosition())
            if (auto bpmValue = info->getBpm())
                dawBpm = *bpmValue > 0.0 ? *bpmValue : 0.0;
    hostBpm = dawBpm;

    // pads / computer keys from the editor come in through the keyboard state
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    {
        // the message thread only holds this lock for a few copies, so waiting is safer than dropping a block
        const juce::SpinLock::ScopedLockType sl (lock);
        {
            if (sample != nullptr)
            {
                // pitch / sync changes are heard right away, also on chops that are already playing
                const auto ratios = ratiosLocked();
                const double base = sample->sampleRate / hostRate;
                for (auto& v : voices) if (v.active) v.setRates (ratios.first * base, ratios.second * base);
                if (preview.active) preview.setRates (ratios.first * base, ratios.second * base);
            }

            if (stopRequest.exchange (false))
                killAll();

            if (const auto req = previewRequest.exchange (-1); req >= 0 && sample != nullptr)
            {
                const auto end = juce::jlimit<juce::int64> (req + 1, sample->buffer.getNumSamples(), previewEnd.load());
                const auto ratios = ratiosLocked();
                const double base = sample->sampleRate / hostRate;
                preview.begin (req, end - req, ratios.first * base, ratios.second * base, hostRate, false, 0.8f);
            }

            int pos = 0;
            for (const auto meta : midi)
            {
                const auto msg = meta.getMessage();
                const int when = juce::jlimit (0, buffer.getNumSamples(), meta.samplePosition);
                if (when > pos && sample != nullptr)
                {
                    for (auto& v : voices) v.render (sample->buffer, buffer, pos, when - pos);
                    pos = when;
                }

                const int chop = msg.getNoteNumber() - firstNote;
                if (msg.isNoteOn())
                {
                    if (! tapping.load())   // while tapping only the tapped loop plays
                        startVoice (chop, msg.getFloatVelocity());
                }
                else if (msg.isNoteOff())
                    stopVoice (chop);
                else if (msg.isAllNotesOff() || msg.isAllSoundOff())
                    killAll();
            }

            if (sample != nullptr)
            {
                if (pos < buffer.getNumSamples())
                    for (auto& v : voices) v.render (sample->buffer, buffer, pos, buffer.getNumSamples() - pos);

                preview.render (sample->buffer, buffer, 0, buffer.getNumSamples());
            }

            if (preview.active && ! preview.releasing)
                previewPosition = preview.start + (juce::int64) preview.pos;
            else if (previewRequest.load() < 0)
            {
                previewPosition = -1;
                tapping = false;    // the loop ran out: the tap pass is over
            }
        }
    }

    const auto target = juce::Decibels::decibelsToGain (volumeParam->load(), -60.0f);
    buffer.applyGainRamp (0, buffer.getNumSamples(), lastGain, target);
    lastGain = target;
}

//==============================================================================
void CatnipProcessor::refreshTargets()
{
    for (int i = 0; i < taps.count; ++i)
        taps.target[(size_t) i] = sample != nullptr
            ? snapTarget (sample->analysis.onsetsByTime, taps.raw[(size_t) i], sample->sampleRate) : -1;
}

void CatnipProcessor::pushUndo()
{
    undoStack.push_back (taps);
    if (undoStack.size() > 64) undoStack.erase (undoStack.begin());
}

TapSet CatnipProcessor::getTaps() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return taps;
}

std::vector<juce::int64> CatnipProcessor::getBounds() const
{
    std::array<juce::int64, maxChops + 1> b;
    int n;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        n = computeBounds (taps, snapParam->load(), b);
    }
    if (n == 0) return {};
    return std::vector<juce::int64> (b.begin(), b.begin() + n + 1);
}

int CatnipProcessor::getNumChops() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return taps.count;
}

void CatnipProcessor::beginEdit()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    pushUndo();
}

void CatnipProcessor::moveTap (int index, juce::int64 position)
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (sample == nullptr || index < 0 || index >= taps.count) return;
        const auto gap = (juce::int64) (sample->sampleRate * 0.01);
        const auto lo = index > 0 ? taps.raw[(size_t) index - 1] + gap : 0;
        const auto hi = index + 1 < taps.count ? taps.raw[(size_t) index + 1] - gap : taps.end - gap;
        if (hi < lo) return;
        taps.raw[(size_t) index] = juce::jlimit (lo, hi, position);
        taps.target[(size_t) index] = snapTarget (sample->analysis.onsetsByTime, taps.raw[(size_t) index], sample->sampleRate);
    }
    changed();
}

void CatnipProcessor::clearTaps()
{
    stopAll();
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (taps.count == 0) return;
        pushUndo();
        taps.count = 0;
    }
    selectedChop = 0;
    changed();
}

bool CatnipProcessor::undo()
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (undoStack.empty()) return false;
        taps = undoStack.back();
        undoStack.pop_back();
        refreshTargets();
        tapsThisPass = taps.count;   // tapping on continues the restored chops instead of wiping them
    }
    selectedChop = juce::jlimit (0, juce::jmax (0, getNumChops() - 1), selectedChop.load());
    changed();
    return true;
}

bool CatnipProcessor::canUndo() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return ! undoStack.empty();
}

void CatnipProcessor::snapAllToHits()
{
    auto* p = apvts.getParameter ("snap");
    const float now = snapParam->load();
    const float next = now >= 0.999f ? snapBeforeFull : 1.0f;
    if (now < 0.999f) snapBeforeFull = now;
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (next));
    p->endChangeGesture();
    changed();
}

//==============================================================================
void CatnipProcessor::play()
{
    tapping = false;
    const auto view = getView();
    if (getSample() == nullptr || view.second <= 0) return;
    stopRequest = true;
    previewEnd = view.first + view.second;
    previewPosition = view.first;
    previewRequest = view.first;
    changed();
}

void CatnipProcessor::stopAll()
{
    tapping = false;
    previewRequest = -1;
    stopRequest = true;
    previewPosition = -1;
    changed();
}

void CatnipProcessor::startTapping()
{
    const auto view = getView();
    if (getSample() == nullptr || view.second <= 0) return;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        tapsThisPass = 0;          // the first tap of this pass replaces the old chops
    }
    stopRequest = true;            // silence everything that is playing
    previewEnd = view.first + view.second;
    previewPosition = view.first;
    previewRequest = view.first;
    tapping = true;
    changed();
}

void CatnipProcessor::stopTapping()
{
    stopAll();
}

bool CatnipProcessor::isFreshPass() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return tapping.load() && tapsThisPass == 0;
}

int CatnipProcessor::tap()
{
    if (! tapping.load()) return -1;
    const auto pos = previewPosition.load();
    return pos >= 0 ? addTapAt (pos) : -1;
}

int CatnipProcessor::addTapAt (juce::int64 position)
{
    int index = -1;
    bool full = false;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (sample == nullptr) return -1;
        const juce::int64 len = sample->buffer.getNumSamples();
        position = juce::jlimit<juce::int64> (0, len - 1, position);

        if (tapsThisPass == 0)
        {
            pushUndo();
            taps.count = 0;
            taps.end = juce::jlimit<juce::int64> (1, len, viewStart + viewLength);
        }
        if (taps.count >= maxChops) return -1;
        if (taps.count > 0 && position <= taps.raw[(size_t) taps.count - 1] + (juce::int64) (sample->sampleRate * 0.05))
            return -1;                 // double tap on the same hit
        if (position >= taps.end - 256)
            return -1;                 // too close to the end to make a chop

        index = taps.count++;
        taps.raw[(size_t) index] = position;
        taps.target[(size_t) index] = snapTarget (sample->analysis.onsetsByTime, position, sample->sampleRate);
        ++tapsThisPass;
        full = taps.count >= maxChops;
    }
    selectedChop = index;
    hitCounters[(size_t) index].fetch_add (1);
    if (full)
        stopTapping();
    changed();
    return index;
}

//==============================================================================
std::pair<juce::int64, juce::int64> CatnipProcessor::getView() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return { viewStart, viewLength };
}

juce::int64 CatnipProcessor::minViewLength() const
{
    auto s = getSample();
    if (s == nullptr) return 1;
    return juce::jmin<juce::int64> (s->buffer.getNumSamples(), (juce::int64) (s->sampleRate * 0.5));
}

void CatnipProcessor::setView (juce::int64 start, juce::int64 length)
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (sample == nullptr) return;
        const juce::int64 len = sample->buffer.getNumSamples();
        const auto minLen = juce::jmin<juce::int64> (len, (juce::int64) (sample->sampleRate * 0.5));
        length = juce::jlimit (minLen, len, length);
        start = juce::jlimit<juce::int64> (0, len - length, start);
        if (start == viewStart && length == viewLength) return;
        viewStart = start;
        viewLength = length;
    }
    changed();
}

//==============================================================================
bool CatnipProcessor::loadFile (const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "FORMAT NICHT LESBAR";
        return false;
    }

    const auto len = reader->lengthInSamples;
    if (len <= 0 || reader->sampleRate <= 0)
    {
        error = "DATEI IST LEER";
        return false;
    }
    if (len > (juce::int64) (reader->sampleRate * 600.0))
    {
        error = juce::String (juce::CharPointer_UTF8 ("ZU LANG \xe2\x80\x93 MAX. 10 MINUTEN"));
        return false;
    }

    auto data = std::make_shared<SampleData>();
    const int channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
    data->buffer.setSize (channels, (int) len);
    reader->read (&data->buffer, 0, (int) len, 0, true, channels > 1);
    data->sampleRate = reader->sampleRate;
    data->file = file;
    data->analysis = analyse (data->buffer, data->sampleRate, 160);

    stopAll();
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (sample, data);
        viewStart = 0;
        viewLength = len;
        taps = {};
        taps.end = len;
        undoStack.clear();
        tapsThisPass = 0;
    }
    for (auto& f : flags) { f.reverse = false; f.gate = false; }
    missingFile = {};
    selectedChop = 0;
    changed();
    return true;   // the old sample (now in `data`) is released here, off the audio thread
}

std::shared_ptr<const SampleData> CatnipProcessor::getSample() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return sample;
}

//==============================================================================
juce::StringArray CatnipProcessor::exportChopsToTempFiles()
{
    juce::StringArray paths;
    auto data = getSample();
    auto b = getBounds();
    if (data == nullptr || b.size() < 2) return paths;

    const auto ratios = getRatios();
    const double rate = data->sampleRate;
    const double bpmNow = getPlaybackBpm();

    auto base = juce::File::createLegalFileName (data->file.getFileNameWithoutExtension());
    if (bpmNow > 0.0) base << "_" << juce::String (bpmNow, 1) << "bpm";

    // a fresh folder per export: a DAW may still point at files from an earlier drag
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Catnip Chops")
                   .getChildFile (base + "_" + juce::Time::getCurrentTime().formatted ("%H%M%S"))
                   .getNonexistentSibling (false);
    dir.createDirectory();

    juce::WavAudioFormat wav;
    const int count = (int) b.size() - 1;
    const juce::int64 srcLen = data->buffer.getNumSamples();
    for (int i = 0; i < count; ++i)
    {
        const auto start = juce::jlimit<juce::int64> (0, srcLen, b[(size_t) i]);
        const auto len = juce::jlimit<juce::int64> (0, srcLen, b[(size_t) i + 1]) - start;
        if (len < 2) continue;

        Voice v;
        v.begin (start, len, ratios.first, ratios.second, rate, flags[(size_t) i].reverse.load(), 1.0f);
        const int outLen = (int) std::ceil ((double) len / ratios.first) + 16;
        juce::AudioBuffer<float> chop (data->buffer.getNumChannels(), outLen);
        chop.clear();
        int written = 0;
        while (written < outLen && v.active)
        {
            const int block = std::min (512, outLen - written);
            v.render (data->buffer, chop, written, block);
            written += block;
        }

        auto out = dir.getChildFile (base + "_chop" + juce::String (i + 1).paddedLeft ('0', 2) + ".wav");
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (out);
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate (rate)
                                                       .withNumChannels (chop.getNumChannels())
                                                       .withBitsPerSample (24));
        if (writer != nullptr && writer->writeFromAudioSampleBuffer (chop, 0, juce::jmin (written, outLen)))
            paths.add (out.getFullPathName());
    }
    return paths;
}

//==============================================================================
void CatnipProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("CatnipState");
    state.setProperty ("version", 2, nullptr);
    auto data = getSample();
    state.setProperty ("file", data != nullptr ? data->file.getFullPathName() : missingFile, nullptr);
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        juce::StringArray t;
        for (int i = 0; i < taps.count; ++i) t.add (juce::String (taps.raw[(size_t) i]));
        state.setProperty ("taps", t.joinIntoString (","), nullptr);
        state.setProperty ("tapEnd", taps.end, nullptr);
        state.setProperty ("viewStart", viewStart, nullptr);
        state.setProperty ("viewLength", viewLength, nullptr);
    }
    state.setProperty ("selected", selectedChop.load(), nullptr);

    for (int i = 0; i < maxChops; ++i)
    {
        juce::ValueTree c ("Chop");
        c.setProperty ("reverse", flags[(size_t) i].reverse.load(), nullptr);
        c.setProperty ("gate", flags[(size_t) i].gate.load(), nullptr);
        state.appendChild (c, nullptr);
    }
    state.appendChild (apvts.copyState(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void CatnipProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType ("CatnipState")) return;

    if (auto params = state.getChildWithName (apvts.state.getType()); params.isValid())
        apvts.replaceState (params);

    const juce::File file (state.getProperty ("file").toString());
    juce::String error;
    if (file.existsAsFile() && loadFile (file, error))
    {
        if ((int) state.getProperty ("version", 1) >= 2)
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            const juce::int64 len = sample->buffer.getNumSamples();
            juce::StringArray t;
            t.addTokens (state.getProperty ("taps").toString(), ",", {});
            taps.count = 0;
            taps.end = juce::jlimit<juce::int64> (1, len, (juce::int64) state.getProperty ("tapEnd", len));
            for (auto& x : t)
            {
                const auto v = x.getLargeIntValue();
                if (x.isEmpty() || taps.count >= maxChops || v < 0 || v >= taps.end) continue;
                if (taps.count > 0 && v <= taps.raw[(size_t) taps.count - 1]) continue;
                taps.raw[(size_t) taps.count++] = v;
            }
            refreshTargets();
            tapsThisPass = 0;

            const auto vl = (juce::int64) state.getProperty ("viewLength", len);
            viewLength = juce::jlimit<juce::int64> (juce::jmin<juce::int64> (len, (juce::int64) (sample->sampleRate * 0.5)), len, vl);
            viewStart = juce::jlimit<juce::int64> (0, len - viewLength, (juce::int64) state.getProperty ("viewStart", 0));
        }

        int ci = 0;
        for (auto c : state)
        {
            if (! c.hasType ("Chop") || ci >= maxChops) continue;
            auto& f = flags[(size_t) ci++];
            f.reverse = (bool) c.getProperty ("reverse", false);
            f.gate = (bool) c.getProperty ("gate", false);
        }
    }
    else if (file != juce::File())
    {
        missingFile = file.getFullPathName();
    }

    selectedChop = juce::jlimit (0, juce::jmax (0, getNumChops() - 1), (int) state.getProperty ("selected", 0));
    changed();
}

juce::AudioProcessorEditor* CatnipProcessor::createEditor()
{
    return new CatnipEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CatnipProcessor();
}
