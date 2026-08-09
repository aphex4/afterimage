#pragma once

#include "ScaleTheory.h"
#include "../Utilities/Constants.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cmath>
#include <vector>

namespace afterimage
{

/**
    Monophonic pitch correction (TUNE).

    YIN / NACF detector with confidence + voiced gate (~55-1500 Hz).
    Fixed-latency overlap-add pitch shift (no delay-line read-head jumps).
    When disabled / Amount 0: pure delay of getLatencySamples() (identity, PDC-stable).
*/
class PitchTune
{
public:
    static constexpr int kLatency = 1024;
    static constexpr int kAnalysis = 2048;
    static constexpr int kGrain = 512;
    static constexpr int kDetectHop = 256;
    static constexpr int kGrainHop = 128;

    [[nodiscard]] static int getLatencySamples() noexcept { return kLatency; }

    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        sampleRate_ = std::max (1.0, sampleRate);
        numChannels_ = juce::jmax (1, juce::jmin (2, numChannels));
        maxBlock_ = juce::jmax (1, maxBlock);

        const int bufLen = juce::nextPowerOfTwo (kAnalysis + kLatency + maxBlock_ + kGrain * 4 + 16);
        delayMask_ = bufLen - 1;
        for (int ch = 0; ch < 2; ++ch)
        {
            delay_[static_cast<size_t> (ch)].assign (static_cast<size_t> (bufLen), 0.0f);
            ola_[static_cast<size_t> (ch)].assign (static_cast<size_t> (bufLen), 0.0f);
        }
        anal_.assign (static_cast<size_t> (kAnalysis), 0.0f);
        yin_.assign (static_cast<size_t> (kAnalysis / 2), 0.0f);
        window_.assign (static_cast<size_t> (kGrain), 0.0f);
        for (int i = 0; i < kGrain; ++i)
            window_[static_cast<size_t> (i)] = 0.5f - 0.5f * std::cos (
                juce::MathConstants<float>::twoPi * (float) i / (float) (kGrain - 1));

        reset();
    }

    void reset() noexcept
    {
        for (auto& d : delay_)
            std::fill (d.begin(), d.end(), 0.0f);
        for (auto& o : ola_)
            std::fill (o.begin(), o.end(), 0.0f);
        std::fill (anal_.begin(), anal_.end(), 0.0f);
        writePos_ = 0;
        olaRead_ = 0;
        analPos_ = 0;
        samplesSinceDetect_ = 0;
        samplesSinceGrain_ = 0;
        detectedHz_ = 0.0f;
        confidence_ = 0.0f;
        confidenceGate_ = 0.0f;
        targetRatio_ = 1.0f;
        ratioSmoothed_ = 1.0f;
        amountSmoothed_ = 0.0f;
        enableSmoothed_ = 0.0f;
        env_ = 0.0f;
    }

    void setEnabled (bool on) noexcept { enabledTarget_ = on ? 1.0f : 0.0f; }
    [[nodiscard]] bool isEnabled() const noexcept { return enabledTarget_ > 0.5f; }

    void setParams (int rootPc, ScaleType type, float retune01, float humanize01, float amount01,
                    std::uint16_t midiChordMask) noexcept
    {
        root_ = ((rootPc % 12) + 12) % 12;
        type_ = type;
        retune_ = juce::jlimit (0.0f, 1.0f, retune01);
        humanize_ = juce::jlimit (0.0f, 1.0f, humanize01);
        amountTarget_ = juce::jlimit (0.0f, 1.0f, amount01);
        const auto base = scaleMask (type_, root_);
        activeMask_ = (midiChordMask != 0) ? midiChordMask : base;
    }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int n = buffer.getNumSamples();
        const int chans = juce::jmin (buffer.getNumChannels(), numChannels_);
        if (n <= 0 || chans <= 0 || delay_[0].empty())
            return;

        const float enableCoeff = 1.0f - std::exp (-1.0f / (float) (sampleRate_ * 0.02));
        const float amountCoeff = 1.0f - std::exp (-1.0f / (float) (sampleRate_ * 0.03));
        const float retuneMs = juce::jmap (retune_, 120.0f, 8.0f);
        const float ratioCoeff = 1.0f - std::exp (-1.0f / (float) (sampleRate_ * (double) retuneMs * 0.001));

