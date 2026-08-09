#pragma once

#include "Biquad.h"
#include "../Utilities/Constants.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cmath>
#include <vector>

namespace afterimage
{

/**
    True formant shift via short-time spectral envelope warp.

    Liftered log-magnitude envelope is warped on a log-frequency axis while
    fine structure (and phase) are preserved. Centre (0.5) is transparent.
    Not a whole-signal pitch shift.
*/
class FormantShifter
{
public:
    static constexpr int kFftOrder = 10; // 1024
    static constexpr int kFftSize = 1 << kFftOrder;
    static constexpr int kHop = 256;
    static constexpr int kBins = kFftSize / 2 + 1;
    static constexpr int kCepstrumKeep = 32; // low-quefrency envelope

    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        sampleRate_ = std::max (1.0, sampleRate);
        numChannels_ = juce::jmax (1, juce::jmin (2, numChannels));
        maxBlock_ = juce::jmax (1, maxBlock);

        for (int ch = 0; ch < 2; ++ch)
        {
            inputFifo_[static_cast<size_t> (ch)].assign (static_cast<size_t> (kFftSize), 0.0f);
            outputAccum_[static_cast<size_t> (ch)].assign (static_cast<size_t> (kFftSize * 2), 0.0f);
            fftTime_[static_cast<size_t> (ch)].assign (static_cast<size_t> (kFftSize * 2), 0.0f);
        }
        window_.assign (static_cast<size_t> (kFftSize), 0.0f);
        for (int i = 0; i < kFftSize; ++i)
            window_[static_cast<size_t> (i)] = 0.5f - 0.5f * std::cos (
                juce::MathConstants<float>::twoPi * (float) i / (float) (kFftSize - 1));

        mag_.assign (static_cast<size_t> (kBins), 0.0f);
        phase_.assign (static_cast<size_t> (kBins), 0.0f);
        env_.assign (static_cast<size_t> (kBins), 0.0f);
        envWarped_.assign (static_cast<size_t> (kBins), 0.0f);
        cepstrum_.assign (static_cast<size_t> (kFftSize), 0.0f);
        logMag_.assign (static_cast<size_t> (kFftSize * 2), 0.0f);

        fifoPos_ = 0;
        hopCounter_ = 0;
        amountSmoothed_ = 0.5f;
        enabled_ = false;
        gainNorm_ = 1.0f;
        envFast_ = 0.0f;
        envSlow_ = 0.0f;
        reset();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill (inputFifo_[static_cast<size_t> (ch)].begin(), inputFifo_[static_cast<size_t> (ch)].end(), 0.0f);
            std::fill (outputAccum_[static_cast<size_t> (ch)].begin(), outputAccum_[static_cast<size_t> (ch)].end(), 0.0f);
        }
        fifoPos_ = 0;
        hopCounter_ = 0;
        amountSmoothed_ = 0.5f;
        gainNorm_ = 1.0f;
        envFast_ = 0.0f;
        envSlow_ = 0.0f;
    }

    void setEnabled (bool on) noexcept { enabled_ = on; }
    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }

    void setAmount (float amount01) noexcept
    {
        target_ = juce::jlimit (0.0f, 1.0f, amount01);
    }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        if (! enabled_)
            return;

        const int numSamples = buffer.getNumSamples();
        const int chans = juce::jmin (buffer.getNumChannels(), numChannels_);
        if (numSamples <= 0 || chans <= 0)
            return;

        const float coeff = 1.0f - std::exp (-1.0f / (float) (sampleRate_ * (double) constants::formantSmoothSec));

        for (int i = 0; i < numSamples; ++i)
        {
            amountSmoothed_ += coeff * (target_ - amountSmoothed_);

            float mono = 0.0f;
            for (int ch = 0; ch < chans; ++ch)
            {
                const float x = buffer.getSample (ch, i);
                inputFifo_[static_cast<size_t> (ch)][static_cast<size_t> (fifoPos_)] = x;
                mono += x;
            }
            mono *= 1.0f / (float) chans;

            const float absx = std::abs (mono);
            envFast_ += 0.2f * (absx - envFast_);
            envSlow_ += 0.02f * (absx - envSlow_);
            const float transient = juce::jlimit (0.0f, 1.0f, (envFast_ - envSlow_) * 10.0f);

            if (++hopCounter_ >= kHop)
            {
                hopCounter_ = 0;
                processHop (chans, amountSmoothed_, transient);
            }

            // Output from OLA accum (latency = kFftSize - kHop ≈ 768); dry blend near centre.
            const float depth = std::abs (amountSmoothed_ - 0.5f) * 2.0f;
            const float wet = depth * (1.0f - transient * 0.65f);
            const int outIdx = (fifoPos_ + kFftSize - kHop) % kFftSize;

            for (int ch = 0; ch < chans; ++ch)
            {
                const float dry = buffer.getSample (ch, i);
                float y = outputAccum_[static_cast<size_t> (ch)][static_cast<size_t> (outIdx)];
                outputAccum_[static_cast<size_t> (ch)][static_cast<size_t> (outIdx)] = 0.0f;
                y *= gainNorm_;
                // Near centre: identity (ignore STFT path).
                buffer.setSample (ch, i, wet < 1.0e-4f ? dry : dry * (1.0f - wet) + y * wet);
            }

            fifoPos_ = (fifoPos_ + 1) % kFftSize;
        }
    }

