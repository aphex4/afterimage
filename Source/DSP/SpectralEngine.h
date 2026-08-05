#pragma once

#include "STFTProcessor.h"
#include "SpectralHistoryBuffer.h"
#include "SpectralModes.h"
#include "DryWetMixer.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace afterimage
{

/**
    Owns the STFT, spectral history, and mode processor.

    Phase 2: transparent STFT reconstruction (identity spectrum).
    Phase 3+: history push + mode algorithms via the STFT spectrum callback.
*/
class SpectralEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void releaseResources();

    /** In-place STFT processing. Phase 2 leaves the spectrum unchanged. */
    void process (juce::AudioBuffer<float>& buffer, const ModeParams& params, SpectralMode mode);

    void setFrozen (bool shouldFreeze) noexcept { history_.setFrozen (shouldFreeze); }
    void clearHistory() { history_.clear(); }

    [[nodiscard]] int getLatencySamples() const noexcept { return stft_.getLatencySamples(); }
    [[nodiscard]] SpectralHistoryBuffer& getHistory() noexcept { return history_; }
    [[nodiscard]] const SpectralHistoryBuffer& getHistory() const noexcept { return history_; }
    [[nodiscard]] STFTProcessor& getSTFT() noexcept { return stft_; }

private:
    STFTProcessor stft_;
    SpectralHistoryBuffer history_;
    SpectralModeProcessor modes_;

    double sampleRate_ = 44100.0;
    bool prepared_ = false;

    // Cached for Phase 3+ spectrum callback (set before each process call).
    ModeParams pendingParams_ {};
    SpectralMode pendingMode_ = SpectralMode::Shadow;
};

} // namespace afterimage
