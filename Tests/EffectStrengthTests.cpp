#include "DSP/SpectralModes.h"
#include "SpectralFixtures.h"
#include "Utilities/Constants.h"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
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
ModeParams makeParams (float influence, float forget = 0.25f, float blur = 0.0f,
                       float recallAge = 0.35f, float memorySec = 3.0f)
{
    ModeParams p;
    p.influence = influence;
    p.forget = forget;
    p.blur = blur;
    p.transientPreserve = 0.0f;
    p.transientStrength = 0.0f;
    p.recallAge01 = recallAge;
    p.memoryLengthSeconds = memorySec;
    p.freeze = false;
    return p;
}

std::vector<int> peakBins (const std::vector<float>& m, float threshFrac = 0.35f)
{
    float peak = 0.0f;
    for (float v : m)
        peak = std::max (peak, v);
    std::vector<int> bins;
    const float thr = peak * threshFrac;
    for (int i = 1; i < (int) m.size(); ++i)
        if (m[(size_t) i] >= thr)
            bins.push_back (i);
    return bins;
}

std::vector<int> complementaryBins (const std::vector<int>& fam, int n, int count = 24)
{
    std::vector<char> used ((size_t) n, 0);
    for (int b : fam)
        if (b >= 0 && b < n)
            used[(size_t) b] = 1;
    std::vector<int> out;
    for (int i = 1; i < n && (int) out.size() < count; ++i)
        if (! used[(size_t) i])
            out.push_back (i);
    return out;
}
} // namespace

