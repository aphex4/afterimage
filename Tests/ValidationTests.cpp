#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include "DSP/SpectralEngine.h"
#include "DSP/SpectralHistoryBuffer.h"
#include "DSP/SpectralMemoryProfile.h"
#include "DSP/SpectralModes.h"
#include "DSP/STFTProcessor.h"
#include "DSP/SpectralFrame.h"
#include "Utilities/Constants.h"
#include "Utilities/DebugSafety.h"
#include "SpectralFixtures.h"
#include "EffectStrengthMeasure.h"

#include <cmath>
#include <iostream>
#include <vector>

using namespace afterimage;

int gFailures = 0;

void runEffectStrengthTests();
void runLicensingTests();
void runGainMatchTests();
int runScaleTheoryTests();

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
static void testSpectralMemoryProfile()
{
    std::cout << "SpectralMemoryProfile temporal averaging...\n";

    constexpr double sr = 48000.0;
    SpectralHistoryBuffer hist;
    hist.prepare (sr, constants::hopSize, 2.0f);

    SpectralFrame frame;
    frame.prepare (constants::numBins);

    // Fill ~500 ms of history with a stable tone + one spike frame
    const int frames = 50;
    for (int i = 0; i < frames; ++i)
    {
        frame.clear();
        frame.magnitudes[10] = 1.0f;
        frame.magnitudes[20] = 0.5f;
        if (i == frames - 5)
            frame.magnitudes[100] = 20.0f; // transient spike
        hist.pushFrame (frame);
    }

    SpectralMemoryProfile profile;
    profile.prepare (constants::numBins, sr, constants::hopSize);

    MemoryProfileOptions opts;
    opts.windowMs = 200.0f;
    opts.applyStability = true;
    profile.buildFromHistory (hist, 0.0f, opts); // newest-centered

    CHECK (profile.getFramesUsed() > 1);
    CHECK (profile.getEnergy() > 0.0f);
    CHECK (std::isfinite (profile.getEnergy()));

    // Stable bins preserved
    CHECK (profile.getMagnitudes()[10] > 0.5f);
    CHECK (profile.getMagnitudes()[20] > 0.2f);

    // Spike bin attenuated vs raw single-frame capture
    const float spiked = hist.getFrameByAgeFrames (4).magnitudes[100];
    CHECK (spiked > 10.0f);
    CHECK (profile.getMagnitudes()[100] < spiked * 0.5f);

    // Boundary: age at oldest edge still builds
    profile.buildFromHistory (hist, 1.0f, opts);
    CHECK (profile.getFramesUsed() >= 1);
    CHECK (std::isfinite (profile.getMagnitudes()[10]));
    CHECK (profile.getMagnitudes()[10] >= 0.0f);

    // Empty history → zeros
    hist.clear();
    profile.buildFromHistory (hist, 0.5f, opts);
    CHECK_NEAR (profile.getEnergy(), 0.0f, 1e-7);
    CHECK_NEAR (profile.getMagnitudes()[10], 0.0f, 1e-7);

    // Gaussian weight peaks at center
    CHECK_NEAR (SpectralMemoryProfile::gaussianWeight (0.0f, 2.0f), 1.0f, 1e-6);
    CHECK (SpectralMemoryProfile::gaussianWeight (4.0f, 2.0f)
           < SpectralMemoryProfile::gaussianWeight (1.0f, 2.0f));

    // Capture recent uses multiple frames
    for (int i = 0; i < 30; ++i)
    {
        frame.clear();
        frame.magnitudes[5] = 2.0f;
        if (i == 29)
            frame.magnitudes[50] = 15.0f;
        hist.pushFrame (frame);
    }
    profile.captureRecent (hist, 200.0f, opts);
    CHECK (profile.getFramesUsed() > 1);
    CHECK (profile.getMagnitudes()[5] > 1.0f);
    CHECK (profile.getMagnitudes()[50] < 15.0f * 0.6f);

    // Lerp / copy finite
    SpectralMemoryProfile a, b, out;
    a.prepare (constants::numBins, sr, constants::hopSize);
    b.prepare (constants::numBins, sr, constants::hopSize);
    out.prepare (constants::numBins, sr, constants::hopSize);
    a.captureRecent (hist, 180.0f, opts);
    b.buildFromHistory (hist, 0.4f, opts);
    out.lerpFrom (a, b, 0.5f);
    CHECK (std::isfinite (out.getEnergy()));
    out.copyFrom (a);
    CHECK_NEAR (out.getEnergy(), a.getEnergy(), 1e-4);
}

