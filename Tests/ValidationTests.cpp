#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include "DSP/SpectralEngine.h"
#include "DSP/SpectralHistoryBuffer.h"
#include "DSP/SpectralModes.h"
#include "DSP/STFTProcessor.h"
#include "DSP/SpectralFrame.h"
#include "Utilities/Constants.h"
#include "EffectStrengthMeasure.h"
#include "EffectStrengthMeasure.h"

#include <cmath>
#include <iostream>
#include <vector>

using namespace afterimage;

int gFailures = 0;

void runEffectStrengthTests();
void runLicensingTests();

#define CHECK(cond) \
    do { \
        if (! (cond)) { \
            std::cerr << "FAIL: " << #cond << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++gFailures; \
        } \
    } while (0)

#define CHECK_NEAR(a, b, tol) \
    do { \
        const double _a = (double) (a), _b = (double) (b); \
        if (std::abs (_a - _b) > (tol)) { \
            std::cerr << "FAIL NEAR: " << #a << "=" << _a << " " << #b << "=" << _b \
                      << " tol=" << (tol) << " at " << __LINE__ << "\n"; \
            ++gFailures; \
        } \
    } while (0)

//==============================================================================
static void testHistoryBuffer()
{
    std::cout << "SpectralHistoryBuffer...\n";
    SpectralHistoryBuffer hist;
    hist.prepare (48000.0, constants::hopSize, 1.0f); // 1 second max for faster test

    CHECK (hist.getCapacity() > 0);
    CHECK (hist.getAvailableFrameCount() == 0);
    CHECK (hist.getActiveFrameCount() == 0);

    SpectralFrame frame;
    frame.prepare (constants::numBins);

    for (int i = 0; i < 5; ++i)
    {
        frame.clear();
        frame.magnitudes[0] = (float) i;
        frame.frameIndex = (uint64_t) i;
        hist.pushFrame (frame);
    }

    CHECK (hist.getAvailableFrameCount() == 5);
    CHECK_NEAR (hist.getFrameByAgeFrames (0).magnitudes[0], 4.0f, 1e-6);
    CHECK_NEAR (hist.getFrameByAgeFrames (4).magnitudes[0], 0.0f, 1e-6);
    CHECK_NEAR (hist.getFrameByNormalizedAge (0.0f).magnitudes[0], 4.0f, 1e-6);

    // Wraparound
    const int cap = hist.getCapacity();
    for (int i = 0; i < cap + 10; ++i)
    {
        frame.magnitudes[0] = 100.0f + (float) i;
        frame.frameIndex = (uint64_t) (1000 + i);
        hist.pushFrame (frame);
    }
    CHECK (hist.getAvailableFrameCount() == cap);
    CHECK_NEAR (hist.getFrameByAgeFrames (0).magnitudes[0], 100.0f + (float) (cap + 9), 1e-4);

    // Active memory window shorter than capacity
    hist.setActiveMemoryLengthSeconds (0.1f);
    CHECK (hist.getActiveFrameCount() <= hist.getAvailableFrameCount());
    CHECK (hist.getActiveFrameCount() >= 1);

    // Interpolation
    std::vector<float> dest ((size_t) constants::numBins, 0.0f);
    hist.getInterpolatedMagnitudes (0.0f, dest.data(), constants::numBins);
    CHECK_NEAR (dest[0], hist.getFrameByAgeFrames (0).magnitudes[0], 1e-4);

    // Freeze
    hist.setFrozen (true);
    const float before = hist.getFrameByAgeFrames (0).magnitudes[0];
    frame.magnitudes[0] = -999.0f;
    hist.pushFrame (frame);
    CHECK_NEAR (hist.getFrameByAgeFrames (0).magnitudes[0], before, 1e-6);
    hist.setFrozen (false);

    hist.clear();
    CHECK (hist.getAvailableFrameCount() == 0);
    CHECK_NEAR (hist.getFrameByAgeFrames (0).magnitudes[0], 0.0f, 1e-6);
}

