#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace catnip;

CatnipProcessor::CatnipProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Params", createLayout())
{
    volumeParam = apvts.getRawParameterValue ("volume");
}

juce::AudioProcessorValueTreeState::ParameterLayout CatnipProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
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
}

//==============================================================================
void CatnipProcessor::startVoice (int chop, float velocity)
{
    if (sample == nullptr || chop < 0 || chop + 1 >= (int) bounds.size())
        return;

    // a pad always takes over from the preview
    if (preview.active && ! preview.releasing)
    {
        preview.releasing = true;
        preview.releaseStep = (float) (1.0 / std::max (1.0, hostRate * 0.004));
    }

    const auto start = bounds[(size_t) chop];
    const auto end = bounds[(size_t) chop + 1];
    const auto len = (double) (end - start);
    if (len < 2.0) return;

    // Choke: a new chop cuts every sound that is still playing (4 ms fade, no click, no overlap).
    const auto chokeStep = (float) (1.0 / std::max (1.0, hostRate * 0.004));
    for (auto& other : voices)
        if (other.active && ! (other.releasing && other.releaseStep >= chokeStep))
        {
            other.releasing = true;
            other.releaseStep = chokeStep;
        }

    // take a free voice; if all are still fading, reuse the one closest to silence
    Voice* slot = nullptr;
    for (auto& candidate : voices)
        if (! candidate.active) { slot = &candidate; break; }
    if (slot == nullptr)
    {
        slot = &voices[0];
        for (auto& candidate : voices)
            if (candidate.release < slot->release) slot = &candidate;
    }

    auto& p = chops[(size_t) chop];
    auto& v = *slot;
    v.chop = chop;
    v.start = start;
    v.end = end;
    v.reverse = p.reverse.load();
    v.total = len * juce::jlimit (0.05, 1.0, (double) p.decay.load());
    v.increment = (sample->sampleRate / hostRate) * std::pow (2.0, p.pitch.load() / 12.0);
    v.played = 0.0;

    const double minFade = sample->sampleRate * 0.004;
    v.fadeLen = p.decay.load() < 0.999f ? std::max (minFade, v.total * 0.3) : std::min (minFade, v.total * 0.25);

    v.gain = p.level.load() * (0.3f + 0.7f * juce::jlimit (0.0f, 1.0f, velocity));
    v.attack = 0.0f;
    v.attackStep = (float) (1.0 / std::max (1.0, hostRate * 0.002));
    v.release = 1.0f;
    v.releaseStep = 0.0f;
    v.releasing = false;
    v.active = true;

    hitCounters[(size_t) chop].fetch_add (1);
}

void CatnipProcessor::stopVoice (int chop)
{
    if (chop < 0 || chop >= maxChops || ! chops[(size_t) chop].gate.load()) return;
    for (auto& v : voices)
        if (v.active && v.chop == chop && ! v.releasing)
        {
            v.releasing = true;
            v.releaseStep = (float) (1.0 / std::max (1.0, hostRate * 0.015));
        }
}

int CatnipProcessor::getActiveVoiceCount() const noexcept
{
    int n = 0;
    for (auto& v : voices)
        if (v.active && ! v.releasing) ++n;
    return n;
}

void CatnipProcessor::renderVoices (juce::AudioBuffer<float>& out, int startSample, int numSamples)
{
    if (sample == nullptr) return;

    const auto& src = sample->buffer;
    const int srcChannels = src.getNumChannels();
    const juce::int64 srcLen = src.getNumSamples();
    const int outChannels = out.getNumChannels();

    for (auto& v : voices)
    {
        if (! v.active) continue;

        for (int n = 0; n < numSamples; ++n)
        {
            if (v.played >= v.total) { v.active = false; break; }

            float amp = v.gain;
            if (v.attack < 1.0f) { v.attack = std::min (1.0f, v.attack + v.attackStep); amp *= v.attack; }
            const double remaining = v.total - v.played;
            if (remaining < v.fadeLen) amp *= (float) (remaining / v.fadeLen);
            if (v.releasing)
            {
                v.release -= v.releaseStep;
                if (v.release <= 0.0f) { v.active = false; break; }
                amp *= v.release;
            }

            const double pos = v.reverse ? (double) (v.end - 1) - v.played : (double) v.start + v.played;
            const auto i0 = (juce::int64) std::floor (pos);
            const auto frac = (float) (pos - (double) i0);
            const auto a = juce::jlimit<juce::int64> (0, srcLen - 1, i0);
            const auto b = juce::jlimit<juce::int64> (0, srcLen - 1, i0 + 1);

            for (int c = 0; c < outChannels; ++c)
            {
                const int sc = std::min (c, srcChannels - 1);
                const float s0 = src.getSample (sc, (int) a);
                const float s1 = src.getSample (sc, (int) b);
                const float s = s0 + (s1 - s0) * frac;
                out.addSample (c, startSample + n, s * amp);
            }

            v.played += v.increment;
        }
    }
}

