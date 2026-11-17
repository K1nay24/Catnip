#include "ChopAnalysis.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <limits>

namespace catnip
{
    namespace
    {
        constexpr int hop = 256;   // ~5.8 ms at 44.1 kHz

        float monoSample (const juce::AudioBuffer<float>& b, juce::int64 i)
        {
            const int chs = b.getNumChannels();
            if (chs == 0) return 0.0f;
            float s = 0.0f;
            for (int c = 0; c < chs; ++c)
                s += b.getSample (c, (int) i);
            return s / (float) chs;
        }

        // Moves a coarse onset to just before the first sample that is loud compared with the hit
        // that follows, so chops start tight without clipping the attack.
        juce::int64 refineOnset (const juce::AudioBuffer<float>& buffer, juce::int64 coarse)
        {
            const juce::int64 len = buffer.getNumSamples();
            const auto from = std::max<juce::int64> (0, coarse - hop);
            const auto to = std::min<juce::int64> (len, coarse + 6 * hop);
            float peak = 0.0f;
            for (auto i = from; i < to; ++i)
                peak = std::max (peak, std::abs (monoSample (buffer, i)));
            if (peak <= 0.0f)
                return coarse;

            const float threshold = peak * 0.1f;
            for (auto i = from; i < to; ++i)
                if (std::abs (monoSample (buffer, i)) >= threshold)
                    return std::max<juce::int64> (0, i - 32);   // ~0.7 ms pre-roll
            return coarse;
        }

        // Onset strength per frame: rise in loudness of the full signal plus rise in loudness of its
        // high frequencies (first difference), both log-compressed so quiet hits still count.
        struct OnsetFunctions
        {
            std::vector<float> all;   // full + high band: finds every hit
            std::vector<float> low;   // below ~150 Hz: kicks, used to find the "1" of the bar
        };

        OnsetFunctions onsetFunctions (const juce::AudioBuffer<float>& buffer, double sampleRate, int frames)
        {
            std::vector<float> full ((size_t) frames, 0.0f), high ((size_t) frames, 0.0f), lowE ((size_t) frames, 0.0f);
            float prev = 0.0f, lp = 0.0f;
            const float lpCoeff = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 150.0 / sampleRate));
            for (int f = 0; f < frames; ++f)
            {
                double e1 = 0.0, e2 = 0.0, e3 = 0.0;
                for (int j = 0; j < hop; ++j)
                {
                    const float x = monoSample (buffer, (juce::int64) f * hop + j);
                    const float d = x - prev;
                    prev = x;
                    lp += lpCoeff * (x - lp);
                    e1 += (double) x * x;
                    e2 += (double) d * d;
                    e3 += (double) lp * lp;
                }
                full[(size_t) f] = (float) std::log1p (1000.0 * std::sqrt (e1 / hop));
                high[(size_t) f] = (float) std::log1p (1000.0 * std::sqrt (e2 / hop));
                lowE[(size_t) f] = (float) std::log1p (1000.0 * std::sqrt (e3 / hop));
            }