//==============================================================================
static void testFreezeProfileCaptureAndLimiter()
{
    std::cout << "Freeze profile capture / contrast limiter / Shadow decay...\n";

    constexpr double sr = 48000.0;
    SpectralHistoryBuffer hist;
    hist.prepare (sr, constants::hopSize, 2.0f);

    SpectralFrame frame;
    frame.prepare (constants::numBins);

    // Vocal-like formants + one harsh spike frame (simulates unlucky Freeze instant)
    for (int i = 0; i < 40; ++i)
    {
        frame.clear();
        for (int k = 1; k < constants::numBins; ++k)
        {
            const float f1 = std::exp (-0.5f * std::pow ((k - 40) / 12.0f, 2.0f));
            const float f2 = 0.7f * std::exp (-0.5f * std::pow ((k - 120) / 18.0f, 2.0f));
            frame.magnitudes[(size_t) k] = f1 + f2 + 0.02f;
        }
        if (i == 39)
            frame.magnitudes[200] = 25.0f; // consonant/spike
        hist.pushFrame (frame);
    }

    SpectralMemoryProfile single, stabilized;
    single.prepare (constants::numBins, sr, constants::hopSize);
    stabilized.prepare (constants::numBins, sr, constants::hopSize);

    // Single-frame "old Freeze" proxy
    const auto& spikeFrame = hist.getFrameByAgeFrames (0);
    for (int k = 0; k < constants::numBins; ++k)
        single.getMagnitudesWritable()[k] = spikeFrame.magnitudes[(size_t) k];

    MemoryProfileOptions opts;
    opts.applyStability = true;
    stabilized.captureRecent (hist, constants::freezeCaptureWindowMs, opts);

    const float singleSpike = spikeFrame.magnitudes[200];
    const float stabSpike = stabilized.getMagnitudes()[200];
    CHECK (stabSpike < singleSpike * 0.5f);

    // Isolated-peak contrast vs local neighborhood (not adjacent-bin ratio into silence floor)
    auto peakContrast = [] (const float* m, int peakBin) -> float
    {
        float local = 0.0f;
        int count = 0;
        for (int k = peakBin - 4; k <= peakBin + 4; ++k)
        {
            if (k == peakBin || k < 1 || k >= constants::numBins)
                continue;
            local += m[k];
            ++count;
        }
        local = (local / (float) std::max (1, count)) + 1.0e-6f;
        return m[peakBin] / local;
    };
    const float singleContrast = peakContrast (single.getMagnitudes(), 200);
    const float stabContrast = peakContrast (stabilized.getMagnitudes(), 200);
    CHECK (stabContrast < singleContrast * 0.75f);
    std::cout << "  peakContrast single=" << singleContrast << " stabilized=" << stabContrast
              << " spikeMag " << singleSpike << "→" << stabSpike << "\n";

    // Per-bin limiter softens isolated peaks
    std::vector<float> mags ((size_t) constants::numBins, 0.1f);
    mags[50] = 8.0f;
    std::vector<float> scratch ((size_t) constants::numBins, 0.0f);
    std::vector<float> prefix ((size_t) constants::numBins + 1, 0.0f);
    const float before = mags[50];
    applyPerBinContrastLimiter (mags.data(), constants::numBins, 12.0f, scratch.data(), prefix.data());
    CHECK (mags[50] < before);
    CHECK (mags[50] > 0.1f);
    CHECK (std::isfinite (mags[50]));

    // Multi-tap Shadow: low Forget keeps more older-memory energy than high Forget
    SpectralModeProcessor modes;
    modes.prepare (constants::numBins, sr, 1);
    std::vector<float> destLow ((size_t) constants::numBins, 0.0f);
    std::vector<float> destHigh ((size_t) constants::numBins, 0.0f);
    ModeParams p;
    p.influence = 1.0f;
    p.recallAge01 = 0.55f;
    p.recallPosition = 0.55f;
    p.memoryLengthSeconds = 4.0f;
    p.blur = 0.0f;
    p.transientPreserve = 0.0f;

    auto runForget = [&] (float forget, std::vector<float>& dest)
    {
        modes.reset();
        p.forget = forget;
        dest.assign ((size_t) constants::numBins, 0.02f);
        modes.applyShadowMagnitudes (dest.data(), stabilized.getMagnitudes(),
                                     constants::numBins, p, 0);
    };

    runForget (0.1f, destLow);
    runForget (0.9f, destHigh);
    double eLow = 0.0, eHigh = 0.0;
    for (int i = 0; i < constants::numBins; ++i)
    {
        // Ghost contribution ≈ out - dry current
        const float gLow = std::max (0.0f, destLow[(size_t) i] - 0.02f);
        const float gHigh = std::max (0.0f, destHigh[(size_t) i] - 0.02f);
        eLow += (double) gLow * gLow;
        eHigh += (double) gHigh * gHigh;
        CHECK (std::isfinite (destLow[(size_t) i]));
    }
    CHECK (eLow > eHigh * 1.05); // low Forget retains more recalled energy
    CHECK (eLow > 0.0);

    // Engine Freeze capture + crossfade
    SpectralEngine engine;
    engine.prepare (sr, 512, 2);
    engine.setMode (SpectralMode::Shadow);
    engine.setActiveMemoryLengthSeconds (3.0f);
    engine.setSpectralParameterTargets (0.5f, 0.4f, 0.25f, 0.2f, 0.3f, 0.0f, false);
    engine.snapSpectralSmoothersToTargets();

    juce::AudioBuffer<float> buf (2, 512);
    for (int n = 0; n < 80; ++n)
    {
        for (int s = 0; s < 512; ++s)
        {
            const float t = (float) (n * 512 + s) / (float) sr;
            const float v = 0.2f * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * t);
            buf.setSample (0, s, v);
            buf.setSample (1, s, v * 0.9f);
        }
        engine.process (buf);
    }

    CHECK (engine.getHistory (0).getAvailableFrameCount() > 10);
    engine.setSpectralParameterTargets (0.5f, 0.4f, 0.25f, 0.2f, 0.3f, 0.0f, true);
    for (int n = 0; n < 20; ++n)
    {
        buf.clear();
        engine.process (buf);
    }
    CHECK (engine.isFreezeEngaged());
    CHECK (engine.getFreezeCrossfadeAmount() > 0.5f);
    CHECK (engine.getEffectiveMemoryProfile (0).getEnergy() > 0.0f);
    CHECK (std::isfinite (engine.getEffectiveMemoryProfile (0).getEnergy()));
}

