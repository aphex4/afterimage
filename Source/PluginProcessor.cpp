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
juce::String hzText (float hz, int)
{
    if (hz >= 1000.0f)
        return juce::String (hz / 1000.0f, 1) + " kHz";
    return juce::String (juce::roundToInt (hz)) + " Hz";
}
juce::String formantText (float v, int)
{
    if (v < 0.45f) return "Low";
    if (v > 0.55f) return "High";
    return "Center";
}

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
    auto* revType = apvts.getRawParameterValue (idReverbType);
    auto* revWet = apvts.getRawParameterValue (idReverbWet);
    auto* formant = apvts.getRawParameterValue (idFormant);
    auto* deEss = apvts.getRawParameterValue (idDeEsser);
    auto* tuneEn = apvts.getRawParameterValue (idTuneEnabled);
    auto* formEn = apvts.getRawParameterValue (idFormantEnabled);
    auto* deEn = apvts.getRawParameterValue (idDeEsserEnabled);
    auto* revEn = apvts.getRawParameterValue (idReverbEnabled);
    auto* eqEn = apvts.getRawParameterValue (idEqEnabled);
    if (mode == nullptr || mem == nullptr || recall == nullptr || infl == nullptr
        || forget == nullptr || blur == nullptr || trans == nullptr || freeze == nullptr
        || random == nullptr || outG == nullptr || mix == nullptr || gainMatch == nullptr
        || revType == nullptr || revWet == nullptr || formant == nullptr || deEss == nullptr
        || tuneEn == nullptr || formEn == nullptr || deEn == nullptr || revEn == nullptr
        || eqEn == nullptr)
        return false;

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
        && (gainMatch->load() < 0.5f)
        && juce::roundToInt (revType->load()) == pr.reverbType
        && near (revWet->load(), pr.reverbWet, 0.01f)
        && near (formant->load(), pr.formant, 0.01f)
        && near (deEss->load(), pr.deEsser, 0.01f)
        && ((tuneEn->load() > 0.5f) == pr.tuneEnabled)
        && ((formEn->load() > 0.5f) == pr.formantEnabled)
        && ((deEn->load() > 0.5f) == pr.deEsserEnabled)
        && ((revEn->load() > 0.5f) == pr.reverbEnabled)
        && (eqEn->load() < 0.5f);
}

void addEqBandParams (std::vector<std::unique_ptr<juce::RangedAudioParameter>>& params,
                      const char* freqId, const char* gainId, float defaultHz)
{
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { freqId, 1 }, juce::String (freqId),
        juce::NormalisableRange<float> (40.0f, 16000.0f, 0.1f, 0.35f), defaultHz,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (hzText)));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { gainId, 1 }, juce::String (gainId),
        juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));
}
} // namespace

