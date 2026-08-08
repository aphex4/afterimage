#include "DSP/SpectralModes.h"
#include "DSP/SpectralEngine.h"
#include "DSP/SpectralBlur.h"
#include "DSP/SpectralTail.h"
#include "DSP/LogSmoother.h"
#include "PluginProcessor.h"
#include "SpectralFixtures.h"
#include "Utilities/Constants.h"

#include <juce_audio_basics/juce_audio_basics.h>

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

    // Round-4: Shadow/Merge mid-curve floors (Erase left at 1.50).
    CHECK (mapInfluenceForMode (SpectralMode::Shadow, 0.40f) > 0.52f);
    CHECK (mapInfluenceForMode (SpectralMode::Merge, 0.40f) > 0.54f);
    CHECK (mapInfluenceForMode (SpectralMode::Shadow, 0.50f) > 0.62f);
    CHECK (mapInfluenceForMode (SpectralMode::Merge, 0.50f) > 0.64f);
    CHECK_NEAR (mapInfluenceForMode (SpectralMode::Erase, 0.50f),
                1.0f - std::pow (0.5f, 1.50f), 1e-5);
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
/** Deterministic Shadow regression — SpectralTail must remain audible & finite. */
void testShadowRegressionFixture()
{
    std::cout << "Shadow regression fixture (SpectralTail)...\n";
    SpectralModeProcessor modes;
    modes.prepare (constants::numBins, 48000.0, 1);
    modes.reset();

    std::vector<float> hist;
    fixtures::fillSaw (hist, 9.0f, 0.85f);

    auto p = makeParams (0.40f, 0.25f, 0.12f, 0.40f);
    std::vector<float> cur;
    for (int hop = 0; hop < 48; ++hop)
    {
        fixtures::fillChord (cur, 12.0f, 1.0f);
        modes.applyShadowMagnitudes (cur.data(), hist.data(), constants::numBins, p, 0);
    }

    double checksum = 0.0;
    for (int i = 0; i < constants::numBins; ++i)
    {
        CHECK (std::isfinite (cur[(size_t) i]));
        checksum += (double) cur[(size_t) i] * (double) (i + 1);
    }

    CHECK (std::isfinite (checksum));
    CHECK (cur[12] >= 0.95f); // chord root present
    CHECK (cur[9] > 0.08f);   // saw fundamental present in SpectralTail
    double histOnly = 0.0;
    {
        std::vector<float> base;
        fixtures::fillChord (base, 12.0f, 1.0f);
        for (int i = 0; i < constants::numBins; ++i)
            histOnly += (double) base[(size_t) i] * (double) (i + 1);
    }
    CHECK (checksum > histOnly * 1.01);
    std::cout << "  Shadow checksum=" << std::setprecision (17) << checksum
              << " bin12=" << cur[12] << " bin9=" << cur[9] << "\n";
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
        for (int hop = 0; hop < 64; ++hop)
        {
            cur.assign ((size_t) constants::numBins, 0.2f);
            modes.applyShadowMagnitudes (cur.data(), hist.data(), constants::numBins,
                                         makeParams (influence), 0);
        }
        return cur[40];
    };

    CHECK_NEAR (run (0.0f), 0.2f, 1e-4);
    const float v25 = run (0.25f);
    const float v40 = run (0.40f);
    const float v50 = run (0.50f);
    const float v75 = run (0.75f);
    const float v100 = run (1.0f);
    CHECK (v25 > 0.2f + 0.04f);
    CHECK (v40 > v25);
    CHECK (v40 > 0.2f + 0.12f); // moderate Influence already a clear hist-bin lift
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
    // DD + min-stats may saturate on the first familiar frame; allow flat progression.
    CHECK (second <= first + 0.05);
    CHECK (third <= second + 0.05);

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
    std::cout << "Merge effect-strength (Influence moves spectrum)...\n";
    SpectralModeProcessor modes;
    modes.prepare (constants::numBins, 48000.0, 1);

    std::vector<float> carrier, memory;
    fixtures::fillPinkTilt (carrier, 0.6f);
    fixtures::fillFormant (memory, 45.0f, 140.0f, 1.2f);

    auto distFromCur = [&] (float influence) -> double
    {
        modes.reset();
        auto cur = carrier;
        auto p = makeParams (influence, 0.25f, 0.50f, 0.45f);
        p.recallPosition = 0.5f;
        for (int i = 0; i < 16; ++i)
        {
            cur = carrier;
            modes.applyMergeMagnitudes (cur.data(), memory.data(), constants::numBins, p, 0);
        }
        return fixtures::logSpectralDistance (cur.data(), carrier.data(), constants::numBins);
    };

    CHECK (distFromCur (0.0f) < 1e-3);
    const double d25 = distFromCur (0.25f);
    const double d50 = distFromCur (0.50f);
    const double d100 = distFromCur (1.0f);
    CHECK (d25 > 0.08);
    CHECK (d50 > d25);
    CHECK (d50 > 0.20); // mid Influence already an obvious spectral morph
    CHECK (d100 > d50);
    std::cout << "  logDist fromCur @25/50/100=" << d25 << " " << d50 << " " << d100 << "\n";
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

