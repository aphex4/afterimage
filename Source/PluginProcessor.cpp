#include "PluginProcessor.h"

#include <cmath>

namespace
{
using namespace afterimage::constants;

juce::String percentText (float value01, int) { return juce::String (juce::roundToInt (value01 * 100.0f)) + " %"; }
juce::String secondsText (float seconds, int)
{
    return seconds < 1.0f ? juce::String (seconds, 2) + " s" : juce::String (seconds, 1) + " s";
}
juce::String dbText (float db, int) { return juce::String (db, 1) + " dB"; }

bool paramsMatchPreset (const juce::AudioProcessorValueTreeState& apvts, int index) noexcept
{
    if (index < 0 || index >= afterimage::factory::kNumPresets)
        return false;

    const auto& pr = afterimage::factory::kPresets[static_cast<std::size_t> (index)];
    auto near = [] (float a, float b, float tol) { return std::abs (a - b) <= tol; };

    auto* mode = apvts.getRawParameterValue (idMode);
    auto* mem = apvts.getRawParameterValue (idMemoryLength);
    auto* recall = apvts.getRawParameterValue (idRecallPosition);
    auto* infl = apvts.getRawParameterValue (idInfluence);
    auto* forget = apvts.getRawParameterValue (idForget);
    auto* blur = apvts.getRawParameterValue (idBlur);
    auto* trans = apvts.getRawParameterValue (idTransientPreserve);
    auto* freeze = apvts.getRawParameterValue (idFreeze);
    auto* random = apvts.getRawParameterValue (idRandomRecall);
    auto* outG = apvts.getRawParameterValue (idOutputGain);
    auto* mix = apvts.getRawParameterValue (idMix);
    auto* gainMatch = apvts.getRawParameterValue (idGainMatch);
    if (mode == nullptr || mem == nullptr || recall == nullptr || infl == nullptr
        || forget == nullptr || blur == nullptr || trans == nullptr || freeze == nullptr
        || random == nullptr || outG == nullptr || mix == nullptr || gainMatch == nullptr)
        return false;

    // Factory presets always leave Gain Match off; enabling it marks Custom.
    return juce::roundToInt (mode->load()) == pr.mode
        && near (mem->load(), pr.memoryLengthSec, 0.02f)
        && near (recall->load(), pr.recallPosition, 0.002f)
        && near (infl->load(), pr.influence, 0.002f)
        && near (forget->load(), pr.forget, 0.002f)
        && near (blur->load(), pr.blur, 0.002f)
        && near (trans->load(), pr.transientPreserve, 0.002f)
        && ((freeze->load() > 0.5f) == pr.freeze)
        && near (random->load(), pr.randomRecall, 0.002f)
        && near (outG->load(), pr.outputGainDb, 0.15f)
        && near (mix->load(), pr.mix, 0.002f)
        && (gainMatch->load() < 0.5f);
}
} // namespace

AfterimageAudioProcessor::AfterimageAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AFTERIMAGE", createParameterLayout())
{
    auto bind = [this] (const char* id) -> std::atomic<float>*
    {
        jassert (apvts.getParameter (id) != nullptr);
        return apvts.getRawParameterValue (id);
    };

    pMode = bind (idMode);
    pMemoryLength = bind (idMemoryLength);
    pRecallPosition = bind (idRecallPosition);
    pInfluence = bind (idInfluence);
    pForget = bind (idForget);
    pBlur = bind (idBlur);
    pTransientPreserve = bind (idTransientPreserve);
    pFreeze = bind (idFreeze);
    pRandomRecall = bind (idRandomRecall);
    pOutputGain = bind (idOutputGain);
    pMix = bind (idMix);
    pBypass = bind (idBypass);
    pGainMatch = bind (idGainMatch);

#if defined (AFTERIMAGE_ENABLE_LICENSING)
  #if ! defined (AFTERIMAGE_UNIT_TESTS)
    licenseManager_.initialise();
  #endif
    entitlementDryAmount_.reset (44100.0, 0.05);
    entitlementDryAmount_.setCurrentAndTargetValue (
  #if defined (AFTERIMAGE_UNIT_TESTS)
        0.0f);
  #else
        licenseManager_.getEntitlement()
            == afterimage::licensing::EntitlementState::DryPassThrough ? 1.0f : 0.0f);
  #endif