//==============================================================================
static void testFreezeArmUntilHistoryReady()
{
    std::cout << "Freeze arms until history ready (empty / preset load)...\n";

    constexpr double sr = 48000.0;
    SpectralEngine engine;
    engine.prepare (sr, 512, 2);
    engine.setMode (SpectralMode::Shadow);
    engine.setActiveMemoryLengthSeconds (4.0f);

    // Freeze on with empty history (Frozen Choir / factory preset path).
    engine.setSpectralParameterTargets (0.62f, 0.45f, 0.15f, 0.35f, 0.30f, 0.0f, true);
    engine.snapSpectralSmoothersToTargets();

    CHECK (engine.isFreezeTarget());
    CHECK (engine.isFreezeArmed());
    CHECK (! engine.isFreezeEngaged());
    CHECK (! engine.getHistory (0).isFrozen());

    juce::AudioBuffer<float> silence (2, 512);
    silence.clear();
    for (int n = 0; n < 40; ++n)
        engine.process (silence);

    // Silence alone must not lock an empty freeze profile.
    CHECK (engine.isFreezeArmed());
    CHECK (! engine.isFreezeEngaged());
    CHECK (! engine.getHistory (0).isFrozen());
    CHECK (engine.getHistory (0).getAvailableFrameCount() > 0);

    juce::AudioBuffer<float> buf (2, 512);
    auto fillTone = [&] (int blockIndex)
    {
        for (int s = 0; s < 512; ++s)
        {
            const float t = (float) (blockIndex * 512 + s) / (float) sr;
            const float v = 0.25f * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * t);
            buf.setSample (0, s, v);
            buf.setSample (1, s, v * 0.95f);
        }
    };

    bool captured = false;
    for (int n = 0; n < 80; ++n)
    {
        fillTone (n);
        engine.process (buf);
        if (engine.isFreezeEngaged())
        {
            captured = true;
            break;
        }
    }

    CHECK (captured);
    CHECK (engine.isFreezeEngaged());
    CHECK (! engine.isFreezeArmed());
    CHECK (engine.getHistory (0).isFrozen());
    CHECK (engine.getEffectiveMemoryProfile (0).getEnergy() > 0.0f);

    const int lockedFrames = engine.getHistory (0).getAvailableFrameCount();
    for (int n = 0; n < 20; ++n)
    {
        fillTone (n + 100);
        engine.process (buf);
    }
    CHECK (engine.getHistory (0).getAvailableFrameCount() == lockedFrames);

    // Frozen Shadow must remain audible on silence (non-identity) once the tail locked.
    double wetEnergy = 0.0;
    for (int n = 0; n < 40; ++n)
    {
        silence.clear();
        engine.process (silence);
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < silence.getNumSamples(); ++s)
            {
                const float y = silence.getSample (ch, s);
                wetEnergy += (double) y * (double) y;
                CHECK (std::isfinite (y));
            }
    }
    CHECK (wetEnergy > 1.0e-4);

    // Cancel arming if Freeze turns off before capture.
    SpectralEngine engine2;
    engine2.prepare (sr, 512, 1);
    engine2.setMode (SpectralMode::Merge);
    engine2.setSpectralParameterTargets (0.5f, 0.4f, 0.25f, 0.3f, 0.3f, 0.0f, true);
    CHECK (engine2.isFreezeArmed());
    engine2.setSpectralParameterTargets (0.5f, 0.4f, 0.25f, 0.3f, 0.3f, 0.0f, false);
    CHECK (! engine2.isFreezeArmed());
    CHECK (! engine2.isFreezeEngaged());

    // Clear-history while Freeze stays on must re-arm (preset reload).
    engine.requestClearHistory();
    silence.clear();
    engine.process (silence);
    CHECK (engine.isFreezeTarget());
    CHECK (engine.isFreezeArmed());
    CHECK (! engine.isFreezeEngaged());
    CHECK (! engine.getHistory (0).isFrozen());
}