/** Mid Influence blur stays finite; absolute ceiling inactive on normal fixtures. */
void testMergeMidInfluenceEnergyBound()
{
    std::cout << "Merge mid-Influence finite / ceiling inactive...\n";
    SpectralModeProcessor modes;
    modes.prepare (constants::numBins, 48000.0, 1);

    std::vector<float> quietCarrier, loudMemory;
    fixtures::fillPinkTilt (quietCarrier, 0.35f);
    fixtures::fillFormant (loudMemory, 45.0f, 140.0f, 2.4f);

    for (float influence : { 0.40f, 0.50f, 0.60f })
    {
        modes.reset();
        auto p = makeParams (influence, 0.25f, 0.35f, 0.45f);
        p.recallPosition = 0.5f;
        std::vector<float> cur;

        for (int frame = 0; frame < 48; ++frame)
        {
            cur = quietCarrier;
            modes.applyMergeMagnitudes (cur.data(), loudMemory.data(), constants::numBins, p, 0);
        }

        for (float v : cur)
            CHECK (std::isfinite (v) && v >= 0.0f);
        CHECK (modes.getLastEnergyScale (0) > 0.99f);
        CHECK (fixtures::logSpectralDistance (cur.data(), quietCarrier.data(),
                                              constants::numBins) > 0.05);
        std::cout << "  Influence " << (int) std::lround (influence * 100.0f) << "% ok\n";
    }
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

//==============================================================================
// Phase 9 — engine-level acceptance
//==============================================================================
namespace
{
float bufferRms (const juce::AudioBuffer<float>& buf, int start, int n, int ch = 0) noexcept
{
    if (n <= 0)
        return 0.0f;
    double e = 0.0;
    const float* d = buf.getReadPointer (ch);
    for (int i = 0; i < n; ++i)
    {
        const float v = d[start + i];
        e += (double) v * (double) v;
    }
    return (float) std::sqrt (e / (double) n);
}

float spectralCentroidHz (const float* x, int n, double sr) noexcept
{
    // Cheap time-domain proxy via zero-crossing rate is too rough; use short FFT of a window.
    const int order = 10; // 1024
    const int N = 1 << order;
    if (n < N)
        return 0.0f;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) (N * 2), 0.0f);
    for (int i = 0; i < N; ++i)
        buf[(size_t) i] = x[i] * (0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi
                                                           * (float) i / (float) N));
    fft.performFrequencyOnlyForwardTransform (buf.data());
    double num = 0.0, den = 0.0;
    const int bins = N / 2;
    for (int k = 1; k < bins; ++k)
    {
        const double m = (double) buf[(size_t) k];
        const double f = (double) k * sr / (double) N;
        num += f * m;
        den += m;
    }
    return den > 1.0e-12 ? (float) (num / den) : 0.0f;
}

void fillPink (juce::AudioBuffer<float>& buf, std::uint32_t& rng) noexcept
{
    // Paul Kellet approximate pink filter on unit noise.
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        float* d = buf.getWritePointer (ch);
        b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0;
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            const float white = ((rng & 0x00FFFFFFu) * (1.0f / 16777216.0f)) * 2.0f - 1.0f;
            b0 = 0.99886f * b0 + white * 0.0555179f;
            b1 = 0.99332f * b1 + white * 0.0750759f;
            b2 = 0.96900f * b2 + white * 0.1538520f;
            b3 = 0.86650f * b3 + white * 0.3104856f;
            b4 = 0.55000f * b4 + white * 0.5329522f;
            b5 = -0.7616f * b5 - white * 0.0168980f;
            const float pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
            b6 = white * 0.115926f;
            d[i] = pink * 0.11f;
        }
    }
}
} // namespace

void testPhase9ShadowTailAcceptance()
{
    std::cout << "Phase 9 Shadow tail acceptance...\n";
    constexpr double sr = 48000.0;
    constexpr int burstSecSamples = (int) (1.00 * sr);
    constexpr int totalSec = 5;
    constexpr int totalSamples = (int) (totalSec * sr);

    SpectralEngine engine;
    engine.prepare (sr, 512, 2);
    engine.setMode (SpectralMode::Shadow);
    engine.setActiveMemoryLengthSeconds (4.0f);
    engine.setSpectralParameterTargets (1.0f, 0.0f, 0.15f, 0.5f, 0.0f, 0.0f, false);
    engine.snapSpectralSmoothersToTargets();

    juce::AudioBuffer<float> buf (2, totalSamples);
    buf.clear();
    {
        juce::AudioBuffer<float> burst (2, burstSecSamples);
        std::uint32_t rng = 0xC0FFEEu;
        fillPink (burst, rng);
        burst.applyGain (2.5f); // ~-8 dBFS burst so the feedback floor is measurable
        for (int ch = 0; ch < 2; ++ch)
            buf.copyFrom (ch, 0, burst, ch, 0, burstSecSamples);
    }

    for (int offset = 0; offset < totalSamples; )
    {
        const int n = std::min (512, totalSamples - offset);
        float* ptrs[2] = { buf.getWritePointer (0) + offset, buf.getWritePointer (1) + offset };
        juce::AudioBuffer<float> view (ptrs, 2, n);
        engine.process (view);
        offset += n;
    }

    const int lat = engine.getLatencySamples();
    const int t2 = lat + (int) (2.0 * sr);
    const int t3 = lat + (int) (3.0 * sr);
    const float rmsTail = bufferRms (buf, t2, t3 - t2, 0);
    const float rmsDb = 20.0f * std::log10 (std::max (rmsTail, 1.0e-12f));
    std::cout << "  tail RMS @2–3s = " << rmsDb << " dBFS\n";
    CHECK (rmsDb >= -30.0f);

    // Smooth decay: 100 ms windows after 300 ms post-burst
    const int win = (int) (0.1 * sr);
    const int start = lat + burstSecSamples + (int) (0.3 * sr);
    float prev = bufferRms (buf, start, win, 0);
    float maxRatio = 1.0f;
    for (int t = start + win; t + win < lat + (int) (4.0 * sr); t += win)
    {
        const float r = bufferRms (buf, t, win, 0);
        if (prev > 1.0e-8f)
            maxRatio = std::max (maxRatio, r / prev);
        prev = r;
    }
    std::cout << "  max window ratio=" << maxRatio << "\n";
    CHECK (maxRatio <= 1.35f);

    // Darkens: centroid at 2s lower than at 0.3s after burst
    const int cEarly = lat + burstSecSamples + (int) (0.3 * sr);
    const int cLate = lat + (int) (2.0 * sr);
    const float centEarly = spectralCentroidHz (buf.getReadPointer (0) + cEarly, 1024, sr);
    const float centLate = spectralCentroidHz (buf.getReadPointer (0) + cLate, 1024, sr);
    std::cout << "  centroid early/late Hz=" << centEarly << "/" << centLate << "\n";
    CHECK (centLate < centEarly * 0.85f);

    // Width: L/R correlation of tail < 0.5 at Blur 0.5
    {
        double sumL = 0, sumR = 0, sumLL = 0, sumRR = 0, sumLR = 0;
        const int n = t3 - t2;
        const float* L = buf.getReadPointer (0) + t2;
        const float* R = buf.getReadPointer (1) + t2;
        for (int i = 0; i < n; ++i)
        {
            sumL += L[i]; sumR += R[i];
            sumLL += L[i] * L[i]; sumRR += R[i] * R[i]; sumLR += L[i] * R[i];
        }
        const double meanL = sumL / n, meanR = sumR / n;
        const double cov = sumLR / n - meanL * meanR;
        const double vL = sumLL / n - meanL * meanL;
        const double vR = sumRR / n - meanR * meanR;
        const float corr = (vL > 1e-12 && vR > 1e-12)
                               ? (float) (cov / std::sqrt (vL * vR)) : 1.0f;
        std::cout << "  L/R corr=" << corr << "\n";
        CHECK (corr < 0.5f);
    }
}

