// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
//
// The loaded-preset identity and its dirty mark, at the processor. These guard the bug
// that started it: a session reload brought the SOUND back and left the browser field
// saying nothing, because nothing recorded which preset had been loaded.
//
// Needs the real processor (a user load moves every parameter through applyPreset), so
// it lives in the full-plugin test target beside EditorSizeTests.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "AmyPlugProcessor.h"
#include "AmyPlugEditor.h"
#include "state/Parameters.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

using namespace amyplug;

namespace
{
juce::File tempLibraryDir()
{
    auto d = juce::File::getSpecialLocation(juce::File::tempDirectory)
                 .getChildFile("amyplug_preset_" + juce::String(juce::Time::getHighResolutionTicks()));
    d.createDirectory();
    return d;
}

void set(AmyPlugProcessor& p, const char* id, float v)
{
    auto* param = p.apvts().getParameter(id);
    REQUIRE(param != nullptr);
    param->setValueNotifyingHost(param->convertTo0to1(v));
}
} // namespace

TEST_CASE("loaded preset identity and dirty mark", "[preset]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto dir = tempLibraryDir();

    // --- a fresh instance is "on" the factory patch the parameters default to ----------
    {
        AmyPlugProcessor p;
        const auto ref = p.loadedPreset();
        CHECK(ref.isFactory());
        CHECK(ref.factoryIndex == 0);
        CHECK(ref.bank == "Juno");
        CHECK_FALSE(p.isPresetDirty());
    }

    // --- turning a knob makes it dirty; picking a factory patch makes it clean again ----
    {
        AmyPlugProcessor p;
        set(p, params::id::filterCutoff, 1234.0f);
        CHECK(p.isPresetDirty());

        set(p, params::id::patchA, 130.0f);           // a DX7 factory patch
        CHECK(p.loadedPreset().isFactory());
        CHECK(p.loadedPreset().factoryIndex == 130);
        CHECK(p.loadedPreset().bank == "DX7");
        CHECK_FALSE(p.isPresetDirty());               // selecting a patch IS a load
    }

    // --- an FX switch IS part of the preset: full inclusion, it dirties -----------------
    {
        AmyPlugProcessor p;
        set(p, params::id::reverbOn, 0.0f);
        CHECK(p.isPresetDirty());
    }

    // --- parameters no preset owns must not dirty it ---------------------------------
    {
        AmyPlugProcessor p;
        set(p, params::id::pitchBendRange, 12.0f);
        CHECK_FALSE(p.isPresetDirty());
        // (mode is exercised in the host: switching it also opens a serial port here.)
    }

    // --- save -> on that user preset, clean; edit -> dirty; load -> clean -------------
    {
        AmyPlugProcessor p;
        p.patchLibrary().setDirectory(dir);
        set(p, params::id::filterCutoff, 2222.0f);
        p.saveUserPatch("Test Bank", "My Lead");

        auto ref = p.loadedPreset();
        CHECK(ref.isUser());
        CHECK(ref.bank == "Test Bank");
        CHECK(ref.name == "My Lead");
        CHECK_FALSE(p.isPresetDirty());

        set(p, params::id::filterReso, 0.9f);
        CHECK(p.isPresetDirty());

        // Loading it back moves every parameter (which trips the mark) and must end CLEAN.
        REQUIRE(p.loadUserPatch("Test Bank", "My Lead"));
        CHECK(p.loadedPreset() == ref);
        CHECK_FALSE(p.isPresetDirty());
    }

    // --- the identity and the mark survive a session round-trip ----------------------
    {
        juce::MemoryBlock blob;
        {
            AmyPlugProcessor p;
            p.patchLibrary().setDirectory(dir);
            REQUIRE(p.loadUserPatch("Test Bank", "My Lead"));
            set(p, params::id::filterReso, 0.55f);        // dirty on purpose
            REQUIRE(p.isPresetDirty());
            p.getStateInformation(blob);
        }

        // Remove the file before restoring: the name must come back WITHOUT the preset
        // being re-loaded from disk (JUCE-UI-LnF__14 §2/§8 — an orphan shows its name).
        for (const auto& f : dir.findChildFiles(juce::File::findFilesAndDirectories, true))
            f.deleteRecursively();

        AmyPlugProcessor q;
        q.patchLibrary().setDirectory(dir);
        q.setStateInformation(blob.getData(), (int) blob.getSize());

        const auto ref = q.loadedPreset();
        CHECK(ref.isUser());
        CHECK(ref.bank == "Test Bank");
        CHECK(ref.name == "My Lead");
        CHECK(q.isPresetDirty());                          // the mark rides too
        // ...and the SOUND is the session's own values, not a reload.
        auto* reso = q.apvts().getRawParameterValue(params::id::filterReso);
        REQUIRE(reso != nullptr);
        CHECK(reso->load() == 0.55f);
    }

    // --- a session written before identity existed falls back to patchA --------------
    {
        juce::MemoryBlock blob;
        {
            AmyPlugProcessor p;
            set(p, params::id::patchA, 200.0f);
            p.getStateInformation(blob);
        }
        // Strip the identity properties to simulate the old format.
        auto xml = juce::XmlDocument::parse(juce::String::fromUTF8((const char*) blob.getData(), (int) blob.getSize()));
        // getStateInformation writes binary-wrapped XML; go through the same helper the
        // processor uses to unwrap it.
        AmyPlugProcessor q;
        auto tree = juce::ValueTree::fromXml(*juce::AudioProcessor::getXmlFromBinary(blob.getData(), (int) blob.getSize()));
        for (auto* prop : { "presetSource", "presetFactory", "presetBank", "presetName", "presetDirty" })
            tree.removeProperty(prop, nullptr);
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary(*tree.createXml(), old);
        q.setStateInformation(old.getData(), (int) old.getSize());
        CHECK(q.loadedPreset().isFactory());
        CHECK(q.loadedPreset().factoryIndex == 200);
        CHECK_FALSE(q.isPresetDirty());
    }

    // --- opening the editor must not touch the sound, so it must not dirty the preset ---
    // Every attachment sends an initial UI<-parameter update; none may send one the other
    // way. If this fails, the list below names the parameter that moved, and therefore the
    // control that wrote it.
    {
        AmyPlugProcessor p;
        p.prepareToPlay(48000.0, 512);
        REQUIRE_FALSE(p.isPresetDirty());
        {
            AmyPlugEditor editor(p);
            if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
                mm->runDispatchLoopUntil(200);
            juce::StringArray moved;
            for (auto* param : p.getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(param))
                    if (std::abs(r->getValue() - r->getDefaultValue()) > 1.0e-6f)
                        moved.add(r->paramID + " = " + juce::String(r->getValue()) + " (default " + juce::String(r->getDefaultValue()) + ")");
            INFO("parameters moved by constructing the editor:\n" << moved.joinIntoString("\n"));
            CHECK_FALSE(p.isPresetDirty());
            CHECK(moved.isEmpty());
        }
    }

    dir.deleteRecursively();
}