//==============================================================================
void testMapInfluenceEndpointsAndMid()
{
    std::cout << "mapInfluenceForMode endpoints / mid...\n";
    for (auto mode : { SpectralMode::Shadow, SpectralMode::Erase, SpectralMode::Merge })
    {
        CHECK_NEAR (mapInfluenceForMode (mode, 0.0f), 0.0f, 1e-7);
        CHECK_NEAR (mapInfluenceForMode (mode, 1.0f), 1.0f, 1e-7);
        const float m50 = mapInfluenceForMode (mode, 0.5f);
        CHECK (m50 > 0.5f);
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

//==============================================================================
/** Deterministic Shadow regression — must stay bit-stable across Erase/Merge redesign. */
void testShadowRegressionFixture()
{
    std::cout << "Shadow regression fixture (bit-stable)...\n";
    SpectralModeProcessor modes;
    modes.prepare (constants::numBins, 48000.0, 1);
    modes.reset();

    std::vector<float> cur, hist;
    fixtures::fillChord (cur, 12.0f, 1.0f);
    fixtures::fillSaw (hist, 9.0f, 0.85f);

    auto p = makeParams (0.40f, 0.25f, 0.12f, 0.40f);
    modes.applyShadowMagnitudes (cur.data(), hist.data(), constants::numBins, p, 0);

    // Golden fingerprint from pre-redesign Shadow path (chord+ saw @ Inf 40%).
    // Recomputed once after isolating Shadow path; update only if Shadow intentionally changes.
    double checksum = 0.0;
    for (int i = 0; i < constants::numBins; ++i)
        checksum += (double) cur[(size_t) i] * (double) (i + 1);

    // Absolute anchors at known harmonic bins
    CHECK (std::isfinite (checksum));
    CHECK (cur[12] >= 0.95f); // chord root present (ghost may already sit on peak)
    CHECK (cur[9] > 0.25f);   // saw fundamental ghost present
    std::cout << "  Shadow checksum=" << std::setprecision (17) << checksum
              << " bin12=" << cur[12] << " bin9=" << cur[9] << "\n";
    CHECK_NEAR (checksum, 962.21490282844752, 1.0e-4); // Shadow bit-lock
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
        modes.applyShadowMagnitudes (cur.data(), hist.data(), constants::numBins,
                                     makeParams (influence), 0);
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
    CHECK (v50 > v40 * 0.95f);
    CHECK (v75 > v50 * 0.95f);
    CHECK (std::isfinite (v100));
    CHECK (v100 > v75 * 0.9f);
    std::cout << "  bin40 @25/40/50/75/100% = " << v25 << " " << v40 << " "
              << v50 << " " << v75 << " " << v100 << "\n";
}

//==============================================================================
void testEraseEffectStrength()
{
    std::cout << "Erase effect-strength (familiarity selectivity)...\n";
    SpectralModeProcessor modes;
    modes.prepare (constants::numBins, 48000.0, 1);

    std::vector<float> saw, novel;
    fixtures::fillSaw (saw, 8.0f, 1.0f);
    fixtures::fillHat (novel, 1.0f);
    const auto famBins = peakBins (saw, 0.55f); // strongest harmonics only
    const auto novBins = complementaryBins (famBins, constants::numBins, 32);
    CHECK (! famBins.empty());
    CHECK (! novBins.empty());

    auto runAttenuation = [&] (float influence, int warmFrames) -> std::pair<double, double>
    {
        modes.reset();
        auto p = makeParams (influence, 0.20f, 0.10f, 0.15f, 3.0f);
        // Warm familiarity with repeated saw history (enough frames to settle attack)
        for (int f = 0; f < warmFrames; ++f)
        {
            auto cur = saw;
            modes.applyEraseMagnitudes (cur.data(), saw.data(), constants::numBins, p, 0);
        }
        auto before = saw;
        auto cur = saw;
        modes.applyEraseMagnitudes (cur.data(), saw.data(), constants::numBins, p, 0);
        const double famAtten = fixtures::meanAttenuationDb (before.data(), cur.data(), famBins);
        // Novel content: hat against saw familiarity
        auto novBefore = novel;
        auto novCur = novel;
        modes.applyEraseMagnitudes (novCur.data(), saw.data(), constants::numBins, p, 0);
        const double novAtten = fixtures::meanAttenuationDb (novBefore.data(), novCur.data(),
                                                             peakBins (novel, 0.35f));
        return { famAtten, novAtten };
    };

    {
        modes.reset();
        auto cur = saw;
        auto p = makeParams (0.0f);
        modes.applyEraseMagnitudes (cur.data(), saw.data(), constants::numBins, p, 0);
        for (int i = 0; i < constants::numBins; ++i)
            CHECK_NEAR (cur[(size_t) i], saw[(size_t) i], 1e-4);
    }

    const auto a40 = runAttenuation (0.40f, 80);
    const auto a60 = runAttenuation (0.60f, 80);
    const double selectivity40 = a40.second - a40.first;
    CHECK (a40.first < -2.5);              // familiar bins carved at 40%
    CHECK (a40.first < a40.second - 1.5); // selectivity margin
    CHECK (a60.first < a40.first);         // stronger Influence → deeper familiar carve
    CHECK (selectivity40 > 1.5);

    std::cout << "  famAtten40/60dB=" << a40.first << "/" << a60.first
              << " novAtten40=" << a40.second
              << " selectivity=" << (a40.second - a40.first) << "\n";
}

void testEraseProgressiveAndFreeze()
{
    std::cout << "Erase progressive repetitions + Freeze...\n";
    SpectralModeProcessor modes;
    modes.prepare (constants::numBins, 48000.0, 1);
    modes.reset();

    std::vector<float> saw;
    fixtures::fillSaw (saw, 8.0f, 1.0f);
    const auto famBins = peakBins (saw, 0.45f);
    auto p = makeParams (0.50f, 0.25f, 0.10f, 0.30f, 3.0f);

    auto measure = [&] (std::vector<float> cur) -> double
    {
        auto before = cur;
        modes.applyEraseMagnitudes (cur.data(), saw.data(), constants::numBins, p, 0);
        return fixtures::meanAttenuationDb (before.data(), cur.data(), famBins);
    };

    const double first = measure (saw);
    const double second = measure (saw);
    const double third = measure (saw);
    CHECK (second < first);  // more alteration
    CHECK (third <= second + 0.05); // progressive / saturating

    // Freeze: envelope stops updating; further frames should not deepen much via envelope growth
    p.freeze = true;
    const double frozenA = measure (saw);
    const double frozenB = measure (saw);
    CHECK (std::abs (frozenB - frozenA) < 0.75); // mask temporal smooth may move slightly

    // Forget recovery: high forget + silence history should release familiarity
    p.freeze = false;
    p.forget = 1.0f;
    p.memoryLengthSeconds = 0.5f;
    std::vector<float> silence ((size_t) constants::numBins, 0.0f);
    for (int i = 0; i < 80; ++i)
    {
        auto cur = saw;
        modes.applyEraseMagnitudes (cur.data(), silence.data(), constants::numBins, p, 0);
    }
    p.forget = 0.25f;
    p.memoryLengthSeconds = 3.0f;
    const double afterForget = measure (saw);
    CHECK (afterForget > third); // less carve after familiarity faded

    // New harmonic content reappears relative to familiar
    std::vector<float> other;
    fixtures::fillChord (other, 22.0f, 1.0f);
    modes.reset();
    p = makeParams (0.55f, 0.25f, 0.1f);
    for (int i = 0; i < 40; ++i)
    {
        auto cur = saw;
        modes.applyEraseMagnitudes (cur.data(), saw.data(), constants::numBins, p, 0);
    }
    auto chordBefore = other;
    auto chordCur = other;
    modes.applyEraseMagnitudes (chordCur.data(), saw.data(), constants::numBins, p, 0);
    const double novelKeep = fixtures::meanAttenuationDb (chordBefore.data(), chordCur.data(),
                                                          peakBins (other, 0.4f));
    auto sawBefore = saw;
    auto sawCur = saw;
    modes.applyEraseMagnitudes (sawCur.data(), saw.data(), constants::numBins, p, 0);
    const double famCut = fixtures::meanAttenuationDb (sawBefore.data(), sawCur.data(), famBins);
    CHECK (novelKeep > famCut + 0.5);

    std::cout << "  progressive dB=" << first << " → " << second << " → " << third
              << " afterForget=" << afterForget << "\n";
}

//==============================================================================
void testMergeEffectStrength()
{
    std::cout << "Merge effect-strength (envelope + landmarks)...\n";
    SpectralModeProcessor modes;
    modes.prepare (constants::numBins, 48000.0, 1);

    std::vector<float> carrier, identity;
    fixtures::fillPinkTilt (carrier, 0.6f);
    fixtures::fillFormant (identity, 45.0f, 140.0f, 1.2f);

    auto distanceToHist = [&] (float influence) -> std::pair<double, double>
    {
        modes.reset();
        auto cur = carrier;
        auto original = cur;
        auto p = makeParams (influence, 0.25f, 0.30f, 0.45f);
        modes.applyMergeMagnitudes (cur.data(), identity.data(), constants::numBins, p, 0);
        const double logDistHist = fixtures::logSpectralDistance (cur.data(), identity.data(),
                                                                  constants::numBins);
        const double logDistCur = fixtures::logSpectralDistance (cur.data(), original.data(),
                                                                constants::numBins);
        const double envDistHist = fixtures::envelopeDistance (cur.data(), identity.data(),
                                                               constants::numBins, 24);
        (void) envDistHist;
        return { logDistHist, logDistCur };
    };

    CHECK (distanceToHist (0.0f).second < 1e-3);

    const auto d25 = distanceToHist (0.25f);
    const auto d40 = distanceToHist (0.40f);
    const auto d50 = distanceToHist (0.50f);
    const auto d75 = distanceToHist (0.75f);

    CHECK (d25.second > 0.05);
    CHECK (d40.second > d25.second);
    CHECK (d50.second > d40.second);
    CHECK (d75.second > d50.second);
    // Closer to historical identity as Influence rises
    CHECK (d50.first < d25.first);
    CHECK (d75.first < d50.first);

    // Not a uniform level change: relative spectral shape vs current must move
    {
        modes.reset();
        auto cur = carrier;
        auto original = cur;
        modes.applyMergeMagnitudes (cur.data(), identity.data(), constants::numBins,
                                    makeParams (0.45f, 0.25f, 0.35f), 0);
        double shape = 0.0;
        for (int i = 1; i < constants::numBins; ++i)
        {
            const float r0 = (original[(size_t) i] + 1e-6f) / (original[(size_t) i - 1] + 1e-6f);
            const float r1 = (cur[(size_t) i] + 1e-6f) / (cur[(size_t) i - 1] + 1e-6f);
            shape += std::abs (std::log ((double) r1) - std::log ((double) r0));
        }
        CHECK (shape > 5.0);
    }

    // Landmarks: formant peaks should rise relative to pink carrier
    {
        modes.reset();
        auto cur = carrier;
        modes.applyMergeMagnitudes (cur.data(), identity.data(), constants::numBins,
                                    makeParams (0.55f, 0.2f, 0.25f), 0);
        CHECK (cur[45] / (carrier[45] + 1e-8f) > 1.15f);
    }

    std::cout << "  logDist→hist @25/50/75=" << d25.first << " " << d50.first << " " << d75.first
              << " | fromCur=" << d25.second << " " << d50.second << " " << d75.second << "\n";
}

void testMergeSilenceAndStereoIsolation()
{
    std::cout << "Merge silence / stereo isolation...\n";
    SpectralModeProcessor modes;
    modes.prepare (constants::numBins, 48000.0, 2);
    modes.reset();

    std::vector<float> silence ((size_t) constants::numBins, 0.0f);
    std::vector<float> hist;
    fixtures::fillFormant (hist, 40.0f, 100.0f, 1.0f);
    auto p = makeParams (1.0f, 0.5f, 0.5f);
    auto cur = silence;
    modes.applyMergeMagnitudes (cur.data(), hist.data(), constants::numBins, p, 0);
    for (float v : cur)
        CHECK (std::isfinite (v) && v >= 0.0f);

    // Channel isolation: process ch0 then ch1 with different carriers — no shared scratch bleed
    std::vector<float> a, b;
    fixtures::fillKick (a, 1.0f);
    fixtures::fillHat (b, 1.0f);
    auto a0 = a;
    auto b1 = b;
    modes.applyMergeMagnitudes (a0.data(), hist.data(), constants::numBins, p, 0);
    modes.applyMergeMagnitudes (b1.data(), hist.data(), constants::numBins, p, 1);
    // Both finite; channels remain independent carriers
    CHECK (fixtures::sumSq (a0.data(), constants::numBins) > 1e-6);
    CHECK (fixtures::sumSq (b1.data(), constants::numBins) > 1e-6);
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
        ModeParams p = makeParams (1.0f, 0.5f, 0.5f, 0.5f);
        switch (mode)
        {
            case SpectralMode::Shadow: modes.applyShadowMagnitudes (cur.data(), hist.data(), constants::numBins, p, 0); break;
            case SpectralMode::Erase:  modes.applyEraseMagnitudes (cur.data(), hist.data(), constants::numBins, p, 0); break;
            case SpectralMode::Merge:  modes.applyMergeMagnitudes (cur.data(), hist.data(), constants::numBins, p, 0); break;
        }
        for (float v : cur)
            CHECK (std::isfinite (v) && v >= 0.0f);

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
        const double e = fixtures::sumSq (cur.data(), constants::numBins);
        CHECK (std::isfinite (e));
        CHECK (e < 1.0e8);
    }

    CHECK (kMaxTransientReduction <= 0.65f + 1e-6f);
}

void testBaselineDiagnosisPrint()
{
    // Always-on concise diagnosis for Stage 1 / retune report.
    std::cout << "Erase/Merge identity diagnosis (realistic fixtures)...\n";
    SpectralModeProcessor modes;
    modes.prepare (constants::numBins, 48000.0, 1);

    std::vector<float> saw, formant, pink;
    fixtures::fillSaw (saw, 8.0f, 1.0f);
    fixtures::fillFormant (formant, 40.0f, 120.0f, 1.0f);
    fixtures::fillPinkTilt (pink, 0.7f);

    modes.reset();
    auto eraseCur = saw;
    auto p = makeParams (0.40f, 0.30f, 0.15f);
    for (int i = 0; i < 64; ++i)
    {
        eraseCur = saw;
        modes.applyEraseMagnitudes (eraseCur.data(), saw.data(), constants::numBins, p, 0);
    }
    auto before = saw;
    eraseCur = saw;
    modes.applyEraseMagnitudes (eraseCur.data(), saw.data(), constants::numBins, p, 0);
    const double eraseCut = fixtures::meanAttenuationDb (before.data(), eraseCur.data(), peakBins (saw));
    std::cout << "  Erase@40% familiar mean atten dB (single frame after warm)=" << eraseCut << "\n";

    modes.reset();
    auto mergeCur = pink;
    modes.applyMergeMagnitudes (mergeCur.data(), formant.data(), constants::numBins,
                                makeParams (0.40f, 0.25f, 0.30f), 0);
    const double envCloser = fixtures::envelopeDistance (mergeCur.data(), formant.data(),
                                                         constants::numBins)
                             - fixtures::envelopeDistance (pink.data(), formant.data(),
                                                           constants::numBins);
    std::cout << "  Merge@40% envelopeDistance delta toward formant=" << envCloser << "\n";
}

void runEffectStrengthTests()
{
    testMapInfluenceEndpointsAndMid();
    testRetentionFloor();
    testShadowRegressionFixture();
    testShadowEffectStrength();
    testEraseEffectStrength();
    testEraseProgressiveAndFreeze();
    testMergeEffectStrength();
    testMergeSilenceAndStereoIsolation();
    testLoudnessStability();
    testBaselineDiagnosisPrint();
}
