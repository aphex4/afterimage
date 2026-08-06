#include "MemoryWellComponent.h"
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

        // Bass expansion / snare ripple from levels
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
    // Steal oldest
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
        // Start just outside the rim so particles drift in rather than popping on.
        p.age = -0.055f - seed.magnitude * 0.02f;
        p.life = 0.0f;
        p.angle = seed.freqNorm * juce::MathConstants<float>::twoPi - juce::MathConstants<float>::halfPi;
        p.magnitude = seed.magnitude;
        p.transient = seed.transient;
        p.centroid = seed.centroidNorm;
        p.spiral = (seed.freqNorm - 0.5f) * 0.15f;
    }
}

juce::Point<float> MemoryWellComponent::wellCentre() const
{
    return getLocalBounds().toFloat().getCentre();
}

float MemoryWellComponent::wellRadius() const
{
    const auto b = getLocalBounds().toFloat();
    const float base = juce::jmin (b.getWidth(), b.getHeight()) * 0.42f;
    return base * wellBreath_ * (0.92f + memoryNorm_ * 0.08f);
}

void MemoryWellComponent::updateRecallFromPoint (juce::Point<float> p)
{
    const auto c = wellCentre();
    const float r = wellRadius();
    const float d = c.getDistanceFrom (p);
    // Outer = 0 (newest), center = 1 (oldest)
    const float age = juce::jlimit (0.0f, 1.0f, 1.0f - d / juce::jmax (1.0f, r));
    recallAge_ = age;
    if (onRecallChanged)
        onRecallChanged (recallAge_);
}

void MemoryWellComponent::mouseDown (const juce::MouseEvent& e)
{
    draggingRecall_ = true;
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
    if (draggingRecall_)
    {
        draggingRecall_ = false;
        if (onRecallGestureEnd)
            onRecallGestureEnd();
    }
}

