// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "AmyPlugProcessor.h"
#include "AmyPlugEditor.h"
#include "engine/SoftwareBackend.h"
#include "engine/HardwareBackend.h"
#include "engine/EngineOwnership.h"
#include "engine/AmyWire.h"
#include "state/Dx7Import.h"
#include "state/Dx7Envelope.h"   // DX7 4R/4L EG -> AMY breakpoints (RT-safe C variant)
#include "state/Dx7Lfo.h"        // DX7 LFO -> AMY (speed/wave/PMS+PMD/AMD conversions)
#include "state/Dx7Osc.h"        // DX7 operator freq/level -> AMY (Coarse/Fine/Detune/Level)
#include "BuiltinPatchNames.h"   // kBuiltinPatchCommands / kBuiltinPatchCount (generated)
#include <cstdio>
#include <cstring>
#include <cmath>

namespace amyplug
{
AmyPlugProcessor::AmyPlugProcessor()
    : juce::AudioProcessor(BusesProperties()
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "AMYPLUG", params::createLayout())
{
    software = std::make_unique<SoftwareBackend>();
    hardware = std::make_unique<HardwareBackend>();
    active     = software.get();
    activeKind = IAmyBackend::Kind::Software;

    cacheParamPointers();

    // Structural params (need a full synth rebuild) are listened to; continuous
    // macros are polled on the audio thread (streamMacrosToBackend) instead.
    state.addParameterListener(params::id::mode,      this);
    state.addParameterListener(params::id::patchA,    this);
    state.addParameterListener(params::id::numVoices, this);
    // Structural changes rebuild the osc graph (a wave/filter-type restructure is
    // unsafe to stream live — it can race the engine-switch rebuild and hit an
    // oscillator that isn't set up yet). Continuous params stream live.
    for (auto* id : { params::id::engine, params::id::oscAWave, params::id::oscBWave,
                      params::id::oscCWave, params::id::oscDWave,
                      params::id::lfoWave, params::id::vcfType,
                      params::id::fmAlgorithm,    // FM: routing change → rebuild
                      params::id::fmLfoSpeed, params::id::fmLfoWave,   // FM LFO wiring → rebuild
                      params::id::fmLfoPmd,   params::id::fmLfoAmd, params::id::fmLfoPms,
                      params::id::voiceMode,      // Mono/Legato rebuild the synth to 1 voice
                      params::id::unisonVoices }) // unison count changes the osc count → rebuild
        state.addParameterListener(id, this);
    // FM operator freq MODE: ratio<->fixed swaps the wire field (I vs f), so rebuild.
    // AMS gates the LFO tremolo (an amp-coef restructure), also structural. Coarse/
    // Fine/Detune and Output Level are NOT structural — they stream live (ratio/Hz and
    // amp coef updates), so rapid automation/fuzz doesn't storm the rebuild path.
    for (int op = 1; op <= PatchModel::kFmOps; ++op)
    {
        state.addParameterListener(params::id::fmOp(op, "fixed"),  this);
        state.addParameterListener(params::id::fmOp(op, "ams"),    this);
    }
    // NOTE: unisonDetune is deliberately NOT structural — it only re-tunes existing
    // oscillators, which streamAnalogParams does live (no rebuild → no dropout).
}

void AmyPlugProcessor::cacheParamPointers()
{
    mCutoff.ptr  = state.getRawParameterValue(params::id::filterCutoff);
    mReso.ptr    = state.getRawParameterValue(params::id::filterReso);
    mVolume.ptr  = state.getRawParameterValue(params::id::masterVolume);
    mReverb.ptr  = state.getRawParameterValue(params::id::reverb);
    mChorus.ptr  = state.getRawParameterValue(params::id::chorus);
    mEcho.ptr    = state.getRawParameterValue(params::id::echo);
    mAttack.ptr  = state.getRawParameterValue(params::id::ampAttack);
    mDecay.ptr   = state.getRawParameterValue(params::id::ampDecay);
    mSustain.ptr = state.getRawParameterValue(params::id::ampSustain);
    mRelease.ptr = state.getRawParameterValue(params::id::ampRelease);
    pBendRange   = state.getRawParameterValue(params::id::pitchBendRange);
    pPatch       = state.getRawParameterValue(params::id::patchA);
    pEngine      = state.getRawParameterValue(params::id::engine);
    pVoiceMode   = state.getRawParameterValue(params::id::voiceMode);
    pMode        = state.getRawParameterValue(params::id::mode);
    pClipDrive   = state.getRawParameterValue(params::id::clipDrive);
    pBcFreq      = state.getRawParameterValue(params::id::bcFreq);
    pBcBits      = state.getRawParameterValue(params::id::bcBits);
    pOutputGain  = state.getRawParameterValue(params::id::outputGain);

    mVcfKbd.ptr    = state.getRawParameterValue(params::id::vcfKbd);
    mVcfEnv.ptr    = state.getRawParameterValue(params::id::vcfEnv);
    mLfoFreq.ptr   = state.getRawParameterValue(params::id::lfoFreq);
    mLfoPitch.ptr  = state.getRawParameterValue(params::id::lfoToPitch);
    mLfoPwm.ptr    = state.getRawParameterValue(params::id::lfoToPwm);
    mLfoFilter.ptr = state.getRawParameterValue(params::id::lfoToFilter);
    mOscADuty.ptr  = state.getRawParameterValue(params::id::oscADuty);
    mOscALevel.ptr = state.getRawParameterValue(params::id::oscALevel);
    mOscBDuty.ptr  = state.getRawParameterValue(params::id::oscBDuty);
    mOscBLevel.ptr = state.getRawParameterValue(params::id::oscBLevel);
    mOscAFreq.ptr  = state.getRawParameterValue(params::id::oscAFreq);
    mOscBFreq.ptr  = state.getRawParameterValue(params::id::oscBFreq);
    mOscACoarse.ptr = state.getRawParameterValue(params::id::oscACoarse);
    mOscAFine.ptr   = state.getRawParameterValue(params::id::oscAFine);
    mOscBCoarse.ptr = state.getRawParameterValue(params::id::oscBCoarse);
    mOscBFine.ptr   = state.getRawParameterValue(params::id::oscBFine);
    mOscCDuty.ptr  = state.getRawParameterValue(params::id::oscCDuty);
    mOscCLevel.ptr = state.getRawParameterValue(params::id::oscCLevel);
    mOscDDuty.ptr  = state.getRawParameterValue(params::id::oscDDuty);
    mOscDLevel.ptr = state.getRawParameterValue(params::id::oscDLevel);
    mOscCFreq.ptr  = state.getRawParameterValue(params::id::oscCFreq);
    mOscDFreq.ptr  = state.getRawParameterValue(params::id::oscDFreq);
    mOscCCoarse.ptr = state.getRawParameterValue(params::id::oscCCoarse);
    mOscCFine.ptr   = state.getRawParameterValue(params::id::oscCFine);
    mOscDCoarse.ptr = state.getRawParameterValue(params::id::oscDCoarse);
    mOscDFine.ptr   = state.getRawParameterValue(params::id::oscDFine);
    mGlide.ptr      = state.getRawParameterValue(params::id::glide);
    mUnisonDetune.ptr = state.getRawParameterValue(params::id::unisonDetune);
    pUnisonVoices     = state.getRawParameterValue(params::id::unisonVoices);
    mVcfA.ptr      = state.getRawParameterValue(params::id::vcfAttack);
    mVcfD.ptr      = state.getRawParameterValue(params::id::vcfDecay);
    mVcfS.ptr      = state.getRawParameterValue(params::id::vcfSustain);
    mVcfR.ptr      = state.getRawParameterValue(params::id::vcfRelease);
    mEqLow.ptr     = state.getRawParameterValue(params::id::eqLow);
    mEqMid.ptr     = state.getRawParameterValue(params::id::eqMid);
    mEqHigh.ptr    = state.getRawParameterValue(params::id::eqHigh);
    mReverbSize.ptr  = state.getRawParameterValue(params::id::reverbSize);
    mReverbDamp.ptr  = state.getRawParameterValue(params::id::reverbDamping);
    mChorusRate.ptr  = state.getRawParameterValue(params::id::chorusRate);
    mChorusDepth.ptr = state.getRawParameterValue(params::id::chorusDepth);
    mEchoTime.ptr    = state.getRawParameterValue(params::id::echoTime);
    mEchoFb.ptr      = state.getRawParameterValue(params::id::echoFeedback);
    mEchoTone.ptr    = state.getRawParameterValue(params::id::echoTone);

    pAlgorithm     = state.getRawParameterValue(params::id::fmAlgorithm);
    mFmFeedback.ptr = state.getRawParameterValue(params::id::fmFeedback);
    for (int e = 0; e < 4; ++e)
    {
        mFmPitchRate[e].ptr  = state.getRawParameterValue(params::id::fmPitchEg('r', e + 1));
        mFmPitchLevel[e].ptr = state.getRawParameterValue(params::id::fmPitchEg('l', e + 1));
    }
    for (int i = 0; i < PatchModel::kFmOps; ++i)
    {
        const int op = i + 1;
        pFmCoarse[i] = state.getRawParameterValue(params::id::fmOp(op, "coarse"));
        pFmFine[i]   = state.getRawParameterValue(params::id::fmOp(op, "fine"));
        pFmDetune[i] = state.getRawParameterValue(params::id::fmOp(op, "detune"));
        mFmOutLevel[i].ptr = state.getRawParameterValue(params::id::fmOp(op, "outlvl"));
        for (int e = 0; e < 4; ++e)
        {
            mFmEgRate[i][e].ptr  = state.getRawParameterValue(params::id::fmOp(op, ("r" + juce::String(e + 1)).toRawUTF8()));
            mFmEgLevel[i][e].ptr = state.getRawParameterValue(params::id::fmOp(op, ("l" + juce::String(e + 1)).toRawUTF8()));
        }
        pFmFixed[i]     = state.getRawParameterValue(params::id::fmOp(op, "fixed"));
        pFmAms[i]       = state.getRawParameterValue(params::id::fmOp(op, "ams"));
        pFmVel[i]       = state.getRawParameterValue(params::id::fmOp(op, "vel"));
    }
    pFmLfoSpeed = state.getRawParameterValue(params::id::fmLfoSpeed);
    pFmLfoWave  = state.getRawParameterValue(params::id::fmLfoWave);
    pFmLfoPmd   = state.getRawParameterValue(params::id::fmLfoPmd);
    pFmLfoAmd   = state.getRawParameterValue(params::id::fmLfoAmd);
    pFmLfoPms   = state.getRawParameterValue(params::id::fmLfoPms);
    pFmTranspose = state.getRawParameterValue(params::id::fmTranspose);
}

bool AmyPlugProcessor::engineIsAnalog() const
{
    return pEngine != nullptr
        && (int) std::lround(pEngine->load(std::memory_order_relaxed)) == 1;   // 1 = Analog
}

bool AmyPlugProcessor::engineIsFM() const
{
    return pEngine != nullptr
        && (int) std::lround(pEngine->load(std::memory_order_relaxed)) == 2;   // 2 = FM
}

AmyPlugProcessor::~AmyPlugProcessor()
{
    // Free the global engine token so a still-loaded silent instance can grab it.
    engineown::releaseSoftware(this);
}

void AmyPlugProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    lastSampleRate = sampleRate;
    lastBlockSize  = samplesPerBlock;
    hardware->prepare(sampleRate, samplesPerBlock);
    // In Hardware mode do NOT boot the software AMY engine: AMY is a single global
    // instance shared by every plugin instance, so a hardware instance holding it
    // would collide with a real Software instance (you'd hear its engine in parallel).
    if (activeKind == IAmyBackend::Kind::Software)
    {
        software->prepare(sampleRate, samplesPerBlock);
        engineown::claimSoftwareIfFree(this);   // grab the engine early if it's free
    }
    else
        software->release();
    router.prepare();
    if (pBcFreq) crush.setFreqHz(pBcFreq->load());
    if (pBcBits) crush.setBits(pBcBits->load());
    crush.prepare(sampleRate);
    if (pClipDrive) clip.setDriveDb(pClipDrive->load());
    clip.setOutputDb(0.0f);          // ceiling at 0 dBFS (also a safety limiter)
    clip.prepare(sampleRate);
    outGainCurrent = pOutputGain ? juce::Decibels::decibelsToGain(pOutputGain->load()) : 1.0f;
    // Bus-FX smoothing ~25 ms — stops fast Echo Time / EQ moves from crackling. It
    // does NOT stop the pop when a level hits exactly 0: AMY bypasses the effect on
    // the final ->0 step (no internal ramp), so the click is unavoidable from here.
    // See ROADMAP: replacing AMY's bus effects with host-side DSP fixes both that and
    // the echo's single-filter limitation.
    const double blockSec = (sampleRate > 0.0 ? (double) samplesPerBlock / sampleRate : 0.01);
    fxSmoothCoef = (float) juce::jlimit(0.02, 1.0, 1.0 - std::exp(-blockSec / 0.025));
    rebuildEngineFromModel();
}

