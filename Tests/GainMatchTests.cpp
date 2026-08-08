#include "PluginProcessor.h"
#include "Utilities/Constants.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <functional>
#include <iostream>
#include <vector>

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
                      << " tol=" << (tol) << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            ++gFailures; \
        } \
    } while (0)

namespace
{
using namespace afterimage::constants;

void setParam (AfterimageAudioProcessor& p, const char* id, float value)
{
    if (auto* param = p.getAPVTS().getParameter (id))
    {
        param->beginChangeGesture();
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            param->setValueNotifyingHost (ranged->convertTo0to1 (value));
        else
            param->setValueNotifyingHost (value);
        param->endChangeGesture();
    }
}

void setBool (AfterimageAudioProcessor& p, const char* id, bool on)
{
    if (auto* param = p.getAPVTS().getParameter (id))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (on ? 1.0f : 0.0f);
        param->endChangeGesture();
    }
}

void prepareProc (AfterimageAudioProcessor& p, double sr, int block, int channels)
{
    p.setPlayConfigDetails (channels, channels, sr, block);
    p.prepareToPlay (sr, block);
}

void processBuffer (AfterimageAudioProcessor& p, juce::AudioBuffer<float>& buf)
{
    juce::MidiBuffer midi;
    p.processBlock (buf, midi);
}

void fillSine (juce::AudioBuffer<float>& buf, double sr, float freq, float amp, int sampleOffset = 0)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * (double) freq / sr;
    for (int i = 0; i < buf.getNumSamples(); ++i)
    {
        const float s = amp * (float) std::sin (w * (double) (sampleOffset + i));
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            buf.setSample (ch, i, s);
    }
}

void fillSilence (juce::AudioBuffer<float>& buf) { buf.clear(); }

float bufferRms (const juce::AudioBuffer<float>& buf, int start, int n)
{
    double acc = 0.0;
    const int chs = buf.getNumChannels();
    const int end = juce::jmin (buf.getNumSamples(), start + n);
    const int count = juce::jmax (1, (end - start) * chs);
    for (int ch = 0; ch < chs; ++ch)
        for (int i = start; i < end; ++i)
        {
            const float x = buf.getSample (ch, i);
            acc += (double) x * (double) x;
        }
    return std::sqrt ((float) (acc / (double) count));
}

float bufferPeak (const juce::AudioBuffer<float>& buf)
{
    float m = 0.0f;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        m = juce::jmax (m, buf.getMagnitude (ch, 0, buf.getNumSamples()));
    return m;
}

struct RenderResult
{
    float correctionDb = 0.0f;
    float outRms = 0.0f;
};

RenderResult renderSeconds (AfterimageAudioProcessor& p,
                            double sr,
                            int block,
                            int channels,
                            float seconds,
                            const std::function<void(juce::AudioBuffer<float>&, int offset)>& fill)
{
    RenderResult r;
    const int total = juce::roundToInt (seconds * (float) sr);
    juce::AudioBuffer<float> buf (channels, block);

    for (int offset = 0; offset < total; offset += block)
    {
        const int n = juce::jmin (block, total - offset);
        buf.setSize (channels, n, false, false, true);
        fill (buf, offset);
        processBuffer (p, buf);
        if (offset + n >= total - block)
            r.outRms = bufferRms (buf, 0, n);
    }
    r.correctionDb = p.getGainMatchCorrectionDb();
    return r;
}

void configureIdentityWet (AfterimageAudioProcessor& p)
{
    setParam (p, idInfluence, 0.0f);
    setParam (p, idMix, 1.0f);
    setParam (p, idOutputGain, 0.0f);
    setBool (p, idBypass, false);
    setBool (p, idFreeze, false);
    setParam (p, idRandomRecall, 0.0f);
}

void testTailLength()
{
    std::cout << "GainMatch/Tail: getTailLengthSeconds...\n";
    static_assert (pluginTailLengthSec > (double) memoryLengthMaxSec, "tail must exceed max memory");
    AfterimageAudioProcessor p;
    CHECK (p.getTailLengthSeconds() > 0.0);
    CHECK (p.getTailLengthSeconds() >= (double) memoryLengthMaxSec);
    CHECK_NEAR (p.getTailLengthSeconds(), pluginTailLengthSec, 1.0e-9);
}

