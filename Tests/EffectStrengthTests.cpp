#include "DSP/SpectralModes.h"
#include "Utilities/Constants.h"

#include <cmath>
#include <iostream>
#include <vector>

using namespace afterimage;

extern int gFailures;

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

namespace
{
double spectralL1 (const float* a, const float* b, int n)
{
    double s = 0.0;
    for (int i = 0; i < n; ++i)
        s += std::abs ((double) a[i] - (double) b[i]);
    return s;
}

double sumSq (const float* a, int n)
{
    double e = 0.0;
    for (int i = 0; i < n; ++i)
        e += (double) a[i] * (double) a[i];
    return e;
}
} // namespace

void testMapInfluenceEndpointsAndMid()
{
    std::cout << "mapInfluenceForMode endpoints / mid...\n";
    for (auto mode : { SpectralMode::Shadow, SpectralMode::Erase, SpectralMode::Merge })
    {
        CHECK_NEAR (mapInfluenceForMode (mode, 0.0f), 0.0f, 1e-7);
        CHECK_NEAR (mapInfluenceForMode (mode, 1.0f), 1.0f, 1e-7);
        const float m50 = mapInfluenceForMode (mode, 0.5f);
        CHECK (m50 > 0.5f); // stronger mid than linear
        CHECK (m50 < 1.0f);
        float prev = -1.0f;
        for (float x = 0.0f; x <= 1.0f + 1e-6f; x += 0.05f)
        {
            const float y = mapInfluenceForMode (mode, x);
            CHECK (y >= prev - 1e-6f);
            prev = y;
        }
    }
}

void testRetentionFloor()
{
    std::cout << "Forget retention floor...\n";
    const float wOld = remappedHistoryWeight (SpectralMode::Shadow, 0.8f, 0.5f);
    const float wRaw = ageWeightFromForget (0.8f, 0.5f);
    CHECK (wOld > wRaw);
    CHECK (wOld >= retentionFloorForMode (SpectralMode::Shadow) - 1e-5f);
    CHECK_NEAR (remappedHistoryWeight (SpectralMode::Merge, 0.0f, 0.0f), 1.0f, 1e-5);
}

void testShadowEffectStrength()
{
    std::cout << "Shadow effect-strength (hist bin energy)...\n";
    SpectralModeProcessor modes;
    modes.prepare (constants::numBins, 48000.0, 1);

    std::vector<float> hist ((size_t) constants::numBins, 0.0f);
    hist[40] = 5.0f;

    auto run = [&] (float influence) -> float
    {
        modes.reset();
        std::vector<float> cur ((size_t) constants::numBins, 0.2f);
        ModeParams p;
        p.influence = influence;
        p.forget = 0.2f;
        p.blur = 0.0f;
        p.transientPreserve = 0.0f;
        p.transientStrength = 0.0f;
        p.recallAge01 = 0.35f;
        modes.applyShadowMagnitudes (cur.data(), hist.data(), constants::numBins, p, 0);
        return cur[40];
    };

    CHECK_NEAR (run (0.0f), 0.2f, 1e-4);
    const float v25 = run (0.25f);
    const float v40 = run (0.40f);
    const float v50 = run (0.50f);
    const float v75 = run (0.75f);
    const float v100 = run (1.0f);
    CHECK (v25 > 0.2f + 0.02f);
    CHECK (v40 > v25);
    CHECK (v50 > v40 * 0.95f); // roughly monotonic
    CHECK (v75 > v50 * 0.95f);
    CHECK (std::isfinite (v100));
    CHECK (v100 > v75 * 0.9f);
    std::cout << "  bin40 @25/40/50/75/100% = " << v25 << " " << v40 << " "
              << v50 << " " << v75 << " " << v100 << "\n";
}

void testEraseEffectStrength()
{
    std::cout << "Erase effect-strength (overlap carve)...\n";
    SpectralModeProcessor modes;
    modes.prepare (constants::numBins, 48000.0, 1);

    auto run = [&] (float influence) -> float
    {
        modes.reset();
        std::vector<float> cur ((size_t) constants::numBins, 1.0f);
        std::vector<float> hist = cur;
        hist[40] = 4.0f;
        ModeParams p;
        p.influence = influence;
        p.forget = 0.2f;
        p.blur = 0.0f;
        p.transientPreserve = 0.0f;
        p.transientStrength = 0.0f;
        p.recallAge01 = 0.3f;
        modes.applyEraseMagnitudes (cur.data(), hist.data(), constants::numBins, p, 0);
        return cur[40];
    };

    CHECK_NEAR (run (0.0f), 1.0f, 1e-4);
    const float v40 = run (0.40f);
    CHECK (v40 < 0.85f); // meaningful carve at 40%
    CHECK (run (0.60f) < v40);
    CHECK (run (1.0f) < run (0.60f));
    std::cout << "  overlap bin @40% = " << v40 << "\n";
}

