#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

namespace catnip
{
    /** AGE modes: the character of classic samplers. */
    enum class Age { clean = 0, nineties, sp1200, mpc60, count };

    inline const char* ageName (int mode)
    {
        static const char* names[] = { "CLEAN", "90s", "SP-1200", "MPC60" };
        return names[juce::jlimit (0, 3, mode)];
    }

    inline const char* ageDetail (int mode)
    {
        static const char* details[] = { "ORIGINAL", "12 BIT \xc2\xb7 32 KHZ \xc2\xb7 WEICH", "12 BIT \xc2\xb7 26 KHZ \xc2\xb7 CRUNCH", "12 BIT \xc2\xb7 40 KHZ \xc2\xb7 WARM" };
        return details[juce::jlimit (0, 3, mode)];
    }

    /** RBJ low-pass biquad, transposed direct form II. */
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;

        void setLowPass (double sampleRate, double freq, double q)
        {
            freq = juce::jlimit (10.0, sampleRate * 0.49, freq);
            const double w = juce::MathConstants<double>::twoPi * freq / sampleRate;
            const double cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
            const double a0 = 1.0 + alpha;
            b0 = (1.0 - cw) * 0.5 / a0;
            b1 = (1.0 - cw) / a0;
            b2 = b0;
            a1 = -2.0 * cw / a0;
            a2 = (1.0 - alpha) / a0;
        }
        void reset() { z1 = z2 = 0.0; }
        inline float process (float x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
    };

    /** One AGE voicing: anti-alias filter -> sample & hold at the old rate -> bit crush -> drive -> output filter. */
    struct AgeStage
    {
        double targetRate = 44100.0, bits = 16.0, drive = 0.0;
        bool preFilter = true;
        std::array<Biquad, 2> pre, post;
        std::array<double, 2> phase {}, held {};

        void prepare (int mode, double sampleRate)
        {
            double preHz = 20000.0, postHz = 20000.0;
            switch ((Age) mode)
            {
                case Age::nineties: targetRate = 32000.0; bits = 12.0; drive = 0.15; preFilter = true;  preHz = 11000.0; postHz = 10000.0; break;
                case Age::sp1200:   targetRate = 26040.0; bits = 12.0; drive = 0.35; preFilter = false; preHz = 20000.0; postHz = 15000.0; break;
                case Age::mpc60:    targetRate = 40000.0; bits = 12.0; drive = 0.55; preFilter = true;  preHz = 16000.0; postHz = 14000.0; break;
                case Age::clean: case Age::count: break;
            }
            for (auto& f : pre)  { f.setLowPass (sampleRate, preHz, 0.707); f.reset(); }
            for (auto& f : post) { f.setLowPass (sampleRate, postHz, 0.707); f.reset(); }
            phase = {};
            held = {};
            step = targetRate / sampleRate;
        }

        inline float process (int ch, float x)
        {
            if (preFilter) x = pre[(size_t) ch].process (x);
            phase[(size_t) ch] += step;
            if (phase[(size_t) ch] >= 1.0)
            {
                phase[(size_t) ch] -= std::floor (phase[(size_t) ch]);
                const double q = std::pow (2.0, bits - 1.0);
                held[(size_t) ch] = std::round ((double) x * q) / q;   // 12-bit steps
            }
            double y = held[(size_t) ch];
            if (drive > 0.0)
            {
                const double k = 1.0 + drive;
                y = std::tanh (y * k) / k;                              // warm saturation, same level for quiet parts
            }
            return post[(size_t) ch].process ((float) y);
        }

    private:
        double step = 1.0;
    };

    /** AGE + TONE: the colour stage after all voices. Real-time safe, no allocation. */
    struct ColorFx
    {
        void prepare (double rate)
        {
            sampleRate = rate > 0.0 ? rate : 44100.0;
            for (int m = 0; m < (int) Age::count; ++m) stages[(size_t) m].prepare (m, sampleRate);
            for (auto& f : tone) f.reset();
            smoothedTone = -1.0;
            current = -1;
            fadeFrom = -1;
        }

        /** mode: Age, toneAmount: 0 (dark) .. 1 (open). */
        void process (juce::AudioBuffer<float>& buffer, int start, int num, int mode, float toneAmount)
        {
            mode = juce::jlimit (0, (int) Age::count - 1, mode);
            if (current < 0) current = mode;
            if (mode != current)
            {
                stages[(size_t) mode].prepare (mode, sampleRate);   // fresh state, then crossfade 15 ms
                fadeFrom = current;
                current = mode;
                fadePos = 0;
            }
            const int fadeLen = std::max (1, (int) (sampleRate * 0.015));
            const int channels = std::min (2, buffer.getNumChannels());

            for (int i = 0; i < num; ++i)
            {
                if ((i & 15) == 0) updateTone (toneAmount);
                float fade = 1.0f;
                if (fadeFrom >= 0)
                {
                    fade = (float) fadePos / (float) fadeLen;
                    if (++fadePos >= fadeLen) fadeFrom = -1;
                }
                for (int c = 0; c < channels; ++c)
                {
                    const float x = buffer.getSample (c, start + i);
                    float y = render (current, c, x);
                    if (fade < 1.0f)
                        y = y * fade + render (fadeFrom, c, x) * (1.0f - fade);
                    if (toneActive) y = tone[(size_t) c].process (y);
                    buffer.setSample (c, start + i, y);
                }
            }
        }

        static double toneFrequency (float amount)
        {
            return 250.0 * std::pow (80.0, (double) juce::jlimit (0.0f, 1.0f, amount));   // 250 Hz .. 20 kHz
        }

    private:
        float render (int mode, int ch, float x)
        {
            return mode == (int) Age::clean ? x : stages[(size_t) mode].process (ch, x);
        }

        void updateTone (float amount)
        {
            if (smoothedTone < 0.0) smoothedTone = amount;
            smoothedTone += ((double) amount - smoothedTone) * 0.08;
            if (std::abs (smoothedTone - (double) amount) < 1.0e-4) smoothedTone = amount;
            const bool active = smoothedTone < 0.995;
            if (active && ! toneActive) for (auto& f : tone) f.reset();
            toneActive = active;
            if (toneActive && std::abs (smoothedTone - lastToneSet) > 1.0e-5)
            {
                lastToneSet = smoothedTone;
                for (auto& f : tone) f.setLowPass (sampleRate, toneFrequency ((float) smoothedTone), 0.9);
            }
        }

        double sampleRate = 44100.0;
        std::array<AgeStage, (size_t) Age::count> stages;
        std::array<Biquad, 2> tone;
        double smoothedTone = -1.0, lastToneSet = -1.0;
        bool toneActive = false;
        int current = -1, fadeFrom = -1, fadePos = 0;
    };
}
