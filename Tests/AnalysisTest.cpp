// Offline checks for the analysis: transients, tempo, downbeat and the bar grid,
// on synthesised drum loops where the right answers are known.
#include <juce_core/juce_core.h>
#include "ChopAnalysis.h"
#include <cmath>
#include <iostream>

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (! ok) ++failures;
}

// kick on 1 and 3, snare on 2 and 4, closed hats on eighths; optional leading silence and pickup hat
static juce::AudioBuffer<float> drumLoop (double sr, double bpm, int bars, double leadIn, bool pickup, juce::Array<juce::int64>& beatStarts)
{
    const double beat = 60.0 / bpm;
    const int len = (int) std::ceil (sr * (leadIn + bars * 4 * beat));
    juce::AudioBuffer<float> buf (2, len);
    buf.clear();
    juce::Random rnd (11);

    auto add = [&] (double t, int kind)
    {
        const auto start = (int) std::lround (t * sr);
        const int dur = (int) (sr * (kind == 2 ? 0.05 : 0.25));
        for (int n = 0; n < dur && start + n < len; ++n)
        {
            if (start + n < 0) continue;
            const double tt = n / sr;
            float v = 0.0f;
            if (kind == 0) v = (float) (0.9 * std::exp (-tt * 18.0) * std::sin (2.0 * juce::MathConstants<double>::pi * (55.0 + 90.0 * std::exp (-tt * 40.0)) * tt));
            if (kind == 1) v = (float) (0.6 * std::exp (-tt * 25.0) * (rnd.nextFloat() * 2.0f - 1.0f));
            if (kind == 2) v = (float) (0.18 * std::exp (-tt * 90.0) * (rnd.nextFloat() * 2.0f - 1.0f));
            buf.addSample (0, start + n, v);
            buf.addSample (1, start + n, v);
        }
    };

    if (pickup) add (leadIn - beat * 0.5, 2);
    for (int b = 0; b < bars * 4; ++b)
    {
        const double t = leadIn + b * beat;
        beatStarts.add ((juce::int64) std::lround (t * sr));
        add (t, (b % 2 == 0) ? 0 : 1);
        add (t, 2);
        add (t + beat * 0.5, 2);
    }
    return buf;
}