// ---------------------------------------------------------------------------
// Selecting a factory patch adopts the patch's OWN settings.
// ---------------------------------------------------------------------------
TEST_CASE("a factory preset brings its own filter, envelope and chorus", "[preset]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto pump = [] { if (auto* mm = juce::MessageManager::getInstanceWithoutCreating()) mm->runDispatchLoopUntil(250); };

    amyplug::AmyPlugProcessor proc;
    proc.prepareToPlay(48000.0, 512);
    auto set = [&] (const char* id, float v)
    { if (auto* p = proc.apvts().getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v)); };
    auto val = [&] (const char* id)
    { auto* r = proc.apvts().getRawParameterValue(id); return r ? r->load() : -1.0f; };

    // ⚠️ toWireMessages broadcasts filterFreq, resonance and bp0 ON TOP of the baked patch,
    // so without adoption you heard every Juno preset through cutoff 8000, resonance 0.7 and
    // a 5 ms attack, whatever the patch said — and "To Editor", which decodes the real
    // values, then sounded like a different instrument. Measured: patch 20's own attack is
    // 582 ms and its own cutoff is 299 Hz.
    set(amyplug::params::id::patchA, 20.0f);
    pump();
    CHECK(val(amyplug::params::id::filterCutoff) == Catch::Approx(298.86f).margin(0.5));
    CHECK(val(amyplug::params::id::ampAttack)    == Catch::Approx(0.582f).margin(0.01));
    CHECK(val(amyplug::params::id::chorus)       == Catch::Approx(1.0f).margin(0.01));
    CHECK(val(amyplug::params::id::eqLow)        == Catch::Approx(7.0f).margin(0.1));
    // Adopting a preset's own settings IS the load, not an edit.
    CHECK_FALSE(proc.isPresetDirty());

    // A DX7 patch has no analog structure, so only its effects are adopted — its bp0 is the
    // PITCH envelope and must not be read as an amp ADSR.
    set(amyplug::params::id::patchA, 224.0f);
    pump();
    CHECK(val(amyplug::params::id::ampAttack) == Catch::Approx(0.582f).margin(0.01));   // unchanged
}

TEST_CASE("reopening a session does not re-adopt over what it saved", "[preset]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto pump = [] { if (auto* mm = juce::MessageManager::getInstanceWithoutCreating()) mm->runDispatchLoopUntil(250); };

    // ⚠️ The trap: adoption keys off the patch NUMBER, so a restored session looks like a
    // fresh selection unless the guard is seeded from the restored state — and it would then
    // stamp the factory values over the ones the user tuned and saved. Silent recall loss.
    juce::MemoryBlock saved;
    {
        amyplug::AmyPlugProcessor proc;
        proc.prepareToPlay(48000.0, 512);
        auto set = [&] (const char* id, float v)
        { if (auto* p = proc.apvts().getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v)); };
        set(amyplug::params::id::patchA, 20.0f);
        pump();
        set(amyplug::params::id::filterCutoff, 5000.0f);   // the user opens the filter right up
        pump();
        proc.getStateInformation(saved);
    }
    {
        amyplug::AmyPlugProcessor proc;
        proc.prepareToPlay(48000.0, 512);
        proc.setStateInformation(saved.getData(), (int) saved.getSize());
        pump();
        auto* r = proc.apvts().getRawParameterValue(amyplug::params::id::filterCutoff);
        REQUIRE(r != nullptr);
        CHECK(r->load() == Catch::Approx(5000.0f).margin(1.0));   // NOT re-stamped back to 298.86
    }
}