void MemoryWellComponent::updateParticles (float dt)
{
    const float ageSpeed = frozen_ ? 0.015f : (0.085f + (1.0f - memoryNorm_) * 0.04f);
    const float freezeDrag = frozen_ ? 0.92f : 1.0f;

    for (auto& p : particles_)
    {
        if (! p.active)
            continue;

        p.life += dt;
        float drift = ageSpeed * dt * freezeDrag;

        if (mode_ == afterimage::SpectralMode::Erase)
            drift *= 1.35f + p.magnitude * 0.5f; // dissolve faster
        else if (mode_ == afterimage::SpectralMode::Merge)
            drift *= 0.85f;

        p.age += drift;
        p.angle += p.spiral * dt * (frozen_ ? 0.15f : 0.55f);

        // Soft disturbance near recall ring
        const float ringDist = std::abs (p.age - recallDisplay_);
        if (ringDist < 0.08f)
            p.angle += (0.08f - ringDist) * 0.8f * dt;

        if (p.age >= 1.0f)
            p.active = false;
    }

    // Idle ambient sparks when silent
    if (! hasAudio_ && ! frozen_ && (int) (ambientPhase_ * 10.0f) % 17 == 0)
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
    const auto bounds = getLocalBounds().toFloat();
    const auto centre = wellCentre();
    const float radius = wellRadius();

    // Soft radial depth
    {
        juce::ColourGradient grad (AfterimageLookAndFeel::panel().brighter (0.04f),
                                   centre.x, centre.y,
                                   AfterimageLookAndFeel::background().darker (0.2f),
                                   centre.x, centre.y - radius * 1.2f,
                                   true);
        g.setGradientFill (grad);
        g.fillEllipse (centre.x - radius * 1.08f, centre.y - radius * 1.08f,
                       radius * 2.16f, radius * 2.16f);
    }

    // Outer rim
    g.setColour (AfterimageLookAndFeel::panelEdge().withAlpha (0.9f));
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.2f);

    // Concentric age guides
    for (int i = 1; i <= 3; ++i)
    {
        const float t = (float) i / 4.0f;
        const float r = radius * (1.0f - t);
        g.setColour (AfterimageLookAndFeel::panelEdge().withAlpha (0.18f));
        g.drawEllipse (centre.x - r, centre.y - r, r * 2.0f, r * 2.0f, 0.7f);
    }

    // Transient ripple
    if (ripple_ > 0.01f)
    {
        const float rr = radius * (0.2f + (1.0f - ripple_) * 0.85f);
        g.setColour (AfterimageLookAndFeel::accentCyan().withAlpha (ripple_ * 0.25f));
        g.drawEllipse (centre.x - rr, centre.y - rr, rr * 2.0f, rr * 2.0f, 1.5f);
    }

    // Particles
    for (const auto& p : particles_)
    {
        if (! p.active)
            continue;

        // Negative age = just outside the rim (birth). Positive age drifts inward.
        const float ageClamped = juce::jmax (0.0f, p.age);
        const float radialNorm = p.age < 0.0f ? p.age : smoothstep (p.age);
        const float radial = radius * (1.0f - radialNorm);
        const float x = centre.x + std::cos (p.angle) * radial;
        const float y = centre.y + std::sin (p.angle) * radial;

        // Fade + grow in over ~350ms so particles don't pop into existence.
        const float birth = smoothstep (p.life / 0.35f);
        const float ageFade = 1.0f - ageClamped;
        const float opacity = birth * ageFade * (0.25f + p.magnitude * 0.75f)
                              * (0.55f + influence_ * 0.45f);
        const float size = (1.2f + p.transient * 3.5f + p.magnitude * 2.0f)
                           * (0.2f + 0.8f * birth);

        juce::Colour colour = AfterimageLookAndFeel::accentCyan()
                                  .interpolatedWith (AfterimageLookAndFeel::accentViolet(), p.centroid)
                                  .interpolatedWith (juce::Colours::white, p.magnitude * 0.25f);

        if (mode_ == afterimage::SpectralMode::Erase)
            colour = AfterimageLookAndFeel::textMuted().interpolatedWith (AfterimageLookAndFeel::background(), 0.3f);
        else if (mode_ == afterimage::SpectralMode::Merge)
            colour = AfterimageLookAndFeel::accentViolet().interpolatedWith (AfterimageLookAndFeel::accentCyan(), p.centroid);

        // Shadow afterimages
        if (mode_ == afterimage::SpectralMode::Shadow && p.magnitude > 0.2f && birth > 0.35f)
        {
            for (int gHost = 1; gHost <= 3; ++gHost)
            {
                const float gHf = static_cast<float> (gHost);
                const float ga = ageClamped + gHf * 0.035f;
                if (ga >= 1.0f)
                    continue;
                const float gr = radius * (1.0f - smoothstep (ga));
                const float gx = centre.x + std::cos (p.angle - gHf * 0.04f) * gr;
                const float gy = centre.y + std::sin (p.angle - gHf * 0.04f) * gr;
                g.setColour (colour.withAlpha (opacity * (0.22f / gHf)));
                g.fillEllipse (gx - size * 0.45f, gy - size * 0.45f, size * 0.9f, size * 0.9f);
            }
        }

        // Erase: dark voids
        if (mode_ == afterimage::SpectralMode::Erase && ageClamped > 0.35f)
        {
            g.setColour (AfterimageLookAndFeel::background().withAlpha (opacity * 0.55f));
            g.fillEllipse (x - size * 1.4f, y - size * 1.4f, size * 2.8f, size * 2.8f);
        }

        g.setColour (colour.withAlpha (juce::jlimit (0.0f, 1.0f, opacity)));
        g.fillEllipse (x - size * 0.5f, y - size * 0.5f, size, size);
    }

    // Merge strands — connect nearby particles sparsely
    if (mode_ == afterimage::SpectralMode::Merge)
    {
        g.setColour (AfterimageLookAndFeel::accentViolet().withAlpha (0.12f + influence_ * 0.12f));
        int drawn = 0;
        for (int i = 0; i < kPoolSize && drawn < 90; ++i)
        {
            const auto& a = particles_[static_cast<size_t> (i)];
            if (! a.active || a.magnitude < 0.25f)
                continue;
            const float ar = radius * (1.0f - smoothstep (a.age));
            const juce::Point<float> ap (centre.x + std::cos (a.angle) * ar,
                                         centre.y + std::sin (a.angle) * ar);

            for (int j = i + 37; j < kPoolSize && drawn < 90; j += 73)
            {
                const auto& b = particles_[static_cast<size_t> (j)];
                if (! b.active)
                    continue;
                if (std::abs (a.age - b.age) > 0.12f)
                    continue;
                const float br = radius * (1.0f - smoothstep (b.age));
                const juce::Point<float> bp (centre.x + std::cos (b.angle) * br,
                                             centre.y + std::sin (b.angle) * br);
                if (ap.getDistanceFrom (bp) < radius * 0.22f)
                {
                    g.drawLine ({ ap, bp }, 0.7f);
                    ++drawn;
                }
            }
        }
    }

    // Recall Ring
    {
        const float rr = radius * (1.0f - smoothstep (recallDisplay_));
        const float glow = draggingRecall_ ? 0.95f : 0.55f;
        g.setColour (AfterimageLookAndFeel::accentCyan().withAlpha (0.15f * glow));
        g.drawEllipse (centre.x - rr - 3.0f, centre.y - rr - 3.0f, (rr + 3.0f) * 2.0f, (rr + 3.0f) * 2.0f, 6.0f);
        g.setColour (AfterimageLookAndFeel::accentCyan().withAlpha (0.75f * glow));
        g.drawEllipse (centre.x - rr, centre.y - rr, rr * 2.0f, rr * 2.0f, 1.6f);

        // Handle tick
        const float tickAngle = -juce::MathConstants<float>::halfPi;
        const juce::Point<float> tip (centre.x + std::cos (tickAngle) * rr,
                                      centre.y + std::sin (tickAngle) * rr);
        g.setColour (AfterimageLookAndFeel::textPrimary().withAlpha (0.9f));
        g.fillEllipse (tip.x - 3.5f, tip.y - 3.5f, 7.0f, 7.0f);
    }

    // Freeze icy pulse
    if (freezePulse_ > 0.01f)
    {
        g.setColour (AfterimageLookAndFeel::accentWarm().withAlpha (0.08f + freezePulse_ * 0.12f
                                                                   * (0.5f + 0.5f * std::sin (ambientPhase_ * 2.0f))));
        g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    }

    // Labels
    g.setColour (AfterimageLookAndFeel::textMuted().withAlpha (0.65f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("MEMORY WELL", bounds.reduced (18.0f).removeFromTop (18.0f),
                juce::Justification::topLeft, false);

    if (frozen_)
    {
        g.setColour (AfterimageLookAndFeel::accentWarm().withAlpha (0.9f));
        g.setFont (juce::FontOptions (12.0f).withStyle ("Bold"));
        g.drawText ("TIME HELD", bounds.reduced (18.0f).removeFromTop (18.0f),
                    juce::Justification::topRight, false);
    }

    g.setColour (AfterimageLookAndFeel::textMuted().withAlpha (0.4f));
    g.setFont (juce::FontOptions (10.0f));
    g.drawFittedText ("drag the ring to reach through time",
                      bounds.reduced (18.0f).removeFromBottom (22).toNearestInt(),
                      juce::Justification::centred, 1);
}
