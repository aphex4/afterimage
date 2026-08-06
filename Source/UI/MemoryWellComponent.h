#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../DSP/SpectralModes.h"
#include "../DSP/VisualizationAtomics.h"

#include <array>
#include <functional>

/**
    The Memory Well: circular spectral memory visualization.

    Layout regions: optional status strip, central well, lower caption strip.
    wellCentre/wellRadius are computed from the central drawing region only.

    Recall interaction: click/drag inside the well radius (or near the ring)
    moves Recall. Clicks outside the well do nothing.
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
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;

    void applySnapshot (const afterimage::VisualizationSnapshot& snap);
    void setMode (afterimage::SpectralMode mode);
    void setRecallPosition (float age01);
    void setInfluence (float influence01);
    void setMemoryLengthNorm (float norm01);

    std::function<void (float age01)> onRecallChanged;
    std::function<void()> onRecallGestureStart;
    std::function<void()> onRecallGestureEnd;

private:
    struct Particle
    {
        float angle = 0.0f;
        float age = 0.0f;     // <0 birth outside rim, 0 outer/new, 1 center/old
        float magnitude = 0.0f;
        float transient = 0.0f;
        float centroid = 0.5f;
        float spiral = 0.0f;
        float life = 0.0f;
        bool  active = false;
    };

    void timerCallback() override;
    void spawnFromSnapshot (const afterimage::VisualizationSnapshot& snap);
    void updateParticles (float dt);
    void updateRecallFromPoint (juce::Point<float> p);
    void endRecallGesture();

    [[nodiscard]] juce::Rectangle<float> getStatusBounds() const;
    [[nodiscard]] juce::Rectangle<float> getWellDrawingBounds() const;
    [[nodiscard]] juce::Rectangle<float> getCaptionBounds() const;
    [[nodiscard]] juce::Point<float> wellCentre() const;
    [[nodiscard]] float wellRadius() const;
    [[nodiscard]] float recallRingRadius() const;
    [[nodiscard]] bool hitTestsWell (juce::Point<float> p) const;
    [[nodiscard]] int acquireParticle();

    static constexpr int kPoolSize = 2800;
    static constexpr float kStatusH = 22.0f;
    static constexpr float kCaptionH = 28.0f;
    static constexpr float kCaptionClearance = 18.0f;
    static constexpr float kRingGrabPx = 14.0f;

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
    float freezeMotion_ = 1.0f; // 1 = full motion, 0 = fully settled
    float ripple_ = 0.0f;
    float ambientPhase_ = 0.0f;
    float idleSpawnCountdown_ = 1.4f;
    bool  frozen_ = false;
    bool  draggingRecall_ = false;
    bool  hasAudio_ = false;
    bool  hoverNearRing_ = false;
    bool  showCaptionHint_ = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MemoryWellComponent)
};