void AmyPlugProcessor::releaseResources()
{
    software->release();
    hardware->release();
}

bool AmyPlugProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void AmyPlugProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Single-owner arbitration: AMY is one global engine. Only the owning instance
    // may render/stream. A non-owner (e.g. a duplicated track) stays fully silent and
    // touches nothing global — no notes, no wires, no render — so it can't corrupt the
    // owner or double-pull the sample clock. When the engine is free (owner gone) the
    // first block to run here claims it and rebuilds AMY from this instance's model.
    if (activeKind == IAmyBackend::Kind::Software)
    {
        const bool own = engineown::claimSoftwareIfFree(this);
        if (! own)
        {
            wasSoftwareOwner = false;
            buffer.clear();
            midi.clear();
            return;   // another instance owns the engine — silence
        }
        if (! wasSoftwareOwner)
        {
            // Just acquired it: AMY may hold the previous owner's state. Rebuild from
            // our model (async, off the audio thread) and emit one silent block.
            wasSoftwareOwner = true;
            triggerAsyncUpdate();
            buffer.clear();
            midi.clear();
            return;
        }
    }

    // Honour a pending Panic from the UI before anything else.
    if (panicRequested.exchange(false))
        router.allNotesOff(active);

    // A structural rebuild reset AMY's voices; flush the note tracker to match, so a
    // held note doesn't desync into a phantom (stuck) note across an engine/patch/
    // voice-mode switch. (allNotesOff also fully clears the mono/legato stacks.)
    // Skip the work when nothing is sounding: a param-fuzz storm rebuilds every block,
    // and allNotesOff sends an AMY reset wire each time — pure overhead when there are
    // no notes to flush. anyActive()==false guarantees nothing is tracked (a non-empty
    // mono held-stack always keeps its top note sounding), so this is safe.
    if (routerFlushRequested.exchange(false) && router.anyActive())
        router.allNotesOff(active);

    // Transport-aware hang prevention: stop -> flush.
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            router.updateTransport(pos->getIsPlaying(), *active);

    // Apply any queued structural rebuild BEFORE anything else touches AMY. The rebuild
    // begins with RESET_SYNTHS (it tears synth 1 down, then redefines it), so a note-on
    // routed before it lands would target a momentarily-undefined synth — AMY then
    // accesses unallocated voice state (out-of-bounds). Applying it first also means the
    // live param deltas below stream on top of the fresh graph, not a stale one.
    active->flushPending();

    // Keep the pitch-bend range current, then translate incoming MIDI into backend
    // note/CC events (deterministic offs) — now targeting the freshly-rebuilt synth.
    if (pBendRange != nullptr) router.setPitchBendRangeSemitones(pBendRange->load());
    if (pVoiceMode != nullptr) router.setVoiceMode((int) std::lround(pVoiceMode->load()));
    // DX7 Transpose = a keyboard transpose (FM only); shifts the AMY note, so fixed ops
    // stay put and ratio ops move. 0 for the other engines.
    router.setNoteTranspose(engineIsFM() && pFmTranspose ? (int) std::lround(pFmTranspose->load()) : 0);
    router.process(midi, *active);

    // Stream any changed automatable macros to AMY (RT-safe, no allocation).
    streamMacrosToBackend();

    // Render (Software) or emit silence (Hardware).
    active->processBlock(buffer);

    // Master output DSP — bitcrusher (retro grit) -> WDF diode saturator (analog
    // warmth). The saturator's Drive (THD) carries its own gain compensation, so
    // level stays steady. Software only (Hardware mode's buffer is silence; the
    // board colours its own output).
    if (activeKind == IAmyBackend::Kind::Software && buffer.getNumChannels() > 0)
    {
        if (pBcFreq)    crush.setFreqHz(pBcFreq->load(std::memory_order_relaxed));
        if (pBcBits)    crush.setBits(pBcBits->load(std::memory_order_relaxed));
        if (pClipDrive) clip.setDriveDb(pClipDrive->load(std::memory_order_relaxed));
        float* chans[2] = { buffer.getWritePointer(0),
                            buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr };
        crush.process(chans, buffer.getNumChannels(), buffer.getNumSamples());
        clip.process(chans, buffer.getNumChannels(), buffer.getNumSamples());

        // Final output gain — the true end of the chain. Ramped per block so
        // automation doesn't zipper.
        const float target = pOutputGain ? juce::Decibels::decibelsToGain(pOutputGain->load(std::memory_order_relaxed)) : 1.0f;
        buffer.applyGainRamp(0, buffer.getNumSamples(), outGainCurrent, target);
        outGainCurrent = target;
    }

    // Hardware mode: if routing through the host MIDI-out, merge queued messages.
    if (activeKind == IAmyBackend::Kind::Hardware)
        if (auto* hw = dynamic_cast<HardwareBackend*>(active))
            hw->collectHostMidi(midi, buffer.getNumSamples());
        else
            midi.clear();
    else
        midi.clear(); // don't pass notes through in Software mode
}