void testStartupSmootherSnap()
{
    std::cout << "GainMatch/Startup: frame smoothers snap to APVTS...\n";
    AfterimageAudioProcessor p;
    prepareProc (p, 48000.0, 512, 2);
    CHECK_NEAR (p.getEngine().getSmoothedInfluence(), afterimage::constants::influenceDefault, 1.0e-5);
    CHECK_NEAR (p.getEngine().getSmoothedRecall(), 0.40f, 1.0e-5);
    CHECK_NEAR (p.getEngine().getSmoothedForget(), 0.25f, 1.0e-5);
    CHECK_NEAR (p.getEngine().getSmoothedBlur(), afterimage::constants::blurDefault, 1.0e-5);
    CHECK_NEAR (p.getEngine().getSmoothedTransient(), 0.35f, 1.0e-5);
}

void testStateMigrationMissingGainMatch()
{
    std::cout << "GainMatch/State: old APVTS without gainMatch...\n";
    // Simulate an older session XML that never contained gainMatch: strip the
    // property from a saved tree and load into a fresh instance (defaults apply).
    AfterimageAudioProcessor donor;
    juce::ValueTree stripped = donor.getAPVTS().copyState();
    for (int i = stripped.getNumChildren(); --i >= 0;)
    {
        auto child = stripped.getChild (i);
        if (child.hasType ("PARAM") && child.getProperty ("id").toString() == idGainMatch)
            stripped.removeChild (i, nullptr);
    }

    AfterimageAudioProcessor loaded;
    setBool (loaded, idGainMatch, true); // ensure we do not rely on construction alone wrongly
    // Fresh load path: construct defaults then restore tree missing the ID.
    AfterimageAudioProcessor fresh;
    CHECK (fresh.getAPVTS().getRawParameterValue (idGainMatch)->load() < 0.5f);
    fresh.applyRestoredParameterTree (stripped);
    CHECK (fresh.getAPVTS().getRawParameterValue (idGainMatch)->load() < 0.5f);
}

void testIdentityFullWet()
{
    std::cout << "GainMatch: identity at full wet...\n";
    AfterimageAudioProcessor p;
    prepareProc (p, 48000.0, 512, 2);
    configureIdentityWet (p);
    setBool (p, idGainMatch, true);

    auto fill = [] (juce::AudioBuffer<float>& buf, int offset)
    {
        fillSine (buf, 48000.0, 440.0f, 0.2f, offset);
    };
    auto r = renderSeconds (p, 48000.0, 512, 2, 2.5f, fill);
    CHECK (std::abs (r.correctionDb) < 0.75f);
}

void testPartialMixLevel()
{
    std::cout << "GainMatch: partial mix level match...\n";
    const float mixes[] = { 0.25f, 0.50f, 0.75f };
    for (float mix : mixes)
    {
        AfterimageAudioProcessor p;
        prepareProc (p, 48000.0, 512, 2);
        configureIdentityWet (p);
        setParam (p, idMix, mix);
        setBool (p, idGainMatch, true);

        constexpr float amp = 0.25f;
        auto fill = [] (juce::AudioBuffer<float>& buf, int offset)
        {
            fillSine (buf, 48000.0, 1000.0f, amp, offset);
        };
        auto r = renderSeconds (p, 48000.0, 512, 2, 3.0f, fill);
        const float dryRmsTarget = amp / std::sqrt (2.0f);
        CHECK (std::abs (r.outRms - dryRmsTarget) < 0.05f);
        juce::ignoreUnused (mix);
    }
}

void testFrequencyNeutrality()
{
    std::cout << "GainMatch: frequency neutrality...\n";
    const float freqs[] = { 80.0f, 250.0f, 1000.0f, 5000.0f, 10000.0f };
    float corrs[5] {};
    int i = 0;
    for (float f : freqs)
    {
        AfterimageAudioProcessor p;
        prepareProc (p, 48000.0, 512, 2);
        configureIdentityWet (p);
        setParam (p, idMix, 0.5f);
        setBool (p, idGainMatch, true);
        auto fill = [f] (juce::AudioBuffer<float>& buf, int offset)
        {
            fillSine (buf, 48000.0, f, 0.2f, offset);
        };
        auto r = renderSeconds (p, 48000.0, 512, 2, 3.0f, fill);
        corrs[i++] = r.correctionDb;
    }
    float mn = corrs[0], mx = corrs[0];
    for (float c : corrs)
    {
        mn = juce::jmin (mn, c);
        mx = juce::jmax (mx, c);
    }
    CHECK ((mx - mn) < 0.1f);
}