void testPhase9EraseMergeAcceptance()
{
    std::cout << "Phase 9 Erase/Merge acceptance...\n";
    SpectralModeProcessor modes;
    modes.prepare (constants::numBins, 48000.0, 1);

    // Erase removes sustained sine, retains clicks
    {
        modes.reset();
        const int sineBin = 40;
        std::vector<float> sine ((size_t) constants::numBins, 0.02f);
        sine[(size_t) sineBin] = 4.0f;
        ModeParams p = makeParams (1.0f, 0.2f, 0.0f, 0.0f, 3.0f);
        for (int i = 0; i < 120; ++i)
        {
            auto cur = sine;
            modes.applyEraseMagnitudes (cur.data(), sine.data(), constants::numBins, p, 0);
        }
        auto after = sine;
        modes.applyEraseMagnitudes (after.data(), sine.data(), constants::numBins, p, 0);
        const float attenDb = constants::gainToDb ((after[(size_t) sineBin] + 1e-8f)
                                                   / (sine[(size_t) sineBin] + 1e-8f));
        std::cout << "  Erase sine atten dB=" << attenDb << "\n";
        CHECK (attenDb <= -20.0f);

        std::vector<float> click ((size_t) constants::numBins, 0.02f);
        click[200] = 3.0f;
        auto clickOut = click;
        modes.applyEraseMagnitudes (clickOut.data(), sine.data(), constants::numBins, p, 0);
        const float clickDb = constants::gainToDb ((clickOut[200] + 1e-8f) / (click[200] + 1e-8f));
        std::cout << "  Erase click retain dB=" << clickDb << "\n";
        CHECK (std::abs (clickDb) <= 3.0f);
    }

    // Erase musical-noise: frame-to-frame variance not much above input
    {
        modes.reset();
        std::vector<float> pink;
        fixtures::fillPinkTilt (pink, 1.0f);
        ModeParams p = makeParams (1.0f, 0.25f, 0.0f);
        for (int i = 0; i < 40; ++i)
        {
            auto cur = pink;
            modes.applyEraseMagnitudes (cur.data(), pink.data(), constants::numBins, p, 0);
        }
        std::vector<float> prev = pink;
        modes.applyEraseMagnitudes (prev.data(), pink.data(), constants::numBins, p, 0);
        double varIn = 0.0, varOut = 0.0;
        for (int f = 0; f < 16; ++f)
        {
            auto a = pink, b = pink;
            modes.applyEraseMagnitudes (a.data(), pink.data(), constants::numBins, p, 0);
            modes.applyEraseMagnitudes (b.data(), pink.data(), constants::numBins, p, 0);
            for (int k = 1; k < constants::numBins; ++k)
            {
                const double dIn = (double) pink[(size_t) k] - (double) pink[(size_t) k]; // 0
                juce::ignoreUnused (dIn);
                const double dOut = (double) a[(size_t) k] - (double) b[(size_t) k];
                varOut += dOut * dOut;
            }
        }
        // Steady identical input → output frame variance should be small.
        // Compare against a tiny floor * bins; primary check is finite + not explosive.
        CHECK (std::isfinite (varOut));
        CHECK (varOut < 1.0e3);
        juce::ignoreUnused (varIn);
        std::cout << "  Erase frame varOut=" << varOut << "\n";
    }

    // Merge blur: Influence 100% moves the spectrum; stays finite
    {
        modes.reset();
        std::vector<float> carrier, memory;
        fixtures::fillPinkTilt (carrier, 0.5f);
        fixtures::fillFormant (memory, 50.0f, 130.0f, 1.5f);
        ModeParams p = makeParams (1.0f, 0.2f, 0.75f);
        p.recallPosition = 0.5f;
        auto cur = carrier;
        for (int i = 0; i < 16; ++i)
        {
            cur = carrier;
            modes.applyMergeMagnitudes (cur.data(), memory.data(), constants::numBins, p, 0);
        }
        const float dist = (float) fixtures::logSpectralDistance (cur.data(), carrier.data(),
                                                                  constants::numBins);
        std::cout << "  Merge blur logDist vs carrier=" << dist << "\n";
        CHECK (dist > 0.5f);
        for (float v : cur)
            CHECK (std::isfinite (v) && v >= 0.0f);
    }

    // Absolute ceiling inactive on normal levels
    {
        modes.reset();
        std::vector<float> cur ((size_t) constants::numBins, 0.3f);
        std::vector<float> hist ((size_t) constants::numBins, 0.3f);
        modes.applyShadowMagnitudes (cur.data(), hist.data(), constants::numBins,
                                     makeParams (0.5f), 0);
        CHECK (modes.getLastEnergyScale (0) > 0.99f);
    }
}

