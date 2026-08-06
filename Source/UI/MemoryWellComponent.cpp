#include "MemoryWellComponent.h"
#include "AfterimageFonts.h"
#include "AfterimageLookAndFeel.h"

#include <cmath>

namespace
{
inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }
inline float smoothstep (float t) noexcept
{
    t = juce::jlimit (0.0f, 1.0f, t);
    return t * t * (3.0f - 2.0f * t);
}
} // namespace

MemoryWellComponent::MemoryWellComponent()
{
    setOpaque (false);
    setWantsKeyboardFocus (true);
    setTooltip ("RECALL RING\n"
                "Click or drag in the well to set memory age (outer = newest).\n"
                "Shadow/Merge: historical spectrum in use.\n"
                "Erase: age region that updates the familiarity envelope.");
    startTimerHz (60);
}

MemoryWellComponent::~MemoryWellComponent()
{
    stopTimer();
}

void MemoryWellComponent::setMode (afterimage::SpectralMode mode) { mode_ = mode; }
void MemoryWellComponent::setInfluence (float v) { influence_ = juce::jlimit (0.0f, 1.0f, v); }
void MemoryWellComponent::setMemoryLengthNorm (float v) { memoryNorm_ = juce::jlimit (0.0f, 1.0f, v); }

void MemoryWellComponent::setRecallPosition (float age01)
{
    recallAge_ = juce::jlimit (0.0f, 1.0f, age01);
    if (! draggingRecall_)
        recallDisplay_ = lerp (recallDisplay_, recallAge_, 0.18f);
}

void MemoryWellComponent::applySnapshot (const afterimage::VisualizationSnapshot& snap)
{
    latest_ = snap;
    frozen_ = snap.frozen;
    hasAudio_ = snap.inputPeak > 0.002f || snap.numSeeds > 0;
    memoryNorm_ = snap.memoryLengthNorm;

    if (snap.sequence != lastSequence_)
    {
        lastSequence_ = snap.sequence;
        if (! frozen_)
            spawnFromSnapshot (snap);

        wellBreath_ = lerp (wellBreath_, 1.0f + snap.inputPeak * 0.06f, 0.25f);
        if (snap.transientStrength > 0.35f)
            ripple_ = juce::jmax (ripple_, snap.transientStrength);
    }
}

int MemoryWellComponent::acquireParticle()
{
    for (int n = 0; n < kPoolSize; ++n)
    {
        const int i = (nextParticle_ + n) % kPoolSize;
        if (! particles_[static_cast<size_t> (i)].active)
        {
            nextParticle_ = (i + 1) % kPoolSize;
            return i;
        }
    }
    int oldest = nextParticle_;
    float maxAge = -1.0f;
    for (int i = 0; i < kPoolSize; ++i)
    {
        if (particles_[static_cast<size_t> (i)].age > maxAge)
        {
            maxAge = particles_[static_cast<size_t> (i)].age;
            oldest = i;
        }
    }
    nextParticle_ = (oldest + 1) % kPoolSize;
    return oldest;
}

void MemoryWellComponent::spawnFromSnapshot (const afterimage::VisualizationSnapshot& snap)
{
    const int count = juce::jmin (snap.numSeeds, 48);
    for (int i = 0; i < count; ++i)
    {
        const auto& seed = snap.seeds[i];
        auto& p = particles_[static_cast<size_t> (acquireParticle())];
        p.active = true;
        p.age = -0.055f - seed.magnitude * 0.02f;
        p.life = 0.0f;
        p.angle = seed.freqNorm * juce::MathConstants<float>::twoPi - juce::MathConstants<float>::halfPi;
        p.magnitude = seed.magnitude;
        p.transient = seed.transient;
        p.centroid = seed.centroidNorm;
        p.spiral = (seed.freqNorm - 0.5f) * 0.15f;
    }
}

juce::Rectangle<float> MemoryWellComponent::getStatusBounds() const
{
    return getLocalBounds().toFloat().removeFromTop (kStatusH).reduced (14.0f, 0.0f);
}

juce::Rectangle<float> MemoryWellComponent::getCaptionBounds() const
{
    return getLocalBounds().toFloat().removeFromBottom (kCaptionH).reduced (14.0f, 0.0f);
}

juce::Rectangle<float> MemoryWellComponent::getWellDrawingBounds() const
{
    auto b = getLocalBounds().toFloat();
    b.removeFromTop (kStatusH);
    b.removeFromBottom (kCaptionH + kCaptionClearance);
    return b.reduced (8.0f);
}

juce::Point<float> MemoryWellComponent::wellCentre() const
{
    return getWellDrawingBounds().getCentre();
}

float MemoryWellComponent::wellRadius() const
{
    const auto b = getWellDrawingBounds();
    const float base = juce::jmin (b.getWidth(), b.getHeight()) * 0.46f;
    return base * wellBreath_ * (0.92f + memoryNorm_ * 0.08f);
}