AfterimageAudioProcessor::AfterimageAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AFTERIMAGE", createParameterLayout())
{
    auto bind = [this] (const juce::String& id) -> std::atomic<float>*
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
    pReverbType = bind (idReverbType);
    pReverbWet = bind (idReverbWet);
    pReverbSafeBass = bind (idReverbSafeBass);
    pFormant = bind (idFormant);
    pDeEsser = bind (idDeEsser);
    pTuneEnabled = bind (idTuneEnabled);
    pFormantEnabled = bind (idFormantEnabled);
    pDeEsserEnabled = bind (idDeEsserEnabled);
    pReverbEnabled = bind (idReverbEnabled);
    pEqEnabled = bind (idEqEnabled);
    pScaleRoot = bind (idScaleRoot);
    pScaleType = bind (idScaleType);
    pRetune = bind (idRetune);
    pHumanize = bind (idHumanize);
    pTuneAmount = bind (idTuneAmount);
    pHarmonicsEnabled = bind (idHarmonicsEnabled);
    pScaleColor = bind (idScaleColor);
    pScaleTransient = bind (idScaleTransient);

    for (int b = 0; b < eqBandsPerStage; ++b)
    {
        pPreEqFreq[static_cast<size_t> (b)] = bind (kPreEqFreqIds[b]);
        pPreEqGain[static_cast<size_t> (b)] = bind (kPreEqGainIds[b]);
        pPostEqFreq[static_cast<size_t> (b)] = bind (kPostEqFreqIds[b]);
        pPostEqGain[static_cast<size_t> (b)] = bind (kPostEqGainIds[b]);
    }

    for (int b = 0; b < parametricEqBands; ++b)
    {
        const auto n = juce::String (b + 1);
        pEqOn[static_cast<size_t> (b)] = bind ("eq" + n + "On");
        pEqType[static_cast<size_t> (b)] = bind ("eq" + n + "Type");
        pEqFreq[static_cast<size_t> (b)] = bind ("eq" + n + "Freq");
        pEqGain[static_cast<size_t> (b)] = bind ("eq" + n + "Gain");
        pEqQ[static_cast<size_t> (b)] = bind ("eq" + n + "Q");
        pEqX4[static_cast<size_t> (b)] = bind ("eq" + n + "X4");
        pEqSolo[static_cast<size_t> (b)] = bind ("eq" + n + "Solo");
    }

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
        juce::ParameterID { idMode, 1 }, "Mode", juce::StringArray { "Shadow", "Erase" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idMemoryLength, 1 }, "Memory Length",
        juce::NormalisableRange<float> (memoryLengthMinSec, memoryLengthMaxSec, 0.01f, 0.45f),
        memoryLengthDefaultSec,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (secondsText)));
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
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { idGainMatch, 1 }, "Gain Match", false));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { idReverbEnabled, 1 }, "Reverb On", false));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { idReverbType, 1 }, "Reverb Type",
        juce::StringArray { "Spring", "Hall", "Room" }, 1));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idReverbWet, 1 }, "Reverb Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { idReverbSafeBass, 1 }, "Safe Bass", false));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { idFormantEnabled, 1 }, "Formant On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idFormant, 1 }, "Formant",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (formantText)));
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { idDeEsserEnabled, 1 }, "De-Esser On", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idDeEsser, 1 }, "De-Esser",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));

    for (int b = 0; b < eqBandsPerStage; ++b)
    {
        addEqBandParams (params, kPreEqFreqIds[b], kPreEqGainIds[b], kDefaultEqFreqs[b]);
        addEqBandParams (params, kPostEqFreqIds[b], kPostEqGainIds[b], kDefaultEqFreqs[b]);
    }

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { idTuneEnabled, 1 }, "Tune On", false));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { idScaleRoot, 1 }, "Root",
        juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { idScaleType, 1 }, "Scale",
        juce::StringArray { "Major", "Nat. Minor", "Dorian", "Pent Major", "Pent Minor", "Chromatic" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idRetune, 1 }, "Retune",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.55f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idHumanize, 1 }, "Humanize",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.35f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idTuneAmount, 1 }, "Tune Amount",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));

    // Obsolete HARMONICS IDs (session compat; not on shipping DSP path)
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { idHarmonicsEnabled, 1 }, "Harmonics On (legacy)", false));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idScaleColor, 1 }, "Harmonics Color (legacy)",
        juce::NormalisableRange<float> (0.0f, 2.0f, 0.001f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { idScaleTransient, 1 }, "Harmonics Transient (legacy)",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.35f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentText)));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { idEqEnabled, 1 }, "EQ On", false));

    for (int b = 0; b < parametricEqBands; ++b)
    {
        const auto n = juce::String (b + 1);
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "eq" + n + "On", 1 }, "EQ" + n + " On", false));
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "eq" + n + "Type", 1 }, "EQ" + n + " Type",
            juce::StringArray { "Low Pass", "High Pass", "Low Shelf", "High Shelf", "Bell", "Notch" }, 4));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "eq" + n + "Freq", 1 }, "EQ" + n + " Freq",
            juce::NormalisableRange<float> (20.0f, 20000.0f, 0.1f, 0.35f),
            kDefaultParaEqFreqs[b],
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (hzText)));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "eq" + n + "Gain", 1 }, "EQ" + n + " Gain",
            juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (dbText)));
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "eq" + n + "Q", 1 }, "EQ" + n + " Q",
            juce::NormalisableRange<float> (0.1f, 20.0f, 0.01f, 0.4f), 0.7f));
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "eq" + n + "X4", 1 }, "EQ" + n + " Slope", false));
        params.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "eq" + n + "Solo", 1 }, "EQ" + n + " Solo", false));
    }

    return { params.begin(), params.end() };
}