//==============================================================================
static void testHistoryInterpolationFocused()
{
    std::cout << "History fractional interpolation...\n";
    SpectralHistoryBuffer hist;
    hist.prepare (48000.0, constants::hopSize, 2.0f);

    SpectralFrame frame;
    frame.prepare (constants::numBins);

    // Empty → zeros
    std::vector<float> dest ((size_t) constants::numBins, -1.0f);
    hist.getInterpolatedMagnitudes (0.5f, dest.data(), constants::numBins);
    CHECK_NEAR (dest[0], 0.0f, 1e-7);

    // Single frame
    frame.magnitudes[10] = 3.0f;
    hist.pushFrame (frame);
    hist.getInterpolatedMagnitudes (0.0f, dest.data(), constants::numBins);
    CHECK_NEAR (dest[10], 3.0f, 1e-6);
    hist.getInterpolatedMagnitudes (1.0f, dest.data(), constants::numBins);
    CHECK_NEAR (dest[10], 3.0f, 1e-6);

    // Two frames: newest=5, oldest=1 at bin 0
    hist.clear();
    frame.clear();
    frame.magnitudes[0] = 1.0f;
    hist.pushFrame (frame);
    frame.magnitudes[0] = 5.0f;
    hist.pushFrame (frame);

    hist.getInterpolatedMagnitudes (0.0f, dest.data(), constants::numBins);
    CHECK_NEAR (dest[0], 5.0f, 1e-5); // newest
    hist.getInterpolatedMagnitudes (1.0f, dest.data(), constants::numBins);
    CHECK_NEAR (dest[0], 1.0f, 1e-5); // oldest
    hist.getInterpolatedMagnitudes (0.5f, dest.data(), constants::numBins);
    CHECK_NEAR (dest[0], 3.0f, 1e-4); // lerp midpoint

    // Clamp out-of-range age
    hist.getInterpolatedMagnitudes (-0.5f, dest.data(), constants::numBins);
    CHECK_NEAR (dest[0], 5.0f, 1e-5);
    hist.getInterpolatedMagnitudes (1.5f, dest.data(), constants::numBins);
    CHECK_NEAR (dest[0], 1.0f, 1e-5);
}

//==============================================================================
static void testForgetAgeWeight()
{
    std::cout << "Forget age weighting...\n";

    for (float forget : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        float prev = 2.0f;
        for (float age = 0.0f; age <= 1.0f + 1e-6f; age += 0.1f)
        {
            const float w = ageWeightFromForget (age, forget);
            CHECK (std::isfinite (w));
            CHECK (w >= 0.0f && w <= 1.0f);
            CHECK (w <= prev + 1e-6f); // monotonic non-increasing in age
            prev = w;
        }
    }

    // Stronger decay at higher Forget for a mid/old age
    const float wLow = ageWeightFromForget (0.8f, 0.1f);
    const float wHigh = ageWeightFromForget (0.8f, 0.9f);
    CHECK (wHigh < wLow);

    const float c0 = forgetToDecayCoefficient (0.0f);
    const float c1 = forgetToDecayCoefficient (1.0f);
    CHECK (c1 > c0);
    CHECK (std::isfinite (c0) && std::isfinite (c1));
}

//==============================================================================
static void testBlur()
{
    std::cout << "Historical magnitude blur...\n";
    constexpr int N = 64;
    std::vector<float> in ((size_t) N, 0.0f);
    std::vector<float> out ((size_t) N, 0.0f);
    std::vector<float> prefix ((size_t) (N + 1), 0.0f);

    // Blur=0 / radius 0 → exact identity
    in[32] = 1.0f;
    boxBlurMagnitudes (in.data(), out.data(), N, 0, prefix.data());
    for (int i = 0; i < N; ++i)
        CHECK_NEAR (out[(size_t) i], in[(size_t) i], 1e-7);

    CHECK (blurRadiusFromAmount (0.0f) == 0);
    CHECK (blurRadiusFromAmount (1.0f) == constants::maxBlurRadiusBins);

    // Impulse spreads with positive radius
    boxBlurMagnitudes (in.data(), out.data(), N, 4, prefix.data());
    CHECK (out[32] > 0.0f);
    CHECK (out[32] < 1.0f);
    CHECK (out[30] > 0.0f);
    CHECK (out[0] >= 0.0f); // edges finite / non-negative

    double eIn = 0.0, eOut = 0.0;
    for (int i = 0; i < N; ++i)
    {
        eIn += (double) in[(size_t) i] * in[(size_t) i];
        eOut += (double) out[(size_t) i] * out[(size_t) i];
        CHECK (std::isfinite (out[(size_t) i]));
    }
    // Energy redistributed but remains bounded (box blur conserves sum, not sum-squares)
    CHECK (eOut > 0.0);
    CHECK (eOut < eIn * 2.0 + 1e-6); // sum-squares should not explode

    // Max radius bounded
    const int rMax = blurRadiusFromAmount (1.0f);
    boxBlurMagnitudes (in.data(), out.data(), N, rMax, prefix.data());
    for (int i = 0; i < N; ++i)
        CHECK (std::isfinite (out[(size_t) i]) && out[(size_t) i] >= 0.0f);
}