HardwareBackend* AmyPlugProcessor::hardwareBackend()
{
    return dynamic_cast<HardwareBackend*>(hardware.get());
}

void AmyPlugProcessor::requestPanic()
{
    panicRequested.store(true);   // handled next processBlock (Software path)
    // In Hardware mode processBlock may not be running (transport stopped), so also
    // flush the board directly from here (the button/message thread). enqueue is
    // thread-safe; the sender thread delivers the all-notes-off/all-sound-off.
    if (activeKind == IAmyBackend::Kind::Hardware)
        if (auto* hw = hardwareBackend()) hw->allNotesOff();
}

void AmyPlugProcessor::sendPatchToHardware()
{
    std::lock_guard<std::recursive_mutex> lk(modelMutex);
    syncModelFromParams();                              // capture live params
    if (auto* hw = hardwareBackend()) hw->rebuildFrom(model);
}

void AmyPlugProcessor::setMode(IAmyBackend::Kind mode)
{
    // MUST run on the message thread (it may amy_start/amy_stop, which allocate).
    // handleAsyncUpdate/prepareToPlay call it there. suspendProcessing() fences the
    // audio callback so we never release the software engine mid-render.
    if (mode == activeKind) return;
    router.allNotesOff(active);                 // never leave a hung note across modes
    suspendProcessing(true);
    if (mode == IAmyBackend::Kind::Hardware)
    {
        active = hardware.get();
        activeKind = mode;
        software->release();                    // drop the shared global AMY engine
        engineown::releaseSoftware(this);       // ...and free it for other instances
        wasSoftwareOwner = false;
    }
    else
    {
        software->prepare(lastSampleRate, lastBlockSize);   // re-acquire the engine
        engineown::claimSoftwareIfFree(this);               // grab the global token if free
        active = software.get();
        activeKind = mode;
    }
    suspendProcessing(false);
    rebuildEngineFromModel();                   // bring the new backend up to date
}

bool AmyPlugProcessor::ownsSoftwareEngine() const
{
    return engineown::ownsSoftware(this);
}

void AmyPlugProcessor::takeOverSoftwareEngine()
{
    // User pressed "Use Engine Here": seize the global engine from whoever holds it,
    // then rebuild AMY from this instance's model (the previous owner falls silent on
    // its next block). Message thread — the rebuild is dispatched async.
    if (activeKind != IAmyBackend::Kind::Software) return;
    engineown::forceClaimSoftware(this);
    triggerAsyncUpdate();
}

void AmyPlugProcessor::rebuildEngineFromModel()
{
    // Off audio-thread. Sync APVTS -> model, then project the model onto the backend.
    std::lock_guard<std::recursive_mutex> lk(modelMutex);
    syncModelFromParams();
    // The rebuild resets AMY's voices; ask the audio thread to flush the note tracker
    // to match (avoids phantom held notes across a switch).
    routerFlushRequested.store(true);
    // A non-owning Software instance renders silence and never drains its wire FIFO,
    // so pushing a rebuild here would only accumulate and eventually overflow the FIFO
    // (dropping messages) — corrupting the patch it finally takes over with. Skip it;
    // the take-over path does one clean rebuild once this instance owns the engine.
    if (activeKind == IAmyBackend::Kind::Software && ! engineown::ownsSoftware(this))
        return;
    if (active) active->rebuildFrom(model);
}