float MemoryWellComponent::recallRingRadius() const
{
    return wellRadius() * (1.0f - smoothstep (recallDisplay_));
}

bool MemoryWellComponent::hitTestsWell (juce::Point<float> p) const
{
    const auto c = wellCentre();
    const float r = wellRadius();
    const float d = c.getDistanceFrom (p);
    if (d <= r + 2.0f)
        return true;

    // Grab tolerance around the recall ring
    const float ringR = recallRingRadius();
    return std::abs (d - ringR) <= kRingGrabPx;
}

void MemoryWellComponent::updateRecallFromPoint (juce::Point<float> p)
{
    const auto c = wellCentre();
    const float r = wellRadius();
    const float d = c.getDistanceFrom (p);
    const float age = juce::jlimit (0.0f, 1.0f, 1.0f - d / juce::jmax (1.0f, r));
    recallAge_ = age;
    if (onRecallChanged)
        onRecallChanged (recallAge_);
}

void MemoryWellComponent::endRecallGesture()
{
    if (! draggingRecall_)
        return;
    draggingRecall_ = false;
    setMouseCursor (hoverNearRing_ ? juce::MouseCursor::DraggingHandCursor
                                   : juce::MouseCursor::NormalCursor);
    if (onRecallGestureEnd)
        onRecallGestureEnd();
}

void MemoryWellComponent::mouseDown (const juce::MouseEvent& e)
{
    if (! hitTestsWell (e.position))
        return;

    draggingRecall_ = true;
    showCaptionHint_ = false;
    if (onRecallGestureStart)
        onRecallGestureStart();
    updateRecallFromPoint (e.position);
}

void MemoryWellComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingRecall_)
        updateRecallFromPoint (e.position);
}

void MemoryWellComponent::mouseUp (const juce::MouseEvent&)
{
    endRecallGesture();
}

