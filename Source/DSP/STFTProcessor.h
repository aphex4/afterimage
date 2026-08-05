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
    Overlap-add STFT processor (Phase 2).

    Per channel:
      input ring → Hann window → real FFT → optional spectrum callback
      → IFFT → Hann → WOLA output ring

    With Hann on analysis and synthesis and hop = N/4, the overlapped
    sum of window² is constant; we divide by that measured COLA scale for
    transparent unity-gain reconstruction when the spectrum is untouched.

    Latency is fftSize samples (one full ring revolution before a written
    sample is read back from the OLA buffer).
*/
class STFTProcessor
{
public:
    /** Called after the forward FFT, before the inverse. data is the
        juce real-only interleaved buffer (length >= 2 * fftSize).
        Must be real-time safe (no alloc / lock). nullptr = identity. */
    using SpectrumCallback = void (*) (void* userData, float* interleavedFftData, int fftSize) noexcept;

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void releaseResources();

    void setSpectrumCallback (SpectrumCallback callback, void* userData) noexcept;

    /** In-place STFT. Handles arbitrary host block sizes. */
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    [[nodiscard]] int getLatencySamples() const noexcept { return latencySamples_; }
    [[nodiscard]] int getFftSize() const noexcept { return constants::fftSize; }
    [[nodiscard]] int getHopSize() const noexcept { return constants::hopSize; }
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

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
        int  count  = 0;
        bool primed = false;

        void clear() noexcept
        {
            inRing.fill (0.0f);
            outRing.fill (0.0f);
            fftBuf.fill (0.0f);
            pos = 0;
            count = 0;
            primed = false;
        }
    };

    void processFrame (Channel& ch) noexcept;

    std::vector<std::unique_ptr<Channel>> channels_;
    std::array<float, fftSize> window_ {};

    SpectrumCallback spectrumCallback_ = nullptr;
    void*            spectrumUserData_  = nullptr;

    double sampleRate_     = 44100.0;
    int    numChannels_    = 2;
    int    latencySamples_ = 0;
    float  wolaScale_      = 1.0f;
    bool   prepared_       = false;
};

} // namespace afterimage
