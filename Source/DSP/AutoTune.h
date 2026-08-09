#pragma once

// QUARANTINED — not on the AFTERIMAGE shipping path (see docs/CLEANUP_AUDIT.md,
// docs/HARMONICS_ENGINE.md). Retained for git history / research only.
// Do not include from PluginProcessor.

#include "ScaleTheory.h"
#include "../Utilities/Constants.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cmath>
#include <vector>

namespace afterimage
{

/**
    LEGACY conventional auto-tune (autocorr + delay-line). Removed from product.
*/
class AutoTune
{
public:
    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        sampleRate_ = std::max (1.0, sampleRate);
        numChannels_ = juce::jmax (1, juce::jmin (2, numChannels));
        maxBlock_ = juce::jmax (1, maxBlock);

        const int delayLen = juce::nextPowerOfTwo ((int) (sampleRate_ * 0.06) + maxBlock_ * 2 + 8);
        delayMask_ = delayLen - 1;
        for (int ch = 0; ch < 2; ++ch)
        {
            delay_[static_cast<size_t> (ch)].assign (static_cast<size_t> (delayLen), 0.0f);
            writePos_[static_cast<size_t> (ch)] = 0;
            readPos_[static_cast<size_t> (ch)] = (float) ((int) (sampleRate_ * 0.02));
        }

        corrBuf_.assign (static_cast<size_t> ((int) (sampleRate_ * 0.045)), 0.0f);
        corrPos_ = 0;
        detectedHz_ = 0.0f;
        ratioSmoothed_ = 1.0f;
        holdSamples_ = 0;
        stability_ = 0.0f;
    }

    void reset() noexcept
    {
        for (auto& d : delay_)
            std::fill (d.begin(), d.end(), 0.0f);
        std::fill (corrBuf_.begin(), corrBuf_.end(), 0.0f);
        corrPos_ = 0;
        detectedHz_ = 0.0f;
        ratioSmoothed_ = 1.0f;
        holdSamples_ = 0;
        stability_ = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            writePos_[static_cast<size_t> (ch)] = 0;
            readPos_[static_cast<size_t> (ch)] = (float) ((int) (sampleRate_ * 0.02));
        }
    }

    void setParams (int rootPc, ScaleType type, float retuneSpeed01, float humanize01,
                    std::uint16_t midiChordMask) noexcept
    {
        root_ = ((rootPc % 12) + 12) % 12;
        type_ = type;
        retuneSpeed_ = juce::jlimit (0.0f, 1.0f, retuneSpeed01);
        humanize_ = juce::jlimit (0.0f, 1.0f, humanize01);
        auto mask = scaleMask (type_, root_);
        activeMask_ = (midiChordMask != 0) ? midiChordMask : mask;
    }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int n = buffer.getNumSamples();
        const int chans = juce::jmin (buffer.getNumChannels(), numChannels_);
        if (n <= 0 || delay_[0].empty())
            return;

        for (int i = 0; i < n; ++i)
        {
            float mono = 0.0f;
            for (int ch = 0; ch < chans; ++ch)
                mono += buffer.getSample (ch, i);
            mono *= 1.0f / (float) juce::jmax (1, chans);

            // Pitch detect buffer
            corrBuf_[static_cast<size_t> (corrPos_)] = mono;
            if (++corrPos_ >= (int) corrBuf_.size())
            {
                corrPos_ = 0;
                updatePitchDetect();
            }

            float targetRatio = 1.0f;
            if (detectedHz_ > 40.0f && detectedHz_ < 1200.0f)
            {
                const float midi = hzToMidiNote (detectedHz_);
                const int pc = ((int) std::round (midi) % 12 + 12) % 12;
                const int delta = snapSemitoneDelta (activeMask_, pc);
                const float targetMidi = std::round (midi) + (float) delta;
                const float targetHz = midiNoteToHz (targetMidi);
                targetRatio = detectedHz_ > 1.0e-6f ? (targetHz / detectedHz_) : 1.0f;
                targetRatio = juce::jlimit (0.5f, 2.0f, targetRatio);

                // Humanize: pull toward unity when note is stable/sustained
                const float humanAmt = humanize_ * stability_;
                targetRatio = 1.0f + (targetRatio - 1.0f) * (1.0f - humanAmt * 0.85f);
            }

            // Retune speed → smoothing time
            const float smoothSec = juce::jmap (1.0f - retuneSpeed_, 0.008f, 0.35f);
            const float coeff = 1.0f - std::exp (-1.0f / (float) (sampleRate_ * (double) smoothSec));
            ratioSmoothed_ += coeff * (targetRatio - ratioSmoothed_);

            for (int ch = 0; ch < chans; ++ch)
            {
                auto& del = delay_[static_cast<size_t> (ch)];
                int& w = writePos_[static_cast<size_t> (ch)];
                float& r = readPos_[static_cast<size_t> (ch)];

                const float x = buffer.getSample (ch, i);
                del[static_cast<size_t> (w & delayMask_)] = x;
                ++w;

                // Fractional read
                const int i0 = ((int) r) & delayMask_;
                const int i1 = (i0 + 1) & delayMask_;
                const float frac = r - std::floor (r);
                const float y = del[static_cast<size_t> (i0)] * (1.0f - frac)
                              + del[static_cast<size_t> (i1)] * frac;

                // Advance read by ratio (ratio>1 = higher pitch = faster read)
                r += ratioSmoothed_;
                // Keep read ~20ms behind write
                const float targetBehind = (float) ((int) (sampleRate_ * 0.02));
                const float behind = (float) ((w - (int) r) & delayMask_);
                if (behind > targetBehind + 64.0f || behind < targetBehind - 64.0f)
                    r = (float) w - targetBehind;

                buffer.setSample (ch, i, y);
            }
        }
    }