//==============================================================================
static void testShadowInfluenceZeroIdentity()
{
    std::cout << "Shadow Influence 0% = identity...\n";
    SpectralModeProcessor modes;
    modes.prepare (constants::numBins, 48000.0, 1);

    SpectralFrame frame;
    frame.prepare (constants::numBins);
    std::vector<float> hist ((size_t) constants::numBins, 0.0f);

    for (int i = 0; i < constants::numBins; ++i)
    {
        frame.magnitudes[(size_t) i] = 0.1f + 0.01f * (float) (i % 17);
        hist[(size_t) i] = 2.0f; // very different history
    }

    std::vector<float> original = frame.magnitudes;

    ModeParams p;
    p.influence = 0.0f;
    p.forget = 0.5f;
    p.blur = 0.8f;
    p.transientPreserve = 0.5f;
    p.transientStrength = 0.8f;
    p.recallAge01 = 0.5f;

    const bool changed = modes.process (SpectralMode::Shadow, frame, p, hist.data(), 0, constants::hopSize);
    CHECK (! changed);
    for (int i = 0; i < constants::numBins; ++i)
        CHECK_NEAR (frame.magnitudes[(size_t) i], original[(size_t) i], 1e-6);
}

//==============================================================================
static void testShadowMeasurableChange()
{
    std::cout << "Shadow measurable spectral change...\n";
    SpectralModeProcessor modes;
    modes.prepare (constants::numBins, 48000.0, 1);

    SpectralFrame frame;
    frame.prepare (constants::numBins);
    std::vector<float> hist ((size_t) constants::numBins, 0.0f);

    for (int i = 0; i < constants::numBins; ++i)
    {
        frame.magnitudes[(size_t) i] = 0.2f;
        hist[(size_t) i] = (i == 40) ? 5.0f : 0.0f;
    }

    ModeParams p;
    p.influence = 0.8f;
    p.forget = 0.0f;
    p.blur = 0.0f;
    p.transientPreserve = 0.0f;
    p.transientStrength = 0.0f;
    p.recallAge01 = 0.0f;

    modes.applyShadowMagnitudes (frame.magnitudes.data(), hist.data(), constants::numBins, p, 0);

    CHECK (frame.magnitudes[40] > 0.2f + 0.1f); // ghost peak added
    for (int i = 0; i < constants::numBins; ++i)
        CHECK (std::isfinite (frame.magnitudes[(size_t) i]));
}

//==============================================================================
static void testShadowSilenceAndExtremes()
{
    std::cout << "Shadow silence / extremes / finite...\n";
    SpectralModeProcessor modes;
    modes.prepare (constants::numBins, 48000.0, 1);

    SpectralFrame frame;
    frame.prepare (constants::numBins);
    std::vector<float> hist ((size_t) constants::numBins, 0.0f);

    ModeParams p;
    p.influence = 1.0f;
    p.forget = 1.0f;
    p.blur = 1.0f;
    p.transientPreserve = 1.0f;
    p.transientStrength = 1.0f;
    p.recallAge01 = 1.0f;

    modes.applyShadowMagnitudes (frame.magnitudes.data(), hist.data(), constants::numBins, p, 0);
    for (int i = 0; i < constants::numBins; ++i)
    {
        CHECK (std::isfinite (frame.magnitudes[(size_t) i]));
        CHECK_NEAR (frame.magnitudes[(size_t) i], 0.0f, 1e-5);
    }

    // Extreme magnitudes
    for (int i = 0; i < constants::numBins; ++i)
    {
        frame.magnitudes[(size_t) i] = 50.0f;
        hist[(size_t) i] = 50.0f;
    }
    modes.applyShadowMagnitudes (frame.magnitudes.data(), hist.data(), constants::numBins, p, 0);
    for (int i = 0; i < constants::numBins; ++i)
        CHECK (std::isfinite (frame.magnitudes[(size_t) i]));
}

//==============================================================================
static void testTransientPreserve()
{
    std::cout << "Transient preserve reduces influence...\n";
    SpectralModeProcessor modes;
    modes.prepare (constants::numBins, 48000.0, 1);

    auto run = [&] (float transient, float preserve) -> float
    {
        SpectralFrame frame;
        frame.prepare (constants::numBins);
        std::vector<float> hist ((size_t) constants::numBins, 0.0f);
        frame.magnitudes[20] = 1.0f;
        hist[20] = 10.0f;

        ModeParams p;
        p.influence = 1.0f;
        p.forget = 0.0f;
        p.blur = 0.0f;
        p.transientPreserve = preserve;
        p.transientStrength = transient;
        p.recallAge01 = 0.0f;

        // Reset energy/transient smoothers between runs
        modes.reset();
        modes.applyShadowMagnitudes (frame.magnitudes.data(), hist.data(), constants::numBins, p, 0);
        return frame.magnitudes[20];
    };

    const float noPreserve = run (1.0f, 0.0f);
    const float fullPreserve = run (1.0f, 1.0f);
    CHECK (fullPreserve < noPreserve); // less history bleed when preserving transients
}