//==============================================================================
static void testMergeEnvelopeAndNaNGuard()
{
    std::cout << "Merge blur / NaN / gain bounds...\n";
    SpectralModeProcessor modes;
    modes.prepare (constants::numBins, 48000.0, 1);

    std::vector<float> cur, hist;
    fixtures::fillFormant (cur, 40.0f, 120.0f, 1.0f);
    fixtures::fillFormant (hist, 55.0f, 90.0f, 1.0f);

    ModeParams p;
    p.influence = 0.55f;
    p.blur = 0.5f;
    p.forget = 0.25f;
    p.recallAge01 = 0.4f;
    p.recallPosition = 0.4f;
    p.transientPreserve = 0.0f;

    auto before = cur;
    for (int frame = 0; frame < 48; ++frame)
    {
        cur = before;
        modes.applyMergeMagnitudes (cur.data(), hist.data(), constants::numBins, p, 0);
    }

    double eIn = 0.0, eOut = 0.0;
    for (int i = 0; i < constants::numBins; ++i)
    {
        CHECK (std::isfinite (cur[(size_t) i]));
        CHECK (cur[(size_t) i] >= 0.0f);
        eIn += (double) before[(size_t) i] * before[(size_t) i];
        eOut += (double) cur[(size_t) i] * cur[(size_t) i];
    }
    const float ratio = (float) std::sqrt (eOut / std::max (eIn, 1e-20));
    // Unit-test magnitude fold-in includes incoherent OLA compensation — allow headroom.
    CHECK (ratio > 0.2f && ratio < 6.0f);
    CHECK (fixtures::logSpectralDistance (cur.data(), before.data(), constants::numBins) > 0.05);
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

    const bool changed = modes.process (SpectralMode::Shadow, frame, p, hist.data(), nullptr, 0, constants::hopSize);
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
    p.memoryLengthSeconds = 3.0f;

    for (int hop = 0; hop < 48; ++hop)
    {
        for (int i = 0; i < constants::numBins; ++i)
            frame.magnitudes[(size_t) i] = 0.2f;
        modes.applyShadowMagnitudes (frame.magnitudes.data(), hist.data(), constants::numBins, p, 0);
    }

    CHECK (frame.magnitudes[40] > 0.2f + 0.1f); // ghost peak added after accumulator warm-up
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

        // Reset + warm SpectralTail so transient duck is measurable
        modes.reset();
        for (int hop = 0; hop < 32; ++hop)
        {
            frame.magnitudes[20] = 1.0f;
            modes.applyShadowMagnitudes (frame.magnitudes.data(), hist.data(), constants::numBins, p, 0);
        }
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

        CHECK (! modes.process (mode, frame, p, hist.data(), nullptr, 0, constants::hopSize));
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
    p.memoryLengthSeconds = 3.0f;

    // Warm familiarity envelope from repeated overlapping history
    for (int i = 0; i < 64; ++i)
    {
        for (int b = 0; b < constants::numBins; ++b)
            frame.magnitudes[(size_t) b] = 1.0f;
        modes.applyEraseMagnitudes (frame.magnitudes.data(), hist.data(), constants::numBins, p, 0);
    }

    for (int b = 0; b < constants::numBins; ++b)
        frame.magnitudes[(size_t) b] = 1.0f;
    modes.applyEraseMagnitudes (frame.magnitudes.data(), hist.data(), constants::numBins, p, 0);

    CHECK (frame.magnitudes[40] < 0.85f); // carved where history is familiar
    CHECK (frame.magnitudes[10] > 0.90f); // relatively intact where history is empty
    for (int i = 0; i < constants::numBins; ++i)
    {
        CHECK (std::isfinite (frame.magnitudes[(size_t) i]));
        CHECK (frame.magnitudes[(size_t) i] >= 0.0f);
    }
}

