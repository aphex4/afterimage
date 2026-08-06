#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using namespace afterimage::constants;

juce::String percentText (float value01, int) { return juce::String (juce::roundToInt (value01 * 100.0f)) + " %"; }
juce::String secondsText (float seconds, int)
{
    return seconds < 1.0f ? juce::String (seconds, 2) + " s" : juce::String (seconds, 1) + " s";
}
juce::String dbText (float db, int) { return juce::String (db, 1) + " dB"; }
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
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idRecallPosition, 1 }, "Recall Position",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.45f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idInfluence, 1 }, "Influence",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.50f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idForget, 1 }, "Forget",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.35f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idBlur, 1 }, "Blur",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.15f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idTransientPreserve, 1 }, "Transient Preserve",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.50f,
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

    return { params.begin(), params.end() };
}

void AfterimageAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxChannels_ = juce::jmax (2, juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels()));
    maxChunk_ = afterimage::constants::maxInternalBlockSize;

    engine.prepare (sampleRate, maxChunk_, maxChannels_);
    smoothers.prepareSampleSmoothers (sampleRate);

    const int latency = engine.getLatencySamples();
    dryWetMixer.prepare (maxChannels_, maxChunk_, latency);
    setLatencySamples (latency);

    inputScratch.setSize (maxChannels_, maxChunk_, false, true, true);
    delayedDry.setSize (maxChannels_, maxChunk_, false, true, true);
    wetPtrs_.assign (static_cast<size_t> (maxChannels_), nullptr);
    dryInPtrs_.assign (static_cast<size_t> (maxChannels_), nullptr);
    delayedPtrs_.assign (static_cast<size_t> (maxChannels_), nullptr);

    updateParameterTargets();
    smoothers.mix.setCurrentAndTargetValue (smoothers.mix.getTargetValue());
    smoothers.outputGain.setCurrentAndTargetValue (smoothers.outputGain.getTargetValue());
    smoothers.bypassAmount.setCurrentAndTargetValue (smoothers.bypassAmount.getTargetValue());

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

    // Wet: identity STFT + history capture
    engine.process (wetChunk);

    // Latency-aligned dry
    dryWetMixer.processDryDelay (dryInChunk, delayedDryChunk);

    // Mix → bypass → output gain (final trim after bypass)
    for (int i = 0; i < numSamples; ++i)
    {
        const float mix = smoothers.mix.getNextValue();
        const float bypass = smoothers.bypassAmount.getNextValue();
        const float gain = smoothers.outputGain.getNextValue();

        const float dryGain = std::cos (mix * juce::MathConstants<float>::halfPi);
        const float wetGain = std::sin (mix * juce::MathConstants<float>::halfPi);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float dry = delayedDryChunk.getSample (ch, i);
            const float wet = wetChunk.getSample (ch, i);
            float mixed = dry * dryGain + wet * wetGain;
            mixed = mixed * (1.0f - bypass) + dry * bypass;
            mixed *= gain;
            wetChunk.setSample (ch, i, mixed);
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
            engine.requestClearHistory(); // audio thread clears — no race
            updateParameterTargets();
        }
    }
}

juce::AudioProcessorEditor* AfterimageAudioProcessor::createEditor()
{
    return new AfterimageAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AfterimageAudioProcessor();
}
