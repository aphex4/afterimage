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
    Audio thread accumulates a prepared FFT; UI copies published bins.
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
        for (auto& b : published_)
            b.store (0.0f, std::memory_order_relaxed);
    }

    void reset() noexcept
    {
        std::fill (fifo_.begin(), fifo_.end(), 0.0f);
        fifoPos_ = 0;
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
            if (++fifoPos_ >= n)
            {
                fifoPos_ = 0;
                publishFrame();
            }
        }
    }

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
        for (int i = 0; i < n; ++i)
            fftData_[static_cast<size_t> (i)] = fifo_[static_cast<size_t> (i)]
                                                * window_[static_cast<size_t> (i)];

        fft_.performFrequencyOnlyForwardTransform (fftData_.data());

        const float nyquist = (float) sampleRate_ * 0.5f;
        const float minHz = 40.0f;
        const float maxHz = juce::jmin (nyquist * 0.98f, 18000.0f);
        const float logMin = std::log (minHz);
        const float logMax = std::log (maxHz);

        for (int b = 0; b < kBins; ++b)
        {
            const float t0 = (float) b / (float) kBins;
            const float t1 = (float) (b + 1) / (float) kBins;
            const float f0 = std::exp (logMin + (logMax - logMin) * t0);
            const float f1 = std::exp (logMin + (logMax - logMin) * t1);
            const int i0 = juce::jlimit (0, n / 2, (int) std::floor (f0 / nyquist * (float) (n / 2)));
            const int i1 = juce::jlimit (i0 + 1, n / 2, (int) std::ceil (f1 / nyquist * (float) (n / 2)));

            float peak = 0.0f;
            for (int k = i0; k <= i1; ++k)
                peak = juce::jmax (peak, fftData_[static_cast<size_t> (k)]);

            // Soft display mapping
            const float mapped = juce::jlimit (0.0f, 1.0f, std::sqrt (peak) * 2.5f);
            const float prev = published_[static_cast<size_t> (b)].load (std::memory_order_relaxed);
            published_[static_cast<size_t> (b)].store (
                mapped > prev ? mapped : prev * 0.85f + mapped * 0.15f,
                std::memory_order_relaxed);
        }
    }

    double sampleRate_ = 44100.0;
    juce::dsp::FFT fft_ { constants::spectrumProbeFftOrder };
    std::vector<float> fifo_;
    std::vector<float> fftData_;
    std::vector<float> window_;
    int fifoPos_ = 0;
    std::array<std::atomic<float>, kBins> published_ {};
};

} // namespace afterimage