void AfterimageAudioProcessor::resetAdaptiveProcessingState() noexcept
{
    gainMatch_.reset();
    tune_.reset();
    formant_.reset();
    deEsser_.reset();
    postReverb_.reset();
    parametricEq_.reset();
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
    tune_.prepare (sampleRate, maxChunk_, maxChannels_);
    formant_.prepare (sampleRate, maxChunk_, maxChannels_);
    deEsser_.prepare (sampleRate, maxChunk_, maxChannels_);
    postReverb_.prepare (sampleRate, maxChunk_, maxChannels_);
    parametricEq_.prepare (sampleRate, maxChunk_, maxChannels_);

    int latency = engine.getLatencySamples();
    if constexpr (afterimage::constants::auxDspEnabled)
        latency += afterimage::PitchTune::getLatencySamples();
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
    smoothers.gainMatchAmount.setCurrentAndTargetValue (smoothers.gainMatchAmount.getTargetValue());
    engine.snapSpectralSmoothersToTargets();
    resetAdaptiveProcessingState();

#if defined (AFTERIMAGE_ENABLE_LICENSING)
    entitlementDryAmount_.reset (sampleRate, 0.05);
    entitlementDryAmount_.setCurrentAndTargetValue (
        licenseManager_.getEntitlement()
            == afterimage::licensing::EntitlementState::DryPassThrough ? 1.0f : 0.0f);
#endif

    engine.requestClearHistory();
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

    // Stage 1 recovery: aux path is hard-off regardless of APVTS / session state.
    constexpr bool aux = afterimage::constants::auxDspEnabled;
    const bool revOn = aux && pReverbEnabled->load() > 0.5f;
    const bool formOn = aux && pFormantEnabled->load() > 0.5f;
    const bool deOn = aux && pDeEsserEnabled->load() > 0.5f;
    const bool tuneOn = aux && pTuneEnabled->load() > 0.5f
                        && (afterimage::constants::stageBypassMask & afterimage::constants::stageBypassTune) == 0;
    const bool eqOn = aux && pEqEnabled->load() > 0.5f;

    postReverb_.setEnabled (revOn);
    formant_.setEnabled (formOn);
    deEsser_.setEnabled (deOn);
    tune_.setEnabled (tuneOn);
    parametricEq_.setMasterEnabled (eqOn);

    const int revIdx = juce::roundToInt (pReverbType->load());
    postReverb_.setType (revIdx == 0 ? afterimage::ReverbType::Spring
                        : (revIdx == 2 ? afterimage::ReverbType::Room
                                       : afterimage::ReverbType::Hall));
    postReverb_.setWet (pReverbWet->load());
    postReverb_.setSafeBass (pReverbSafeBass->load() > 0.5f);
    formant_.setAmount (pFormant->load());
    deEsser_.setIntensity (pDeEsser->load());

    const int root = juce::roundToInt (pScaleRoot->load());
    const auto scaleType = static_cast<afterimage::ScaleType> (
        juce::jlimit (0, (int) afterimage::ScaleType::NumTypes - 1, juce::roundToInt (pScaleType->load())));
    const auto midiMask = midiScaleMask_.load (std::memory_order_relaxed);
    tune_.setParams (root, scaleType, pRetune->load(), pHumanize->load(), pTuneAmount->load(), midiMask);
    juce::ignoreUnused (pHarmonicsEnabled, pScaleColor, pScaleTransient, pPreEqFreq, pPreEqGain, pPostEqFreq, pPostEqGain);

    int soloCount = 0;
    int firstSolo = -1;
    for (int b = 0; b < parametricEqBands; ++b)
    {
        afterimage::EqBandParams bp;
        bp.enabled = pEqOn[static_cast<size_t> (b)]->load() > 0.5f;
        bp.type = static_cast<afterimage::EqFilterType> (
            juce::jlimit (0, (int) afterimage::EqFilterType::NumTypes - 1,
                          juce::roundToInt (pEqType[static_cast<size_t> (b)]->load())));
        bp.freqHz = pEqFreq[static_cast<size_t> (b)]->load();
        bp.gainDb = pEqGain[static_cast<size_t> (b)]->load();
        bp.q = pEqQ[static_cast<size_t> (b)]->load();
        bp.x4 = pEqX4[static_cast<size_t> (b)]->load() > 0.5f;
        const bool wantSolo = pEqSolo[static_cast<size_t> (b)]->load() > 0.5f;
        if (wantSolo)
        {
            if (firstSolo < 0)
                firstSolo = b;
            ++soloCount;
        }
        bp.solo = wantSolo && b == firstSolo;
        parametricEq_.setBand (b, bp);
    }

    // Enforce exclusive solo in APVTS when multiple are true (UI may race).
    if (soloCount > 1 && firstSolo >= 0)
    {
        for (int b = 0; b < parametricEqBands; ++b)
        {
            if (b == firstSolo)
                continue;
            if (auto* p = apvts.getParameter ("eq" + juce::String (b + 1) + "Solo"))
                if (p->getValue() > 0.5f)
                    p->setValueNotifyingHost (0.0f);
        }
    }
}