void AmyPlugProcessor::syncModelFromParams()
{
    std::lock_guard<std::recursive_mutex> lk(modelMutex);
    if (model.synths.empty()) model.synths.push_back({});
    auto& s = model.synths[0];                       // M2: single synth (channel 1)
    if (auto* p = state.getRawParameterValue(params::id::patchA))    s.patchNumber = (int) p->load();
    if (auto* p = state.getRawParameterValue(params::id::numVoices)) s.numVoices   = (int) p->load();
    // Mono/Legato are monophonic: rebuild the synth to a single voice so AMY steals
    // it on every new note (otherwise a held + new note would both sound).
    if (pVoiceMode && (int) std::lround(pVoiceMode->load()) != 0) s.numVoices = 1;
    if (mCutoff.ptr)  s.filterCutoff   = mCutoff.ptr->load();
    if (mReso.ptr)    s.filterReso     = mReso.ptr->load();
    if (mAttack.ptr)  s.ampAttack      = mAttack.ptr->load();
    if (mDecay.ptr)   s.ampDecay       = mDecay.ptr->load();
    if (mSustain.ptr) s.ampSustain     = mSustain.ptr->load();
    if (mRelease.ptr) s.ampRelease     = mRelease.ptr->load();
    if (mVolume.ptr)  model.masterVolume = mVolume.ptr->load();
    if (mReverb.ptr)  model.reverb     = mReverb.ptr->load();
    if (mChorus.ptr)  model.chorus     = mChorus.ptr->load();
    if (mEcho.ptr)    model.echo       = mEcho.ptr->load();

    // Engine + analog (Juno) params. filterCutoff/reso and the amp ADSR above are
    // reused as VCF freq/reso and the amp envelope, so mirror them into AnalogParams.
    s.engine = engineIsFM()     ? PatchModel::Engine::FM
             : engineIsAnalog() ? PatchModel::Engine::Analog
                                : PatchModel::Engine::FactoryPreset;
    auto& a = s.analog;
    auto rd = [this] (const char* id, float def) {
        if (auto* p = state.getRawParameterValue(id)) return p->load(); return def; };
    a.aWave = (int) rd(params::id::oscAWave, 3);  a.bWave = (int) rd(params::id::oscBWave, 1);
    a.cWave = (int) rd(params::id::oscCWave, 3);  a.dWave = (int) rd(params::id::oscDWave, 3);
    a.aFreq = rd(params::id::oscAFreq, 440.0f);  a.bFreq = rd(params::id::oscBFreq, 440.0f);
    a.cFreq = rd(params::id::oscCFreq, 440.0f);  a.dFreq = rd(params::id::oscDFreq, 440.0f);
    a.aCoarse = (int) rd(params::id::oscACoarse, 0); a.bCoarse = (int) rd(params::id::oscBCoarse, 0);
    a.cCoarse = (int) rd(params::id::oscCCoarse, 0); a.dCoarse = (int) rd(params::id::oscDCoarse, 0);
    a.aFine   = (int) rd(params::id::oscAFine, 0);   a.bFine   = (int) rd(params::id::oscBFine, 0);
    a.cFine   = (int) rd(params::id::oscCFine, 0);   a.dFine   = (int) rd(params::id::oscDFine, 0);
    a.vcfFreq = s.filterCutoff; a.vcfReso = s.filterReso;     // reused params
    a.aDuty = rd(params::id::oscADuty, 0.5f);  a.bDuty = rd(params::id::oscBDuty, 0.5f);
    a.cDuty = rd(params::id::oscCDuty, 0.5f);  a.dDuty = rd(params::id::oscDDuty, 0.5f);
    a.aLevel = rd(params::id::oscALevel, 0.7f); a.bLevel = rd(params::id::oscBLevel, 0.5f);
    a.cLevel = rd(params::id::oscCLevel, 0.0f); a.dLevel = rd(params::id::oscDLevel, 0.0f);
    a.lfoWave = (int) rd(params::id::lfoWave, 4); a.lfoFreq = rd(params::id::lfoFreq, 4.0f);
    a.lfoToPitch = rd(params::id::lfoToPitch, 0.0f); a.lfoToPwm = rd(params::id::lfoToPwm, 0.0f);
    a.lfoToFilter = rd(params::id::lfoToFilter, 0.0f);
    a.filterType = (int) rd(params::id::vcfType, 1);
    a.vcfKbd = rd(params::id::vcfKbd, 0.0f); a.vcfEnv = rd(params::id::vcfEnv, 0.0f);
    a.vcfA = rd(params::id::vcfAttack, 0.0f); a.vcfD = rd(params::id::vcfDecay, 0.6f);
    a.vcfS = rd(params::id::vcfSustain, 0.3f); a.vcfR = rd(params::id::vcfRelease, 0.4f);
    a.ampA = s.ampAttack; a.ampD = s.ampDecay; a.ampS = s.ampSustain; a.ampR = s.ampRelease;
    if (mEqLow.ptr)  model.eqLow  = mEqLow.ptr->load();
    if (mEqMid.ptr)  model.eqMid  = mEqMid.ptr->load();
    if (mEqHigh.ptr) model.eqHigh = mEqHigh.ptr->load();
    model.reverbSize    = rd(params::id::reverbSize,    0.85f);
    model.reverbDamping = rd(params::id::reverbDamping, 0.5f);
    model.chorusRate    = rd(params::id::chorusRate,    0.5f);
    model.chorusDepth   = rd(params::id::chorusDepth,   0.5f);
    model.echoTime      = rd(params::id::echoTime,      500.0f);
    model.echoFeedback  = rd(params::id::echoFeedback,  0.0f);
    model.echoTone      = rd(params::id::echoTone,      0.0f);
    model.clipDrive     = rd(params::id::clipDrive,     0.0f);
    model.bcFreq        = rd(params::id::bcFreq,        48000.0f);
    model.bcBits        = rd(params::id::bcBits,        16.0f);
    model.outputGain    = rd(params::id::outputGain,    0.0f);
    model.glide         = rd(params::id::glide,         0.0f);
    model.voiceMode     = (int) rd(params::id::voiceMode,    0.0f);
    model.unisonVoices  = (int) rd(params::id::unisonVoices, 1.0f);
    model.unisonDetune  = rd(params::id::unisonDetune,  12.0f);

    // FM (DX7) engine params. The master amp envelope reuses s.ampAttack..ampRelease.
    auto& fm = s.fm;
    if (pAlgorithm)      fm.algorithm = (int) std::lround(pAlgorithm->load()) + 1;   // choice index 0..31 -> algo 1..32
    if (mFmFeedback.ptr) fm.feedback  = mFmFeedback.ptr->load();
    for (int e = 0; e < 4; ++e)
    {
        if (mFmPitchRate[e].ptr)  fm.pitchEgRate[e]  = mFmPitchRate[e].ptr->load();
        if (mFmPitchLevel[e].ptr) fm.pitchEgLevel[e] = mFmPitchLevel[e].ptr->load();
    }
    if (pFmLfoSpeed) fm.lfoSpeed = pFmLfoSpeed->load();
    if (pFmLfoWave)  fm.lfoWave  = (int) std::lround(pFmLfoWave->load());   // choice index 0..5
    if (pFmLfoPmd)   fm.lfoPmd   = pFmLfoPmd->load();
    if (pFmLfoAmd)   fm.lfoAmd   = pFmLfoAmd->load();
    if (pFmLfoPms)   fm.lfoPms   = (int) std::lround(pFmLfoPms->load());
    if (pFmTranspose) fm.transpose = (int) std::lround(pFmTranspose->load());
    for (int i = 0; i < PatchModel::kFmOps; ++i)
    {
        auto& op = fm.ops[i];
        if (pFmCoarse[i])      op.coarse      = (int) std::lround(pFmCoarse[i]->load());
        if (pFmFine[i])        op.fine        = (int) std::lround(pFmFine[i]->load());
        if (pFmDetune[i])      op.detune      = (int) std::lround(pFmDetune[i]->load());
        if (mFmOutLevel[i].ptr) op.outputLevel = (int) std::lround(mFmOutLevel[i].ptr->load());
        for (int e = 0; e < 4; ++e)
        {
            if (mFmEgRate[i][e].ptr)  op.egRate[e]  = mFmEgRate[i][e].ptr->load();
            if (mFmEgLevel[i][e].ptr) op.egLevel[e] = mFmEgLevel[i][e].ptr->load();
        }
        if (pFmFixed[i])     op.fixedFreq = pFmFixed[i]->load() > 0.5f;
        if (pFmAms[i])       op.ampModSens = (int) std::lround(pFmAms[i]->load());  // choice 0..3
        if (pFmVel[i])       op.velSens    = (int) std::lround(pFmVel[i]->load());  // 0..7
    }
}

void AmyPlugProcessor::streamMacrosToBackend()
{
    if (active == nullptr) return;
    if (engineIsAnalog()) { streamAnalogParams(); return; }
    if (engineIsFM())     { streamFmParams();     return; }
    constexpr int ch = kMacroSynth;

    auto stream = [this] (Macro& m, auto build)
    {
        if (m.ptr == nullptr) return;
        const float v = m.ptr->load(std::memory_order_relaxed);
        if (v != m.last)
        {
            m.last = v;
            WireBuilder w; build(w, v);
            active->streamWire(w.str(), w.size());
        }
    };

    stream(mCutoff, [] (WireBuilder& w, float v) { w.synth(ch).filterFreq(v); });
    stream(mReso,   [] (WireBuilder& w, float v) { w.synth(ch).resonance(v); });
    streamGlobalFx();   // master volume + reverb/chorus/echo/EQ

    // Amp ADSR -> one bp0 breakpoint message, rebuilt if any of the four changed.
    if (mAttack.ptr && mDecay.ptr && mSustain.ptr && mRelease.ptr)
    {
        const float a = mAttack.ptr->load(std::memory_order_relaxed);
        const float d = mDecay.ptr->load(std::memory_order_relaxed);
        const float s = mSustain.ptr->load(std::memory_order_relaxed);
        const float r = mRelease.ptr->load(std::memory_order_relaxed);
        if (a != mAttack.last || d != mDecay.last || s != mSustain.last || r != mRelease.last)
        {
            mAttack.last = a; mDecay.last = d; mSustain.last = s; mRelease.last = r;
            // bp0 is the amp envelope only on Juno (0..127); on DX7 etc. it drives
            // the carrier's pitch, so don't override it there (record the change but
            // don't send). Mirrors PatchModel::ampEnvIsBp0.
            const int curPatch = pPatch ? (int) std::lround(pPatch->load(std::memory_order_relaxed)) : 0;
            if (curPatch >= 0 && curPatch <= 127)
            {
                char bp[48];
                std::snprintf(bp, sizeof bp, "%d,1,%d,%.3f,%d,0",
                              (int) std::lround(a * 1000.0f), (int) std::lround(d * 1000.0f),
                              (double) s, (int) std::lround(r * 1000.0f));
                WireBuilder w; w.synth(ch).bp0(bp);
                active->streamWire(w.str(), w.size());
            }
        }
    }
}

