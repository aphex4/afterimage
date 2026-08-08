#pragma once

#include "STFTProcessor.h"
#include "SpectralHistoryBuffer.h"
#include "SpectralMemoryProfile.h"
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
    Slow Random Recall wander: one-pole LPF on bipolar noise.

    @param wanderOffset  smoothed state in [-1, 1] (mutated)
    @param rng           xorshift state (mutated)
    @return effective recall age in [0, 1]
*/
[[nodiscard]] float computeRandomRecallAge (float& wanderOffset,
                                            std::uint32_t& rng,
                                            float recallPosition,
                                            float randomAmount,
                                            float hopSeconds) noexcept;

/**
    STFT + per-channel spectral history + memory profiles + modes.

    Audio-thread only for history mutation. UI reads VisualizationAtomics.
    History is read BEFORE committing the current analysis frame.
    Modes consume stabilized SpectralMemoryProfile magnitudes (not raw frames).
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

    /** Snap frame smoothers to current targets (call after setSpectralParameterTargets on prepare). */
    void snapSpectralSmoothersToTargets() noexcept { frameSmoothers_.snapSpectralToTargets(); }

    /** Read current frame-smoother values (tests / startup verification). */
    [[nodiscard]] float getSmoothedInfluence() const noexcept { return frameSmoothers_.influence.getCurrentValue(); }
    [[nodiscard]] float getSmoothedRecall() const noexcept { return frameSmoothers_.recallPosition.getCurrentValue(); }
    [[nodiscard]] float getSmoothedForget() const noexcept { return frameSmoothers_.forget.getCurrentValue(); }
    [[nodiscard]] float getSmoothedBlur() const noexcept { return frameSmoothers_.blur.getCurrentValue(); }
    [[nodiscard]] float getSmoothedTransient() const noexcept { return frameSmoothers_.transientPreserve.getCurrentValue(); }

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

    /** Diagnostics / tests: effective memory profile after Freeze crossfade. */
    [[nodiscard]] const SpectralMemoryProfile& getEffectiveMemoryProfile (int channel = 0) const noexcept;
    [[nodiscard]] float getFreezeCrossfadeAmount() const noexcept { return freezeCrossfade_; }
    [[nodiscard]] bool isFreezeEngaged() const noexcept { return freezeEngaged_; }

private:
    static void spectrumCallback (void* userData,
                                  float* interleavedFftData,
                                  int fftSize,
                                  int channelIndex) noexcept;

    void onSpectrum (float* interleavedFftData, int fftSize, int channelIndex) noexcept;
    void clearHistoryOnAudioThread() noexcept;
    void publishVisualization (int channelIndex) noexcept;
    void updateFreezeState (bool freezeTarget, int hopSamples) noexcept;
    void buildEffectiveProfile (int channelIndex, float recallAge01) noexcept;

    STFTProcessor stft_;
    std::vector<std::unique_ptr<SpectralHistoryBuffer>> histories_;
    SpectralModeProcessor modes_;
    ParameterSmoother frameSmoothers_;

    SpectralFrame workingFrame_;
    SpectralFrame analysisForHistory_; // unmodified analysis pushed to history

    // Per-channel memory profiles (live / frozen / crossfade result)
    std::vector<SpectralMemoryProfile> liveProfiles_;
    std::vector<SpectralMemoryProfile> frozenProfiles_;
    std::vector<SpectralMemoryProfile> effectiveProfiles_;
    /** Scratch for Shadow multi-age taps (owned by modes, but prepared with engine SR). */
    SpectralMemoryProfile tapProfileScratch_;

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

    // Random Recall wander (audio-thread state; no alloc)
    float wanderOffset_ = 0.0f;
    std::uint32_t wanderRng_ = 0xA5F1C3E9u;

    bool freezeTarget_ = false;
    bool freezeEngaged_ = false;
    bool freezeWasTarget_ = false;
    float freezeCrossfade_ = 1.0f; // 0 = live, 1 = fully frozen

    double sampleRate_ = 44100.0;
    int numChannels_ = 2;
    bool prepared_ = false;
};

} // namespace afterimage
