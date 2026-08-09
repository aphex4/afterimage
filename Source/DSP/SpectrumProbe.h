#pragma once

#include "../Utilities/Constants.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace afterimage
{

/**
    Lock-free spectrum snapshot for UI paint.
    Hann FFT with overlap, log-frequency bins, dB scale -90..0, attack/release.
*/
class SpectrumProbe
{
public:
    static constexpr int kBins = constants::spectrumProbeBins;

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sampleRate_ = std::max (1.0, sampleRate);
        const int n = 1 << constants::spectrumProbeFftOrder;
        fifo_.assign (static_cast<size_t> (n), 0.0f);
        fftData_.assign (static_cast<size_t> (n * 2), 0.0f);
        window_.assign (static_cast<size_t> (n), 0.0f);
        for (int i = 0; i < n; ++i)
            window_[static_cast<size_t> (i)] = 0.5f - 0.5f * std::cos (
                juce::MathConstants<float>::twoPi * (float) i / (float) (n - 1));
        fifoPos_ = 0;
        hopCounter_ = 0;
        hopSize_ = n / 4; // 75% overlap
        windowSum_ = 0.0f;
        for (float w : window_)
            windowSum_ += w * w;
        windowSum_ = std::max (1.0e-6f, windowSum_);
        for (auto& b : published_)
            b.store (0.0f, std::memory_order_relaxed);
    }

    void reset() noexcept
    {
        std::fill (fifo_.begin(), fifo_.end(), 0.0f);
        fifoPos_ = 0;
        hopCounter_ = 0;
        for (auto& b : published_)
            b.store (0.0f, std::memory_order_relaxed);
    }

    void process (const float* left, const float* right, int numSamples) noexcept
    {
        if (fifo_.empty() || left == nullptr)
            return;

        const int n = (int) fifo_.size();
        for (int i = 0; i < numSamples; ++i)
        {
            const float s = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
            fifo_[static_cast<size_t> (fifoPos_)] = s;
            fifoPos_ = (fifoPos_ + 1) % n;
            if (++hopCounter_ >= hopSize_)
            {
                hopCounter_ = 0;
                publishFrame();
            }
        }
    }

    /** Copy bins as 0..1 display values mapped from -90..0 dBFS. */
    void copyBins (float* dest, int maxBins) const noexcept
    {
        const int n = juce::jmin (maxBins, kBins);
        for (int i = 0; i < n; ++i)
            dest[i] = published_[static_cast<size_t> (i)].load (std::memory_order_relaxed);
    }

private:
    void publishFrame() noexcept
    {
        const int n = (int) fifo_.size();
        std::fill (fftData_.begin(), fftData_.end(), 0.0f);

        // fifoPos_ points to next write = oldest sample
        for (int i = 0; i < n; ++i)
        {
            const int idx = (fifoPos_ + i) % n;
            fftData_[static_cast<size_t> (i)] = fifo_[static_cast<size_t> (idx)]
                                              * window_[static_cast<size_t> (i)];
        }

        fft_.performFrequencyOnlyForwardTransform (fftData_.data());

        const float nyquist = (float) sampleRate_ * 0.5f;
        const float minHz = 20.0f;
        const float maxHz = juce::jmin (nyquist * 0.98f, 20000.0f);
        const float logMin = std::log (minHz);
        const float logMax = std::log (maxHz);
        const float norm = 2.0f / windowSum_;

        constexpr float dbMin = -90.0f;
        constexpr float dbMax = 0.0f;
        constexpr float attack = 0.35f;
        constexpr float release = 0.08f;

        for (int b = 0; b < kBins; ++b)
        {
            const float t0 = (float) b / (float) kBins;
            const float t1 = (float) (b + 1) / (float) kBins;
            const float f0 = std::exp (logMin + (logMax - logMin) * t0);
            const float f1 = std::exp (logMin + (logMax - logMin) * t1);
            const int i0 = juce::jlimit (0, n / 2, (int) std::floor (f0 / nyquist * (float) (n / 2)));
            const int i1 = juce::jlimit (i0 + 1, n / 2, (int) std::ceil (f1 / nyquist * (float) (n / 2)));

            float power = 0.0f;
            for (int k = i0; k <= i1; ++k)
            {
                const float m = fftData_[static_cast<size_t> (k)] * norm;
                power += m * m;
            }
            power /= (float) juce::jmax (1, i1 - i0 + 1);

            const float db = juce::Decibels::gainToDecibels (std::sqrt (power), dbMin);
            const float mapped = juce::jlimit (0.0f, 1.0f, (db - dbMin) / (dbMax - dbMin));

            const float prev = published_[static_cast<size_t> (b)].load (std::memory_order_relaxed);
            const float next = mapped > prev ? prev + (mapped - prev) * attack
                                             : prev + (mapped - prev) * release;
            published_[static_cast<size_t> (b)].store (next, std::memory_order_relaxed);
        }
    }

    double sampleRate_ = 44100.0;
    juce::dsp::FFT fft_ { constants::spectrumProbeFftOrder };
    std::vector<float> fifo_;
    std::vector<float> fftData_;
    std::vector<float> window_;
    int fifoPos_ = 0;
    int hopCounter_ = 0;
    int hopSize_ = 512;
    float windowSum_ = 1.0f;
    std::array<std::atomic<float>, kBins> published_ {};
};

} // namespace afterimage