        for (int i = 0; i < n; ++i)
        {
            enableSmoothed_ += enableCoeff * (enabledTarget_ - enableSmoothed_);
            amountSmoothed_ += amountCoeff * (amountTarget_ - amountSmoothed_);

            float mono = 0.0f;
            for (int ch = 0; ch < chans; ++ch)
            {
                const float x = buffer.getSample (ch, i);
                delay_[static_cast<size_t> (ch)][static_cast<size_t> (writePos_ & delayMask_)] = x;
                mono += x;
            }
            mono *= 1.0f / (float) chans;

            const float absx = std::abs (mono);
            env_ += ((absx > env_) ? 0.25f : 0.02f) * (absx - env_);

            anal_[static_cast<size_t> (analPos_)] = mono;
            analPos_ = (analPos_ + 1) % kAnalysis;

            if (++samplesSinceDetect_ >= kDetectHop)
            {
                samplesSinceDetect_ = 0;
                runYin();
                updateTargetRatio();
            }

            ratioSmoothed_ += ratioCoeff * (targetRatio_ - ratioSmoothed_);

            const float wetAmt = enableSmoothed_ * amountSmoothed_ * juce::jmax (confidenceGate_, 0.0f);
            const float ratio = 1.0f + (ratioSmoothed_ - 1.0f) * wetAmt;
            const bool correct = wetAmt > 1.0e-4f && std::abs (ratio - 1.0f) > 1.0e-4f;

            if (correct && ++samplesSinceGrain_ >= kGrainHop)
            {
                samplesSinceGrain_ = 0;
                placeGrain (ratio);
            }

            for (int ch = 0; ch < chans; ++ch)
            {
                const int dryR = (writePos_ - kLatency) & delayMask_;
                const float dry = delay_[static_cast<size_t> (ch)][static_cast<size_t> (dryR)];

                float y = dry;
                if (correct)
                {
                    const int orPos = olaRead_ & delayMask_;
                    const float g = ola_[static_cast<size_t> (ch)][static_cast<size_t> (orPos)];
                    ola_[static_cast<size_t> (ch)][static_cast<size_t> (orPos)] = 0.0f;
                    // Normalize-ish: 4x overlap Hann peaks ~2; soft scale.
                    y = dry * (1.0f - wetAmt) + (g * 0.55f) * wetAmt;
                }

                buffer.setSample (ch, i, y);
            }

            ++olaRead_;
            ++writePos_;
        }
    }