//==============================================================================
static void testEraseMergeInfluenceZeroIdentity()
{
    std::cout << "Erase/Merge Influence 0% = identity...\n";
    SpectralModeProcessor modes;
    modes.prepare (constants::numBins, 48000.0, 1);

    for (auto mode : { SpectralMode::Erase, SpectralMode::Merge })
    {
        SpectralFrame frame;
        frame.prepare (constants::numBins);
        std::vector<float> hist ((size_t) constants::numBins, 9.0f);
        for (int i = 0; i < constants::numBins; ++i)
            frame.magnitudes[(size_t) i] = 0.5f;
        auto original = frame.magnitudes;

        ModeParams p;
        p.influence = 0.0f;
        p.forget = 0.0f;
        p.blur = 0.5f;
        p.recallAge01 = 0.3f;

        modes.reset();
        modes.setMode (mode);
        for (int i = 0; i < 200; ++i)
            modes.tickModeCrossfade (constants::hopSize);

        CHECK (! modes.process (mode, frame, p, hist.data(), 0, constants::hopSize));
        for (int i = 0; i < constants::numBins; ++i)
            CHECK_NEAR (frame.magnitudes[(size_t) i], original[(size_t) i], 1e-6);
    }
}

//==============================================================================
static void testEraseSuppressesOverlap()
{
    std::cout << "Erase suppresses overlapping bins...\n";
    SpectralModeProcessor modes;
    modes.prepare (constants::numBins, 48000.0, 1);
    modes.reset();

    SpectralFrame frame;
    frame.prepare (constants::numBins);
    std::vector<float> hist ((size_t) constants::numBins, 0.0f);

    for (int i = 0; i < constants::numBins; ++i)
        frame.magnitudes[(size_t) i] = 1.0f;
    hist[40] = 8.0f; // strong memory only at bin 40

    ModeParams p;
    p.influence = 1.0f;
    p.forget = 0.0f;
    p.blur = 0.0f;
    p.transientPreserve = 0.0f;
    p.transientStrength = 0.0f;
    p.recallAge01 = 0.0f;

    modes.applyEraseMagnitudes (frame.magnitudes.data(), hist.data(), constants::numBins, p, 0);

    CHECK (frame.magnitudes[40] < 0.95f); // carved where history overlaps
    CHECK (frame.magnitudes[10] > 0.95f); // untouched where history is empty
    for (int i = 0; i < constants::numBins; ++i)
    {
        CHECK (std::isfinite (frame.magnitudes[(size_t) i]));
        CHECK (frame.magnitudes[(size_t) i] >= 0.0f);
    }
}

//==============================================================================
static void testMergePullsTowardHistory()
{
    std::cout << "Merge morphs toward history...\n";
    SpectralModeProcessor modes;
    modes.prepare (constants::numBins, 48000.0, 1);
    modes.reset();

    SpectralFrame frame;
    frame.prepare (constants::numBins);
    std::vector<float> hist ((size_t) constants::numBins, 0.0f);

    for (int i = 0; i < constants::numBins; ++i)
    {
        frame.magnitudes[(size_t) i] = 0.2f;
        hist[(size_t) i] = 2.0f;
    }

    ModeParams p;
    p.influence = 1.0f;
    p.forget = 0.0f;
    p.blur = 0.0f;
    p.transientPreserve = 0.0f;
    p.transientStrength = 0.0f;
    p.recallAge01 = 0.0f;

    modes.applyMergeMagnitudes (frame.magnitudes.data(), hist.data(), constants::numBins, p, 0);

    // Full mix toward history (energy compensation may scale, but direction is clear)
    CHECK (frame.magnitudes[20] > 0.5f);
    for (int i = 0; i < constants::numBins; ++i)
        CHECK (std::isfinite (frame.magnitudes[(size_t) i]));
}

//==============================================================================
static void testModeCrossfadeFinite()
{
    std::cout << "Mode crossfade finite...\n";
    SpectralModeProcessor modes;
    modes.prepare (constants::numBins, 48000.0, 1);
    modes.reset();

    const SpectralMode cycle[] = {
        SpectralMode::Shadow, SpectralMode::Erase, SpectralMode::Merge
    };

    SpectralFrame frame;
    frame.prepare (constants::numBins);
    std::vector<float> hist ((size_t) constants::numBins, 1.0f);
    for (int i = 0; i < constants::numBins; ++i)
        frame.magnitudes[(size_t) i] = 0.4f;

    ModeParams p;
    p.influence = 0.7f;
    p.forget = 0.2f;
    p.blur = 0.3f;
    p.recallAge01 = 0.4f;

    for (int i = 0; i < 60; ++i)
    {
        const auto mode = cycle[i % 3];
        modes.setMode (mode);
        modes.tickModeCrossfade (constants::hopSize);
        CHECK (std::isfinite (modes.getModeAmount()));
        CHECK (modes.getModeAmount() >= 0.0f && modes.getModeAmount() <= 1.0f);

        auto work = frame;
        CHECK (modes.process (mode, work, p, hist.data(), 0, constants::hopSize));
        for (int b = 0; b < constants::numBins; ++b)
        {
            CHECK (std::isfinite (work.magnitudes[(size_t) b]));
            CHECK (work.magnitudes[(size_t) b] >= 0.0f);
        }
    }

    // Settle fully onto Merge
    modes.setMode (SpectralMode::Merge);
    for (int i = 0; i < 200; ++i)
        modes.tickModeCrossfade (constants::hopSize);
    CHECK_NEAR (modes.getModeAmount(), 1.0f, 1e-3f);
}