void CatnipProcessor::renderPreview (juce::AudioBuffer<float>& out)
{
    if (! preview.active || sample == nullptr)
    {
        previewPosition = -1;
        return;
    }

    const auto& src = sample->buffer;
    const int srcChannels = src.getNumChannels();
    const auto attackStep = (float) (1.0 / std::max (1.0, hostRate * 0.003));
    for (int n = 0; n < out.getNumSamples(); ++n)
    {
        const double remaining = (double) preview.end - preview.position;
        if (remaining <= 1.0) { preview.active = false; break; }

        float amp = 0.8f;
        if (preview.attack < 1.0f) { preview.attack = std::min (1.0f, preview.attack + attackStep); amp *= preview.attack; }
        const double fade = sample->sampleRate * 0.004;
        if (remaining < fade) amp *= (float) (remaining / fade);
        if (preview.releasing)
        {
            preview.release -= preview.releaseStep;
            if (preview.release <= 0.0f) { preview.active = false; break; }
            amp *= preview.release;
        }

        const auto i0 = (juce::int64) preview.position;
        const auto frac = (float) (preview.position - (double) i0);
        const auto i1 = juce::jmin<juce::int64> (i0 + 1, src.getNumSamples() - 1);
        for (int c = 0; c < out.getNumChannels(); ++c)
        {
            const int sc = std::min (c, srcChannels - 1);
            const float s0 = src.getSample (sc, (int) i0), s1 = src.getSample (sc, (int) i1);
            out.addSample (c, n, (s0 + (s1 - s0) * frac) * amp);
        }
        preview.position += preview.increment;
    }
    previewPosition = preview.active && ! preview.releasing ? (juce::int64) preview.position : -1;
}

void CatnipProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // merge pads / computer keyboard from the editor into the incoming MIDI
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (! sl.isLocked())
        return;

    // preview (whole sample / window playback for listening and tapping)
    if (previewStopRequest.exchange (false) && preview.active)
    {
        preview.releasing = true;
        preview.releaseStep = (float) (1.0 / std::max (1.0, hostRate * 0.004));
    }
    if (const auto req = previewStartRequest.exchange (-1); req >= 0 && sample != nullptr)
    {
        preview.active = true;
        preview.releasing = false;
        preview.release = 1.0f;
        preview.position = (double) req;
        preview.end = juce::jmin<juce::int64> (previewEndRequest.load(), sample->buffer.getNumSamples());
        preview.increment = sample->sampleRate / hostRate;
        preview.attack = 0.0f;
    }

    int pos = 0;
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const int when = juce::jlimit (0, buffer.getNumSamples(), meta.samplePosition);
        if (when > pos) { renderVoices (buffer, pos, when - pos); pos = when; }

        const int chop = msg.getNoteNumber() - firstNote;
        if (msg.isNoteOn())
            startVoice (chop, msg.getFloatVelocity());
        else if (msg.isNoteOff())
            stopVoice (chop);
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            for (auto& v : voices) v.active = false;
    }
    if (pos < buffer.getNumSamples())
        renderVoices (buffer, pos, buffer.getNumSamples() - pos);

    renderPreview (buffer);

    buffer.applyGain (juce::Decibels::decibelsToGain (volumeParam->load()));
}

//==============================================================================
double CatnipProcessor::samplesPerChop() const
{
    if (sample == nullptr) return 0.0;
    const double beat = sample->sampleRate * 60.0 / juce::jmax (20.0, bpm);
    return beat * (division == Division::bar ? 4.0 : division == Division::half ? 2.0 : 1.0);
}