void AmyPlugProcessor::streamAnalogParams()
{
    // Audio thread, RT-safe (snprintf into stack buffers). Waves/filter-type/engine
    // are structural (rebuild); everything here streams osc-level coef updates that
    // AMY applies per-index without retriggering. Synth 1 (kMacroSynth) for M3b.
    auto one = [this] (Macro& m, const char* fmt)
    {
        if (m.ptr == nullptr) return;
        const float v = m.ptr->load(std::memory_order_relaxed);
        if (v != m.last) { m.last = v; char b[64]; std::snprintf(b, sizeof b, fmt, (double) v);
                           active->streamWire(b, (int) std::strlen(b)); }
    };

    // VCF (osc 0): freq=idx0, reso, kbd=idx1, env=idx4, lfo->filter=idx5.
    one(mCutoff,    "i1v0F%g");
    one(mReso,      "i1v0R%g");
    one(mVcfKbd,    "i1v0F,%g");
    one(mVcfEnv,    "i1v0F,,,,%g");
    one(mLfoFilter, "i1v0F,,,,,%g");
    // Unison stacks U detuned copies of the OSC A+B pair (emitAnalog): OSC A copies
    // live on oscs 2,4,6,…, OSC B on 3,5,7,…. EVERY per-osc edit must reach ALL
    // copies, or a changed level/env/tune only lands on copy 0 and the rest keep
    // their stale (rebuild-time) values — which reads as "the amp env doesn't apply
    // and the note hangs until a rebuild".
    const int   U   = pUnisonVoices ? juce::jlimit(1, 4, (int) std::lround(pUnisonVoices->load())) : 1;
    const float det = mUnisonDetune.ptr ? mUnisonDetune.ptr->load(std::memory_order_relaxed) : 0.0f;
    const bool  detuneChanged = mUnisonDetune.ptr && det != mUnisonDetune.last;
    auto offCents = [] (int k, int u, float d) { return u <= 1 ? 0.0f : -d + 2.0f * d * (float) k / (float) (u - 1); };
    // Four base oscs A/B/C/D per unison copy: osc = 2 + copy*4 + which (0=A..3=D).
    auto oscFor = [] (int which, int k) { return 2 + k * 4 + which; };

    // LFO osc1 freq, and its mod depth onto every copy's pitch (idx5) / PWM (idx5).
    one(mLfoFreq, "i1v1f%g,0");
    auto lfoDepth = [&] (Macro& m, char letter)   // letter 'f' (pitch) or 'd' (pwm)
    {
        if (m.ptr == nullptr) return;
        const float v = m.ptr->load(std::memory_order_relaxed);
        if (v == m.last) return;
        m.last = v;
        for (int k = 0; k < U; ++k) for (int w = 0; w < 4; ++w)
        { char b[48]; std::snprintf(b, sizeof b, "i1v%d%c,,,,,%g", oscFor(w, k), letter, (double) v);
          active->streamWire(b, (int) std::strlen(b)); }
    };
    lfoDepth(mLfoPitch, 'f');
    lfoDepth(mLfoPwm,   'd');

    // OSC A/B/C/D pitch: base Freq * 2^((coarse + fine/100)/12), then the per-copy
    // unison detune. Re-send all copies if Freq/Coarse/Fine OR the unison detune changed.
    auto pitch = [&] (Macro& f, Macro& c, Macro& n, int which)
    {
        if (! (f.ptr && c.ptr && n.ptr)) return;
        const float fv=f.ptr->load(), cv=c.ptr->load(), nv=n.ptr->load();
        if (! (fv!=f.last || cv!=c.last || nv!=n.last || detuneChanged)) return;
        f.last=fv; c.last=cv; n.last=nv;
        const float base = fv * std::pow(2.0f, (cv + nv / 100.0f) / 12.0f);
        for (int k = 0; k < U; ++k)
        {
            const float hz = base * std::pow(2.0f, offCents(k, U, det) / 1200.0f);
            char b[48]; std::snprintf(b, sizeof b, "i1v%df%g", oscFor(which, k), (double) hz);
            active->streamWire(b, (int) std::strlen(b));
        }
    };
    pitch(mOscAFreq, mOscACoarse, mOscAFine, 0);
    pitch(mOscBFreq, mOscBCoarse, mOscBFine, 1);
    pitch(mOscCFreq, mOscCCoarse, mOscCFine, 2);
    pitch(mOscDFreq, mOscDCoarse, mOscDFine, 3);
    if (mUnisonDetune.ptr) mUnisonDetune.last = det;   // consume after all pitch() calls

    // OSC duty / level to every copy. Level writes const AND eg0 (a<L>,0,0,<L>,0,0)
    // so the amp env keeps shaping it (must match emitAnalog).
    auto perCopy = [&] (Macro& m, int which, const char* fmt, bool twice)
    {
        if (m.ptr == nullptr) return;
        const float v = m.ptr->load(std::memory_order_relaxed);
        if (v == m.last) return;
        m.last = v;
        for (int k = 0; k < U; ++k)
        {
            char b[64];
            if (twice) std::snprintf(b, sizeof b, fmt, oscFor(which, k), (double) v, (double) v);
            else       std::snprintf(b, sizeof b, fmt, oscFor(which, k), (double) v);
            active->streamWire(b, (int) std::strlen(b));
        }
    };
    perCopy(mOscADuty,  0, "i1v%dd%g", false);
    perCopy(mOscBDuty,  1, "i1v%dd%g", false);
    perCopy(mOscCDuty,  2, "i1v%dd%g", false);
    perCopy(mOscDDuty,  3, "i1v%dd%g", false);
    perCopy(mOscALevel, 0, "i1v%da%g,0,0,%g,0,0", true);
    perCopy(mOscBLevel, 1, "i1v%da%g,0,0,%g,0,0", true);
    perCopy(mOscCLevel, 2, "i1v%da%g,0,0,%g,0,0", true);
    perCopy(mOscDLevel, 3, "i1v%da%g,0,0,%g,0,0", true);

    // Amp env (bp0 'A') to every audio osc; filter env (bp1 'B') to osc 0.
    auto env = [this] (std::initializer_list<int> oscs, char letter,
                       Macro& a, Macro& d, Macro& s, Macro& r)
    {
        if (! (a.ptr && d.ptr && s.ptr && r.ptr)) return;
        const float av=a.ptr->load(), dv=d.ptr->load(), sv=s.ptr->load(), rv=r.ptr->load();
        if (av!=a.last || dv!=d.last || sv!=s.last || rv!=r.last)
        {
            a.last=av; d.last=dv; s.last=sv; r.last=rv;
            for (int osc : oscs)
            {
                char b[80]; std::snprintf(b, sizeof b, "i1v%d%c%d,1,%d,%.3f,%d,0", osc, letter,
                    (int) std::lround(av*1000.0f), (int) std::lround(dv*1000.0f), (double) sv,
                    (int) std::lround(rv*1000.0f));
                active->streamWire(b, (int) std::strlen(b));
            }
        }
    };
    // Amp env -> every audio osc (2 .. 2U+1).
    if (mAttack.ptr && mDecay.ptr && mSustain.ptr && mRelease.ptr)
    {
        const float av=mAttack.ptr->load(), dv=mDecay.ptr->load(), sv=mSustain.ptr->load(), rv=mRelease.ptr->load();
        if (av!=mAttack.last || dv!=mDecay.last || sv!=mSustain.last || rv!=mRelease.last)
        {
            mAttack.last=av; mDecay.last=dv; mSustain.last=sv; mRelease.last=rv;
            for (int osc = 2; osc < 2 + 4 * U; ++osc)
            {
                char b[80]; std::snprintf(b, sizeof b, "i1v%dA%d,1,%d,%.3f,%d,0", osc,
                    (int) std::lround(av*1000.0f), (int) std::lround(dv*1000.0f), (double) sv,
                    (int) std::lround(rv*1000.0f));
                active->streamWire(b, (int) std::strlen(b));
            }
        }
    }
    env({ 0 }, 'B', mVcfA, mVcfD, mVcfS, mVcfR);      // filter env -> osc 0

    streamGlobalFx();   // master volume + reverb/chorus/echo/EQ
}