//==============================================================================
static void testPhaseWriteback()
{
    std::cout << "Phase writeback DC/Nyquist...\n";
    constexpr int N = constants::fftSize;
    std::vector<float> interleaved ((size_t) (2 * N), 0.0f);
    std::vector<float> mags ((size_t) constants::numBins, 0.0f);
    std::vector<float> phases ((size_t) constants::numBins, 0.0f);

    mags[0] = 2.0f;
    phases[0] = 1.2f; // should still force imag=0 at DC
    mags[N / 2] = 1.5f;
    phases[N / 2] = -0.7f;
    mags[5] = 3.0f;
    phases[5] = 0.4f;

    writeInterleavedFromMagnitudePhase (interleaved.data(), N, mags.data(), phases.data(), constants::numBins);

    CHECK_NEAR (interleaved[1], 0.0f, 1e-6);           // DC imag
    CHECK_NEAR (interleaved[2 * (N / 2) + 1], 0.0f, 1e-6); // Nyquist imag
    CHECK_NEAR (interleaved[2 * 5], 3.0f * std::cos (0.4f), 1e-5);
    CHECK_NEAR (interleaved[2 * 5 + 1], 3.0f * std::sin (0.4f), 1e-5);
}

//==============================================================================
static void testRandomRecallWander()
{
    std::cout << "Random Recall wander...\n";

    const float hopSec = static_cast<float> (constants::hopSize) / 48000.0f;
    const float recall = 0.45f;

    // Random = 0 → exact recall position (wander state may still advance internally)
    {
        float offset = 0.0f;
        std::uint32_t rng = 0x12345678u;
        for (int i = 0; i < 500; ++i)
        {
            const float age = computeRandomRecallAge (offset, rng, recall, 0.0f, hopSec);
            CHECK_NEAR (age, recall, 1e-6);
            CHECK (age >= 0.0f && age <= 1.0f);
        }
    }

    // Random = 1 → ages stay in [0,1] and slowly vary (not stuck, not chaotic jumps)
    {
        float offset = 0.0f;
        std::uint32_t rng = 0xC0FFEE01u;
        float prev = computeRandomRecallAge (offset, rng, recall, 1.0f, hopSec);
        float minAge = prev, maxAge = prev;
        double hopDeltaSum = 0.0;
        int hops = 0;

        for (int i = 0; i < 2000; ++i)
        {
            const float age = computeRandomRecallAge (offset, rng, recall, 1.0f, hopSec);
            CHECK (age >= 0.0f && age <= 1.0f);
            CHECK (std::isfinite (age));
            CHECK (std::abs (age - prev) < 0.08f); // slow: no per-hop chaos
            hopDeltaSum += std::abs (age - prev);
            ++hops;
            minAge = std::min (minAge, age);
            maxAge = std::max (maxAge, age);
            prev = age;
        }

        CHECK (maxAge - minAge > 0.05f); // actually wanders over time
        CHECK (hopDeltaSum / hops < 0.02); // average hop step is small
        std::cout << "  wander span=" << (maxAge - minAge)
                  << " meanHopDelta=" << (hopDeltaSum / hops) << "\n";
    }

    // Depth scales with random amount
    {
        float o0 = 0.5f, o1 = 0.5f;
        std::uint32_t r0 = 1, r1 = 1;
        // Same RNG seed / offset: smaller random → closer to centre
        const float aLow = computeRandomRecallAge (o0, r0, 0.5f, 0.2f, hopSec);
        const float aHigh = computeRandomRecallAge (o1, r1, 0.5f, 1.0f, hopSec);
        CHECK (std::abs (aLow - 0.5f) <= std::abs (aHigh - 0.5f) + 1e-5f);
    }
}