void CatnipProcessor::rechop()
{
    if (sample == nullptr) { bounds.clear(); return; }
    const juce::int64 len = sample->buffer.getNumSamples();
    if (viewLength <= 0 || viewLength > len) viewLength = len;
    viewStart = juce::jlimit<juce::int64> (0, len - viewLength, viewStart);

    const auto a = viewStart, windowLen = viewLength, b = viewStart + viewLength;
    const int count = chopCount.load();
    const auto m = mode.load();

    if (m == ChopMode::bars)
    {
        bounds = beatGridBounds (gridOrigin, samplesPerChop(), a, b, count);
        if (bounds.empty())                       // window shorter than one chop
            bounds = { a, b };
    }
    else if (m == ChopMode::tap && ! taps.empty())
    {
        bounds = taps;
        const auto last = taps.back();
        bounds.push_back (last < b ? b : len);    // last chop runs to the end of the window
    }
    else
    {
        // only the transients inside the visible window, relative to its start
        std::vector<Onset> local;
        for (const auto& o : sample->analysis.onsets)
            if (o.sample > a && o.sample < b)
                local.push_back ({ o.sample - a, o.strength });
        bounds = transientBounds (local, windowLen, count);
        for (auto& x : bounds) x += a;
        snapToZeroCrossings (bounds, sample->buffer);
    }

    // fade out whatever is playing instead of cutting it hard
    for (auto& v : voices)
        if (v.active) { v.releasing = true; v.releaseStep = (float) (1.0 / std::max (1.0, hostRate * 0.004)); }
}

int CatnipProcessor::getNumChops() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return juce::jmax (0, (int) bounds.size() - 1);
}

void CatnipProcessor::setMode (ChopMode newMode)
{
    if (newMode == mode.load()) return;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        mode = newMode;
        rechop();
    }
    selectedChop = 0;
    sendChangeMessage();
}

void CatnipProcessor::setDivision (Division d)
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (d == division) return;
        division = d;
        rechop();
    }
    sendChangeMessage();
}

void CatnipProcessor::setBpm (double newBpm)
{
    newBpm = juce::jlimit (40.0, 250.0, newBpm);
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (std::abs (newBpm - bpm) < 1e-6) return;
        bpm = newBpm;
        rechop();
    }
    sendChangeMessage();
}

double CatnipProcessor::getBpm() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return bpm;
}

double CatnipProcessor::getDetectedBpm() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return sample != nullptr ? sample->analysis.bpm : 0.0;
}

void CatnipProcessor::resetBpm()
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (sample == nullptr) return;
        bpm = sample->analysis.bpm > 0.0 ? sample->analysis.bpm : 120.0;
        gridOrigin = sample->analysis.firstDownbeat;
        rechop();
    }
    sendChangeMessage();
}

void CatnipProcessor::shiftGrid (juce::int64 delta)
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (sample == nullptr) return;
        // keep bar 1 inside the file: wrap the origin by whole chops
        const double spc = samplesPerChop();
        auto origin = gridOrigin + delta;
        while (spc > 0.0 && origin < 0) origin += (juce::int64) std::llround (spc);
        gridOrigin = juce::jmin<juce::int64> (origin, sample->buffer.getNumSamples() - 1);
        rechop();
    }
    sendChangeMessage();
}

juce::int64 CatnipProcessor::getGridOrigin() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return gridOrigin;
}

//==============================================================================
void CatnipProcessor::startPreview (juce::int64 from)
{
    auto s = getSample();
    if (s == nullptr) return;
    const auto view = getView();
    previewEndRequest = view.first + view.second;
    previewStartRequest = juce::jlimit<juce::int64> (0, view.first + view.second - 1, from < 0 ? view.first : from);
    previewPosition = previewStartRequest.load();   // so a tap right away already has a position
}

void CatnipProcessor::stopPreview()
{
    previewStartRequest = -1;
    previewStopRequest = true;
    previewPosition = -1;
    tapping = false;
}

void CatnipProcessor::startTapping()
{
    if (getSample() == nullptr) return;
    if (mode.load() != ChopMode::tap) setMode (ChopMode::tap);
    tapping = true;
    tapsThisPass = 0;
    startPreview (-1);
    sendChangeMessage();
}

int CatnipProcessor::tap()
{
    if (! tapping) return -1;
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
        const double sr = sample->sampleRate;

        // people tap a little after they hear the hit: look back further than forward
        const auto hit = nearestOnset (sample->analysis.onsetsByTime, position, (juce::int64) (sr * 0.15), (juce::int64) (sr * 0.04));
        const auto at = hit >= 0 ? hit : position;

        if (tapsThisPass == 0)
            taps.clear();                          // first tap of a pass replaces the old chops
        if (! taps.empty() && at <= taps.back() + (juce::int64) (sr * 0.05))
            return -1;                             // double tap on the same hit
        taps.push_back (at);
        ++tapsThisPass;
        index = (int) taps.size() - 1;
        full = (int) taps.size() >= chopCount.load();
        rechop();
    }
    selectedChop = index;
    hitCounters[(size_t) index].fetch_add (1);
    if (full)
        stopPreview();
    sendChangeMessage();
    return index;
}