//==============================================================================
static void testMergePullsTowardHistory()
{
    std::cout << "Merge blur changes spectrum (finite)...\n";
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
    hist[80] = 6.0f;

    ModeParams p;
    p.influence = 1.0f;
    p.forget = 0.0f;
    p.blur = 0.25f;
    p.transientPreserve = 0.0f;
    p.transientStrength = 0.0f;
    p.recallAge01 = 0.0f;
    p.recallPosition = 1.0f; // blur source = recalled memory

    auto before = frame.magnitudes;
    for (int hop = 0; hop < 24; ++hop)
    {
        frame.magnitudes = before;
        modes.applyMergeMagnitudes (frame.magnitudes.data(), hist.data(), constants::numBins, p, 0);
    }

    CHECK (fixtures::logSpectralDistance (frame.magnitudes.data(), before.data(),
                                          constants::numBins) > 0.5);
    // Memory landmark survives into the blur magnitude path.
    CHECK (frame.magnitudes[80] > frame.magnitudes[20] * 0.8f);
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
        CHECK (modes.process (mode, work, p, hist.data(), nullptr, 0, constants::hopSize));
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
                buf.setSample (ch, i, 0.2f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * (double) i / 48000.0));

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
            buf.setSample (0, i, 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (double) i / 48000.0));
            buf.setSample (1, i, 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * (double) i / 48000.0));
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
static void testShadowSpikeAndNoiseGrowth()
{
    std::cout << "Shadow spike + noise-growth safety...\n";
    using namespace afterimage;

#if JUCE_DEBUG
    debug::safetyCounters().reset();
#endif

    SpectralEngine engine;
    engine.prepare (48000.0, 512, 2);
    engine.setMode (SpectralMode::Shadow);
    engine.setActiveMemoryLengthSeconds (3.0f);
    engine.setSpectralParameterTargets (0.85f, 0.45f, 0.30f, 0.20f, 0.35f, 0.0f, false);
    engine.snapSpectralSmoothersToTargets();

    // Impulse-like spikes into a quiet bed — must not explode.
    juce::AudioBuffer<float> buf (2, 48000);
    for (int i = 0; i < buf.getNumSamples(); ++i)
    {
        float s = 0.05f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0
                                            * (double) i / 48000.0);
        if ((i % 4000) == 0)
            s += 0.95f;
        buf.setSample (0, i, s);
        buf.setSample (1, i, s * 0.8f);
    }
    engine.process (buf);

    float peak = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = constants::fftSize; i < buf.getNumSamples(); ++i)
            peak = std::max (peak, std::abs (buf.getSample (ch, i)));
    CHECK (std::isfinite (peak));
    CHECK (peak < 2.05f); // emergency ceiling + margin
    std::cout << "  spike peak=" << peak << "\n";

    // Long stress: noise in → noise out floor must not grow without bound.
    juce::Random rng (0xC0FFEEu);
    juce::AudioBuffer<float> noise (2, 48000 * 8); // 8 s @ 48 kHz
    for (int i = 0; i < noise.getNumSamples(); ++i)
    {
        const float n = (rng.nextFloat() * 2.0f - 1.0f) * 0.2f;
        noise.setSample (0, i, n);
        noise.setSample (1, i, n);
    }
    engine.reset();
    engine.setSpectralParameterTargets (0.70f, 0.40f, 0.30f, 0.18f, 0.35f, 0.0f, false);
    engine.snapSpectralSmoothersToTargets();
    engine.process (noise);

    auto rmsOf = [] (const juce::AudioBuffer<float>& b, int start, int len) -> double
    {
        double e = 0.0;
        const int end = juce::jmin (b.getNumSamples(), start + len);
        int n = 0;
        for (int i = start; i < end; ++i, ++n)
        {
            const float s = b.getSample (0, i);
            e += (double) s * s;
        }
        return n > 0 ? std::sqrt (e / (double) n) : 0.0;
    };

    const double early = rmsOf (noise, constants::fftSize + 48000, 48000);
    const double late  = rmsOf (noise, noise.getNumSamples() - 48000, 48000);
    CHECK (early > 1.0e-6);
    CHECK (late < early * 4.0); // no uncontrolled feedback growth
    std::cout << "  noise RMS early=" << early << " late=" << late
              << " ratio=" << (late / early) << "\n";

