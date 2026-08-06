#pragma once

#include "STFTProcessor.h"
#include "SpectralHistoryBuffer.h"
#include "SpectralModes.h"
#include "ParameterSmoother.h"
#include "VisualizationAtomics.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace afterimage
{

/**
    STFT + per-channel spectral history + Shadow mode.

    Audio-thread only for history mutation. UI reads VisualizationAtomics.
    History is read BEFORE committing the current analysis frame.
*/
class SpectralEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void releaseResources();

    /** Set spectral parameter targets (from APVTS). Advanced per FFT hop. */
    void setSpectralParameterTargets (float influence,
                                      float recall,
                                      float forget,
                                      float blur,
                                      float transientPreserve,
                                      float randomRecall,
                                      bool freeze) noexcept;

    void setMode (SpectralMode mode) noexcept;
    void setActiveMemoryLengthSeconds (float seconds) noexcept;

    /** Message-thread safe: request a clear; consumed on the audio thread. */
    void requestClearHistory() noexcept { clearHistoryRequested_.store (true, std::memory_order_release); }

    void process (juce::AudioBuffer<float>& buffer) noexcept;

    [[nodiscard]] int getLatencySamples() const noexcept { return stft_.getLatencySamples(); }
    [[nodiscard]] int getHopSize() const noexcept { return stft_.getHopSize(); }
    [[nodiscard]] STFTProcessor& getSTFT() noexcept { return stft_; }
    [[nodiscard]] const STFTProcessor& getSTFT() const noexcept { return stft_; }
    [[nodiscard]] SpectralModeProcessor& getModeProcessor() noexcept { return modes_; }
    [[nodiscard]] const SpectralModeProcessor& getModeProcessor() const noexcept { return modes_; }

    [[nodiscard]] VisualizationAtomics& getVisualization() noexcept { return viz_; }
    [[nodiscard]] const VisualizationAtomics& getVisualization() const noexcept { return viz_; }
    [[nodiscard]] SnapshotPublisher& getSnapshotPublisher() noexcept { return snapshots_; }
    [[nodiscard]] const SnapshotPublisher& getSnapshotPublisher() const noexcept { return snapshots_; }

    /** Audio-thread only. */
    [[nodiscard]] SpectralHistoryBuffer& getHistory (int channel = 0) noexcept;
    [[nodiscard]] const SpectralHistoryBuffer& getHistory (int channel = 0) const noexcept;

private:
    static void spectrumCallback (void* userData,
                                  float* interleavedFftData,
                                  int fftSize,
                                  int channelIndex) noexcept;

    void onSpectrum (float* interleavedFftData, int fftSize, int channelIndex) noexcept;
    void clearHistoryOnAudioThread() noexcept;
    void publishVisualization (int channelIndex) noexcept;

    STFTProcessor stft_;
    std::vector<std::unique_ptr<SpectralHistoryBuffer>> histories_;
    SpectralModeProcessor modes_;
    ParameterSmoother frameSmoothers_;

    SpectralFrame workingFrame_;
    SpectralFrame analysisForHistory_; // unmodified analysis pushed to history
    std::vector<float> historyMagsScratch_;
    std::vector<std::vector<float>> previousMagnitudes_;
    std::vector<bool> hasPreviousFrame_;
    std::vector<std::uint64_t> frameCounters_;

    VisualizationAtomics viz_;
    SnapshotPublisher snapshots_;
    std::atomic<bool> clearHistoryRequested_ { false };
    ModeParams hopParams_ {};
    SpectralMode currentMode_ = SpectralMode::Shadow;
    float memoryLengthSeconds_ = constants::memoryLengthDefaultSec;
    std::uint32_t vizSequence_ = 0;

    bool freezeTarget_ = false;
    double sampleRate_ = 44100.0;
    int numChannels_ = 2;
    bool prepared_ = false;
};

} // namespace afterimage