void AmyPlugProcessor::streamFmParams()
{
    // Audio thread, RT-safe. The FM (DX7) voice: osc 0 is the ALGO controller, osc 1
    // is the LFO, oscs 2..7 are operators. The algorithm (o) and the whole LFO wiring
    // are structural (rebuild); feedback, per-op ratio/level/envelope and the pitch EG
    // stream as coef updates.
    auto one = [this] (Macro& m, const char* fmt)
    {
        if (m.ptr == nullptr) return;
        const float v = m.ptr->load(std::memory_order_relaxed);
        if (v != m.last) { m.last = v; char b[64]; std::snprintf(b, sizeof b, fmt, (double) v);
                           active->streamWire(b, (int) std::strlen(b)); }
    };

    // Re-send an operator's amp envelope (bp0 'A') if any of its DX7 4R/4L values
    // changed. The full EG is rendered to AMY's exact breakpoint form (Dx7Envelope) —
    // the C variant is allocation-free, so this is RT-safe.
    auto env = [this] (int osc, int i)
    {
        float rate[4], level[4]; bool changed = false;
        for (int e = 0; e < 4; ++e)
        {
            rate[e]  = mFmEgRate[i][e].ptr  ? mFmEgRate[i][e].ptr->load(std::memory_order_relaxed)  : 99.0f;
            level[e] = mFmEgLevel[i][e].ptr ? mFmEgLevel[i][e].ptr->load(std::memory_order_relaxed) : 0.0f;
            if (mFmEgRate[i][e].ptr  && rate[e]  != mFmEgRate[i][e].last)  changed = true;
            if (mFmEgLevel[i][e].ptr && level[e] != mFmEgLevel[i][e].last) changed = true;
        }
        if (! changed) return;
        for (int e = 0; e < 4; ++e) { mFmEgRate[i][e].last = rate[e]; mFmEgLevel[i][e].last = level[e]; }
        char b[160]; int n = std::snprintf(b, sizeof b, "i1v%dA", osc);
        n += dx7env::egToBreakpointsC(b + n, (int) sizeof b - n, rate, level);
        active->streamWire(b, n);
    };

    // The LFO tremolo depth reaching operator i (amp mod-coef, index 5). The LFO is
    // structural, so this is constant between rebuilds; it must be re-sent whenever the
    // operator level streams, else the level update would wipe the amp-coef.
    const float lfoAmd = pFmLfoAmd ? pFmLfoAmd->load(std::memory_order_relaxed) : 0.0f;
    auto tremForOp = [this, lfoAmd] (int i) -> float
    {
        const bool on = pFmAms[i] && pFmAms[i]->load(std::memory_order_relaxed) > 0.5f;
        return on ? (float) dx7lfo::ampLfoAmp(lfoAmd) : 0.0f;
    };

    one(mFmFeedback, "i1v0b%g");                       // ALGO osc self-feedback

    // Global pitch EG on the ALGO osc (v0 bp0 'A') — re-send if any 4R/4L value changed.
    {
        float pr[4], pl[4]; bool ch = false;
        for (int e = 0; e < 4; ++e)
        {
            pr[e] = mFmPitchRate[e].ptr  ? mFmPitchRate[e].ptr->load(std::memory_order_relaxed)  : 99.0f;
            pl[e] = mFmPitchLevel[e].ptr ? mFmPitchLevel[e].ptr->load(std::memory_order_relaxed) : 50.0f;
            if (mFmPitchRate[e].ptr  && pr[e] != mFmPitchRate[e].last)  ch = true;
            if (mFmPitchLevel[e].ptr && pl[e] != mFmPitchLevel[e].last) ch = true;
        }
        if (ch)
        {
            for (int e = 0; e < 4; ++e) { mFmPitchRate[e].last = pr[e]; mFmPitchLevel[e].last = pl[e]; }
            char b[160]; int n = std::snprintf(b, sizeof b, "i1v0A");
            n += dx7env::pitchEgToBreakpointsC(b + n, (int) sizeof b - n, pr, pl);
            active->streamWire(b, n);
        }
    }

    for (int i = 0; i < PatchModel::kFmOps; ++i)
    {
        const int osc = i + 2;   // operators live on oscs 2..7 (osc 1 is the LFO)
        // Coarse/Fine/Detune -> the operator frequency: a ratio (I) or, in fixed mode,
        // an absolute Hz (f<hz>,0). Streamed as a coef update on change (mode toggle
        // itself is structural). Derived via the DX7 math so values stay in AMY's range.
        {
            const int coarse = pFmCoarse[i] ? (int) std::lround(pFmCoarse[i]->load(std::memory_order_relaxed)) : 1;
            const int fine   = pFmFine[i]   ? (int) std::lround(pFmFine[i]->load(std::memory_order_relaxed))   : 0;
            const int detune = pFmDetune[i] ? (int) std::lround(pFmDetune[i]->load(std::memory_order_relaxed)) : 7;
            const bool fixed = pFmFixed[i] && pFmFixed[i]->load(std::memory_order_relaxed) > 0.5f;
            const float f = fixed ? (float) juce::jlimit(0.001, 20000.0, dx7osc::coarseFineFixedHz(coarse, fine, detune))
                                  : (float) dx7osc::coarseFineRatio(coarse, fine, detune);
            if (f != mFmFreqLast[i])
            {
                mFmFreqLast[i] = f;
                char b[48];
                if (fixed) std::snprintf(b, sizeof b, "i1v%df%g,0", osc, (double) f);
                else       std::snprintf(b, sizeof b, "i1v%dI%g",   osc, (double) f);
                active->streamWire(b, (int) std::strlen(b));
            }
        }
        // Amp coefs [const,note,vel,eg0,eg1,mod]: const = Output Level -> amp, vel =
        // Key Velocity Sensitivity/7, eg0 = 1 (env carries depth), mod = LFO tremolo.
        // Re-emit the whole coef list when Output Level OR Vel Sens changes (both stream;
        // the tremolo term is preserved so a sweep doesn't wipe it). Matches emitFm.
        {
            const float lvl = mFmOutLevel[i].ptr ? mFmOutLevel[i].ptr->load(std::memory_order_relaxed) : 0.0f;
            const float vs  = pFmVel[i] ? pFmVel[i]->load(std::memory_order_relaxed) : 0.0f;
            if (mFmOutLevel[i].ptr && (lvl != mFmOutLevel[i].last || vs != mFmVelLast[i]))
            {
                mFmOutLevel[i].last = lvl; mFmVelLast[i] = vs;
                const double amp = dx7osc::outputLevelToAmp((int) std::lround(lvl));
                const double vel = dx7osc::velSensToCoef((int) std::lround(vs));
                char b[80]; std::snprintf(b, sizeof b, "i1v%da%g,0,%g,1,0,%g",
                                          osc, amp, vel, (double) tremForOp(i));
                active->streamWire(b, (int) std::strlen(b));
            }
        }
        env(osc, i);   // per-op DX7 4R/4L envelope
    }

    // No master amp env on the ALGO osc — the operator envelopes own the voice.
    streamGlobalFx();   // master volume + reverb/chorus/echo/EQ
}