#if JUCE_DEBUG
    const auto hits = debug::safetyCounters().emergencyCeilingHits.load();
    std::cout << "  debug emergencyCeilingHits=" << hits << "\n";
#endif
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
        buf.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * (double) i / 48000.0));

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
        const float re = buf[(size_t) (2 * k)];
        const float im = buf[(size_t) (2 * k + 1)];
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
            input.addSample (ch, i, 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * (double) i / sampleRate));
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

    // WOLA table consistency — periodic Hann at 4× overlap is exact COLA
    {
        STFTProcessor stft;
        stft.prepare (48000.0, 512, 1);
        CHECK (stft.getMaxWolaScaleDeviation() < 1.0e-5f);
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
/** Phase 0.3: L/R hop sync — same stereo signal in one large block vs many hops must match. */
static void testStftBlockSizeInvariance()
{
    std::cout << "STFT block-size invariance...\n";

    constexpr double sr = 48000.0;
    constexpr int numCh = 2;
    const int totalSamples = constants::fftSize; // one large block = 4096

    auto fillSignal = [&] (juce::AudioBuffer<float>& buf)
    {
        buf.clear();
        for (int ch = 0; ch < numCh; ++ch)
        {
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                const float t = static_cast<float> (i) / static_cast<float> (sr);
                const float s = 0.4f * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * t)
                              + 0.25f * std::sin (2.0f * juce::MathConstants<float>::pi * 880.0f * t
                                                   + static_cast<float> (ch) * 0.7f);
                buf.setSample (ch, i, s);
            }
        }
    };

    juce::AudioBuffer<float> oneShot (numCh, totalSamples);
    juce::AudioBuffer<float> chunked (numCh, totalSamples);
    fillSignal (oneShot);
    fillSignal (chunked);

    STFTProcessor stftA, stftB;
    stftA.prepare (sr, totalSamples, numCh);
    stftB.prepare (sr, constants::hopSize, numCh);
    stftA.setSpectrumCallback (nullptr, nullptr);
    stftB.setSpectrumCallback (nullptr, nullptr);

    stftA.process (oneShot);

    for (int offset = 0; offset < totalSamples; offset += constants::hopSize)
    {
        const int n = std::min (constants::hopSize, totalSamples - offset);
        float* ptrs[numCh];
        for (int ch = 0; ch < numCh; ++ch)
            ptrs[ch] = chunked.getWritePointer (ch) + offset;
        juce::AudioBuffer<float> view (ptrs, numCh, n);
        stftB.process (view);
    }

    double maxAbsErr = 0.0;
    for (int ch = 0; ch < numCh; ++ch)
        for (int i = 0; i < totalSamples; ++i)
            maxAbsErr = std::max (maxAbsErr,
                                  (double) std::abs (oneShot.getSample (ch, i)
                                                     - chunked.getSample (ch, i)));

    std::cout << "  max |oneShot - chunked| = " << maxAbsErr << "\n";
    CHECK (maxAbsErr < 1.0e-5);
}

//==============================================================================
int main()
{
    std::cout << "AFTERIMAGE validation tests\n";
    testHistoryBuffer();
    testHistoryInterpolationFocused();
    testSpectralMemoryProfile();
    testFreezeProfileCaptureAndLimiter();
    testFreezeArmUntilHistoryReady();
    testMergeEnvelopeAndNaNGuard();
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
    testStftBlockSizeInvariance();
    testRandomRecallWander();
    testEngineInfluenceZeroAndFreeze();
    testEngineShadowAudibleVsIdentity();
    testShadowSpikeAndNoiseGrowth();
    testStereoIsolation();
    runEffectStrengthTests();
    runLicensingTests();
    runGainMatchTests();
    gFailures += runScaleTheoryTests();

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
