#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/SpectralModes.h"
#include "../DSP/VisualizationAtomics.h"

#include <array>
#include <functional>

/**
    The Memory Well — circular spectral memory visualization.

    Outer rim = newest sound. Center = oldest memory.
    Angle = frequency, radius = age. Driven by DSP snapshot seeds.
*/
class MemoryWellComponent : public juce::Component,
                            public juce::SettableTooltipClient,
                            private juce::Timer
{
public:
    MemoryWellComponent();
    ~MemoryWellComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    void applySnapshot (const afterimage::VisualizationSnapshot& snap);
    void setMode (afterimage::SpectralMode mode);
    void setRecallPosition (float age01);
    void setInfluence (float influence01);
    void setMemoryLengthNorm (float norm01);

    /** Called when user scrubs the Recall Ring (0 = newest, 1 = oldest). */
    std::function<void (float age01)> onRecallChanged;
    std::function<void()> onRecallGestureStart;
    std::function<void()> onRecallGestureEnd;

private:
    struct Particle
    {
        float angle = 0.0f;       // radians
        float age = 0.0f;         // <0 = birth outside rim, 0 = outer/new, 1 = center/old
        float magnitude = 0.0f;
        float transient = 0.0f;
        float centroid = 0.5f;
        float spiral = 0.0f;
        float life = 0.0f;        // seconds since spawn — drives birth fade-in
        bool  active = false;
    };

    void timerCallback() override;
    void spawnFromSnapshot (const afterimage::VisualizationSnapshot& snap);
    void updateParticles (float dt);
    void updateRecallFromPoint (juce::Point<float> p);
    [[nodiscard]] juce::Point<float> wellCentre() const;
    [[nodiscard]] float wellRadius() const;
    [[nodiscard]] int acquireParticle();

    static constexpr int kPoolSize = 2800;

    std::array<Particle, kPoolSize> particles_ {};
    int nextParticle_ = 0;

    afterimage::SpectralMode mode_ = afterimage::SpectralMode::Shadow;
    afterimage::VisualizationSnapshot latest_ {};
    std::uint32_t lastSequence_ = 0;

    float recallAge_ = 0.45f;
    float recallDisplay_ = 0.45f;
    float influence_ = 0.5f;
    float memoryNorm_ = 0.5f;
    float wellBreath_ = 1.0f;
    float freezePulse_ = 0.0f;
    float ripple_ = 0.0f;
    float ambientPhase_ = 0.0f;
    bool  frozen_ = false;
    bool  draggingRecall_ = false;
    bool  hasAudio_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MemoryWellComponent)
};