#endif
}

juce::AudioProcessorValueTreeState::ParameterLayout AfterimageAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { idMode, 1 }, "Mode", juce::StringArray { "Shadow", "Erase", "Merge" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idMemoryLength, 1 }, "Memory Length",
        juce::NormalisableRange<float> (memoryLengthMinSec, memoryLengthMaxSec, 0.01f, 0.45f),
        memoryLengthDefaultSec,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (secondsText)));
    // Defaults match Soft Shadow for an instant demo on fresh loads.
    // Influence 0.50 (was 0.40): new-instance default only — saved sessions keep their values.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idRecallPosition, 1 }, "Recall Position",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.40f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idInfluence, 1 }, "Influence",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), influenceDefault,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idForget, 1 }, "Forget",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.25f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idBlur, 1 }, "Blur",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), blurDefault,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idTransientPreserve, 1 }, "Transient Preserve",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.35f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { idFreeze, 1 }, "Freeze", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idRandomRecall, 1 }, "Random Recall",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idOutputGain, 1 }, "Output Gain",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idMix, 1 }, "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { idBypass, 1 }, "Bypass", false));
    // gainMatch: new ID (default off). Absent from older session XML — APVTS keeps default.
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { idGainMatch, 1 }, "Gain Match", false));

    return { params.begin(), params.end() };
}

void AfterimageAudioProcessor::resetAdaptiveProcessingState() noexcept
{
    gainMatch_.reset();
    smoothers.gainMatchAmount.setCurrentAndTargetValue (
        pGainMatch != nullptr && pGainMatch->load() > 0.5f ? 1.0f : 0.0f);
}

void AfterimageAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxChannels_ = juce::jmax (2, juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels()));
    maxChunk_ = afterimage::constants::maxInternalBlockSize;

    engine.prepare (sampleRate, maxChunk_, maxChannels_);
    smoothers.prepareSampleSmoothers (sampleRate);
    gainMatch_.prepare (sampleRate);

    const int latency = engine.getLatencySamples();
    dryWetMixer.prepare (maxChannels_, maxChunk_, latency);
    setLatencySamples (latency);

    inputScratch.setSize (maxChannels_, maxChunk_, false, true, true);
    delayedDry.setSize (maxChannels_, maxChunk_, false, true, true);
    wetPtrs_.assign (static_cast<size_t> (maxChannels_), nullptr);
    dryInPtrs_.assign (static_cast<size_t> (maxChannels_), nullptr);
    delayedPtrs_.assign (static_cast<size_t> (maxChannels_), nullptr);

    updateParameterTargets();
    // Snap sample + frame smoothers to live APVTS — no startup ramp from stale defaults.
    smoothers.mix.setCurrentAndTargetValue (smoothers.mix.getTargetValue());
    smoothers.outputGain.setCurrentAndTargetValue (smoothers.outputGain.getTargetValue());
    smoothers.bypassAmount.setCurrentAndTargetValue (smoothers.bypassAmount.getTargetValue());
    smoothers.gainMatchAmount.setCurrentAndTargetValue (smoothers.gainMatchAmount.getTargetValue());
    engine.snapSpectralSmoothersToTargets();
    resetAdaptiveProcessingState();

#if defined (AFTERIMAGE_ENABLE_LICENSING)
    entitlementDryAmount_.reset (sampleRate, 0.05);
    entitlementDryAmount_.setCurrentAndTargetValue (
        licenseManager_.getEntitlement()
            == afterimage::licensing::EntitlementState::DryPassThrough ? 1.0f : 0.0f);
#endif

    engine.requestClearHistory(); // consumed on first processBlock
    dryWetMixer.reset();
    engine.getVisualization().storeInputPeak (0.0f);
    engine.getVisualization().storeOutputPeak (0.0f);
    juce::ignoreUnused (samplesPerBlock);
}

void AfterimageAudioProcessor::releaseResources()
{
    engine.releaseResources();
    dryWetMixer.releaseResources();
}

bool AfterimageAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet();
}