void AmyPlugProcessor::streamGlobalFx()
{
    // Master volume + bus FX (reverb/chorus/echo) + 3-band EQ — global, shared by
    // every engine. Streamed RT-safe so the UI knobs are live in all modes.
    if (active == nullptr) return;
    auto one = [this] (Macro& m, const char* fmt)
    {
        if (m.ptr == nullptr) return;
        const float v = m.ptr->load(std::memory_order_relaxed);
        if (v != m.last) { m.last = v; char b[48]; std::snprintf(b, sizeof b, fmt, (double) v);
                           active->streamWire(b, (int) std::strlen(b)); }
    };
    one(mVolume, "V%g");
    // Glide (portamento) only bites in Mono/Legato: AMY glides a REUSED voice, so in
    // Poly (a fresh voice per note) it does nothing useful and can sweep each note in
    // from a stale pitch. Force 0 in Poly.
    if (mGlide.ptr != nullptr)
    {
        const bool mono = pVoiceMode && (int) std::lround(pVoiceMode->load(std::memory_order_relaxed)) != 0;
        const float g = mono ? mGlide.ptr->load(std::memory_order_relaxed) : 0.0f;
        if (g != mGlide.last)
        {
            mGlide.last = g;
            char b[32]; std::snprintf(b, sizeof b, "i1m%g", (double) g);
            active->streamWire(b, (int) std::strlen(b));
        }
    }

    // Each effect re-sends its full AMY parameter list when any of its params change.
    // Buffer-size params we don't expose are pinned to AMY's defaults.
    //
    // Each member is SMOOTHED toward its target (one-pole, ~25 ms) instead of
    // snapping: a fast Echo Time move re-lengths the delay line and a fast EQ move
    // recomputes the biquad coefficients, both of which crackle on an abrupt jump.
    // Gliding the streamed value spreads the change into small per-block steps. We
    // keep re-sending the whole list until every member has settled on its target.
    auto group = [this] (std::initializer_list<Macro*> ms, auto build)
    {
        bool moving = false;
        for (auto* m : ms)
        {
            if (m->ptr == nullptr) continue;
            const float target = m->ptr->load(std::memory_order_relaxed);
            if (std::isnan(m->last)) { m->last = target; moving = true; continue; } // first call: send once
            const float eps = 1.0e-5f * (1.0f + std::fabs(target));
            if (std::fabs(target - m->last) > eps)
            {
                m->last += (target - m->last) * fxSmoothCoef;
                if (std::fabs(target - m->last) <= eps) m->last = target;           // settle exactly
                moving = true;
            }
        }
        if (! moving) return;
        char b[80]; build(b);
        active->streamWire(b, (int) std::strlen(b));
    };
    group({ &mReverb, &mReverbSize, &mReverbDamp }, [this] (char* b) {
        std::snprintf(b, 80, "h%g,%g,%g,3000", (double) mReverb.last, (double) mReverbSize.last, (double) mReverbDamp.last); });
    group({ &mChorus, &mChorusRate, &mChorusDepth }, [this] (char* b) {
        std::snprintf(b, 80, "k%g,320,%g,%g", (double) mChorus.last, (double) mChorusRate.last, (double) mChorusDepth.last); });
    group({ &mEcho, &mEchoTime, &mEchoFb, &mEchoTone }, [this] (char* b) {
        std::snprintf(b, 80, "M%g,%g,743,%g,%g", (double) mEcho.last, (double) mEchoTime.last, (double) mEchoFb.last, (double) mEchoTone.last); });
    group({ &mEqLow, &mEqMid, &mEqHigh }, [this] (char* b) {
        std::snprintf(b, 80, "x%g,%g,%g", (double) mEqLow.last, (double) mEqMid.last, (double) mEqHigh.last); });
}

void AmyPlugProcessor::handleAsyncUpdate()
{
    // Message thread. Apply a pending Software/Hardware switch (setMode rebuilds), or
    // otherwise a structural change (patch/voices/engine).
    const auto wanted = (pMode != nullptr && (int) std::lround(pMode->load()) != 0)
                        ? IAmyBackend::Kind::Hardware : IAmyBackend::Kind::Software;
    if (wanted != activeKind) setMode(wanted);
    else                      rebuildEngineFromModel();
}

void AmyPlugProcessor::saveUserPatch(const juce::String& name)
{
    std::lock_guard<std::recursive_mutex> lk(modelMutex);
    syncModelFromParams();          // capture the live preset into the model
    patchLib.save(name, model);
}

bool AmyPlugProcessor::loadUserPatch(const juce::String& name)
{
    return loadUserPatch(juce::String(), name);
}

bool AmyPlugProcessor::loadUserPatch(const juce::String& group, const juce::String& name)
{
    PatchModel preset;
    if (! patchLib.load(group, name, preset))
        return false;
    applyPreset(preset);
    return true;
}

int AmyPlugProcessor::importDx7Cartridge(const juce::File& file)
{
    const auto voices = Dx7Import::parseFile(file);
    // Group the cartridge's voices under a folder named after the file, so they
    // don't clutter the user's own patch list.
    const juce::String group = file.getFileNameWithoutExtension();
    int imported = 0;
    juce::StringArray used;
    for (const auto& v : voices)
    {
        juce::String name = v.name.trim();
        if (name.isEmpty()) name = "DX7 Voice " + juce::String(imported + 1);
        // De-duplicate within the cartridge so each voice gets its own file.
        juce::String unique = name;
        for (int n = 2; used.contains(unique); ++n) unique = name + " " + juce::String(n);
        used.add(unique);

        patchLib.save(group, unique, Dx7Import::toPatchModel(v));
        ++imported;
    }
    return imported;
}