private:
    void updatePitchDetect() noexcept
    {
        const int N = (int) corrBuf_.size();
        if (N < 64)
            return;

        const int minLag = juce::jmax (2, (int) (sampleRate_ / 900.0));
        const int maxLag = juce::jmin (N - 2, (int) (sampleRate_ / 60.0));

        float best = 0.0f;
        int bestLag = 0;
        float energy = 0.0f;
        for (int i = 0; i < N; ++i)
            energy += corrBuf_[static_cast<size_t> (i)] * corrBuf_[static_cast<size_t> (i)];
        if (energy < 1.0e-8f)
        {
            detectedHz_ = 0.0f;
            stability_ *= 0.9f;
            holdSamples_ = 0;
            return;
        }

        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            float c = 0.0f;
            for (int i = 0; i < N - lag; ++i)
                c += corrBuf_[static_cast<size_t> (i)] * corrBuf_[static_cast<size_t> (i + lag)];
            if (c > best)
            {
                best = c;
                bestLag = lag;
            }
        }

        const float conf = best / energy;
        if (bestLag > 0 && conf > 0.25f)
        {
            const float hz = (float) sampleRate_ / (float) bestLag;
            if (detectedHz_ > 1.0f)
            {
                const float rel = std::abs (hz - detectedHz_) / detectedHz_;
                if (rel < 0.03f)
                {
                    ++holdSamples_;
                    stability_ = juce::jmin (1.0f, stability_ + 0.08f);
                }
                else
                {
                    holdSamples_ = 0;
                    stability_ *= 0.7f;
                }
            }
            detectedHz_ = detectedHz_ > 1.0f ? (0.7f * detectedHz_ + 0.3f * hz) : hz;
        }
        else
        {
            detectedHz_ *= 0.9f;
            if (detectedHz_ < 30.0f)
                detectedHz_ = 0.0f;
            stability_ *= 0.85f;
        }
    }

    double sampleRate_ = 44100.0;
    int numChannels_ = 2;
    int maxBlock_ = 512;
    int root_ = 0;
    ScaleType type_ = ScaleType::Major;
    std::uint16_t activeMask_ = 0x0FFFu;
    float retuneSpeed_ = 0.5f;
    float humanize_ = 0.0f;

    std::array<std::vector<float>, 2> delay_ {};
    std::array<int, 2> writePos_ {};
    std::array<float, 2> readPos_ {};
    int delayMask_ = 0;

    std::vector<float> corrBuf_;
    int corrPos_ = 0;
    float detectedHz_ = 0.0f;
    float ratioSmoothed_ = 1.0f;
    int holdSamples_ = 0;
    float stability_ = 0.0f;
};

} // namespace afterimage
