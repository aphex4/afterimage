#pragma once

#include "STFTProcessor.h"
#include "SpectralHistoryBuffer.h"
#include "SpectralModes.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace afterimage
{

/**
    Owns the STFT, per-channel spectral history, and mode processor.

    Phase 3: captures magnitude/phase frames into history each hop.
             Spectrum itself remains unmodified (transparent STFT).
    Phase 4+: mode algorithms rewrite magnitudes before the inverse FFT.
*/
class SpectralEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void releaseResources();

    /** In-place STFT. Phase 3 stores history; spectrum stays identity. */
    void process (juce::AudioBuffer<float>& buffer, const ModeParams& params, SpectralMode mode);

    void setFrozen (bool shouldFreeze) noexcept;
    void clearHistory();
    void setActiveMemoryLengthSeconds (float seconds) noexcept;

    [[nodiscard]] int getLatencySamples() const noexcept { return stft_.getLatencySamples(); }
    [[nodiscard]] int getNumHistoryChannels() const noexcept { return static_cast<int> (histories_.size()); }

    [[nodiscard]] SpectralHistoryBuffer& getHistory (int channel = 0) noexcept;
    [[nodiscard]] const SpectralHistoryBuffer& getHistory (int channel = 0) const noexcept;

    [[nodiscard]] STFTProcessor& getSTFT() noexcept { return stft_; }

    /** Approximate fill of the active memory window [0,1] for UI. */
    [[nodiscard]] float getHistoryFillAmount (int channel = 0) const noexcept;

private:
    static void spectrumCallback (void* userData,
                                  float* interleavedFftData,
                                  int fftSize,
                                  int channelIndex) noexcept;

    void onSpectrum (float* interleavedFftData, int fftSize, int channelIndex) noexcept;

    STFTProcessor stft_;
    std::vector<std::unique_ptr<SpectralHistoryBuffer>> histories_;
    SpectralModeProcessor modes_;

    // Per-channel previous magnitudes for spectral-flux transient estimate.
    std::vector<std::vector<float>> previousMagnitudes_;
    std::vector<bool> hasPreviousFrame_;

    std::vector<std::uint64_t> frameCounters_;

    double sampleRate_ = 44100.0;
    int numChannels_ = 2;
    bool prepared_ = false;

    ModeParams pendingParams_ {};
    SpectralMode pendingMode_ = SpectralMode::Shadow;
};

} // namespace afterimage