void testMultitoneBinRatios()
{
    std::cout << "GainMatch: multitone spectral scalar...\n";
    AfterimageAudioProcessor offProc;
    AfterimageAudioProcessor onProc;
    prepareProc (offProc, 48000.0, 512, 1);
    prepareProc (onProc, 48000.0, 512, 1);
    configureIdentityWet (offProc);
    configureIdentityWet (onProc);
    setParam (offProc, idMix, 0.5f);
    setParam (onProc, idMix, 0.5f);
    setBool (offProc, idGainMatch, false);
    setBool (onProc, idGainMatch, true);

    auto fill = [] (juce::AudioBuffer<float>& buf, int offset)
    {
        const double sr = 48000.0;
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            const double t = (double) (offset + i) / sr;
            const float s = 0.15f * (float) (std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * t)
                                           + std::sin (2.0 * juce::MathConstants<double>::pi * 880.0 * t)
                                           + std::sin (2.0 * juce::MathConstants<double>::pi * 1760.0 * t));
            buf.setSample (0, i, s);
        }
    };

    const int fftOrder = 12;
    const int fftSize = 1 << fftOrder;
    juce::dsp::FFT fft (fftOrder);

    renderSeconds (offProc, 48000.0, 512, 1, 2.0f, fill);
    renderSeconds (onProc, 48000.0, 512, 1, 2.0f, fill);

    juce::AudioBuffer<float> cap (1, fftSize);
    fill (cap, 0);
    processBuffer (offProc, cap);
    std::vector<float> offFft ((size_t) (fftSize * 2), 0.0f), onFft ((size_t) (fftSize * 2), 0.0f);
    for (int i = 0; i < fftSize; ++i)
        offFft[(size_t) i] = cap.getSample (0, i);

    fill (cap, 0);
    processBuffer (onProc, cap);
    for (int i = 0; i < fftSize; ++i)
        onFft[(size_t) i] = cap.getSample (0, i);

    fft.performFrequencyOnlyForwardTransform (offFft.data());
    fft.performFrequencyOnlyForwardTransform (onFft.data());

    auto binFor = [&] (float hz) { return juce::roundToInt (hz * (float) fftSize / 48000.0f); };
    const int b0 = binFor (440.0f), b1 = binFor (880.0f), b2 = binFor (1760.0f);
    const float rOff01 = offFft[(size_t) b0] / juce::jmax (1.0e-8f, offFft[(size_t) b1]);
    const float rOn01 = onFft[(size_t) b0] / juce::jmax (1.0e-8f, onFft[(size_t) b1]);
    const float rOff02 = offFft[(size_t) b0] / juce::jmax (1.0e-8f, offFft[(size_t) b2]);
    const float rOn02 = onFft[(size_t) b0] / juce::jmax (1.0e-8f, onFft[(size_t) b2]);
    CHECK_NEAR (rOff01, rOn01, 0.05);
    CHECK_NEAR (rOff02, rOn02, 0.05);
}

void testTransientImmunity()
{
    std::cout << "GainMatch: transient immunity...\n";
    AfterimageAudioProcessor p;
    prepareProc (p, 48000.0, 256, 2);
    configureIdentityWet (p);
    setParam (p, idMix, 0.7f);
    setBool (p, idGainMatch, true);

    auto pad = [] (juce::AudioBuffer<float>& buf, int offset)
    {
        fillSine (buf, 48000.0, 220.0f, 0.15f, offset);
    };
    renderSeconds (p, 48000.0, 256, 2, 2.0f, pad);
    const float settled = p.getGainMatchCorrectionDb();

    const int burstSamples = juce::roundToInt (0.15 * 48000.0);
    float maxMove = 0.0f;
    float prev = settled;
    int remaining = burstSamples;
    while (remaining > 0)
    {
        const int n = juce::jmin (256, remaining);
        juce::AudioBuffer<float> buf (2, n);
        for (int i = 0; i < n; ++i)
        {
            const float env = std::exp (-(float) (burstSamples - remaining + i) / 2000.0f);
            const float noise = env * 0.9f * ((float) (i % 17) / 17.0f * 2.0f - 1.0f);
            buf.setSample (0, i, noise);
            buf.setSample (1, i, noise);
        }
        processBuffer (p, buf);
        const float c = p.getGainMatchCorrectionDb();
        maxMove = juce::jmax (maxMove, std::abs (c - prev));
        prev = c;
        remaining -= n;
    }
    CHECK (maxMove < 1.5f);
    CHECK (std::abs (p.getGainMatchCorrectionDb() - settled) < 2.0f);
}