int main()
{
    const double sr = 44100.0;

    std::cout << "exact 4-bar loop at 92 BPM\n";
    {
        juce::Array<juce::int64> beats;
        auto buf = drumLoop (sr, 92.0, 4, 0.0, false, beats);
        const auto a = catnip::analyse (buf, sr, 160);
        check (std::abs (a.bpm - 92.0) < 0.5, "tempo " + juce::String (a.bpm, 2) + " BPM (expected 92)");
        check (std::llabs (a.firstDownbeat - 0) < (juce::int64) (sr * 0.02), "first downbeat at " + juce::String ((double) a.firstDownbeat / sr, 3) + " s (expected 0)");

        int found = 0;
        for (auto b : beats) found += catnip::nearestOnset (a.onsetsByTime, b, (juce::int64) (sr * 0.015), (juce::int64) (sr * 0.015)) >= 0 ? 1 : 0;
        check (found == beats.size(), "every beat has a transient within 15 ms (" + juce::String (found) + "/" + juce::String (beats.size()) + ")");

        const double bar = sr * 240.0 / a.bpm;
        const auto grid = catnip::beatGridBounds (a.firstDownbeat, bar, 0, buf.getNumSamples(), 16);
        check (grid.size() == 5, "bar grid gives 4 chops for 4 bars (got " + juce::String ((int) grid.size() - 1) + ")");
        const auto beatGrid = catnip::beatGridBounds (a.firstDownbeat, bar / 4.0, 0, buf.getNumSamples(), 16);
        check (beatGrid.size() == 17, "beat grid gives 16 chops");
    }

    std::cout << "8 bars at 97 BPM after 0.4 s silence and a pickup hat\n";
    {
        juce::Array<juce::int64> beats;
        auto buf = drumLoop (sr, 97.0, 8, 0.4, true, beats);
        const auto a = catnip::analyse (buf, sr, 160);
        check (std::abs (a.bpm - 97.0) < 0.15, "tempo " + juce::String (a.bpm, 2) + " BPM (expected 97)");
        check (std::llabs (a.firstDownbeat - beats[0]) < (juce::int64) (sr * 0.02),
               "first downbeat at " + juce::String ((double) a.firstDownbeat / sr, 3) + " s (expected 0.400, pickup ignored)");

        const double half = sr * 120.0 / a.bpm;
        const auto grid = catnip::beatGridBounds (a.firstDownbeat, half, 0, buf.getNumSamples(), 16);
        bool onBeats = grid.size() == 17;
        for (size_t k = 0; k + 1 < grid.size() && onBeats; ++k)
            onBeats = std::llabs (grid[k] - beats[(int) k * 2]) < (juce::int64) (sr * 0.02);
        check (onBeats, "half-bar chops start on beats 1 and 3 of each bar");

        // zoomed window from bar 3: first chop on bar 3, not before
        const auto zoomed = catnip::beatGridBounds (a.firstDownbeat, sr * 240.0 / a.bpm, beats[8] - 2000, buf.getNumSamples(), 16);
        check (! zoomed.empty() && std::llabs (zoomed.front() - beats[8]) < (juce::int64) (sr * 0.02), "zoomed bar grid starts on the next downbeat");
    }

    std::cout << "12 bars at 87 BPM, 1.3 s lead-in, file cut mid-bar\n";
    {
        juce::Array<juce::int64> beats;
        auto full = drumLoop (sr, 87.0, 12, 1.3, true, beats);
        juce::AudioBuffer<float> buf (2, full.getNumSamples() - (int) (sr * 0.9));
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, full, c, 0, buf.getNumSamples());
        const auto a = catnip::analyse (buf, sr, 160);
        check (std::abs (a.bpm - 87.0) < 0.15, "tempo " + juce::String (a.bpm, 2) + " BPM (expected 87)");
        check (std::llabs (a.firstDownbeat - beats[0]) < (juce::int64) (sr * 0.02),
               "first downbeat at " + juce::String ((double) a.firstDownbeat / sr, 3) + " s (expected 1.300)");
    }

    std::cout << "irregular hits (no steady tempo)\n";
    {
        const int len = (int) (sr * 4.0);
        juce::AudioBuffer<float> buf (2, len);
        buf.clear();
        juce::Random rnd (7);
        std::vector<int> hits;
        int pos = 0;
        for (int i = 0; i < 16; ++i) { hits.push_back (pos); pos += (int) (sr * (0.18 + rnd.nextDouble() * 0.12)); }
        for (auto h : hits)
            for (int n = 0; n < (int) (sr * 0.15) && h + n < len; ++n)
            {
                const float v = (float) (std::exp (-n / sr * 30.0) * (0.6 * std::sin (n * 0.02) + 0.4 * (rnd.nextFloat() * 2.0f - 1.0f)));
                buf.addSample (0, h + n, v); buf.addSample (1, h + n, v);
            }
        const auto a = catnip::analyse (buf, sr, 160);
        auto bounds = catnip::transientBounds (a.onsets, len, 16);
        catnip::snapToZeroCrossings (bounds, buf);
        int matched = 0;
        for (size_t k = 1; k < hits.size(); ++k)
        {
            juce::int64 best = len;
            for (auto b : bounds) best = std::min<juce::int64> (best, std::llabs (b - hits[k]));
            matched += best < 256 ? 1 : 0;
        }
        check (matched == 15, "auto-chop lands on all 15 hits within 6 ms (" + juce::String (matched) + "/15)");
        check (bounds.size() == 17, "16 chops");
    }

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (failures)) << "\n";
    return failures == 0 ? 0 : 1;
}