//==============================================================================
static void testEngineInfluenceZeroAndFreeze()
{
    std::cout << "Engine Influence0 / Freeze / stereo / blocks...\n";

    auto runCase = [] (int blockSize, int channels)
    {
        SpectralEngine engine;
        engine.prepare (48000.0, 512, channels);
        engine.setMode (SpectralMode::Shadow);
        engine.setActiveMemoryLengthSeconds (1.0f);
        engine.setSpectralParameterTargets (0.0f, 0.5f, 0.3f, 0.2f, 0.5f, 0.0f, false);

        // Let frame-rate Influence smoother settle at 0 before measuring identity.
        {
            juce::AudioBuffer<float> warm (channels, 48000);
            warm.clear();
            engine.process (warm);
        }

        const int latency = engine.getLatencySamples();
        const int total = latency + blockSize * 20;
        juce::AudioBuffer<float> buf (channels, total);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < total; ++i)
                buf.setSample (ch, i, 0.2f * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * (double) i / 48000.0));

        juce::AudioBuffer<float> dry;
        dry.makeCopyOf (buf);

        for (int offset = 0; offset < total; )
        {
            const int n = std::min (blockSize, total - offset);
            std::vector<float*> ptrs ((size_t) channels);
            for (int ch = 0; ch < channels; ++ch)
                ptrs[(size_t) ch] = buf.getWritePointer (ch) + offset;
            juce::AudioBuffer<float> view (ptrs.data(), channels, n);
            engine.process (view);
            offset += n;
        }

        bool hasNaN = false;
        double maxErr = 0.0;
        for (int i = latency; i < total; ++i)
        {
            for (int ch = 0; ch < channels; ++ch)
            {
                const float y = buf.getSample (ch, i);
                const float x = dry.getSample (ch, i - latency);
                if (! std::isfinite (y)) hasNaN = true;
                maxErr = std::max (maxErr, (double) std::abs (y - x));
            }
        }
        CHECK (! hasNaN);
        CHECK (maxErr < 0.05);
        std::cout << "  influence0 block=" << blockSize << " ch=" << channels
                  << " maxErr=" << maxErr << "\n";
    };

    for (int bs : { 64, 512, 1024 })
        for (int ch : { 1, 2 })
            runCase (bs, ch);

    // Freeze: history stops growing; reads remain valid
    {
        SpectralEngine engine;
        engine.prepare (48000.0, 512, 2);
        engine.setMode (SpectralMode::Shadow);
        engine.setSpectralParameterTargets (0.7f, 0.3f, 0.2f, 0.0f, 0.0f, 0.0f, false);

        juce::AudioBuffer<float> buf (2, 8192);
        for (int i = 0; i < 8192; ++i)
        {
            buf.setSample (0, i, 0.3f * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (double) i / 48000.0));
            buf.setSample (1, i, 0.3f * std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * (double) i / 48000.0));
        }
        engine.process (buf);

        const int filled = engine.getHistory (0).getAvailableFrameCount();
        CHECK (filled > 0);

        engine.setSpectralParameterTargets (0.7f, 0.3f, 0.2f, 0.0f, 0.0f, 0.0f, true);
        for (int i = 0; i < 8192; ++i)
        {
            buf.setSample (0, i, 0.9f);
            buf.setSample (1, i, -0.9f);
        }
        engine.process (buf);
        CHECK (engine.getHistory (0).getAvailableFrameCount() == filled);
        CHECK (engine.getHistory (1).getAvailableFrameCount() == engine.getHistory (0).getAvailableFrameCount()
               || engine.getHistory (1).getAvailableFrameCount() > 0);

        // Recall still works on frozen history
        std::vector<float> dest ((size_t) constants::numBins, 0.0f);
        engine.getHistory (0).getInterpolatedMagnitudes (0.0f, dest.data(), constants::numBins);
        engine.getHistory (0).getInterpolatedMagnitudes (1.0f, dest.data(), constants::numBins);
        for (float v : dest)
            CHECK (std::isfinite (v));
    }
}

//==============================================================================
static void testEngineShadowAudibleVsIdentity()
{
    std::cout << "Engine Shadow vs identity (measurable)...\n";

    auto processEngine = [] (float influence) -> juce::AudioBuffer<float>
    {
        SpectralEngine engine;
        engine.prepare (48000.0, 512, 2);
        engine.setMode (SpectralMode::Shadow);
        engine.setActiveMemoryLengthSeconds (2.0f);
        engine.setSpectralParameterTargets (influence, 0.6f, 0.1f, 0.0f, 0.0f, 0.0f, false);

        const int total = 48000; // 1 second
        juce::AudioBuffer<float> buf (2, total);
        for (int i = 0; i < total; ++i)
        {
            // Burst then different tone — builds distinct history
            const float env = (i < 8000) ? 1.0f : ((i < 16000) ? 0.0f : 1.0f);
            const double f = (i < 8000) ? 880.0 : 220.0;
            const float s = env * 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * f * (double) i / 48000.0);
            buf.setSample (0, i, s);
            buf.setSample (1, i, s * 0.7f);
        }
        engine.process (buf);
        return buf;
    };

    auto idBuf = processEngine (0.0f);
    auto shBuf = processEngine (0.85f);

    double diff = 0.0;
    const int start = constants::fftSize + 20000;
    for (int i = start; i < idBuf.getNumSamples(); ++i)
        diff += std::abs (shBuf.getSample (0, i) - idBuf.getSample (0, i));

    CHECK (diff > 1.0); // Shadow should diverge measurably after history is filled
    std::cout << "  cumulative |shadow-identity| after fill=" << diff << "\n";
}