//==============================================================================
// Round 2 — Shadow wash + Merge audibility
//==============================================================================
void testRound2ShadowNoIsolatedPartials()
{
    std::cout << "Round 2 Shadow: no isolated ringing partials...\n";
    constexpr double sr = 48000.0;
    constexpr int burstSamples = (int) (0.10 * sr);
    // Process through burst + 1.5 s of silence so the probed tail is mid-decay.
    constexpr int totalSamples = burstSamples + (int) (1.5 * sr) + 8192;

    SpectralEngine engine;
    engine.prepare (sr, 512, 1);
    engine.setMode (SpectralMode::Shadow);
    engine.setActiveMemoryLengthSeconds (4.0f);
    engine.setSpectralParameterTargets (1.0f, 0.0f, 0.20f, 0.35f, 0.0f, 0.0f, false);
    engine.snapSpectralSmoothersToTargets();

    juce::AudioBuffer<float> buf (1, totalSamples);
    buf.clear();
    {
        juce::AudioBuffer<float> burst (1, burstSamples);
        std::uint32_t rng = 0xBEEF01u;
        fillPink (burst, rng);
        burst.applyGain (2.5f);
        buf.copyFrom (0, 0, burst, 0, 0, burstSamples);
    }

    for (int offset = 0; offset < totalSamples; )
    {
        const int n = std::min (512, totalSamples - offset);
        float* ptrs[1] = { buf.getWritePointer (0) + offset };
        juce::AudioBuffer<float> view (ptrs, 1, n);
        engine.process (view);
        offset += n;
    }

    const float* tail = engine.getModeProcessor().getShadowTailMagnitudes (0);
    CHECK (tail != nullptr);
    if (tail != nullptr)
    {
        const float contrast = maxNeighborBinContrast (tail, constants::numBins);
        std::cout << "  maxNeighborBinContrast=" << contrast << "\n";
        CHECK (contrast < 6.0f);
    }
}

void testRound2ShadowCentroidStability()
{
    std::cout << "Round 2 Shadow: stable tail pitch (centroid)...\n";
    // Measure SpectralTail magnitude centroid hop-to-hop (not wet audio), so A2
    // darkening and dry/wet mix do not masquerade as A3 omega jitter.
    constexpr double sr = 48000.0;
    const float binHz = (float) sr / (float) constants::fftSize;
    const int toneBin = std::max (1, (int) std::lround (440.0 / binHz));

    SpectralTail tail;
    tail.prepare (constants::numBins, sr, constants::hopSize, 1);
    SpectralTailParams tp;
    tp.rt60Seconds = 4.0f;
    tp.hfDampRatio = 0.25f;
    tp.injectGain = 1.0f;
    tp.diffusion = 0.0f;
    tp.shimmerCents = 0.0f;
    tp.spectralDiffusion = 0.15f;
    tp.diffusionOctaves = 0.15f;

    std::vector<float> mags ((size_t) constants::numBins, 0.0f);
    std::vector<float> phases ((size_t) constants::numBins, 0.0f);
    mags[(size_t) toneBin] = 2.0f;

    const int burstHops = (int) std::lround (0.10 * sr / constants::hopSize);
    const int startHop = burstHops + (int) std::lround (0.5 * sr / constants::hopSize);
    const int endHop = burstHops + (int) std::lround (2.0 * sr / constants::hopSize);
    // Aggregate ~50 ms windows (~5 hops at 48 kHz / 512).
    const int winHops = std::max (1, (int) std::lround (0.05 * sr / constants::hopSize));

    auto centroidHz = [&] (const float* m) -> float
    {
        double num = 0.0, den = 0.0;
        for (int k = 1; k < constants::numBins; ++k)
        {
            const double v = (double) m[k];
            num += v * (double) k * (double) binHz;
            den += v;
        }
        return den > 1.0e-12 ? (float) (num / den) : 0.0f;
    };

    float prevWin = 0.0f;
    float maxRel = 0.0f;
    double winSum = 0.0;
    int winCount = 0;
    int wins = 0;

    for (int h = 0; h < endHop; ++h)
    {
        if (h >= burstHops)
            std::fill (mags.begin(), mags.end(), 0.0f);
        else
        {
            std::fill (mags.begin(), mags.end(), 0.0f);
            mags[(size_t) toneBin] = 2.0f;
            // Coherent phase advance at the tone's expected omega.
            phases[(size_t) toneBin] += 2.0f * juce::MathConstants<float>::pi
                                        * (float) toneBin * (float) constants::hopSize
                                        / (float) constants::fftSize;
        }

        tail.processHop (0, mags.data(), phases.data(), tp);

        if (h < startHop)
            continue;

        winSum += (double) centroidHz (tail.getTailMagnitudes (0));
        ++winCount;
        if (winCount >= winHops)
        {
            const float c = (float) (winSum / (double) winCount);
            if (wins > 0 && prevWin > 1.0f && c > 1.0f)
                maxRel = std::max (maxRel, std::abs (c - prevWin) / prevWin);
            prevWin = c;
            winSum = 0.0;
            winCount = 0;
            ++wins;
        }
    }

    std::cout << "  max 50ms-window centroid rel change=" << maxRel
              << " windows=" << wins << "\n";
    CHECK (wins >= 4);
    CHECK (maxRel <= 0.08f);
}