void AfterimageAudioProcessor::updateParameterTargets()
{
    smoothers.mix.setTargetValue (pMix->load());
    smoothers.outputGain.setTargetValue (dbToGain (pOutputGain->load()));
    smoothers.bypassAmount.setTargetValue (pBypass->load() > 0.5f ? 1.0f : 0.0f);
    smoothers.gainMatchAmount.setTargetValue (pGainMatch->load() > 0.5f ? 1.0f : 0.0f);

    engine.setActiveMemoryLengthSeconds (pMemoryLength->load());
    engine.setMode (getCurrentMode());
    engine.setSpectralParameterTargets (pInfluence->load(),
                                        pRecallPosition->load(),
                                        pForget->load(),
                                        pBlur->load(),
                                        pTransientPreserve->load(),
                                        pRandomRecall->load(),
                                        pFreeze->load() > 0.5f);
}

void AfterimageAudioProcessor::processChunk (juce::AudioBuffer<float>& wetChunk,
                                             juce::AudioBuffer<float>& dryInChunk,
                                             juce::AudioBuffer<float>& delayedDryChunk) noexcept
{
    const int numSamples = wetChunk.getNumSamples();
    const int numChannels = wetChunk.getNumChannels();

    float inPeak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        inPeak = juce::jmax (inPeak, dryInChunk.getMagnitude (ch, 0, numSamples));

    auto& viz = engine.getVisualization();
    const float prevIn = viz.loadInputPeak();
    constexpr float rise = 0.6f, fall = 0.92f;
    viz.storeInputPeak (inPeak > prevIn ? prevIn + (inPeak - prevIn) * rise : prevIn * fall);

#if defined (AFTERIMAGE_ENABLE_LICENSING)
    // Audio thread: only read cached entitlement — never license IO / crypto.
    const bool dryOnly = licenseManager_.getEntitlement()
                         == afterimage::licensing::EntitlementState::DryPassThrough;
    entitlementDryAmount_.setTargetValue (dryOnly ? 1.0f : 0.0f);
#endif

    // Wet: identity STFT + history capture
    engine.process (wetChunk);

    // Latency-aligned dry
    dryWetMixer.processDryDelay (dryInChunk, delayedDryChunk);

    // Mix → Gain Match on completed mix → bypass → entitlement → output gain
    // Scratch for one sample of mixed across channels (stack; maxChannels_ small).
    float mixedScratch[8];
    const int chLimit = juce::jmin (numChannels, 8);

    for (int i = 0; i < numSamples; ++i)
    {
        const float mix = smoothers.mix.getNextValue();
        const float bypass = smoothers.bypassAmount.getNextValue();
        const float gain = smoothers.outputGain.getNextValue();
        const float matchAmt = smoothers.gainMatchAmount.getNextValue();
#if defined (AFTERIMAGE_ENABLE_LICENSING)
        const float entitlementDry = entitlementDryAmount_.getNextValue();
#else
        const float entitlementDry = 0.0f;
#endif

        const float dryGain = std::cos (mix * juce::MathConstants<float>::halfPi);
        const float wetGain = std::sin (mix * juce::MathConstants<float>::halfPi);

        float dryPower = 0.0f;
        float mixedPower = 0.0f;

        for (int ch = 0; ch < chLimit; ++ch)
        {
            const float dry = delayedDryChunk.getSample (ch, i);
            const float wet = wetChunk.getSample (ch, i);
            const float mixed = dry * dryGain + wet * wetGain;
            mixedScratch[ch] = mixed;
            dryPower += dry * dry;
            mixedPower += mixed * mixed;
        }

        const float invCh = 1.0f / (float) juce::jmax (1, chLimit);
        dryPower *= invCh;
        mixedPower *= invCh;

        const float gmScalar = gainMatch_.advanceAndGetScalar (dryPower, mixedPower);
        // Enable blend: matchAmt=0 → unity (GM off); matchAmt=1 → full correction.
        const float scalar = 1.0f + matchAmt * (gmScalar - 1.0f);

        for (int ch = 0; ch < chLimit; ++ch)
        {
            const float dry = delayedDryChunk.getSample (ch, i);
            float out = mixedScratch[ch] * scalar;
            out = out * (1.0f - bypass) + dry * bypass;
            out = out * (1.0f - entitlementDry) + dry * entitlementDry;
            out *= gain;
            wetChunk.setSample (ch, i, out);
        }
    }

    float outPeak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        outPeak = juce::jmax (outPeak, wetChunk.getMagnitude (ch, 0, numSamples));

    const float prevOut = viz.loadOutputPeak();
    viz.storeOutputPeak (outPeak > prevOut ? prevOut + (outPeak - prevOut) * rise : prevOut * fall);
}

void AfterimageAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), maxChannels_);

    for (int i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, numSamples);

    updateParameterTargets();

    jassert (inputScratch.getNumSamples() >= maxChunk_);
    jassert (delayedDry.getNumSamples() >= maxChunk_);

    for (int offset = 0; offset < numSamples; offset += maxChunk_)
    {
        const int chunk = juce::jmin (maxChunk_, numSamples - offset);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            wetPtrs_[static_cast<size_t> (ch)] = buffer.getWritePointer (ch) + offset;
            inputScratch.copyFrom (ch, 0, buffer, ch, offset, chunk);
            dryInPtrs_[static_cast<size_t> (ch)] = inputScratch.getWritePointer (ch);
            delayedPtrs_[static_cast<size_t> (ch)] = delayedDry.getWritePointer (ch);
        }

        juce::AudioBuffer<float> wetChunk (wetPtrs_.data(), numChannels, chunk);
        juce::AudioBuffer<float> dryInChunk (dryInPtrs_.data(), numChannels, chunk);
        juce::AudioBuffer<float> delayedChunk (delayedPtrs_.data(), numChannels, chunk);

        processChunk (wetChunk, dryInChunk, delayedChunk);
    }
}

afterimage::SpectralMode AfterimageAudioProcessor::getCurrentMode() const noexcept
{
    switch (juce::roundToInt (pMode->load()))
    {
        case 1:  return afterimage::SpectralMode::Erase;
        case 2:  return afterimage::SpectralMode::Merge;
        default: return afterimage::SpectralMode::Shadow;
    }
}

int AfterimageAudioProcessor::getNumPrograms()
{
    return afterimage::factory::kNumPresets;
}

int AfterimageAudioProcessor::getCurrentProgram()
{
    // Hosts expect [0, numPrograms). Custom state keeps last factory index but
    // getProgramName reports "Custom" via isCustomProgram().
    return juce::jlimit (0, afterimage::factory::kNumPresets - 1, currentProgram_);
}

void AfterimageAudioProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= afterimage::factory::kNumPresets)
        return;

    currentProgram_ = index;
    customProgram_ = false;
    afterimage::factory::applyPreset (apvts, index);
    engine.requestClearHistory(); // clears history + Erase familiarity on audio thread
    resetAdaptiveProcessingState();
    updateParameterTargets();
    engine.snapSpectralSmoothersToTargets();
}

const juce::String AfterimageAudioProcessor::getProgramName (int index)
{
    if (index < 0 || index >= afterimage::factory::kNumPresets)
        return {};

    // Do not falsely report a factory name when restored/edited state is custom.
    if (customProgram_ && index == getCurrentProgram())
        return "Custom";

    return afterimage::factory::kPresets[static_cast<std::size_t> (index)].name;
}

void AfterimageAudioProcessor::changeProgramName (int, const juce::String&)
{
    // Factory presets are fixed.
}

void AfterimageAudioProcessor::syncProgramIndexFromParameters() noexcept
{
    for (int i = 0; i < afterimage::factory::kNumPresets; ++i)
    {
        if (paramsMatchPreset (apvts, i))
        {
            currentProgram_ = i;
            customProgram_ = false;
            return;
        }
    }

    // Keep a valid host index but mark custom so getProgramName does not lie.
    customProgram_ = true;
    if (currentProgram_ < 0 || currentProgram_ >= afterimage::factory::kNumPresets)
        currentProgram_ = 0;
}

void AfterimageAudioProcessor::applyRestoredParameterTree (const juce::ValueTree& tree)
{
    apvts.replaceState (tree);
    engine.requestClearHistory();
    resetAdaptiveProcessingState();
    updateParameterTargets();
    engine.snapSpectralSmoothersToTargets();
    syncProgramIndexFromParameters();
}

void AfterimageAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Licensing / entitlement never enters session state — APVTS parameters only.
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AfterimageAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
            applyRestoredParameterTree (juce::ValueTree::fromXml (*xml));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AfterimageAudioProcessor();
}

#if defined (AFTERIMAGE_UNIT_TESTS)
juce::AudioProcessorEditor* AfterimageAudioProcessor::createEditor()
{
    return nullptr;
}
#endif
