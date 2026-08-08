#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "../Utilities/Constants.h"

#include <array>
#include <memory>
#include <vector>

namespace afterimage
{

/**
    Overlap-add STFT with per-hop-phase WOLA normalization.

    Latency = fftSize samples.
    Spectrum callback must leave the FFT buffer untouched for identity reconstruction.
*/
class STFTProcessor
{
public:
    using SpectrumCallback = void (*) (void* userData,
                                       float* interleavedFftData,
                                       int fftSize,
                                       int channelIndex) noexcept;

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void releaseResources();

    void setSpectrumCallback (SpectrumCallback callback, void* userData) noexcept;

    void process (juce::AudioBuffer<float>& buffer) noexcept;

    [[nodiscard]] int getLatencySamples() const noexcept { return latencySamples_; }
    [[nodiscard]] int getFftSize() const noexcept { return constants::fftSize; }
    [[nodiscard]] int getHopSize() const noexcept { return constants::hopSize; }
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    /** Exposed for tests: WOLA scale for hop phase [0, hopSize). */
    [[nodiscard]] float getWolaScaleForPhase (int phase) const noexcept
    {
        jassert (phase >= 0 && phase < hopSize);
        return wolaScaleTable_[static_cast<size_t> (phase)];
    }

    [[nodiscard]] float getMaxWolaScaleDeviation() const noexcept { return wolaMaxDeviation_; }

private:
    static constexpr int fftOrder = constants::fftOrder;
    static constexpr int fftSize  = constants::fftSize;
    static constexpr int hopSize  = constants::hopSize;

    struct Channel
    {
        juce::dsp::FFT fft { fftOrder };
        std::array<float, fftSize>     inRing  {};
        std::array<float, fftSize>     outRing {};
        std::array<float, 2 * fftSize> fftBuf  {};
        int  pos    = 0;
        bool primed = false;

        void clear() noexcept
        {
            inRing.fill (0.0f);
            outRing.fill (0.0f);
            fftBuf.fill (0.0f);
            pos = 0;
            primed = false;
        }
    };

    void processFrame (Channel& ch, int channelIndex) noexcept;
    void buildWolaTable() noexcept;

    std::vector<std::unique_ptr<Channel>> channels_;
    std::array<float, fftSize> window_ {};
    std::array<float, hopSize> wolaScaleTable_ {};
    float wolaMaxDeviation_ = 0.0f;

    SpectrumCallback spectrumCallback_ = nullptr;
    void*            spectrumUserData_  = nullptr;

    double sampleRate_     = 44100.0;
    int    numChannels_    = 2;
    int    hopCounter_     = 0;
    int    latencySamples_ = 0;
    bool   prepared_       = false;
};

} // namespace afterimage