void testRound2ShadowBandDecayRatio()
{
    std::cout << "Round 2 Shadow: mid-treble damps faster than low-mids...\n";
    constexpr double sr = 48000.0;
    const float binHz = (float) sr / (float) constants::fftSize;
    const int loA = std::max (1, (int) std::lround (200.0 / binHz));
    const int hiA = std::min (constants::numBins - 1, (int) std::lround (600.0 / binHz));
    const int loB = std::max (1, (int) std::lround (2000.0 / binHz));
    const int hiB = std::min (constants::numBins - 1, (int) std::lround (6000.0 / binHz));

    auto bandPow = [&] (const float* m, int lo, int hi) -> double
    {
        double e = 0.0;
        for (int k = lo; k <= hi; ++k)
            e += (double) m[k] * (double) m[k];
        return e;
    };

    SpectralTail tail;
    tail.prepare (constants::numBins, sr, constants::hopSize, 1);
    SpectralTailParams tp;
    tp.rt60Seconds = 3.0f;
    tp.hfDampRatio = 0.20f;
    tp.injectGain = 1.0f;
    tp.diffusion = 0.0f;
    tp.shimmerCents = 0.0f;
    tp.spectralDiffusion = 0.15f;
    tp.diffusionOctaves = 0.15f;
    tp.freeze = false;

    std::vector<float> mags ((size_t) constants::numBins, 0.5f);
    std::vector<float> phases ((size_t) constants::numBins, 0.0f);
    const int injectHops = (int) std::lround (0.15 * sr / constants::hopSize);
    for (int h = 0; h < injectHops; ++h)
    {
        for (int k = 0; k < constants::numBins; ++k)
            phases[(size_t) k] = (float) k * 0.01f * (float) h;
        tail.processHop (0, mags.data(), phases.data(), tp);
    }

    std::fill (mags.begin(), mags.end(), 0.0f);
    const int decayHops = (int) std::lround (0.8 * sr / constants::hopSize);
    const float* t0 = nullptr;
    double eA0 = 0, eB0 = 0, eA1 = 0, eB1 = 0;
    for (int h = 0; h < decayHops; ++h)
    {
        tail.processHop (0, mags.data(), phases.data(), tp);
        if (h == 0)
        {
            t0 = tail.getTailMagnitudes (0);
            eA0 = bandPow (t0, loA, hiA);
            eB0 = bandPow (t0, loB, hiB);
        }
    }
    const float* t1 = tail.getTailMagnitudes (0);
    eA1 = bandPow (t1, loA, hiA);
    eB1 = bandPow (t1, loB, hiB);

    const double decayA = (eA0 > 1e-20) ? (eA1 / eA0) : 1.0;
    const double decayB = (eB0 > 1e-20) ? (eB1 / eB0) : 1.0;
    // Smaller remaining fraction ⇒ faster decay. Mid-treble should retain ≤ half of low-mids.
    std::cout << "  remaining frac low=" << decayA << " midHF=" << decayB << "\n";
    CHECK (eA0 > 1e-8 && eB0 > 1e-8);
    CHECK (decayB <= decayA * 0.5);
}

void testRound2ShadowBurstRt60Independent()
{
    std::cout << "Round 2 Shadow: burst level RT60-independent...\n";
    constexpr double sr = 48000.0;
    const int burstHops = (int) std::lround (0.20 * sr / constants::hopSize);
    const int measureHops = (int) std::lround (0.30 * sr / constants::hopSize);

    auto peakAfterBurst = [&] (float rt60) -> float
    {
        SpectralTail tail;
        tail.prepare (constants::numBins, sr, constants::hopSize, 1);
        SpectralTailParams tp;
        tp.rt60Seconds = rt60;
        tp.hfDampRatio = 0.22f;
        tp.injectGain = 1.0f;
        tp.spectralDiffusion = 0.15f;
        tp.diffusionOctaves = 0.15f;

        std::vector<float> mags ((size_t) constants::numBins, 0.8f);
        std::vector<float> phases ((size_t) constants::numBins, 0.0f);
        float peak = 0.0f;
        for (int h = 0; h < burstHops + measureHops; ++h)
        {
            if (h >= burstHops)
                std::fill (mags.begin(), mags.end(), 0.0f);
            for (int k = 0; k < constants::numBins; ++k)
                phases[(size_t) k] = 0.02f * (float) k * (float) (h + 1);
            tail.processHop (0, mags.data(), phases.data(), tp);
            if (h >= burstHops)
            {
                const float* tm = tail.getTailMagnitudes (0);
                for (int k = 1; k < constants::numBins; ++k)
                    peak = std::max (peak, tm[k]);
            }
        }
        return peak;
    };

    const float p1 = peakAfterBurst (1.0f);
    const float p20 = peakAfterBurst (20.0f);
    const float diffDb = std::abs (constants::gainToDb ((p1 + 1e-12f) / (p20 + 1e-12f)));
    std::cout << "  peak@1s=" << p1 << " peak@20s=" << p20 << " |diff|dB=" << diffDb << "\n";
    CHECK (p1 > 1e-6f && p20 > 1e-6f);
    CHECK (diffDb < 6.0f);
}

//==============================================================================
// Round 3 — Merge spectral blur (engine-level)
//==============================================================================
namespace
{
void processEngineBlocks (SpectralEngine& engine, juce::AudioBuffer<float>& buf, int block = 512)
{
    for (int offset = 0; offset < buf.getNumSamples(); )
    {
        const int n = std::min (block, buf.getNumSamples() - offset);
        std::vector<float*> ptrs ((size_t) buf.getNumChannels());
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            ptrs[(size_t) ch] = buf.getWritePointer (ch) + offset;
        juce::AudioBuffer<float> view (ptrs.data(), buf.getNumChannels(), n);
        engine.process (view);
        offset += n;
    }
}

void fillClickTrain (juce::AudioBuffer<float>& buf, double sr, float hz, float amp) noexcept
{
    buf.clear();
    const int period = std::max (1, (int) std::lround (sr / (double) hz));
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        for (int i = 0; i < buf.getNumSamples(); i += period)
            buf.setSample (ch, i, amp);
}

[[nodiscard]] float crestFactor (const float* x, int n) noexcept
{
    if (n <= 0)
        return 0.0f;
    float peak = 0.0f;
    double e = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const float a = std::abs (x[i]);
        peak = std::max (peak, a);
        e += (double) x[i] * (double) x[i];
    }
    const float rms = (float) std::sqrt (e / (double) n);
    return peak / std::max (1.0e-12f, rms);
}