private:
    void processHop (int chans, float amount01, float transient) noexcept
    {
        // Bipolar warp: 0 -> compress formants (darker), 1 -> expand (brighter).
        const float bipolar = (amount01 - 0.5f) * 2.0f; // -1..+1
        const float warp = std::pow (2.0f, bipolar * 0.35f); // ~0.78 .. 1.27

        float inPower = 0.0f;
        float outPower = 0.0f;

        for (int ch = 0; ch < chans; ++ch)
        {
            auto& time = fftTime_[static_cast<size_t> (ch)];
            std::fill (time.begin(), time.end(), 0.0f);
            for (int n = 0; n < kFftSize; ++n)
            {
                const int idx = (fifoPos_ + 1 + n) % kFftSize;
                time[static_cast<size_t> (n)] = inputFifo_[static_cast<size_t> (ch)][static_cast<size_t> (idx)]
                                              * window_[static_cast<size_t> (n)];
            }

            fft_.performRealOnlyForwardTransform (time.data());

            for (int b = 0; b < kBins; ++b)
            {
                const float re = time[static_cast<size_t> (b * 2)];
                const float im = (b == 0 || b == kBins - 1) ? 0.0f : time[static_cast<size_t> (b * 2 + 1)];
                mag_[static_cast<size_t> (b)] = std::sqrt (re * re + im * im) + 1.0e-12f;
                phase_[static_cast<size_t> (b)] = std::atan2 (im, re);
                inPower += mag_[static_cast<size_t> (b)] * mag_[static_cast<size_t> (b)];
            }

            // Log-mag -> cepstrum lifter -> envelope
            std::fill (logMag_.begin(), logMag_.end(), 0.0f);
            for (int b = 0; b < kBins; ++b)
                logMag_[static_cast<size_t> (b)] = std::log (mag_[static_cast<size_t> (b)]);

            // Real cepstrum via DCT-ish: reuse FFT on mirrored log spectrum
            for (int b = 0; b < kBins; ++b)
                cepstrum_[static_cast<size_t> (b)] = logMag_[static_cast<size_t> (b)];
            for (int b = kBins; b < kFftSize; ++b)
                cepstrum_[static_cast<size_t> (b)] = logMag_[static_cast<size_t> (kFftSize - b)];

            // Simple low-pass lifter in quefrency (zero high indices)
            // Reconstruct envelope by keeping low quefrency via moving average on log-mag (cheaper, RT-safe).
            constexpr int smooth = 6;
            for (int b = 0; b < kBins; ++b)
            {
                float s = 0.0f;
                int c = 0;
                for (int k = -smooth; k <= smooth; ++k)
                {
                    const int idx = juce::jlimit (0, kBins - 1, b + k);
                    s += logMag_[static_cast<size_t> (idx)];
                    ++c;
                }
                env_[static_cast<size_t> (b)] = s / (float) c;
            }

            // Warp envelope on log-frequency axis
            for (int b = 0; b < kBins; ++b)
            {
                const float src = (float) b / warp;
                const int i0 = juce::jlimit (0, kBins - 2, (int) std::floor (src));
                const float t = src - (float) i0;
                const float e0 = env_[static_cast<size_t> (i0)];
                const float e1 = env_[static_cast<size_t> (i0 + 1)];
                envWarped_[static_cast<size_t> (b)] = e0 + (e1 - e0) * t;
            }

            // Apply envelope ratio to magnitude; keep phase
            for (int b = 0; b < kBins; ++b)
            {
                const float fine = logMag_[static_cast<size_t> (b)] - env_[static_cast<size_t> (b)];
                const float newLog = envWarped_[static_cast<size_t> (b)] + fine;
                const float newMag = std::exp (newLog);
                outPower += newMag * newMag;
                const float ph = phase_[static_cast<size_t> (b)];
                time[static_cast<size_t> (b * 2)] = newMag * std::cos (ph);
                time[static_cast<size_t> (b * 2 + 1)] = newMag * std::sin (ph);
            }
            time[1] = 0.0f;
            if (kBins * 2 < (int) time.size())
                time[static_cast<size_t> (kFftSize + 1)] = 0.0f;

            fft_.performRealOnlyInverseTransform (time.data());

            const float olaScale = 1.0f / ((float) kFftSize * 1.5f);
            for (int n = 0; n < kFftSize; ++n)
            {
                const int dst = (fifoPos_ + 1 + n) % (kFftSize * 2);
                // Use first kFftSize of circular accum mapped into outputAccum length
                const int a = (fifoPos_ + 1 + n) % (int) outputAccum_[static_cast<size_t> (ch)].size();
                outputAccum_[static_cast<size_t> (ch)][static_cast<size_t> (a)]
                    += time[static_cast<size_t> (n)] * window_[static_cast<size_t> (n)] * olaScale;
                juce::ignoreUnused (dst);
            }
        }

        // Gain normalize (avoid loudness jumps from envelope warp); soften on transients.
        if (inPower > 1.0e-8f && outPower > 1.0e-8f)
        {
            const float g = std::sqrt (inPower / outPower);
            const float targetG = juce::jlimit (0.5f, 2.0f, g);
            gainNorm_ += (0.15f + 0.35f * (1.0f - transient)) * (targetG - gainNorm_);
        }
    }

    double sampleRate_ = 44100.0;
    int numChannels_ = 2;
    int maxBlock_ = 512;
    bool enabled_ = false;
    float target_ = 0.5f;
    float amountSmoothed_ = 0.5f;
    float gainNorm_ = 1.0f;
    float envFast_ = 0.0f;
    float envSlow_ = 0.0f;
    int fifoPos_ = 0;
    int hopCounter_ = 0;

    juce::dsp::FFT fft_ { kFftOrder };
    std::array<std::vector<float>, 2> inputFifo_ {};
    std::array<std::vector<float>, 2> outputAccum_ {};
    std::array<std::vector<float>, 2> fftTime_ {};
    std::vector<float> window_;
    std::vector<float> mag_, phase_, env_, envWarped_, cepstrum_, logMag_;
};

} // namespace afterimage