//==============================================================================
static void testStereoIsolation()
{
    std::cout << "Stereo isolation (independent L/R)...\n";
    SpectralEngine engine;
    engine.prepare (48000.0, 512, 2);
    engine.setMode (SpectralMode::Shadow);
    engine.setSpectralParameterTargets (0.9f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f, false);

    juce::AudioBuffer<float> buf (2, 24000);
    buf.clear();
    // Only left channel has energy
    for (int i = 0; i < 24000; ++i)
        buf.setSample (0, i, 0.5f * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * (double) i / 48000.0));

    engine.process (buf);

    // Right history should be near-silent frames; left should have energy
    std::vector<float> leftM ((size_t) constants::numBins), rightM ((size_t) constants::numBins);
    engine.getHistory (0).getInterpolatedMagnitudes (0.0f, leftM.data(), constants::numBins);
    engine.getHistory (1).getInterpolatedMagnitudes (0.0f, rightM.data(), constants::numBins);

    double leftE = 0.0, rightE = 0.0;
    for (int i = 0; i < constants::numBins; ++i)
    {
        leftE += (double) leftM[(size_t) i] * leftM[(size_t) i];
        rightE += (double) rightM[(size_t) i] * rightM[(size_t) i];
    }
    CHECK (leftE > rightE * 10.0 + 1e-6);
    std::cout << "  leftE=" << leftE << " rightE=" << rightE << "\n";
}

//==============================================================================
static void testFftLayout()
{
    std::cout << "JUCE real-only FFT layout...\n";
    constexpr int order = 8; // 256 — faster than 2048 for unit test
    constexpr int N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) (2 * N), 0.0f);

    // DC: constant signal
    std::fill (buf.begin(), buf.begin() + N, 1.0f);
    std::fill (buf.begin() + N, buf.end(), 0.0f);
    fft.performRealOnlyForwardTransform (buf.data(), false);
    CHECK (std::abs (buf[0]) > 1.0f);           // DC real significant
    CHECK_NEAR (buf[1], 0.0f, 1e-3f);           // DC imag ~0
    // Nyquist at complex index N/2 → floats N, N+1
    CHECK_NEAR (buf[N + 1], 0.0f, 1e-3f);

    // Bin-centered cosine at bin k=4
    const int kTarget = 4;
    std::fill (buf.begin(), buf.end(), 0.0f);
    for (int n = 0; n < N; ++n)
        buf[(size_t) n] = std::cos (2.0f * juce::MathConstants<float>::pi * (float) kTarget * (float) n / (float) N);
    fft.performRealOnlyForwardTransform (buf.data(), false);

    float maxMag = 0.0f;
    int maxBin = -1;
    for (int k = 0; k <= N / 2; ++k)
    {
        const float re = buf[2 * k];
        const float im = buf[2 * k + 1];
        const float mag = std::sqrt (re * re + im * im);
        if (mag > maxMag) { maxMag = mag; maxBin = k; }
    }
    CHECK (maxBin == kTarget);

    // Round-trip identity on impulse
    std::fill (buf.begin(), buf.end(), 0.0f);
    buf[0] = 1.0f;
    std::vector<float> original (buf.begin(), buf.begin() + N);
    fft.performRealOnlyForwardTransform (buf.data(), false);
    fft.performRealOnlyInverseTransform (buf.data());
    double err = 0.0;
    for (int i = 0; i < N; ++i)
        err = std::max (err, (double) std::abs (buf[(size_t) i] - original[(size_t) i]));
    CHECK (err < 1e-5);
    std::cout << "  FFT impulse round-trip max err=" << err << "\n";
}