void CatnipProcessor::clearTaps()
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        taps.clear();
        rechop();
    }
    sendChangeMessage();
}

std::pair<juce::int64, juce::int64> CatnipProcessor::getView() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return { viewStart, viewLength };
}

juce::int64 CatnipProcessor::minViewLength() const
{
    auto s = getSample();
    if (s == nullptr) return 1;
    // never zoom closer than ~0.25 s or 64 samples per chop
    return juce::jmin<juce::int64> (s->buffer.getNumSamples(),
                                    juce::jmax<juce::int64> ((juce::int64) (s->sampleRate * 0.25), 64 * maxChops));
}

void CatnipProcessor::setView (juce::int64 start, juce::int64 length)
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (sample == nullptr) return;
        const juce::int64 len = sample->buffer.getNumSamples();
        const auto minLen = juce::jmin<juce::int64> (len, juce::jmax<juce::int64> ((juce::int64) (sample->sampleRate * 0.25), 64 * maxChops));
        length = juce::jlimit (minLen, len, length);
        start = juce::jlimit<juce::int64> (0, len - length, start);
        if (start == viewStart && length == viewLength) return;
        viewStart = start;
        viewLength = length;
        rechop();
    }
    sendChangeMessage();
}

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
        error = juce::String (juce::CharPointer_UTF8 ("ZU LANG – MAX. 10 MINUTEN"));
        return false;
    }

    auto data = std::make_shared<SampleData>();
    const int channels = (int) juce::jlimit (1u, 2u, reader->numChannels);
    data->buffer.setSize (channels, (int) len);
    reader->read (&data->buffer, 0, (int) len, 0, true, channels > 1);
    data->sampleRate = reader->sampleRate;
    data->file = file;
    data->analysis = analyse (data->buffer, data->sampleRate, numPeaks);

    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (sample, data);
        viewStart = 0;
        viewLength = 0;   // whole file
        bpm = sample->analysis.bpm > 0.0 ? sample->analysis.bpm : 120.0;
        gridOrigin = sample->analysis.firstDownbeat;
        taps.clear();
        preview.active = false;
        rechop();
    }
    missingFile = {};
    selectedChop = 0;
    tapping = false;
    previewPosition = -1;
    sendChangeMessage();
    return true;   // old sample (now in `data`) is released here, off the audio thread
}

std::shared_ptr<const SampleData> CatnipProcessor::getSample() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return sample;
}

std::vector<juce::int64> CatnipProcessor::getBounds() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return bounds;
}

void CatnipProcessor::setBound (int index, juce::int64 position)
{
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        if (index <= 0 || index + 1 >= (int) bounds.size()) return;
        const juce::int64 minGap = 256;
        bounds[(size_t) index] = juce::jlimit (bounds[(size_t) index - 1] + minGap,
                                               bounds[(size_t) index + 1] - minGap, position);
        if (mode.load() == ChopMode::tap && index < (int) taps.size())
            taps[(size_t) index] = bounds[(size_t) index];
    }
    sendChangeMessage();
}

void CatnipProcessor::setChopCount (int newCount)
{
    newCount = newCount <= 8 ? 8 : 16;
    if (newCount == chopCount.load()) return;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        chopCount = newCount;
        rechop();
    }
    selectedChop = juce::jmin (selectedChop.load(), juce::jmax (0, getNumChops() - 1));
    sendChangeMessage();
}

juce::StringArray CatnipProcessor::exportChopsToTempFiles()
{
    juce::StringArray paths;
    auto data = getSample();
    auto b = getBounds();
    if (data == nullptr || b.size() < 2) return paths;

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Catnip Chops");
    dir.createDirectory();
    const auto base = juce::File::createLegalFileName (data->file.getFileNameWithoutExtension());

    juce::WavAudioFormat wav;
    const int count = (int) b.size() - 1;
    for (int i = 0; i < count; ++i)
    {
        const auto start = b[(size_t) i];
        const auto len = (int) (b[(size_t) i + 1] - start);
        if (len <= 0) continue;

        juce::AudioBuffer<float> chop (data->buffer.getNumChannels(), len);
        for (int c = 0; c < chop.getNumChannels(); ++c)
            chop.copyFrom (c, 0, data->buffer, c, (int) start, len);
        if (chops[(size_t) i].reverse.load())
            chop.reverse (0, len);
        const int fade = juce::jmin (len / 4, (int) (data->sampleRate * 0.003));
        if (fade > 0)
        {
            chop.applyGainRamp (0, fade, 0.0f, 1.0f);
            chop.applyGainRamp (len - fade, fade, 1.0f, 0.0f);
        }

        auto out = dir.getChildFile (base + "_chop" + juce::String (i + 1).paddedLeft ('0', 2) + ".wav");
        out.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (out);
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate (data->sampleRate)
                                                       .withNumChannels (chop.getNumChannels())
                                                       .withBitsPerSample (24));
        if (writer != nullptr && writer->writeFromAudioSampleBuffer (chop, 0, len))
            paths.add (out.getFullPathName());
    }
    return paths;
}