void MemoryWellComponent::mouseMove (const juce::MouseEvent& e)
{
    const auto c = wellCentre();
    const float d = c.getDistanceFrom (e.position);
    const float ringR = recallRingRadius();
    const bool near = hitTestsWell (e.position)
                      && (std::abs (d - ringR) <= kRingGrabPx * 1.5f || d <= wellRadius());
    if (near != hoverNearRing_)
    {
        hoverNearRing_ = near;
        setMouseCursor (near ? juce::MouseCursor::DraggingHandCursor
                             : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void MemoryWellComponent::mouseExit (const juce::MouseEvent&)
{
    hoverNearRing_ = false;
    if (! draggingRecall_)
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

bool MemoryWellComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && draggingRecall_)
    {
        endRecallGesture();
        return true;
    }
    return false;
}

void MemoryWellComponent::updateParticles (float dt)
{
    // Ease freezeMotion toward 0 when frozen, toward 1 when live.
    const float freezeTarget = frozen_ ? 0.0f : 1.0f;
    freezeMotion_ = lerp (freezeMotion_, freezeTarget, frozen_ ? 0.08f : 0.12f);
    if (freezeMotion_ < 0.002f)
        freezeMotion_ = 0.0f;

    const float ageSpeed = (0.085f + (1.0f - memoryNorm_) * 0.04f) * freezeMotion_;
    const float spinSpeed = 0.55f * freezeMotion_;

    for (auto& p : particles_)
    {
        if (! p.active)
            continue;

        p.life += dt;

        if (freezeMotion_ > 0.0f)
        {
            float drift = ageSpeed * dt;
            if (mode_ == afterimage::SpectralMode::Erase)
                drift *= 1.35f + p.magnitude * 0.5f;
            else if (mode_ == afterimage::SpectralMode::Merge)
                drift *= 0.85f;

            p.age += drift;
            p.angle += p.spiral * dt * spinSpeed;

            if (freezeMotion_ > 0.5f)
            {
                const float ringDist = std::abs (p.age - recallDisplay_);
                if (ringDist < 0.08f)
                    p.angle += (0.08f - ringDist) * 0.8f * dt * freezeMotion_;
            }
        }

        if (p.age >= 1.0f)
            p.active = false;
    }

    // Idle ambient: one spark on an explicit countdown (no float-modulo bursts).
    if (! hasAudio_ && ! frozen_)
    {
        idleSpawnCountdown_ -= dt;
        if (idleSpawnCountdown_ <= 0.0f)
        {
            auto& p = particles_[static_cast<size_t> (acquireParticle())];
            p.active = true;
            p.age = -0.04f;
            p.life = 0.0f;
            p.angle = ambientPhase_ * 1.7f;
            p.magnitude = 0.08f;
            p.transient = 0.0f;
            p.centroid = 0.45f;
            p.spiral = 0.02f;

            // Deterministic interval ~1.1..2.4 s from ambient phase
            const float phase = std::fmod (ambientPhase_ * 0.37f, 1.0f);
            idleSpawnCountdown_ = 1.1f + phase * 1.3f;
        }
    }
    else
    {
        idleSpawnCountdown_ = 1.4f;
    }
}

void MemoryWellComponent::timerCallback()
{
    constexpr float dt = 1.0f / 60.0f;
    ambientPhase_ += dt;
    freezePulse_ = frozen_ ? lerp (freezePulse_, 1.0f, 0.06f) : lerp (freezePulse_, 0.0f, 0.08f);
    recallDisplay_ = lerp (recallDisplay_, recallAge_, draggingRecall_ ? 0.45f : 0.12f);
    wellBreath_ = lerp (wellBreath_, 1.0f, 0.04f);
    ripple_ = juce::jmax (0.0f, ripple_ - dt * 1.8f);

    updateParticles (dt);
    repaint();
}

void MemoryWellComponent::resized() {}

void MemoryWellComponent::paint (juce::Graphics& g)
{
    const auto centre = wellCentre();
    const float radius = wellRadius();
    const auto status = getStatusBounds();
    const auto caption = getCaptionBounds();

    // Soft radial depth (well region only)
    {
        juce::ColourGradient grad (AfterimageLookAndFeel::panel().brighter (0.04f),
                                   centre.x, centre.y,
                                   AfterimageLookAndFeel::background().darker (0.15f),
                                   centre.x, centre.y - radius * 1.15f,
                                   true);
        g.setGradientFill (grad);
        g.fillEllipse (centre.x - radius * 1.06f, centre.y - radius * 1.06f,
                       radius * 2.12f, radius * 2.12f);
    }

    // Soft outer rim (varied alpha, not a hard outline)
    {
        juce::ColourGradient rim (AfterimageLookAndFeel::panelEdge().withAlpha (0.55f),
                                  centre.x, centre.y - radius,
                                  AfterimageLookAndFeel::panelEdge().withAlpha (0.15f),
                                  centre.x, centre.y + radius,
                                  false);
        g.setGradientFill (rim);
        g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);
    }

    // Quiet concentric guides
    for (int i = 1; i <= 2; ++i)
    {
        const float t = (float) i / 3.0f;
        const float r = radius * (1.0f - t);
        g.setColour (AfterimageLookAndFeel::panelEdge().withAlpha (0.10f));
        g.drawEllipse (centre.x - r, centre.y - r, r * 2.0f, r * 2.0f, 0.6f);
    }

    if (ripple_ > 0.01f)
    {
        const float rr = radius * (0.2f + (1.0f - ripple_) * 0.85f);
        g.setColour (AfterimageLookAndFeel::accentCyan().withAlpha (ripple_ * 0.18f));
        g.drawEllipse (centre.x - rr, centre.y - rr, rr * 2.0f, rr * 2.0f, 1.2f);
    }

    // Particles
    for (const auto& p : particles_)
    {
        if (! p.active)
            continue;

        const float ageClamped = juce::jmax (0.0f, p.age);
        const float radialNorm = p.age < 0.0f ? p.age : smoothstep (p.age);
        const float radial = radius * (1.0f - radialNorm);
        const float x = centre.x + std::cos (p.angle) * radial;
        const float y = centre.y + std::sin (p.angle) * radial;

        const float birth = smoothstep (p.life / 0.35f);
        const float ageFade = 1.0f - ageClamped;
        const float opacity = birth * ageFade * (0.22f + p.magnitude * 0.65f)
                              * (0.5f + influence_ * 0.45f);
        const float size = (1.1f + p.transient * 3.0f + p.magnitude * 1.8f)
                           * (0.2f + 0.8f * birth);

        juce::Colour colour = AfterimageLookAndFeel::accentCyan()
                                  .interpolatedWith (AfterimageLookAndFeel::textPrimary(), p.magnitude * 0.2f);

        if (mode_ == afterimage::SpectralMode::Erase)
            colour = AfterimageLookAndFeel::textMuted().interpolatedWith (
                AfterimageLookAndFeel::background(), 0.25f);
        else if (mode_ == afterimage::SpectralMode::Merge)
            colour = AfterimageLookAndFeel::accentViolet().interpolatedWith (
                AfterimageLookAndFeel::accentCyan(), p.centroid * 0.5f);

        if (mode_ == afterimage::SpectralMode::Shadow && p.magnitude > 0.25f && birth > 0.4f)
        {
            for (int gHost = 1; gHost <= 2; ++gHost)
            {
                const float gHf = (float) gHost;
                const float ga = ageClamped + gHf * 0.04f;
                if (ga >= 1.0f)
                    continue;
                const float gr = radius * (1.0f - smoothstep (ga));
                const float gx = centre.x + std::cos (p.angle - gHf * 0.035f) * gr;
                const float gy = centre.y + std::sin (p.angle - gHf * 0.035f) * gr;
                g.setColour (colour.withAlpha (opacity * (0.18f / gHf)));
                g.fillEllipse (gx - size * 0.4f, gy - size * 0.4f, size * 0.8f, size * 0.8f);
            }
        }

        if (mode_ == afterimage::SpectralMode::Erase && ageClamped > 0.35f)
        {
            g.setColour (AfterimageLookAndFeel::background().withAlpha (opacity * 0.45f));
            g.fillEllipse (x - size * 1.3f, y - size * 1.3f, size * 2.6f, size * 2.6f);
        }

        g.setColour (colour.withAlpha (juce::jlimit (0.0f, 1.0f, opacity)));
        g.fillEllipse (x - size * 0.5f, y - size * 0.5f, size, size);
    }

    if (mode_ == afterimage::SpectralMode::Merge)
    {
        g.setColour (AfterimageLookAndFeel::accentViolet().withAlpha (0.10f + influence_ * 0.10f));
        int drawn = 0;
        for (int i = 0; i < kPoolSize && drawn < 70; ++i)
        {
            const auto& a = particles_[static_cast<size_t> (i)];
            if (! a.active || a.magnitude < 0.25f)
                continue;
            const float ar = radius * (1.0f - smoothstep (juce::jmax (0.0f, a.age)));
            const juce::Point<float> ap (centre.x + std::cos (a.angle) * ar,
                                         centre.y + std::sin (a.angle) * ar);
            for (int j = i + 37; j < kPoolSize && drawn < 70; j += 73)
            {
                const auto& b = particles_[static_cast<size_t> (j)];
                if (! b.active)
                    continue;
                if (std::abs (a.age - b.age) > 0.12f)
                    continue;
                const float br = radius * (1.0f - smoothstep (juce::jmax (0.0f, b.age)));
                const juce::Point<float> bp (centre.x + std::cos (b.angle) * br,
                                             centre.y + std::sin (b.angle) * br);
                if (ap.getDistanceFrom (bp) < radius * 0.22f)
                {
                    g.drawLine ({ ap, bp }, 0.6f);
                    ++drawn;
                }
            }
        }
    }

    // Recall Ring (highest priority when interacting)
    {
        const float rr = recallRingRadius();
        const float glow = draggingRecall_ ? 1.0f : (hoverNearRing_ ? 0.75f : 0.5f);
        g.setColour (AfterimageLookAndFeel::accentCyan().withAlpha (0.12f * glow));
        g.drawEllipse (centre.x - rr - 3.0f, centre.y - rr - 3.0f,
                       (rr + 3.0f) * 2.0f, (rr + 3.0f) * 2.0f, 5.0f);
        g.setColour (AfterimageLookAndFeel::accentCyan().withAlpha (0.85f * glow));
        g.drawEllipse (centre.x - rr, centre.y - rr, rr * 2.0f, rr * 2.0f, 1.8f);

        const float tickAngle = -juce::MathConstants<float>::halfPi;
        const juce::Point<float> tip (centre.x + std::cos (tickAngle) * rr,
                                      centre.y + std::sin (tickAngle) * rr);
        g.setColour (AfterimageLookAndFeel::textPrimary().withAlpha (0.92f));
        g.fillEllipse (tip.x - 3.5f, tip.y - 3.5f, 7.0f, 7.0f);
    }

    if (freezePulse_ > 0.01f)
    {
        g.setColour (AfterimageLookAndFeel::accentWarm().withAlpha (
            0.06f + freezePulse_ * 0.10f * (0.55f + 0.45f * std::sin (ambientPhase_ * 2.0f))));
        g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    }

    // Status strip
    g.setFont (AfterimageFonts::get (AfterimageFontRole::Status));
    g.setColour (AfterimageLookAndFeel::textMuted().withAlpha (0.55f));
    g.drawText ("MEMORY WELL", status, juce::Justification::centredLeft, false);

    if (frozen_)
    {
        g.setColour (AfterimageLookAndFeel::accentWarm().withAlpha (0.9f));
        g.setFont (AfterimageFonts::get (AfterimageFontRole::ControlLabel));
        g.drawText ("TIME HELD", status, juce::Justification::centredRight, false);
    }

    // Caption strip (first-use hint; hidden after first recall interaction)
    if (showCaptionHint_)
    {
        g.setColour (AfterimageLookAndFeel::textMuted().withAlpha (0.4f));
        g.setFont (AfterimageFonts::get (AfterimageFontRole::Caption));
        g.drawFittedText ("Drag the ring to move through memory",
                          caption.toNearestInt(),
                          juce::Justification::centred, 1);
    }
}
