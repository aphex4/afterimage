#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "DSP/DryWetMixer.h"

namespace
{
using namespace afterimage::constants;

juce::String percentText (float value01, int /*maxLen*/)
{
    return juce::String (juce::roundToInt (value01 * 100.0f)) + " %";
}

juce::String secondsText (float seconds, int /*maxLen*/)
{
    if (seconds < 1.0f)
        return juce::String (seconds, 2) + " s";
    return juce::String (seconds, 1) + " s";
}

juce::String dbText (float db, int /*maxLen*/)
{
    return juce::String (db, 1) + " dB";
}
} // namespace

//==============================================================================
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

    pMode              = bind (idMode);
    pMemoryLength      = bind (idMemoryLength);
    pRecallPosition    = bind (idRecallPosition);
    pInfluence         = bind (idInfluence);
    pForget            = bind (idForget);
    pBlur              = bind (idBlur);
    pTransientPreserve = bind (idTransientPreserve);
    pFreeze            = bind (idFreeze);
    pRandomRecall      = bind (idRandomRecall);
    pOutputGain        = bind (idOutputGain);
    pMix               = bind (idMix);
    pBypass            = bind (idBypass);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout AfterimageAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { idMode, 1 },
        "Mode",
        juce::StringArray { "Shadow", "Erase", "Merge" },
        0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idMemoryLength, 1 },
        "Memory Length",
        juce::NormalisableRange<float> (memoryLengthMinSec, memoryLengthMaxSec, 0.01f, 0.45f),
        memoryLengthDefaultSec,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (secondsText)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idRecallPosition, 1 },
        "Recall Position",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.45f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idInfluence, 1 },
        "Influence",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.50f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idForget, 1 },
        "Forget",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.35f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idBlur, 1 },
        "Blur",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.15f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idTransientPreserve, 1 },
        "Transient Preserve",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.50f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { idFreeze, 1 },
        "Freeze",
        false));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idRandomRecall, 1 },
        "Random Recall",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idOutputGain, 1 },
        "Output Gain",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idMix, 1 },
        "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { idBypass, 1 },
        "Bypass",
        false));

    return { params.begin(), params.end() };
}

//==============================================================================
void AfterimageAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numChannels = juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels());

    engine.prepare (sampleRate, samplesPerBlock, numChannels);
    smoothers.prepare (sampleRate);

    const int latency = engine.getLatencySamples();
    dryWetMixer.prepare (numChannels, samplesPerBlock, latency);
    setLatencySamples (latency);

    // Allocate with headroom so hosts that exceed the reported block size
    // still have scratch space without audio-thread realloc (capped).
    const int scratchSamples = juce::jmax (samplesPerBlock, latency);
    inputScratch.setSize (numChannels, scratchSamples, false, true, true);
    delayedDry.setSize (numChannels, scratchSamples, false, true, true);

    updateSmoothedTargets();
    smoothers.influence.setCurrentAndTargetValue (smoothers.influence.getTargetValue());
    smoothers.recallPosition.setCurrentAndTargetValue (smoothers.recallPosition.getTargetValue());
    smoothers.forget.setCurrentAndTargetValue (smoothers.forget.getTargetValue());
    smoothers.blur.setCurrentAndTargetValue (smoothers.blur.getTargetValue());
    smoothers.transientPreserve.setCurrentAndTargetValue (smoothers.transientPreserve.getTargetValue());
    smoothers.randomRecall.setCurrentAndTargetValue (smoothers.randomRecall.getTargetValue());
    smoothers.mix.setCurrentAndTargetValue (smoothers.mix.getTargetValue());
    smoothers.outputGain.setCurrentAndTargetValue (smoothers.outputGain.getTargetValue());
    smoothers.bypassAmount.setCurrentAndTargetValue (smoothers.bypassAmount.getTargetValue());

    engine.clearHistory();
    dryWetMixer.reset();
    inputLevel.store (0.0f, std::memory_order_relaxed);
    outputLevel.store (0.0f, std::memory_order_relaxed);
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

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void AfterimageAudioProcessor::updateSmoothedTargets()
{
    smoothers.influence.setTargetValue (pInfluence->load());
    smoothers.recallPosition.setTargetValue (pRecallPosition->load());
    smoothers.forget.setTargetValue (pForget->load());
    smoothers.blur.setTargetValue (pBlur->load());
    smoothers.transientPreserve.setTargetValue (pTransientPreserve->load());
    smoothers.randomRecall.setTargetValue (pRandomRecall->load());
    smoothers.mix.setTargetValue (pMix->load());
    smoothers.outputGain.setTargetValue (dbToGain (pOutputGain->load()));
    smoothers.bypassAmount.setTargetValue (pBypass->load() > 0.5f ? 1.0f : 0.0f);

    engine.setActiveMemoryLengthSeconds (pMemoryLength->load());
    engine.setFrozen (pFreeze->load() > 0.5f);
}

void AfterimageAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    for (int i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, numSamples);

    updateSmoothedTargets();

    const int copyChannels = juce::jmin (numChannels, inputScratch.getNumChannels());
    const int copySamples  = juce::jmin (numSamples, inputScratch.getNumSamples());

    // Snapshot undelayed input for metering + dry delay.
    for (int ch = 0; ch < copyChannels; ++ch)
        inputScratch.copyFrom (ch, 0, buffer, ch, 0, copySamples);

    const float inPeakSnapshot = [&]
    {
        float peak = 0.0f;
        for (int ch = 0; ch < copyChannels; ++ch)
            peak = juce::jmax (peak, inputScratch.getMagnitude (ch, 0, copySamples));
        return peak;
    }();

    // Wet path: STFT + spectral history capture (Phase 3 — spectrum still identity).
    afterimage::ModeParams modeParams;
    modeParams.influence         = smoothers.influence.getCurrentValue();
    modeParams.recallPosition    = smoothers.recallPosition.getCurrentValue();
    modeParams.forget            = smoothers.forget.getCurrentValue();
    modeParams.blur              = smoothers.blur.getCurrentValue();
    modeParams.transientPreserve = smoothers.transientPreserve.getCurrentValue();
    modeParams.randomRecall      = smoothers.randomRecall.getCurrentValue();
    modeParams.freeze            = pFreeze->load() > 0.5f;

    engine.process (buffer, modeParams, getCurrentMode());

    // Latency-aligned dry for mix / bypass.
    if (delayedDry.getNumChannels() >= copyChannels
        && delayedDry.getNumSamples() >= copySamples
        && inputScratch.getNumSamples() >= copySamples)
    {
        // Ensure delayedDry block matches this callback size for the delay read.
        juce::AudioBuffer<float> dryIn (inputScratch.getArrayOfWritePointers(),
                                        copyChannels,
                                        copySamples);
        juce::AudioBuffer<float> dryOut (delayedDry.getArrayOfWritePointers(),
                                         copyChannels,
                                         copySamples);
        dryWetMixer.processDryDelay (dryIn, dryOut);
    }

    for (int i = 0; i < copySamples; ++i)
    {
        const float mix = smoothers.mix.getNextValue();
        const float gain = smoothers.outputGain.getNextValue();
        const float bypass = smoothers.bypassAmount.getNextValue();

        smoothers.influence.getNextValue();
        smoothers.recallPosition.getNextValue();
        smoothers.forget.getNextValue();
        smoothers.blur.getNextValue();
        smoothers.transientPreserve.getNextValue();
        smoothers.randomRecall.getNextValue();

        const float dryGain = std::cos (mix * juce::MathConstants<float>::halfPi);
        const float wetGain = std::sin (mix * juce::MathConstants<float>::halfPi);

        for (int ch = 0; ch < copyChannels; ++ch)
        {
            const float dry = delayedDry.getSample (ch, i);
            float wet = buffer.getSample (ch, i) * gain;
            float mixed = dry * dryGain + wet * wetGain;
            mixed = mixed * (1.0f - bypass) + dry * bypass;
            buffer.setSample (ch, i, mixed);
        }
    }

    // If the host block exceeded our scratch, leave remaining samples as STFT wet only.
    for (int i = copySamples; i < numSamples; ++i)
    {
        smoothers.mix.skip (1);
        smoothers.outputGain.skip (1);
        smoothers.bypassAmount.skip (1);
        smoothers.influence.skip (1);
        smoothers.recallPosition.skip (1);
        smoothers.forget.skip (1);
        smoothers.blur.skip (1);
        smoothers.transientPreserve.skip (1);
        smoothers.randomRecall.skip (1);
    }

    {
        constexpr float rise = 0.6f;
        constexpr float fall = 0.92f;
        const float prevIn = inputLevel.load (std::memory_order_relaxed);
        inputLevel.store (inPeakSnapshot > prevIn
                              ? prevIn + (inPeakSnapshot - prevIn) * rise
                              : prevIn * fall,
                          std::memory_order_relaxed);

        float outPeak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            outPeak = juce::jmax (outPeak, buffer.getMagnitude (ch, 0, numSamples));

        const float prevOut = outputLevel.load (std::memory_order_relaxed);
        outputLevel.store (outPeak > prevOut
                               ? prevOut + (outPeak - prevOut) * rise
                               : prevOut * fall,
                           std::memory_order_relaxed);
    }
}

afterimage::SpectralMode AfterimageAudioProcessor::getCurrentMode() const noexcept
{
    const int index = juce::roundToInt (pMode->load());
    switch (index)
    {
        case 1:  return afterimage::SpectralMode::Erase;
        case 2:  return afterimage::SpectralMode::Merge;
        default: return afterimage::SpectralMode::Shadow;
    }
}

//==============================================================================
void AfterimageAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AfterimageAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            engine.clearHistory(); // never restore live spectral memory
            updateSmoothedTargets();
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* AfterimageAudioProcessor::createEditor()
{
    return new AfterimageAudioProcessorEditor (*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AfterimageAudioProcessor();
}