//==============================================================================
void CatnipProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("CatnipState");
    auto data = getSample();
    state.setProperty ("file", data != nullptr ? data->file.getFullPathName() : missingFile, nullptr);
    state.setProperty ("count", chopCount.load(), nullptr);
    state.setProperty ("mode", (int) mode.load(), nullptr);
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        state.setProperty ("division", (int) division, nullptr);
        state.setProperty ("bpm", bpm, nullptr);
        state.setProperty ("gridOrigin", gridOrigin, nullptr);
        juce::StringArray t;
        for (auto x : taps) t.add (juce::String (x));
        state.setProperty ("taps", t.joinIntoString (","), nullptr);
    }
    {
        const auto view = getView();
        state.setProperty ("viewStart", view.first, nullptr);
        state.setProperty ("viewLength", view.second, nullptr);
    }
    state.setProperty ("selected", selectedChop.load(), nullptr);

    juce::StringArray b;
    for (auto v : getBounds()) b.add (juce::String (v));
    state.setProperty ("bounds", b.joinIntoString (","), nullptr);

    for (int i = 0; i < maxChops; ++i)
    {
        auto& p = chops[(size_t) i];
        juce::ValueTree c ("Chop");
        c.setProperty ("pitch", p.pitch.load(), nullptr);
        c.setProperty ("decay", p.decay.load(), nullptr);
        c.setProperty ("level", p.level.load(), nullptr);
        c.setProperty ("reverse", p.reverse.load(), nullptr);
        c.setProperty ("gate", p.gate.load(), nullptr);
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

    int ci = 0;
    for (auto c : state)
    {
        if (! c.hasType ("Chop") || ci >= maxChops) continue;
        auto& p = chops[(size_t) ci++];
        p.pitch = (float) c.getProperty ("pitch", 0.0f);
        p.decay = (float) c.getProperty ("decay", 1.0f);
        p.level = (float) c.getProperty ("level", 0.8f);
        p.reverse = (bool) c.getProperty ("reverse", false);
        p.gate = (bool) c.getProperty ("gate", false);
    }

    chopCount = (int) state.getProperty ("count", 16) <= 8 ? 8 : 16;
    mode = (ChopMode) juce::jlimit (0, 2, (int) state.getProperty ("mode", 0));

    const juce::File file (state.getProperty ("file").toString());
    juce::String error;
    if (file.existsAsFile() && loadFile (file, error))
    {
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            division = (Division) juce::jlimit (0, 2, (int) state.getProperty ("division", 0));
            if (state.hasProperty ("bpm")) bpm = juce::jlimit (40.0, 250.0, (double) state.getProperty ("bpm"));
            if (state.hasProperty ("gridOrigin")) gridOrigin = (juce::int64) state.getProperty ("gridOrigin");
            juce::StringArray t;
            t.addTokens (state.getProperty ("taps").toString(), ",", {});
            taps.clear();
            for (auto& x : t) if (x.isNotEmpty()) taps.push_back (x.getLargeIntValue());
            rechop();
        }
        const auto savedLength = (juce::int64) state.getProperty ("viewLength", 0);
        if (savedLength > 0)
            setView ((juce::int64) state.getProperty ("viewStart", 0), savedLength);

        // restore hand-edited chop points if they still fit the file
        juce::StringArray parts;
        parts.addTokens (state.getProperty ("bounds").toString(), ",", {});
        if (parts.size() >= 2)
        {
            std::vector<juce::int64> restored;
            for (auto& s : parts) restored.push_back (s.getLargeIntValue());
            const juce::SpinLock::ScopedLockType sl (lock);
            if (sample != nullptr && restored.front() == viewStart && restored.back() == viewStart + viewLength
                && std::is_sorted (restored.begin(), restored.end()))
                bounds = restored;
        }
    }
    else if (file != juce::File())
    {
        missingFile = file.getFullPathName();
    }

    selectedChop = juce::jlimit (0, juce::jmax (0, getNumChops() - 1), (int) state.getProperty ("selected", 0));
    sendChangeMessage();
}

juce::AudioProcessorEditor* CatnipProcessor::createEditor()
{
    return new CatnipEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CatnipProcessor();
}
