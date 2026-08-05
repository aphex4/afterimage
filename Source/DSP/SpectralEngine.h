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

    Phase 1: prepare/reset only — audio remains a pass-through in the
    AudioProcessor. Phase 2+ routes audio through process().
*/
class SpectralEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void releaseResources();

    /** Phase 2+: run STFT + history + mode processing into buffer. */
    void process (juce::AudioBuffer<float>& buffer, const ModeParams& params, SpectralMode mode);

    void setFrozen (bool shouldFreeze) noexcept { history_.setFrozen (shouldFreeze); }
    void clearHistory() { history_.clear(); }

    [[nodiscard]] int getLatencySamples() const noexcept { return stft_.getLatencySamples(); }
    [[nodiscard]] SpectralHistoryBuffer& getHistory() noexcept { return history_; }
    [[nodiscard]] const SpectralHistoryBuffer& getHistory() const noexcept { return history_; }

private:
    STFTProcessor stft_;
    SpectralHistoryBuffer history_;
    SpectralModeProcessor modes_;
    DryWetMixer dryWet_; // used once latency-compensated dry path exists

    double sampleRate_ = 44100.0;
    bool prepared_ = false;
};

} // namespace afterimage