void testMergeEffectStrength()
{
    std::cout << "Merge effect-strength (spectral distance)...\n";
    SpectralModeProcessor modes;
    modes.prepare (constants::numBins, 48000.0, 1);

    std::vector<float> hist ((size_t) constants::numBins, 2.0f);
    for (int i = 0; i < constants::numBins; ++i)
        hist[(size_t) i] = (i % 2 == 0) ? 2.0f : 0.1f;

    auto distance = [&] (float influence) -> double
    {
        modes.reset();
        std::vector<float> cur ((size_t) constants::numBins, 0.2f);
        auto original = cur;
        ModeParams p;
        p.influence = influence;
        p.forget = 0.2f;
        p.blur = 0.0f;
        p.transientPreserve = 0.0f;
        p.transientStrength = 0.0f;
        p.recallAge01 = 0.3f;
        modes.applyMergeMagnitudes (cur.data(), hist.data(), constants::numBins, p, 0);
        return spectralL1 (cur.data(), original.data(), constants::numBins);
    };

    CHECK (distance (0.0f) < 1e-3);
    const double d25 = distance (0.25f);
    const double d50 = distance (0.50f);
    const double d75 = distance (0.75f);
    CHECK (d25 > 1.0);
    CHECK (d50 > d25 * 0.9);
    CHECK (d75 > d50 * 0.9);
    std::cout << "  L1 delta @25/50/75 = " << d25 << " " << d50 << " " << d75 << "\n";
}

void testLoudnessStability()
{
    std::cout << "Loudness / stability probes...\n";
    SpectralModeProcessor modes;
    modes.prepare (constants::numBins, 48000.0, 1);

    for (auto mode : { SpectralMode::Shadow, SpectralMode::Erase, SpectralMode::Merge })
    {
        modes.reset();
        std::vector<float> cur ((size_t) constants::numBins, 0.0f);
        std::vector<float> hist ((size_t) constants::numBins, 0.0f);
        // silence
        ModeParams p;
        p.influence = 1.0f;
        p.forget = 0.5f;
        p.blur = 0.5f;
        p.recallAge01 = 0.5f;
        switch (mode)
        {
            case SpectralMode::Shadow: modes.applyShadowMagnitudes (cur.data(), hist.data(), constants::numBins, p, 0); break;
            case SpectralMode::Erase:  modes.applyEraseMagnitudes (cur.data(), hist.data(), constants::numBins, p, 0); break;
            case SpectralMode::Merge:  modes.applyMergeMagnitudes (cur.data(), hist.data(), constants::numBins, p, 0); break;
        }
        for (float v : cur)
            CHECK (std::isfinite (v) && v >= 0.0f);

        // loud
        for (int i = 0; i < constants::numBins; ++i)
        {
            cur[(size_t) i] = 8.0f;
            hist[(size_t) i] = 8.0f;
        }
        switch (mode)
        {
            case SpectralMode::Shadow: modes.applyShadowMagnitudes (cur.data(), hist.data(), constants::numBins, p, 0); break;
            case SpectralMode::Erase:  modes.applyEraseMagnitudes (cur.data(), hist.data(), constants::numBins, p, 0); break;
            case SpectralMode::Merge:  modes.applyMergeMagnitudes (cur.data(), hist.data(), constants::numBins, p, 0); break;
        }
        const double e = sumSq (cur.data(), constants::numBins);
        CHECK (std::isfinite (e));
        CHECK (e < 1.0e8);
    }

    // Transient preserve retains >= ~35% contribution at max
    CHECK (kMaxTransientReduction <= 0.65f + 1e-6f);
}

void runEffectStrengthTests()
{
    testMapInfluenceEndpointsAndMid();
    testRetentionFloor();
    testShadowEffectStrength();
    testEraseEffectStrength();
    testMergeEffectStrength();
    testLoudnessStability();
}