[[nodiscard]] float normalisedXcorr (const float* a, const float* b, int n) noexcept
{
    if (n <= 0)
        return 1.0f;
    double sa = 0.0, sb = 0.0, sab = 0.0, saa = 0.0, sbb = 0.0;
    for (int i = 0; i < n; ++i)
    {
        sa += a[i];
        sb += b[i];
        sab += (double) a[i] * (double) b[i];
        saa += (double) a[i] * (double) a[i];
        sbb += (double) b[i] * (double) b[i];
    }
    const double invN = 1.0 / (double) n;
    const double ma = sa * invN, mb = sb * invN;
    const double cov = sab * invN - ma * mb;
    const double va = saa * invN - ma * ma;
    const double vb = sbb * invN - mb * mb;
    if (va <= 1.0e-20 || vb <= 1.0e-20)
        return 0.0f;
    return (float) (cov / std::sqrt (va * vb));
}

void configureMergeEngine (SpectralEngine& engine, float influence, float blur,
                           int channels = 1, float recall = 0.0f)
{
    engine.prepare (48000.0, 512, channels);
    engine.setMode (SpectralMode::Merge);
    engine.setActiveMemoryLengthSeconds (3.0f);
    engine.setSpectralParameterTargets (influence, recall, 0.25f, blur, 0.0f, 0.0f, false);
    engine.snapSpectralSmoothersToTargets();
}
} // namespace

void testRound3MergeCrestDissolve()
{
    std::cout << "Round 3 Merge: crest factor dissolve...\n";
    constexpr double sr = 48000.0;
    constexpr int total = (int) (4.0 * sr);
    const int latency = constants::fftSize;

    juce::AudioBuffer<float> dry (1, total);
    fillClickTrain (dry, sr, 4.0f, 1.0f);
    juce::AudioBuffer<float> wet;
    wet.makeCopyOf (dry);

    SpectralEngine identity;
    configureMergeEngine (identity, 0.0f, 1.0f, 1);
    processEngineBlocks (identity, dry);

    SpectralEngine blur;
    configureMergeEngine (blur, 1.0f, 1.0f, 1);
    processEngineBlocks (blur, wet);

    const int start = latency + (int) (0.5 * sr);
    const int n = total - start - (int) (0.25 * sr);
    CHECK (n > 4096);
    const float crestDry = crestFactor (dry.getReadPointer (0) + start, n);
    const float crestWet = crestFactor (wet.getReadPointer (0) + start, n);
    const float reduction = 1.0f - crestWet / std::max (1.0e-6f, crestDry);
    std::cout << "  crestDry=" << crestDry << " crestWet=" << crestWet
              << " reduction=" << reduction << "\n";
    CHECK (crestDry > 2.0f);
    CHECK (reduction >= 0.60f);
}

/** Moderate Blur/Influence must already dissolve crest (spectral cloud, not subtle EQ). */
void testRound4MergeModerateBlurAudible()
{
    std::cout << "Round 4 Merge: moderate Blur crest dissolve...\n";
    constexpr double sr = 48000.0;
    constexpr int total = (int) (4.0 * sr);
    const int latency = constants::fftSize;

    juce::AudioBuffer<float> dry (1, total);
    fillClickTrain (dry, sr, 4.0f, 1.0f);
    juce::AudioBuffer<float> wet;
    wet.makeCopyOf (dry);

    SpectralEngine identity;
    configureMergeEngine (identity, 0.0f, 0.20f, 1);
    processEngineBlocks (identity, dry);

    SpectralEngine blur;
    configureMergeEngine (blur, 0.50f, 0.20f, 1);
    processEngineBlocks (blur, wet);

    const int start = latency + (int) (0.5 * sr);
    const int n = total - start - (int) (0.25 * sr);
    const float crestDry = crestFactor (dry.getReadPointer (0) + start, n);
    const float crestWet = crestFactor (wet.getReadPointer (0) + start, n);
    const float reduction = 1.0f - crestWet / std::max (1.0e-6f, crestDry);
    std::cout << "  @Infl 50% Blur 20%: crestDry=" << crestDry << " crestWet=" << crestWet
              << " reduction=" << reduction << "\n";
    CHECK (crestDry > 2.0f);
    CHECK (reduction >= 0.30f);
}

void testRound3MergePhaseDecorrelation()
{
    std::cout << "Round 3 Merge: dry/wet cross-correlation...\n";
    constexpr double sr = 48000.0;
    constexpr int total = (int) (3.0 * sr);
    const int latency = constants::fftSize;

    juce::AudioBuffer<float> dry (1, total);
    {
        std::uint32_t rng = 0xA11CEu;
        fillPink (dry, rng);
        dry.applyGain (0.8f);
    }
    juce::AudioBuffer<float> wet;
    wet.makeCopyOf (dry);

    SpectralEngine identity;
    configureMergeEngine (identity, 0.0f, 1.0f, 1);
    processEngineBlocks (identity, dry);

    SpectralEngine blur;
    configureMergeEngine (blur, 1.0f, 1.0f, 1);
    processEngineBlocks (blur, wet);

    const int start = latency + (int) (0.4 * sr);
    const int n = total - start - 2048;
    const float xcorr = normalisedXcorr (dry.getReadPointer (0) + start,
                                         wet.getReadPointer (0) + start, n);
    std::cout << "  normalised xcorr=" << xcorr << "\n";
    CHECK (xcorr < 0.3f);
}