void AmyPlugProcessor::applyPreset(const PatchModel& preset)
{
    // Drive everything through the parameters so the host sees the change (UI,
    // automation, undo) and the existing rebuild/stream paths fire.
    auto setP = [this] (juce::StringRef id, float value)
    {
        if (auto* p = state.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(value));
    };

    const PatchModel::Synth s = preset.synths.empty() ? PatchModel::Synth {} : preset.synths.front();
    setP(params::id::patchA,       (float) s.patchNumber);
    setP(params::id::numVoices,    (float) s.numVoices);
    setP(params::id::filterCutoff, s.filterCutoff);
    setP(params::id::filterReso,   s.filterReso);
    setP(params::id::ampAttack,    s.ampAttack);
    setP(params::id::ampDecay,     s.ampDecay);
    setP(params::id::ampSustain,   s.ampSustain);
    setP(params::id::ampRelease,   s.ampRelease);
    setP(params::id::masterVolume, preset.masterVolume);
    setP(params::id::reverb,       preset.reverb);
    setP(params::id::chorus,       preset.chorus);
    setP(params::id::echo,         preset.echo);

    // Engine + analog (Juno) controls. Engine choice index: 0 Factory, 1 Analog, 2 FM.
    setP(params::id::engine, s.engine == PatchModel::Engine::FM     ? 2.0f
                           : s.engine == PatchModel::Engine::Analog ? 1.0f : 0.0f);
    const auto& a = s.analog;
    setP(params::id::oscAWave, (float) a.aWave);   setP(params::id::oscBWave, (float) a.bWave);
    setP(params::id::oscCWave, (float) a.cWave);   setP(params::id::oscDWave, (float) a.dWave);
    setP(params::id::oscAFreq, a.aFreq);           setP(params::id::oscBFreq, a.bFreq);
    setP(params::id::oscCFreq, a.cFreq);           setP(params::id::oscDFreq, a.dFreq);
    setP(params::id::oscACoarse, (float) a.aCoarse); setP(params::id::oscBCoarse, (float) a.bCoarse);
    setP(params::id::oscCCoarse, (float) a.cCoarse); setP(params::id::oscDCoarse, (float) a.dCoarse);
    setP(params::id::oscAFine, (float) a.aFine);     setP(params::id::oscBFine, (float) a.bFine);
    setP(params::id::oscCFine, (float) a.cFine);     setP(params::id::oscDFine, (float) a.dFine);
    setP(params::id::oscADuty, a.aDuty);           setP(params::id::oscBDuty, a.bDuty);
    setP(params::id::oscCDuty, a.cDuty);           setP(params::id::oscDDuty, a.dDuty);
    setP(params::id::oscALevel, a.aLevel);         setP(params::id::oscBLevel, a.bLevel);
    setP(params::id::oscCLevel, a.cLevel);         setP(params::id::oscDLevel, a.dLevel);
    setP(params::id::lfoWave, (float) a.lfoWave);  setP(params::id::lfoFreq, a.lfoFreq);
    setP(params::id::lfoToPitch, a.lfoToPitch);    setP(params::id::lfoToPwm, a.lfoToPwm);
    setP(params::id::lfoToFilter, a.lfoToFilter);
    setP(params::id::vcfType, (float) a.filterType);
    setP(params::id::vcfKbd, a.vcfKbd);            setP(params::id::vcfEnv, a.vcfEnv);
    setP(params::id::vcfAttack, a.vcfA);  setP(params::id::vcfDecay, a.vcfD);
    setP(params::id::vcfSustain, a.vcfS); setP(params::id::vcfRelease, a.vcfR);
    setP(params::id::eqLow, preset.eqLow); setP(params::id::eqMid, preset.eqMid);
    setP(params::id::eqHigh, preset.eqHigh);
    setP(params::id::reverbSize, preset.reverbSize);   setP(params::id::reverbDamping, preset.reverbDamping);
    setP(params::id::chorusRate, preset.chorusRate);   setP(params::id::chorusDepth, preset.chorusDepth);
    setP(params::id::echoTime, preset.echoTime);       setP(params::id::echoFeedback, preset.echoFeedback);
    setP(params::id::echoTone, preset.echoTone);
    setP(params::id::clipDrive, preset.clipDrive);
    setP(params::id::bcFreq, preset.bcFreq);   setP(params::id::bcBits, preset.bcBits);
    setP(params::id::outputGain, preset.outputGain);
    setP(params::id::glide, preset.glide);
    setP(params::id::voiceMode, (float) preset.voiceMode);
    setP(params::id::unisonVoices, (float) preset.unisonVoices);
    setP(params::id::unisonDetune, preset.unisonDetune);

    // FM (DX7) controls.
    const auto& fm = s.fm;
    setP(params::id::fmAlgorithm, (float) (fm.algorithm - 1));   // algo 1..32 -> choice index 0..31
    setP(params::id::fmFeedback,  fm.feedback);
    for (int e = 0; e < 4; ++e)
    {
        setP(params::id::fmPitchEg('r', e + 1), fm.pitchEgRate[e]);
        setP(params::id::fmPitchEg('l', e + 1), fm.pitchEgLevel[e]);
    }
    setP(params::id::fmLfoSpeed, fm.lfoSpeed);
    setP(params::id::fmLfoWave,  (float) fm.lfoWave);   // choice index 0..5
    setP(params::id::fmLfoPmd,   fm.lfoPmd);
    setP(params::id::fmLfoAmd,   fm.lfoAmd);
    setP(params::id::fmLfoPms,   (float) fm.lfoPms);
    setP(params::id::fmTranspose, (float) fm.transpose);
    for (int i = 0; i < PatchModel::kFmOps; ++i)
    {
        const int op = i + 1;
        const auto& o = fm.ops[i];
        setP(params::id::fmOp(op, "coarse"), (float) o.coarse);
        setP(params::id::fmOp(op, "fine"),   (float) o.fine);
        setP(params::id::fmOp(op, "detune"), (float) o.detune);
        setP(params::id::fmOp(op, "outlvl"), (float) o.outputLevel);
        for (int e = 0; e < 4; ++e)
        {
            setP(params::id::fmOp(op, ("r" + juce::String(e + 1)).toRawUTF8()), o.egRate[e]);
            setP(params::id::fmOp(op, ("l" + juce::String(e + 1)).toRawUTF8()), o.egLevel[e]);
        }
        setP(params::id::fmOp(op, "fixed"),   o.fixedFreq ? 1.0f : 0.0f);
        setP(params::id::fmOp(op, "ams"),     (float) o.ampModSens);   // choice index 0..3
        setP(params::id::fmOp(op, "vel"),     (float) o.velSens);      // 0..7
    }
}

bool AmyPlugProcessor::loadFactoryPatchIntoEditor(int patchNumber)
{
    // Decode a built-in preset's wire string into the editable engine and push it to
    // the knobs. DX7/ALGO patches -> the FM tab; Juno analog patches -> the Juno tab.
    // We keep the user's current FX/output settings and only replace the synth graph.
    if (patchNumber < 0 || patchNumber >= kBuiltinPatchCount) return false;
    const juce::String wire { kBuiltinPatchCommands[patchNumber] };

    std::lock_guard<std::recursive_mutex> lk(modelMutex);
    syncModelFromParams();                       // capture current state (FX, etc.)
    if (model.synths.empty()) model.synths.push_back({});
    auto& s = model.synths[0];

    PatchModel::FmParams fm;
    if (factoryFmWireToParams(wire, fm))
    {
        s.engine = PatchModel::Engine::FM;
        s.fm     = fm;
        applyPreset(model);                      // engine=FM + the operator knobs
        return true;
    }

    PatchModel::AnalogParams an;
    if (factoryAnalogWireToParams(wire, an))
    {
        s.engine = PatchModel::Engine::Analog;
        s.analog = an;
        // VCF freq/reso and the amp ADSR are stored as macros (reused params), so
        // mirror them for applyPreset to reach filterCutoff/filterReso + amp env.
        s.filterCutoff = an.vcfFreq; s.filterReso = an.vcfReso;
        s.ampAttack = an.ampA; s.ampDecay = an.ampD; s.ampSustain = an.ampS; s.ampRelease = an.ampR;
        applyPreset(model);                      // engine=Analog + the Juno knobs
        return true;
    }
    return false;                                // piano / amyboard / unknown structure
}

void AmyPlugProcessor::parameterChanged(const juce::String&, float)
{
    // Everything (incl. the Software/Hardware mode switch, which may amy_start/stop)
    // is applied on the message thread in handleAsyncUpdate — never inline here,
    // since this can fire from the audio thread under host automation.
    triggerAsyncUpdate();
}

void AmyPlugProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    // Make the model reflect the live parameter values, then persist BOTH the
    // APVTS and the PatchModel so recall is complete. Locked: the host may call this
    // concurrently with a message-thread rebuild (both touch `model`).
    std::lock_guard<std::recursive_mutex> lk(modelMutex);
    syncModelFromParams();
    juce::ValueTree root { "AMYplugState" };
    root.appendChild(state.copyState(), nullptr);
    root.appendChild(model.toValueTree(), nullptr);
    // Remember the AMYboard MIDI-out so the connection recalls with the session.
    if (auto* hw = hardwareBackend(); hw != nullptr && hw->isConnected())
        root.setProperty("hwDevice", hw->connectedName(), nullptr);
    if (auto xml = root.createXml()) copyXmlToBinary(*xml, dest);
}

void AmyPlugProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
    {
        std::lock_guard<std::recursive_mutex> lk(modelMutex);   // fromValueTree + rebuild touch `model`
        auto root = juce::ValueTree::fromXml(*xml);
        if (auto apvtsTree = root.getChildWithName(state.state.getType()); apvtsTree.isValid())
            state.replaceState(apvtsTree);
        if (auto patchTree = root.getChildWithName(PatchModel::kStateType); patchTree.isValid())
            model.fromValueTree(patchTree);
        rebuildEngineFromModel();   // recreate exact AMY state from the model

        // Reconnect the saved AMYboard device; the mode param (restored above) then
        // brings us back into Hardware mode via handleAsyncUpdate.
        const juce::String hwDevice = root.getProperty("hwDevice", juce::String()).toString();
        if (hwDevice.isNotEmpty())
            if (auto* hw = hardwareBackend()) hw->openOutput(hwDevice);
    }
}

juce::AudioProcessorEditor* AmyPlugProcessor::createEditor()
{
    return new AmyPlugEditor(*this);
}
} // namespace amyplug

// JUCE plugin entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new amyplug::AmyPlugProcessor();
}