void AfterimageAudioProcessor::processChunk (juce::AudioBuffer<float>& wetChunk,
                                             juce::AudioBuffer<float>& dryInChunk,
                                             juce::AudioBuffer<float>& delayedDryChunk) noexcept
{
    const int numSamples = wetChunk.getNumSamples();
    const int numChannels = wetChunk.getNumChannels();
    auto& viz = engine.getVisualization();
    constexpr float rise = 0.6f, fall = 0.92f;
    constexpr int bypass = afterimage::constants::stageBypassMask;

#if defined (AFTERIMAGE_ENABLE_LICENSING)
    const bool dryOnly = licenseManager_.getEntitlement()
                         == afterimage::licensing::EntitlementState::DryPassThrough;
    entitlementDryAmount_.setTargetValue (dryOnly ? 1.0f : 0.0f);
#endif

    if ((bypass & afterimage::constants::stageBypassSpectral) == 0)
        engine.process (wetChunk);
    // else: leave wet as input copy (identity STFT bypass for regression)

#if AFTERIMAGE_ENABLE_AUX_DSP
    // TUNE always runs for fixed latency when aux is enabled (OFF = pure delay).
    tune_.process (wetChunk);
#endif

    dryWetMixer.processDryDelay (dryInChunk, delayedDryChunk);

    const bool forceDryMix = (bypass & afterimage::constants::stageBypassMixDry) != 0;
    for (int i = 0; i < numSamples; ++i)
    {
        const float mixRaw = smoothers.mix.getNextValue();
        const float mix = forceDryMix ? 0.0f : mixRaw;
        const float dryGain = std::cos (mix * juce::MathConstants<float>::halfPi);
        const float wetGain = std::sin (mix * juce::MathConstants<float>::halfPi);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float dry = delayedDryChunk.getSample (ch, i);
            const float wet = wetChunk.getSample (ch, i);
            wetChunk.setSample (ch, i, dry * dryGain + wet * wetGain);
        }
    }

#if AFTERIMAGE_ENABLE_AUX_DSP
    if ((bypass & afterimage::constants::stageBypassFx) == 0)
    {
        formant_.process (wetChunk);
        deEsser_.process (wetChunk);
        postReverb_.process (wetChunk);
    }

    // Always-live pre-EQ spectrum (works when EQ off / no bands).
    parametricEq_.pushSpectrum (wetChunk);

    if ((bypass & afterimage::constants::stageBypassEq) == 0)
        parametricEq_.process (wetChunk);
#else
    // Spectrum probe still useful for UI even when EQ DSP is disabled.
    parametricEq_.pushSpectrum (wetChunk);
    juce::ignoreUnused (afterimage::constants::stageBypassFx,
                        afterimage::constants::stageBypassEq);
#endif

    const bool bypassMatch = (bypass & afterimage::constants::stageBypassGainMatch) != 0;

    for (int i = 0; i < numSamples; ++i)
    {
        const float bypassAmt = smoothers.bypassAmount.getNextValue();
        const float gain = smoothers.outputGain.getNextValue();
        const float matchRaw = smoothers.gainMatchAmount.getNextValue();
        const float matchAmt = bypassMatch ? 0.0f : matchRaw;
#if defined (AFTERIMAGE_ENABLE_LICENSING)
        const float entitlementDry = entitlementDryAmount_.getNextValue();
#else
        const float entitlementDry = 0.0f;
#endif

        float dryPower = 0.0f;
        float mixedPower = 0.0f;
        const int chLimit = juce::jmin (numChannels, 8);

        for (int ch = 0; ch < chLimit; ++ch)
        {
            const float dry = delayedDryChunk.getSample (ch, i);
            const float mixed = wetChunk.getSample (ch, i);
            dryPower += dry * dry;
            mixedPower += mixed * mixed;
        }

        const float invCh = 1.0f / (float) juce::jmax (1, chLimit);
        dryPower *= invCh;
        mixedPower *= invCh;

        const float gmScalar = gainMatch_.advanceAndGetScalar (dryPower, mixedPower);
        const float scalar = 1.0f + matchAmt * (gmScalar - 1.0f);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float dry = delayedDryChunk.getSample (ch, i);
            float out = wetChunk.getSample (ch, i) * scalar;
            out = out * (1.0f - bypassAmt) + dry * bypassAmt;
            out = out * (1.0f - entitlementDry) + dry * entitlementDry;
            out *= gain;
            wetChunk.setSample (ch, i, out);
        }
    }

    float inPeak = 0.0f;
    float outPeak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        inPeak = juce::jmax (inPeak, delayedDryChunk.getMagnitude (ch, 0, numSamples));
        outPeak = juce::jmax (outPeak, wetChunk.getMagnitude (ch, 0, numSamples));
    }

    const float prevIn = viz.loadInputPeak();
    const float prevOut = viz.loadOutputPeak();
    viz.storeInputPeak (inPeak > prevIn ? prevIn + (inPeak - prevIn) * rise : prevIn * fall);
    viz.storeOutputPeak (outPeak > prevOut ? prevOut + (outPeak - prevOut) * rise : prevOut * fall);
}