//==============================================================================
static void runStftIdentityCase (double sampleRate, int preparedBlock, int hostBlock, int numChannels)
{
    STFTProcessor stft;
    stft.prepare (sampleRate, preparedBlock, numChannels);
    stft.setSpectrumCallback (nullptr, nullptr); // identity

    const int latency = stft.getLatencySamples();
    CHECK (latency == constants::fftSize);

    const int totalSamples = latency + hostBlock * 8 + 4096;
    juce::AudioBuffer<float> input (numChannels, totalSamples);
    juce::AudioBuffer<float> output (numChannels, totalSamples);
    input.clear();
    output.clear();

    // Deterministic signal: impulse at 0 + low sine
    for (int ch = 0; ch < numChannels; ++ch)
    {
        input.setSample (ch, 0, 1.0f);
        for (int i = 0; i < totalSamples; ++i)
            input.addSample (ch, i, 0.25f * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * (double) i / sampleRate));
    }

    output.makeCopyOf (input);

    // Process in host-sized blocks (may exceed preparedBlock)
    for (int offset = 0; offset < totalSamples; )
    {
        const int n = std::min (hostBlock, totalSamples - offset);
        std::vector<float*> ptrs ((size_t) numChannels);
        for (int ch = 0; ch < numChannels; ++ch)
            ptrs[(size_t) ch] = output.getWritePointer (ch) + offset;
        juce::AudioBuffer<float> view (ptrs.data(), numChannels, n);
        stft.process (view);
        offset += n;
    }

    // Silence prefix before primed output is OK; compare after latency
    double maxAbsErr = 0.0;
    double maxAbsOut = 0.0;
    bool hasNaN = false;
    int impulsePeakAt = -1;
    float impulsePeak = 0.0f;

    for (int i = 0; i < totalSamples; ++i)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float y = output.getSample (ch, i);
            if (! std::isfinite (y)) hasNaN = true;
            maxAbsOut = std::max (maxAbsOut, (double) std::abs (y));
            if (std::abs (y) > impulsePeak) { impulsePeak = std::abs (y); impulsePeakAt = i; }

            if (i >= latency)
            {
                const float x = input.getSample (ch, i - latency);
                maxAbsErr = std::max (maxAbsErr, (double) std::abs (y - x));
            }
        }
    }

    CHECK (! hasNaN);
    CHECK (impulsePeakAt >= 0);
    // Impulse energy should appear near reported latency (allow ±hop tolerance for WOLA)
    CHECK (std::abs (impulsePeakAt - latency) <= constants::hopSize);

    std::cout << "  sr=" << sampleRate << " prep=" << preparedBlock << " host=" << hostBlock
              << " ch=" << numChannels << " latency=" << latency
              << " maxErr=" << maxAbsErr << " impulseAt=" << impulsePeakAt << "\n";

    // After startup, reconstruction should be tight for this Hann/WOLA setup
    CHECK (maxAbsErr < 0.05); // report measured; keep threshold practical
}

static void testStftIdentity()
{
    std::cout << "STFT identity...\n";

    // Silence
    {
        STFTProcessor stft;
        stft.prepare (48000.0, 512, 2);
        juce::AudioBuffer<float> buf (2, 4096);
        buf.clear();
        stft.process (buf);
        CHECK_NEAR (buf.getMagnitude (0, 0, 4096), 0.0f, 1e-7);
    }

    // WOLA table consistency
    {
        STFTProcessor stft;
        stft.prepare (48000.0, 512, 1);
        CHECK (stft.getMaxWolaScaleDeviation() < 0.05f); // should be nearly constant
        std::cout << "  WOLA max relative deviation=" << stft.getMaxWolaScaleDeviation() << "\n";
    }

    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    const int blocks[] = { 1, 31, 64, 127, 256, 512, 1024, 3000 }; // 3000 > prepared

    for (double sr : rates)
        for (int hostBlock : blocks)
            for (int ch : { 1, 2 })
                runStftIdentityCase (sr, 512, hostBlock, ch);
}

//==============================================================================
int main()
{
    std::cout << "AFTERIMAGE validation tests\n";
    testHistoryBuffer();
    testHistoryInterpolationFocused();
    testForgetAgeWeight();
    testBlur();
    testShadowInfluenceZeroIdentity();
    testShadowMeasurableChange();
    testShadowSilenceAndExtremes();
    testTransientPreserve();
    testEraseMergeInfluenceZeroIdentity();
    testEraseSuppressesOverlap();
    testMergePullsTowardHistory();
    testModeCrossfadeFinite();
    testPhaseWriteback();
    testFftLayout();
    testStftIdentity();
    testRandomRecallWander();
    testEngineInfluenceZeroAndFreeze();
    testEngineShadowAudibleVsIdentity();
    testStereoIsolation();
    runEffectStrengthTests();
    runLicensingTests();

    afterimage::measure::runEffectStrengthMeasurements (
        afterimage::measure::envWantsMeasurements());

    if (gFailures == 0)
    {
        std::cout << "ALL TESTS PASSED\n";
        return 0;
    }

    std::cerr << gFailures << " FAILURES\n";
    return 1;
}
