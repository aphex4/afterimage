#pragma once

#include "Biquad.h"
#include "ScaleTheory.h"
#include "../Utilities/Constants.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cmath>

namespace afterimage
{

/**
    HARMONICS — polyphonic scale-aware spectral sweetener.

    A bank of band-pass filters accents in-key pitch classes. Out-of-key bands
    are attenuated (not "redirected"). Color 0 = identity wet; Color > 1 adds
    in-key resonance. When disabled, process() is a no-op.
*/
class ScaleAccentuator
{
public:
    static constexpr int kLowestMidi = 36;  // C2
    static constexpr int kNumBands = 48;    // 4 octaves
    static constexpr int kHighestMidi = kLowestMidi + kNumBands - 1;

    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        sampleRate_ = std::max (1.0, sampleRate);
        numChannels_ = juce::jmax (1, juce::jmin (2, numChannels));
        dryScratch_.setSize (numChannels_, juce::jmax (1, maxBlock), false, true, true);
        rebuildFilters();
        reset();
    }

    void reset() noexcept
    {
        for (auto& ch : states_)
            for (auto& s : ch)
                s.reset();
        env_ = 0.0f;
        colorSmoothed_ = 0.0f;
    }

    void setEnabled (bool on) noexcept { enabled_ = on; }
    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }

    void setParams (int rootPc, ScaleType type, float color01to2, float transientPreserve,
                    std::uint16_t midiChordMask) noexcept
    {
        rootPc = ((rootPc % 12) + 12) % 12;
        colorTarget_ = juce::jlimit (0.0f, 2.0f, color01to2);
        transient_ = juce::jlimit (0.0f, 1.0f, transientPreserve);

        const auto base = scaleMask (type, rootPc);
        const auto active = (midiChordMask != 0) ? midiChordMask : base;
        if (rootPc != root_ || type != type_ || active != activeMask_)
        {
            root_ = rootPc;
            type_ = type;
            baseMask_ = base;
            activeMask_ = active;
            rebuildFilters();
        }
    }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        if (! enabled_)
            return;

        const int n = buffer.getNumSamples();
        const int chans = juce::jmin (buffer.getNumChannels(), numChannels_);
        if (n <= 0 || chans <= 0)
            return;

        for (int ch = 0; ch < chans; ++ch)
            dryScratch_.copyFrom (ch, 0, buffer, ch, 0, n);

        const float coeff = 1.0f - std::exp (-1.0f / (float) (sampleRate_ * 0.04));
        const float atk = 1.0f - std::exp (-1.0f / (float) (sampleRate_ * 0.005));
        const float rel = 1.0f - std::exp (-1.0f / (float) (sampleRate_ * 0.060));

        for (int i = 0; i < n; ++i)
        {
            colorSmoothed_ += coeff * (colorTarget_ - colorSmoothed_);
            const float color = colorSmoothed_;
            const float wet = juce::jlimit (0.0f, 1.0f, color);
            const float resonate = juce::jmax (0.0f, color - 1.0f);

            if (wet < 1.0e-5f && resonate < 1.0e-5f)
                continue;

            float mono = 0.0f;
            for (int ch = 0; ch < chans; ++ch)
                mono += dryScratch_.getSample (ch, i);
            mono *= 1.0f / (float) chans;
            const float absx = std::abs (mono);
            env_ += (absx > env_ ? atk : rel) * (absx - env_);
            const float attackiness = juce::jlimit (0.0f, 1.0f, (absx - env_) * 8.0f);
            const float wetEff = wet * (1.0f - attackiness * transient_ * 0.85f);

            for (int ch = 0; ch < chans; ++ch)
            {
                const float dry = dryScratch_.getSample (ch, i);
                float accent = 0.0f;
                float keyRes = 0.0f;

                for (int b = 0; b < kNumBands; ++b)
                {
                    auto& st = states_[static_cast<size_t> (ch)][static_cast<size_t> (b)];
                    const float bp = st.process (dry, coeffs_[static_cast<size_t> (b)]);
                    const float w = bandWeight_[static_cast<size_t> (b)];
                    accent += bp * w;
                    if (bandInKey_[static_cast<size_t> (b)])
                        keyRes += bp;
                }

                accent *= 0.55f;
                keyRes *= 0.35f * resonate;

                buffer.setSample (ch, i, dry * (1.0f - wetEff) + accent * wetEff + keyRes);
            }
        }
    }

private:
    void rebuildFilters() noexcept
    {
        for (int b = 0; b < kNumBands; ++b)
        {
            const int midi = kLowestMidi + b;
            const int pc = midi % 12;
            const bool inKey = isPitchClassInScale (activeMask_, pc);
            bandInKey_[static_cast<size_t> (b)] = inKey;

            // Centre stays on the band's own pitch — attenuate out-of-key, do not retune.
            const float hz = midiNoteToHz ((float) midi);
            const float q = inKey ? 9.0f : 6.0f;
            coeffs_[static_cast<size_t> (b)] = makeBandPass (sampleRate_, hz, q);
            bandWeight_[static_cast<size_t> (b)] = inKey ? 1.0f : 0.35f;
        }
    }

    double sampleRate_ = 44100.0;
    int numChannels_ = 2;
    bool enabled_ = false;
    int root_ = 0;
    ScaleType type_ = ScaleType::Major;
    std::uint16_t baseMask_ = 0x0FFFu;
    std::uint16_t activeMask_ = 0x0FFFu;
    float colorTarget_ = 0.0f;
    float colorSmoothed_ = 0.0f;
    float transient_ = 0.35f;
    float env_ = 0.0f;

    std::array<BiquadCoeffs, kNumBands> coeffs_ {};
    std::array<float, kNumBands> bandWeight_ {};
    std::array<bool, kNumBands> bandInKey_ {};
    std::array<std::array<BiquadState, kNumBands>, 2> states_ {};
    juce::AudioBuffer<float> dryScratch_;
};

} // namespace afterimage