void AfterimageAudioProcessor::handleMidi (const juce::MidiBuffer& midi) noexcept
{
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn())
            midiNoteHeld_[static_cast<size_t> (juce::jlimit (0, 127, msg.getNoteNumber()))] = true;
        else if (msg.isNoteOff())
            midiNoteHeld_[static_cast<size_t> (juce::jlimit (0, 127, msg.getNoteNumber()))] = false;
    }

    std::uint16_t mask = 0;
    int held = 0;
    for (int n = 0; n < 128; ++n)
    {
        if (! midiNoteHeld_[static_cast<size_t> (n)])
            continue;
        mask |= (std::uint16_t) (1u << (n % 12));
        ++held;
    }
    midiScaleMask_.store (held > 0 ? mask : 0, std::memory_order_relaxed);
}

void AfterimageAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    handleMidi (midi);

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
    return afterimage::productModeFromChoiceIndex (juce::roundToInt (pMode->load()));
}

int AfterimageAudioProcessor::getNumPrograms()
{
    return afterimage::factory::kNumPresets;
}

int AfterimageAudioProcessor::getCurrentProgram()
{
    return juce::jlimit (0, afterimage::factory::kNumPresets - 1, currentProgram_);
}

void AfterimageAudioProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= afterimage::factory::kNumPresets)
        return;

    currentProgram_ = index;
    customProgram_ = false;
    afterimage::factory::applyPreset (apvts, index);
    engine.requestClearHistory();
    resetAdaptiveProcessingState();
    updateParameterTargets();
    engine.snapSpectralSmoothersToTargets();
}

const juce::String AfterimageAudioProcessor::getProgramName (int index)
{
    if (index < 0 || index >= afterimage::factory::kNumPresets)
        return {};

    if (customProgram_ && index == getCurrentProgram())
        return "Custom";

    return afterimage::factory::kPresets[static_cast<std::size_t> (index)].name;
}

void AfterimageAudioProcessor::changeProgramName (int, const juce::String&)
{
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

    customProgram_ = true;
    if (currentProgram_ < 0 || currentProgram_ >= afterimage::factory::kNumPresets)
        currentProgram_ = 0;
}

void AfterimageAudioProcessor::migrateLegacyParameterTree (juce::ValueTree& tree)
{
    float legacyPitchPath = -1.0f;

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto child = tree.getChild (i);
        if (! child.hasType ("PARAM"))
            continue;

        const auto id = child.getProperty ("id").toString();
        if (id == idMode)
        {
            const float v = (float) child.getProperty ("value");
            if (v > 0.75f)
                child.setProperty ("value", 0.0f, nullptr); // Merge → Shadow
            else if (v > 0.25f)
                child.setProperty ("value", 1.0f, nullptr); // Erase mid → Erase
        }
        else if (id == idPitchPathLegacy)
        {
            legacyPitchPath = (float) child.getProperty ("value");
        }
    }

    // Legacy pitchPath / HARMONICS do not auto-enable TUNE (different product).
    juce::ignoreUnused (legacyPitchPath);

    // Ensure tuneEnabled exists for older sessions.
    bool hasTune = false;
    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        auto child = tree.getChild (i);
        if (child.hasType ("PARAM") && child.getProperty ("id").toString() == idTuneEnabled)
        {
            hasTune = true;
            break;
        }
    }
    if (! hasTune)
    {
        juce::ValueTree p ("PARAM");
        p.setProperty ("id", idTuneEnabled, nullptr);
        p.setProperty ("value", 0.0f, nullptr);
        tree.addChild (p, -1, nullptr);
    }
}

void AfterimageAudioProcessor::applyRestoredParameterTree (const juce::ValueTree& tree)
{
    auto mutableTree = tree;
    migrateLegacyParameterTree (mutableTree);
    apvts.replaceState (mutableTree);
    engine.requestClearHistory();
    resetAdaptiveProcessingState();
    updateParameterTargets();
    engine.snapSpectralSmoothersToTargets();
    syncProgramIndexFromParameters();
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