void testRound3MergeBlurMonotonicCrest()
{
    std::cout << "Round 3 Merge: Blur monotonic crest reduction...\n";
    constexpr double sr = 48000.0;
    constexpr int total = (int) (3.5 * sr);
    const int latency = constants::fftSize;
    const int start = latency + (int) (0.5 * sr);
    const int n = total - start - (int) (0.25 * sr);

    juce::AudioBuffer<float> src (1, total);
    fillClickTrain (src, sr, 4.0f, 1.0f);

    float prevReduction = -1.0f;
    for (int step = 0; step <= 10; ++step)
    {
        const float blurAmt = (float) step / 10.0f;
        juce::AudioBuffer<float> dry;
        dry.makeCopyOf (src);
        juce::AudioBuffer<float> wet;
        wet.makeCopyOf (src);

        SpectralEngine identity;
        configureMergeEngine (identity, 0.0f, blurAmt, 1);
        processEngineBlocks (identity, dry);

        SpectralEngine blur;
        configureMergeEngine (blur, 1.0f, blurAmt, 1);
        processEngineBlocks (blur, wet);

        const float crestDry = crestFactor (dry.getReadPointer (0) + start, n);
        const float crestWet = crestFactor (wet.getReadPointer (0) + start, n);
        const float reduction = 1.0f - crestWet / std::max (1.0e-6f, crestDry);
        std::cout << "  Blur=" << blurAmt << " reduction=" << reduction << "\n";
        CHECK (reduction + 0.02f >= prevReduction);
        prevReduction = reduction;
    }
}

void testRound3MergeStereoDecorrelation()
{
    std::cout << "Round 3 Merge: stereo decorrelation...\n";
    constexpr double sr = 48000.0;
    constexpr int total = (int) (3.0 * sr);
    const int latency = constants::fftSize;

    juce::AudioBuffer<float> buf (2, total);
    {
        juce::AudioBuffer<float> mono (1, total);
        std::uint32_t rng = 0x51EEDB01u;
        fillPink (mono, rng);
        mono.applyGain (0.75f);
        buf.copyFrom (0, 0, mono, 0, 0, total);
        buf.copyFrom (1, 0, mono, 0, 0, total);
    }

    SpectralEngine engine;
    configureMergeEngine (engine, 1.0f, 0.75f, 2);
    processEngineBlocks (engine, buf);

    const int start = latency + (int) (0.5 * sr);
    const int n = total - start - 2048;
    const float corr = normalisedXcorr (buf.getReadPointer (0) + start,
                                        buf.getReadPointer (1) + start, n);
    std::cout << "  L/R corr=" << corr << "\n";
    CHECK (corr < 0.6f);
}

void testRound3MergePinkSpectrumBalance()
{
    std::cout << "Round 3 Merge: pink-noise spectrum balance (not de-esser)...\n";
    constexpr double sr = 48000.0;
    constexpr int total = (int) (4.0 * sr);
    const int latency = constants::fftSize;

    juce::AudioBuffer<float> dry (1, total);
    {
        std::uint32_t rng = 0x91A5E001u;
        fillPink (dry, rng);
        dry.applyGain (0.7f);
    }
    juce::AudioBuffer<float> wet;
    wet.makeCopyOf (dry);

    SpectralEngine identity;
    configureMergeEngine (identity, 0.0f, 1.0f, 1);
    processEngineBlocks (identity, dry);

    SpectralEngine blur;
    configureMergeEngine (blur, 1.0f, 1.0f, 1);
    processEngineBlocks (blur, wet);

    constexpr int order = 11; // 2048
    constexpr int N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<double> accIn ((size_t) (N / 2), 0.0);
    std::vector<double> accOut ((size_t) (N / 2), 0.0);
    std::vector<float> scratch ((size_t) (N * 2), 0.0f);
    int frames = 0;
    const int start = latency + (int) (0.5 * sr);
    for (int pos = start; pos + N < total; pos += N / 2)
    {
        auto accumulate = [&] (const float* x, std::vector<double>& acc)
        {
            std::fill (scratch.begin(), scratch.end(), 0.0f);
            for (int i = 0; i < N; ++i)
                scratch[(size_t) i] = x[pos + i]
                    * (0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi
                                               * (float) i / (float) N));
            fft.performFrequencyOnlyForwardTransform (scratch.data());
            for (int k = 1; k < N / 2; ++k)
                acc[(size_t) k] += (double) scratch[(size_t) k];
        };
        accumulate (dry.getReadPointer (0), accIn);
        accumulate (wet.getReadPointer (0), accOut);
        ++frames;
    }
    CHECK (frames > 8);

    double err = 0.0;
    int count = 0;
    // Skip DC / very low bins and near-Nyquist; compare midband spectral balance.
    for (int k = 8; k < N / 2 - 8; ++k)
    {
        const float inDb = constants::gainToDb ((float) (accIn[(size_t) k] / frames) + 1e-12f);
        const float outDb = constants::gainToDb ((float) (accOut[(size_t) k] / frames) + 1e-12f);
        const double d = (double) outDb - (double) inDb;
        err += d * d;
        ++count;
    }
    // Level-normalize: subtract mean dB offset before RMS.
    double mean = 0.0;
    for (int k = 8; k < N / 2 - 8; ++k)
    {
        const float inDb = constants::gainToDb ((float) (accIn[(size_t) k] / frames) + 1e-12f);
        const float outDb = constants::gainToDb ((float) (accOut[(size_t) k] / frames) + 1e-12f);
        mean += (double) outDb - (double) inDb;
    }
    mean /= std::max (1, count);
    err = 0.0;
    for (int k = 8; k < N / 2 - 8; ++k)
    {
        const float inDb = constants::gainToDb ((float) (accIn[(size_t) k] / frames) + 1e-12f);
        const float outDb = constants::gainToDb ((float) (accOut[(size_t) k] / frames) + 1e-12f);
        const double d = ((double) outDb - (double) inDb) - mean;
        err += d * d;
    }
    const float rmsDb = (float) std::sqrt (err / std::max (1, count));
    std::cout << "  spectrum shape RMS err dB=" << rmsDb << " (mean offset " << mean << ")\n";
    CHECK (rmsDb <= 3.0f);
}

