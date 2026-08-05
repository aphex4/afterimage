#pragma once

#include "SpectralFrame.h"
#include "../Utilities/Constants.h"

#include <vector>

namespace afterimage
{

/**
    Circular buffer of SpectralFrame objects.

    Allocates storage for the maximum memory length (10 s) during prepare().
    Changing Memory Length only changes the searchable window — never reallocates
    on the audio thread.
*/
class SpectralHistoryBuffer
{
public:
    void prepare (double sampleRate, int hopSize, float maxMemorySeconds = constants::memoryLengthMaxSec);
    void reset();
    void clear();

    void setActiveMemoryLengthSeconds (float seconds) noexcept;
    [[nodiscard]] float getActiveMemoryLengthSeconds() const noexcept { return activeMemorySeconds_; }

    void setFrozen (bool shouldFreeze) noexcept { frozen_ = shouldFreeze; }
    [[nodiscard]] bool isFrozen() const noexcept { return frozen_; }

    /** Copy a prepared frame into the ring (no-op when frozen). */
    void pushFrame (const SpectralFrame& frame) noexcept;

    /**
        Direct write API — avoids an intermediate frame copy.
        Returns nullptr when frozen or unprepared. Caller fills the frame,
        then calls commitWriteFrame().
    */
    [[nodiscard]] SpectralFrame* beginWriteFrame() noexcept;
    void commitWriteFrame() noexcept;

    [[nodiscard]] const SpectralFrame& getFrameByAgeFrames (int age) const noexcept;
    [[nodiscard]] const SpectralFrame& getFrameByNormalizedAge (float age01) const noexcept;

    /** Interpolate between the two frames nearest to age01 (0 = newest). */
    void getInterpolatedMagnitudes (float age01, float* destMagnitudes, int numBins) const noexcept;

    [[nodiscard]] int getAvailableFrameCount() const noexcept { return availableFrames_; }
    [[nodiscard]] int getActiveFrameCount() const noexcept;
    [[nodiscard]] int getCapacity() const noexcept { return capacity_; }

private:
    std::vector<SpectralFrame> frames_;
    SpectralFrame emptyFrame_;

    int capacity_ = 0;
    int writeIndex_ = 0;
    int availableFrames_ = 0;

    double sampleRate_ = 44100.0;
    int hopSize_ = constants::hopSize;
    float activeMemorySeconds_ = constants::memoryLengthDefaultSec;
    bool frozen_ = false;
};

} // namespace afterimage