void testBlockSizeInvariance()
{
    std::cout << "GainMatch: block-size invariance...\n";
    const int blocks[] = { 1, 31, 64, 127, 256, 512, 1024, 2048, 3000 };
    float finalCorrs[9] {};
    int idx = 0;
    for (int block : blocks)
    {
        AfterimageAudioProcessor p;
        prepareProc (p, 48000.0, block, 2);
        configureIdentityWet (p);
        setParam (p, idMix, 0.5f);
        setBool (p, idGainMatch, true);
        auto fill = [] (juce::AudioBuffer<float>& buf, int offset)
        {
            fillSine (buf, 48000.0, 500.0f, 0.2f, offset);
        };
        auto r = renderSeconds (p, 48000.0, block, 2, 3.0f, fill);
        finalCorrs[idx++] = r.correctionDb;
    }
    float mn = finalCorrs[0], mx = finalCorrs[0];
    for (float c : finalCorrs)
    {
        mn = juce::jmin (mn, c);
        mx = juce::jmax (mx, c);
    }
    CHECK ((mx - mn) < 0.15f);
}

void testSilenceReturn()
{
    std::cout << "GainMatch: silence return...\n";
    AfterimageAudioProcessor p;
    prepareProc (p, 48000.0, 512, 2);
    configureIdentityWet (p);
    setParam (p, idMix, 0.5f);
    setBool (p, idGainMatch, true);

    auto tone = [] (juce::AudioBuffer<float>& buf, int offset)
    {
        fillSine (buf, 48000.0, 800.0f, 0.2f, offset);
    };
    auto silent = [] (juce::AudioBuffer<float>& buf, int) { fillSilence (buf); };

    renderSeconds (p, 48000.0, 512, 2, 1.5f, tone);
    renderSeconds (p, 48000.0, 512, 2, 1.5f, silent);
    const float mid = p.getGainMatchCorrectionDb();
    CHECK (std::abs (mid) < gainMatchMaxMakeupDb + 0.1f);
    CHECK (std::isfinite (mid));

    auto r = renderSeconds (p, 48000.0, 512, 2, 2.0f, tone);
    CHECK (std::isfinite (r.correctionDb));
    CHECK (std::abs (r.correctionDb) <= gainMatchMaxMakeupDb + 0.1f);
    CHECK (std::abs (r.correctionDb) < 5.0f);
}

void testToggle()
{
    std::cout << "GainMatch: toggle continuity...\n";
    AfterimageAudioProcessor p;
    prepareProc (p, 48000.0, 512, 2);
    configureIdentityWet (p);
    setParam (p, idMix, 0.5f);
    setBool (p, idGainMatch, true);

    auto fill = [] (juce::AudioBuffer<float>& buf, int offset)
    {
        fillSine (buf, 48000.0, 600.0f, 0.2f, offset);
    };
    renderSeconds (p, 48000.0, 512, 2, 2.0f, fill);

    float prevPeak = 0.0f;
    for (int t = 0; t < 20; ++t)
    {
        setBool (p, idGainMatch, (t % 2) == 0);
        juce::AudioBuffer<float> buf (2, 512);
        fillSine (buf, 48000.0, 600.0f, 0.2f, 0);
        processBuffer (p, buf);
        const float peak = bufferPeak (buf);
        CHECK (std::isfinite (peak));
        if (t > 0)
            CHECK (std::abs (peak - prevPeak) < 0.15f);
        prevPeak = peak;
    }
    setBool (p, idGainMatch, false);
    renderSeconds (p, 48000.0, 512, 2, 0.3f, fill);
    p.resetAdaptiveProcessingState();
    setBool (p, idGainMatch, true);
    CHECK (std::abs (p.getGainMatchCorrectionDb()) < 0.1f);
}