private:
    void placeGrain (float ratio) noexcept
    {
        const float centre = (float) ((writePos_ - kLatency) & delayMask_);
        const float half = 0.5f * (float) kGrain;
        const float invRatio = 1.0f / juce::jmax (0.5f, juce::jmin (2.0f, ratio));

        for (int ch = 0; ch < numChannels_; ++ch)
        {
            auto& d = delay_[static_cast<size_t> (ch)];
            auto& o = ola_[static_cast<size_t> (ch)];
            for (int g = 0; g < kGrain; ++g)
            {
                const float src = centre + ((float) g - half) * invRatio;
                const float s = hermite (d, src);
                const int dst = (olaRead_ + g) & delayMask_;
                o[static_cast<size_t> (dst)] += s * window_[static_cast<size_t> (g)];
            }
        }
    }

    float hermite (const std::vector<float>& buf, float pos) const noexcept
    {
        const int i1 = (int) std::floor (pos);
        const float t = pos - (float) i1;
        const int i0 = (i1 - 1) & delayMask_;
        const int ii1 = i1 & delayMask_;
        const int i2 = (i1 + 1) & delayMask_;
        const int i3 = (i1 + 2) & delayMask_;
        const float y0 = buf[static_cast<size_t> (i0)];
        const float y1 = buf[static_cast<size_t> (ii1)];
        const float y2 = buf[static_cast<size_t> (i2)];
        const float y3 = buf[static_cast<size_t> (i3)];
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + c0;
    }

    void runYin() noexcept
    {
        for (int i = 0; i < kAnalysis; ++i)
            analScratch_[static_cast<size_t> (i)] = anal_[static_cast<size_t> ((analPos_ + i) % kAnalysis)];

        const int tauMax = juce::jmin ((int) yin_.size() - 1, (int) (sampleRate_ / 55.0));
        const int tauMin = juce::jmax (2, (int) (sampleRate_ / 1500.0));

        float sum = 0.0f;
        yin_[0] = 1.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float d = 0.0f;
            const int n = kAnalysis - tau;
            for (int i = 0; i < n; ++i)
            {
                const float diff = analScratch_[static_cast<size_t> (i)]
                                 - analScratch_[static_cast<size_t> (i + tau)];
                d += diff * diff;
            }
            sum += d;
            yin_[static_cast<size_t> (tau)] = (sum > 1.0e-12f) ? (d * (float) tau / sum) : 1.0f;
        }

        constexpr float thresh = 0.15f;
        int bestTau = 0;
        float bestVal = 1.0f;
        for (int tau = tauMin; tau <= tauMax; ++tau)
        {
            const float v = yin_[static_cast<size_t> (tau)];
            if (v < thresh)
            {
                bestTau = tau;
                bestVal = v;
                while (tau + 1 <= tauMax && yin_[static_cast<size_t> (tau + 1)] < yin_[static_cast<size_t> (tau)])
                {
                    ++tau;
                    bestTau = tau;
                    bestVal = yin_[static_cast<size_t> (tau)];
                }
                break;
            }
            if (v < bestVal)
            {
                bestVal = v;
                bestTau = tau;
            }
        }

        if (bestTau > 0 && bestVal < 0.35f && env_ > 1.0e-3f)
        {
            float tauF = (float) bestTau;
            if (bestTau > 1 && bestTau < tauMax)
            {
                const float s0 = yin_[static_cast<size_t> (bestTau - 1)];
                const float s1 = yin_[static_cast<size_t> (bestTau)];
                const float s2 = yin_[static_cast<size_t> (bestTau + 1)];
                const float denom = 2.0f * (s0 - 2.0f * s1 + s2);
                if (std::abs (denom) > 1.0e-8f)
                    tauF += (s0 - s2) / denom;
            }
            detectedHz_ = (float) sampleRate_ / juce::jmax (1.0f, tauF);
            confidence_ = juce::jlimit (0.0f, 1.0f, 1.0f - bestVal / 0.35f);
        }
        else
        {
            detectedHz_ = 0.0f;
            confidence_ *= 0.85f;
        }
    }

    void updateTargetRatio() noexcept
    {
        if (detectedHz_ < 55.0f || detectedHz_ > 1500.0f || confidence_ < 0.35f)
        {
            targetRatio_ = 1.0f;
            confidenceGate_ *= 0.9f;
            return;
        }

        const float midi = hzToMidiNote (detectedHz_);
        const int pc = ((int) std::lround (midi) % 12 + 12) % 12;
        const int delta = snapSemitoneDelta (activeMask_, pc);
        float corrSemis = (float) delta;
        const float near = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (corrSemis) / 0.5f);
        corrSemis *= (1.0f - humanize_ * near * 0.85f);

        const float targetHz = midiNoteToHz (midi + corrSemis);
        targetRatio_ = juce::jlimit (0.5f, 2.0f, targetHz / juce::jmax (1.0f, detectedHz_));
        confidenceGate_ = confidence_;
    }

    double sampleRate_ = 44100.0;
    int numChannels_ = 2;
    int maxBlock_ = 512;
    int delayMask_ = 0;
    int writePos_ = 0;
    int olaRead_ = 0;
    int analPos_ = 0;
    int samplesSinceDetect_ = 0;
    int samplesSinceGrain_ = 0;

    float enabledTarget_ = 0.0f;
    float enableSmoothed_ = 0.0f;
    float amountTarget_ = 1.0f;
    float amountSmoothed_ = 0.0f;
    float retune_ = 0.5f;
    float humanize_ = 0.35f;
    int root_ = 0;
    ScaleType type_ = ScaleType::Major;
    std::uint16_t activeMask_ = 0x0FFFu;

    float detectedHz_ = 0.0f;
    float confidence_ = 0.0f;
    float confidenceGate_ = 0.0f;
    float targetRatio_ = 1.0f;
    float ratioSmoothed_ = 1.0f;
    float env_ = 0.0f;

    std::array<std::vector<float>, 2> delay_ {};
    std::array<std::vector<float>, 2> ola_ {};
    std::vector<float> anal_;
    std::array<float, kAnalysis> analScratch_ {};
    std::vector<float> yin_;
    std::vector<float> window_;
};

} // namespace afterimage