            OnsetFunctions o;
            o.all.assign ((size_t) frames, 0.0f);
            o.low.assign ((size_t) frames, 0.0f);
            for (int f = 0; f < frames; ++f)
            {
                // before the file starts there is silence, so a hit on the very first sample counts too
                const float pf = f > 0 ? full[(size_t) f - 1] : 0.0f;
                const float ph = f > 0 ? high[(size_t) f - 1] : 0.0f;
                const float pl = f > 0 ? lowE[(size_t) f - 1] : 0.0f;
                o.all[(size_t) f] = std::max (0.0f, full[(size_t) f] - pf) + std::max (0.0f, high[(size_t) f] - ph);
                o.low[(size_t) f] = std::max (0.0f, lowE[(size_t) f] - pl);
            }
            return o;
        }

        double estimateTempo (const std::vector<float>& odf, double frameRate)
        {
            const int n = (int) std::min<size_t> (odf.size(), (size_t) (frameRate * 90.0));
            const int minLag = (int) std::floor (frameRate * 60.0 / 200.0);
            const int maxLag = (int) std::ceil (frameRate * 60.0 / 60.0);
            if (n < maxLag * 3)
                return 0.0;

            // light smoothing so beats that fall between frames still line up
            std::vector<double> x ((size_t) n);
            for (int i = 0; i < n; ++i)
                x[(size_t) i] = 0.25 * odf[(size_t) std::max (0, i - 1)] + 0.5 * odf[(size_t) i] + 0.25 * odf[(size_t) std::min (n - 1, i + 1)];
            const double mean = std::accumulate (x.begin(), x.end(), 0.0) / n;
            for (auto& v : x) v -= mean;

            auto ac = [&] (int lag)
            {
                if (lag <= 0 || lag >= n) return 0.0;
                double s = 0.0;
                for (int i = 0; i + lag < n; ++i) s += x[(size_t) i] * x[(size_t) (i + lag)];
                return s / (n - lag);
            };

            const int acLen = std::min (n - 1, 4 * maxLag + 2);
            std::vector<double> acv ((size_t) acLen + 1, 0.0);
            for (int lag = 1; lag <= acLen; ++lag) acv[(size_t) lag] = ac (lag);
            auto acAt = [&] (double lag)   // linear interpolation for fractional lags
            {
                const int i = (int) lag;
                if (i < 1 || i + 1 > acLen) return 0.0;
                const double t = lag - i;
                return acv[(size_t) i] * (1.0 - t) + acv[(size_t) i + 1] * t;
            };

            double lag = 0.0, bestScore = -1e30;
            for (double l = minLag; l <= maxLag; l += 0.25)
            {
                const double bpm = 60.0 * frameRate / l;
                const double pref = std::exp (-0.5 * std::pow (std::log2 (bpm / 110.0) / 1.0, 2.0));   // gentle bias to 70–170
                // a real beat period also repeats every 2 and 4 beats (half bar, bar)
                const double score = pref * (acAt (l) + 0.6 * acAt (2.0 * l) + 0.4 * acAt (4.0 * l));
                if (score > bestScore) { bestScore = score; lag = l; }
            }
            if (lag <= 0.0 || bestScore <= 0.0)
                return 0.0;

            double bpm = 60.0 * frameRate / lag;
            while (bpm < 70.0) bpm *= 2.0;
            while (bpm > 180.0) bpm *= 0.5;

            return bpm;
        }

        // Fine-tunes the tempo so a beat grid stays on the hits over the whole file (autocorrelation alone
        // is only accurate to ~1 %, which drifts audibly over a minute).
        double refineTempo (const std::vector<float>& odf, double frameRate, double bpm)
        {
            const int n = (int) odf.size();
            auto alignment = [&] (double period)
            {
                double best = 0.0;
                for (double phase = 0.0; phase < period; phase += 0.5)
                {
                    double s = 0.0;
                    int terms = 0;
                    for (double p = phase; p < n; p += period, ++terms)
                    {
                        const int i = (int) std::lround (p);
                        s += std::max ({ odf[(size_t) i], i > 0 ? odf[(size_t) i - 1] : 0.0f, i + 1 < n ? odf[(size_t) i + 1] : 0.0f });
                    }
                    if (terms > 0) best = std::max (best, s / terms);
                }
                return best;
            };

            double bestBpm = bpm, bestScore = -1.0;
            for (double b = bpm * 0.985; b <= bpm * 1.015; b += 0.02)
            {
                const double sc = alignment (frameRate * 60.0 / b);
                if (sc > bestScore) { bestScore = sc; bestBpm = b; }
            }
            return bestBpm;
        }

        // Beat phase that best lines up with the transients, then the beat positions of the bar that carry
        // the most low end (kick). Returns candidate bar phases in frames, within [0, 4 * period).
        std::vector<double> downbeatCandidates (const std::vector<float>& odf, const std::vector<float>& lowOdf, double periodFrames)
        {
            const int n = (int) odf.size();
            auto sumAt = [&] (const std::vector<float>& f, double start, double step)
            {
                double s = 0.0;
                for (double p = start; p < n; p += step)
                {
                    const int i = (int) std::lround (p);
                    if (i >= 0 && i < n)
                        s += std::max ({ f[(size_t) i], i > 0 ? f[(size_t) i - 1] : 0.0f, i + 1 < n ? f[(size_t) i + 1] : 0.0f });
                }
                return s;
            };

            double bestPhase = 0.0, bestScore = -1.0;
            for (double phase = 0.0; phase < periodFrames; phase += 0.5)
            {
                const double s = sumAt (odf, phase, periodFrames);
                if (s > bestScore) { bestScore = s; bestPhase = phase; }
            }

            double scores[4], maxScore = 0.0;
            for (int b = 0; b < 4; ++b)
            {
                scores[b] = sumAt (lowOdf, bestPhase + b * periodFrames, periodFrames * 4.0);
                maxScore = std::max (maxScore, scores[b]);
            }
            std::vector<double> out;
            for (int b = 0; b < 4; ++b)
                if (scores[b] >= maxScore * 0.85)
                    out.push_back (bestPhase + b * periodFrames);
            if (out.empty()) out.push_back (bestPhase);
            return out;
        }
    }

    Analysis analyse (const juce::AudioBuffer<float>& buffer, double sampleRate, int numPeaks)
    {
        Analysis a;
        const juce::int64 len = buffer.getNumSamples();
        if (len <= 0 || numPeaks <= 0)
            return a;

        // display peaks
        a.peaks.assign ((size_t) numPeaks, 0.0f);
        float maxPeak = 0.0f;
        for (int p = 0; p < numPeaks; ++p)
        {
            const auto start = (len * p) / numPeaks;
            const auto end = std::max (start + 1, (len * (p + 1)) / numPeaks);
            const auto step = std::max<juce::int64> (1, (end - start) / 2048);
            float m = 0.0f;
            for (auto i = start; i < end && i < len; i += step)
                m = std::max (m, std::abs (monoSample (buffer, i)));
            a.peaks[(size_t) p] = m;
            maxPeak = std::max (maxPeak, m);
        }
        if (maxPeak > 0.0f)
            for (auto& v : a.peaks) v /= maxPeak;

        const auto frames = (int) (len / hop);
        if (frames < 16)
            return a;

        const auto fns = onsetFunctions (buffer, sampleRate, frames);
        const auto& odf = fns.all;
        const float odfMax = *std::max_element (odf.begin(), odf.end());

        // adaptive threshold: local average (±16 frames) plus a small share of the loudest hit
        std::vector<double> prefix ((size_t) frames + 1, 0.0);
        for (int f = 0; f < frames; ++f) prefix[(size_t) f + 1] = prefix[(size_t) f] + odf[(size_t) f];
        const int radius = 16;
        const int minGapFrames = std::max (2, (int) std::lround (sampleRate * 0.045 / hop));   // 45 ms

        std::vector<Onset> found;
        int lastFrame = -1000;
        for (int f = 0; f < frames - 1; ++f)
        {
            const int lo = std::max (0, f - radius), hi = std::min (frames, f + radius + 1);
            const double localMean = (prefix[(size_t) hi] - prefix[(size_t) lo]) / (hi - lo);
            const double threshold = localMean * 1.5 + 0.06 * odfMax;
            const float v = odf[(size_t) f];
            if (v <= threshold) continue;

            bool isMax = true;
            for (int w = -3; w <= 3 && isMax; ++w)
                if (f + w >= 0 && f + w < frames && w != 0 && odf[(size_t) (f + w)] > v)
                    isMax = false;
            if (! isMax) continue;

            const float strength = (float) (v - threshold);
            if (f - lastFrame < minGapFrames && ! found.empty())
            {
                if (strength > found.back().strength)
                    found.back() = { refineOnset (buffer, std::max<juce::int64> (0, (juce::int64) f * hop - hop)), strength };
                continue;
            }
            found.push_back ({ refineOnset (buffer, std::max<juce::int64> (0, (juce::int64) f * hop - hop)), strength });
            lastFrame = f;
        }

        a.onsetsByTime = found;
        std::sort (a.onsetsByTime.begin(), a.onsetsByTime.end(), [] (const Onset& x, const Onset& y) { return x.sample < y.sample; });
        a.onsets = found;
        std::sort (a.onsets.begin(), a.onsets.end(), [] (const Onset& x, const Onset& y) { return x.strength > y.strength; });
        if (a.onsets.size() > 5000)
            a.onsets.resize (5000);

        // tempo + first downbeat
        const double frameRate = sampleRate / hop;
        a.bpm = found.size() >= 4 ? estimateTempo (odf, frameRate) : 0.0;
        if (a.bpm > 0.0)
            a.bpm = refineTempo (odf, frameRate, a.bpm);
        if (a.bpm > 0.0)
        {
            const auto tol = (juce::int64) (sampleRate * 0.04);
            auto firstDownbeatFor = [&] (double bpm)
            {
                const double period = frameRate * 60.0 / bpm;
                const double barSamples = period * 4.0 * hop;
                juce::int64 best = -1;
                // each candidate phase: its earliest bar line in the file that actually has a hit
                for (double phase : downbeatCandidates (odf, fns.low, period))
                {
                    double pos = phase * hop;
                    while (pos - barSamples >= -(double) tol) pos -= barSamples;
                    for (; pos < (double) len; pos += barSamples)
                    {
                        const auto hit = nearestOnset (a.onsetsByTime, (juce::int64) std::llround (pos), tol, tol);
                        if (hit >= 0) { if (best < 0 || hit < best) best = hit; break; }
                    }
                }
                return best >= 0 ? best : (juce::int64) 0;
            };

            a.firstDownbeat = firstDownbeatFor (a.bpm);

            // loops: if bar 1 to the end is (almost exactly) a whole number of bars, lock to that tempo
            const double playable = (double) (len - a.firstDownbeat) / sampleRate;
            if (playable > 1.0 && playable < 120.0)
            {
                const double bars = std::round (playable * a.bpm / 240.0);
                if (bars >= 1.0 && std::abs (240.0 * bars / playable - a.bpm) / a.bpm < 0.006)
                    a.bpm = 240.0 * bars / playable;
            }
        }
        else if (! a.onsetsByTime.empty())
        {
            a.firstDownbeat = a.onsetsByTime.front().sample;
        }
        return a;
    }

    static void fillGaps (std::vector<juce::int64>& b, int count)
    {
        while ((int) b.size() < count + 1)
        {
            size_t gi = 0;
            juce::int64 gw = 0;
            for (size_t q = 0; q + 1 < b.size(); ++q)
                if (b[q + 1] - b[q] > gw) { gw = b[q + 1] - b[q]; gi = q; }
            b.insert (b.begin() + (std::ptrdiff_t) gi + 1, b[gi] + gw / 2);
        }
    }

    std::vector<juce::int64> transientBounds (const std::vector<Onset>& onsets, juce::int64 length, int count)
    {
        std::vector<juce::int64> picked;
        const auto minGap = std::max<juce::int64> (256, length / (count * 3));
        const auto edge = length / 100;

        for (const auto& o : onsets)
        {
            if ((int) picked.size() >= count - 1) break;
            if (o.sample < edge || o.sample > length - edge) continue;

            bool ok = true;
            for (auto p : picked)
                if (std::llabs (p - o.sample) < minGap) { ok = false; break; }
            if (ok) picked.push_back (o.sample);
        }

        std::sort (picked.begin(), picked.end());
        std::vector<juce::int64> b { 0 };
        b.insert (b.end(), picked.begin(), picked.end());
        b.push_back (length);
        fillGaps (b, count);
        return b;
    }

    std::vector<juce::int64> gridBounds (juce::int64 length, int count)
    {
        std::vector<juce::int64> b;
        for (int i = 0; i <= count; ++i)
            b.push_back ((length * i) / count);
        return b;
    }

    std::vector<juce::int64> beatGridBounds (juce::int64 downbeat, double samplesPerChop,
                                             juce::int64 windowStart, juce::int64 windowEnd, int maxCount)
    {
        std::vector<juce::int64> b;
        if (samplesPerChop < 16.0 || windowEnd <= windowStart)
            return b;

        // first grid line at or after the window start, never before bar 1
        auto k = (juce::int64) std::ceil ((double) (windowStart - downbeat) / samplesPerChop - 1e-9);
        k = std::max<juce::int64> (0, k);

        for (int n = 0; n < maxCount; ++n, ++k)
        {
            const auto start = downbeat + (juce::int64) std::llround ((double) k * samplesPerChop);
            if (start >= windowEnd - (juce::int64) (samplesPerChop * 0.25)) break;   // ignore tiny leftovers
            b.push_back (start);
        }
        if (b.empty())
            return b;

        const auto end = downbeat + (juce::int64) std::llround ((double) k * samplesPerChop);
        b.push_back (std::min (end, windowEnd));
        return b;
    }

    juce::int64 nearestOnset (const std::vector<Onset>& byTime, juce::int64 position, juce::int64 before, juce::int64 after)
    {
        auto it = std::lower_bound (byTime.begin(), byTime.end(), position - before,
                                    [] (const Onset& o, juce::int64 p) { return o.sample < p; });
        juce::int64 best = -1, bestDist = std::numeric_limits<juce::int64>::max();
        for (; it != byTime.end() && it->sample <= position + after; ++it)
        {
            const auto d = std::llabs (it->sample - position);
            if (d < bestDist) { bestDist = d; best = it->sample; }
        }
        return best;
    }

    void snapToZeroCrossings (std::vector<juce::int64>& bounds, const juce::AudioBuffer<float>& buffer, int searchRadius)
    {
        const juce::int64 len = buffer.getNumSamples();
        for (size_t k = 1; k + 1 < bounds.size(); ++k)
        {
            const auto centre = bounds[k];
            juce::int64 best = centre, bestDist = searchRadius + 1;
            const auto lo = std::max<juce::int64> (bounds[k - 1] + 1, centre - searchRadius);
            const auto hi = std::min<juce::int64> (bounds[k + 1] - 1, centre);
            for (auto i = std::max<juce::int64> (1, lo); i <= hi && i < len; ++i)
            {
                if (monoSample (buffer, i - 1) <= 0.0f && monoSample (buffer, i) > 0.0f)
                {
                    const auto d = std::llabs (i - centre);
                    if (d < bestDist) { bestDist = d; best = i; }
                }
            }
            bounds[k] = best;
        }
    }
}