void testBypass()
{
    std::cout << "GainMatch: bypass leaves dry...\n";
    AfterimageAudioProcessor p;
    prepareProc (p, 48000.0, 512, 2);
    configureIdentityWet (p);
    setParam (p, idMix, 1.0f);
    setParam (p, idInfluence, 0.8f);
    setBool (p, idGainMatch, true);
    setBool (p, idBypass, true);

    auto fill = [] (juce::AudioBuffer<float>& buf, int offset)
    {
        fillSine (buf, 48000.0, 400.0f, 0.2f, offset);
    };
    renderSeconds (p, 48000.0, 512, 2, 1.0f, fill);

    juce::AudioBuffer<float> buf (2, 2048);
    fillSine (buf, 48000.0, 400.0f, 0.2f, 0);
    juce::AudioBuffer<float> dryCopy;
    dryCopy.makeCopyOf (buf);
    processBuffer (p, buf);

    const int lat = p.getLatencySamples();
    if (buf.getNumSamples() > lat + 256)
    {
        const float outR = bufferRms (buf, lat, 512);
        const float inR = bufferRms (dryCopy, 0, 512);
        CHECK_NEAR (outR, inR, 0.02);
    }
}

void testMonoStereoLink()
{
    std::cout << "GainMatch: mono + stereo link...\n";
    {
        AfterimageAudioProcessor p;
        prepareProc (p, 48000.0, 512, 1);
        configureIdentityWet (p);
        setParam (p, idMix, 0.5f);
        setBool (p, idGainMatch, true);
        auto fill = [] (juce::AudioBuffer<float>& buf, int offset)
        {
            fillSine (buf, 48000.0, 700.0f, 0.2f, offset);
        };
        auto r = renderSeconds (p, 48000.0, 512, 1, 2.5f, fill);
        CHECK (std::isfinite (r.correctionDb));
    }
    {
        AfterimageAudioProcessor p;
        prepareProc (p, 48000.0, 512, 2);
        configureIdentityWet (p);
        setParam (p, idMix, 0.5f);
        setBool (p, idGainMatch, true);
        auto fill = [] (juce::AudioBuffer<float>& buf, int offset)
        {
            fillSine (buf, 48000.0, 700.0f, 0.2f, offset);
            for (int i = 0; i < buf.getNumSamples(); ++i)
                buf.setSample (1, i, buf.getSample (0, i) * 0.85f);
        };
        renderSeconds (p, 48000.0, 512, 2, 2.5f, fill);
        juce::AudioBuffer<float> buf (2, 512);
        fill (buf, 0);
        processBuffer (p, buf);
        double lAcc = 0.0, rAcc = 0.0;
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            lAcc += (double) buf.getSample (0, i) * buf.getSample (0, i);
            rAcc += (double) buf.getSample (1, i) * buf.getSample (1, i);
        }
        const float lRms = std::sqrt ((float) (lAcc / buf.getNumSamples()));
        const float rRms = std::sqrt ((float) (rAcc / buf.getNumSamples()));
        CHECK_NEAR (rRms / juce::jmax (1.0e-8f, lRms), 0.85, 0.05);
    }
}

void testCustomProgramReporting()
{
    std::cout << "GainMatch/Program: custom state reporting...\n";
    AfterimageAudioProcessor p;
    prepareProc (p, 48000.0, 512, 2);
    p.setCurrentProgram (0);
    CHECK (! p.isCustomProgram());
    setParam (p, idInfluence, 0.99f);
    p.refreshProgramStatus();
    CHECK (p.isCustomProgram());
    CHECK (p.getProgramName (p.getCurrentProgram()) == "Custom");

    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    AfterimageAudioProcessor loaded;
    loaded.setStateInformation (mb.getData(), (int) mb.getSize());
    CHECK (loaded.isCustomProgram());
    CHECK (loaded.getProgramName (loaded.getCurrentProgram()) == "Custom");

    // Gain Match on factory params → Custom (presets leave GM off).
    AfterimageAudioProcessor gm;
    prepareProc (gm, 48000.0, 512, 2);
    gm.setCurrentProgram (0);
    setBool (gm, idGainMatch, true);
    gm.refreshProgramStatus();
    CHECK (gm.isCustomProgram());
}
} // namespace

void runGainMatchTests()
{
    std::cout << "Gain Match + RC regression tests...\n";
    testTailLength();
    testStartupSmootherSnap();
    testStateMigrationMissingGainMatch();
    testIdentityFullWet();
    testPartialMixLevel();
    testFrequencyNeutrality();
    testMultitoneBinRatios();
    testTransientImmunity();
    testBlockSizeInvariance();
    testSilenceReturn();
    testToggle();
    testBypass();
    testMonoStereoLink();
    testCustomProgramReporting();
}