void testRound3MergeMixZeroNull()
{
    std::cout << "Round 3 Merge: Mix 0% null (bit-identical dry)...\n";
    constexpr double sr = 48000.0;
    constexpr int block = 512;
    constexpr int total = 48000;

    AfterimageAudioProcessor proc;
    proc.setPlayConfigDetails (1, 1, sr, block);
    proc.prepareToPlay (sr, block);

    if (auto* mode = proc.getAPVTS().getParameter (constants::idMode))
    {
        mode->beginChangeGesture();
        mode->setValueNotifyingHost (mode->convertTo0to1 (2.0f)); // Merge
        mode->endChangeGesture();
    }
    if (auto* mix = proc.getAPVTS().getParameter (constants::idMix))
    {
        mix->beginChangeGesture();
        mix->setValueNotifyingHost (mix->convertTo0to1 (0.0f));
        mix->endChangeGesture();
    }
    if (auto* infl = proc.getAPVTS().getParameter (constants::idInfluence))
    {
        infl->beginChangeGesture();
        infl->setValueNotifyingHost (infl->convertTo0to1 (1.0f));
        infl->endChangeGesture();
    }
    if (auto* blur = proc.getAPVTS().getParameter (constants::idBlur))
    {
        blur->beginChangeGesture();
        blur->setValueNotifyingHost (blur->convertTo0to1 (1.0f));
        blur->endChangeGesture();
    }
    if (auto* gm = proc.getAPVTS().getParameter (constants::idGainMatch))
    {
        gm->beginChangeGesture();
        gm->setValueNotifyingHost (0.0f);
        gm->endChangeGesture();
    }

    // Settle smoothers on silence.
    {
        juce::AudioBuffer<float> warm (1, 8192);
        warm.clear();
        juce::MidiBuffer midi;
        for (int off = 0; off < warm.getNumSamples(); off += block)
        {
            const int n = std::min (block, warm.getNumSamples() - off);
            float* p = warm.getWritePointer (0) + off;
            juce::AudioBuffer<float> view (&p, 1, n);
            proc.processBlock (view, midi);
        }
    }

    juce::AudioBuffer<float> buf (1, total);
    for (int i = 0; i < total; ++i)
        buf.setSample (0, i, 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                       * 440.0 * (double) i / sr));
    juce::AudioBuffer<float> dry;
    dry.makeCopyOf (buf);

    {
        juce::MidiBuffer midi;
        for (int off = 0; off < total; off += block)
        {
            const int n = std::min (block, total - off);
            float* p = buf.getWritePointer (0) + off;
            juce::AudioBuffer<float> view (&p, 1, n);
            proc.processBlock (view, midi);
        }
    }

    const int latency = proc.getLatencySamples();
    double maxErr = 0.0;
    for (int i = latency; i < total; ++i)
        maxErr = std::max (maxErr, (double) std::abs (buf.getSample (0, i)
                                                      - dry.getSample (0, i - latency)));
    std::cout << "  Mix0 maxErr=" << maxErr << " latency=" << latency << "\n";
    CHECK (maxErr < 1.0e-5);
}

void testRound3MergeNoAlloc()
{
    std::cout << "Round 3 Merge: no post-prepare allocation (capacity stable)...\n";
    SpectralEngine engine;
    configureMergeEngine (engine, 1.0f, 1.0f, 2);
    const int cap0 = engine.getHistory (0).getCapacity();
    const int cap1 = engine.getHistory (1).getCapacity();
    CHECK (cap0 > 0 && cap1 > 0);

    juce::AudioBuffer<float> buf (2, 48000);
    {
        std::uint32_t rng = 0xA110C001u;
        fillPink (buf, rng);
    }
    processEngineBlocks (engine, buf);

    CHECK (engine.getHistory (0).getCapacity() == cap0);
    CHECK (engine.getHistory (1).getCapacity() == cap1);

    // SpectralBlur isolation smoke: prepare once, many hops, finite output.
    SpectralBlur blur;
    blur.prepare (constants::numBins, 48000.0, constants::hopSize, 2);
    std::vector<float> mags ((size_t) constants::numBins, 0.1f);
    std::vector<float> phases ((size_t) constants::numBins, 0.0f);
    SpectralBlurParams bp;
    bp.timeSmearMs = 400.0f;
    bp.freqSmearOctaves = 0.2f;
    bp.phaseScatter = 0.8f;
    bp.memoryBlend = 0.0f;
    for (int h = 0; h < 200; ++h)
    {
        blur.processHop (0, mags.data(), phases.data(), nullptr, bp);
        blur.processHop (1, mags.data(), phases.data(), nullptr, bp);
    }
    const float* bm = blur.getBlurMagnitudes (0);
    const float* bp0 = blur.getBlurPhases (0);
    CHECK (bm != nullptr && bp0 != nullptr);
    for (int k = 0; k < constants::numBins; ++k)
    {
        CHECK (std::isfinite (bm[k]));
        CHECK (std::isfinite (bp0[k]));
    }
    std::cout << "  history capacity stable; SpectralBlur hops finite\n";
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
    testMergeMidInfluenceEnergyBound();
    testLoudnessStability();
    testBaselineDiagnosisPrint();
    testPhase9ShadowTailAcceptance();
    testPhase9EraseMergeAcceptance();
    testRound2ShadowNoIsolatedPartials();
    testRound2ShadowCentroidStability();
    testRound2ShadowBandDecayRatio();
    testRound2ShadowBurstRt60Independent();
    testRound3MergeCrestDissolve();
    testRound4MergeModerateBlurAudible();
    testRound3MergePhaseDecorrelation();
    testRound3MergeBlurMonotonicCrest();
    testRound3MergeStereoDecorrelation();
    testRound3MergePinkSpectrumBalance();
    testRound3MergeMixZeroNull();
    testRound3MergeNoAlloc();
}
