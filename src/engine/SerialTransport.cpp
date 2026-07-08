// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "SerialTransport.h"
#include <cstring>

namespace amyplug
{
namespace
{
// Wrap a raw AMY wire string as a REPL statement: amy.send_raw("<wire>")\r\n
// Our wire strings are lower-ASCII (letters/digits/comma/period/minus) and contain no
// double-quotes or backslashes, so no escaping is required — but guard defensively.
juce::String replLineFor(const juce::String& wire)
{
    juce::String safe = wire.replace("\\", "").replace("\"", "").replace("\r", "").replace("\n", "");
    return "amy.send_raw(\"" + safe + "\")\r\n";
}

// "Heavy" = a patch load ('K') or a reset ('S', uppercase; 's' is pitch-bend). These
// do real work on the board's MicroPython loop and MUST be ACK-paced or a burst knocks
// the board off the bus. Everything else (filter, osc, envelope, volume…) is a cheap
// parameter set that can stream without waiting for the prompt.
bool isHeavy(const juce::String& wire) { return wire.containsChar('K') || wire.containsChar('S'); }

// Coalescing key: the command minus its trailing value, so a knob sweep "i1F40","i1F55"…
// collapses to one pending "i1F". Only used for light param messages (heavy loads keep
// their order/identity). Strips the 'Z' terminator then the trailing value run.
juce::String commandKey(const juce::String& wire)
{
    juce::String s = wire.endsWithChar('Z') ? wire.dropLastCharacters(1) : wire;
    int end = s.length();
    while (end > 0)
    {
        const juce::juce_wchar c = s[end - 1];
        if ((c >= '0' && c <= '9') || c == '.' || c == ',' || c == '-') --end; else break;
    }
    return s.substring(0, end);
}
} // namespace

SerialTransport::SerialTransport() : juce::Thread("AMYboard serial") {}
SerialTransport::~SerialTransport() { close(); }

juce::String SerialTransport::portName() const { return name; }

bool SerialTransport::open(const juce::String& devicePath)
{
    close();
    if (! port.open(devicePath))
        return false;

    if (! handshake())
    {
        port.close();
        return false;
    }

    name = devicePath;
    connected.store(true);
    startThread(juce::Thread::Priority::normal);
    return true;
}

void SerialTransport::close()
{
    signalThreadShouldExit();
    notify();
    stopThread(700);
    port.close();
    connected.store(false);
    versionWanted.store(false);   // don't let a pending query fire on the next open
    name = {};
    const juce::ScopedLock sl(qlock);
    queue.clear();
}

bool SerialTransport::handshake()
{
    // Nudge the REPL to a fresh prompt, then make sure `amy` is importable. The board's
    // running app usually already imports it, but a bare REPL may not have it in scope.
    char buf[512];
    const char* nl  = "\r\n";
    const char* imp = "import amy\r\n";
    port.write(nl, (int) std::strlen(nl));
    port.read(buf, sizeof(buf), 200);                 // swallow any prompt/echo
    port.write(imp, (int) std::strlen(imp));
    const int n = port.read(buf, sizeof(buf), 400);
    // A healthy REPL echoes the line and returns to ">>>". If we saw a Python traceback,
    // amy isn't available — treat as a failed handshake so the UI can report it.
    juce::String reply(buf, (size_t) juce::jmax(0, n));
    if (reply.containsIgnoreCase("Traceback") || reply.containsIgnoreCase("ImportError"))
        return false;
    return true;
}

void SerialTransport::drainReplEcho(int timeoutMs)
{
    // Read until we see the ">>>" prompt (line processed) or time out. Used ONLY for heavy
    // ops (patch load / reset): a burst of those unpaced can crash the board off the bus.
    char buf[512];
    const int deadlineChunks = juce::jmax(1, timeoutMs / 40);
    for (int i = 0; i < deadlineChunks; ++i)
    {
        const int n = port.read(buf, sizeof(buf), 40);
        if (n > 0 && juce::String(buf, (size_t) n).contains(">>>"))
            return;
        if (n <= 0)
            break;
    }
}

void SerialTransport::drainAvailable(int maxMs)
{
    // Read and discard whatever the REPL has echoed so far, then return — do NOT block
    // waiting for a prompt. This keeps the board's TX buffer clear without stalling the
    // sender, so light param streams stay fast (and don't starve the board's note loop).
    char buf[512];
    int budget = juce::jmax(0, maxMs);
    for (;;)
    {
        const int n = port.read(buf, sizeof(buf), 3);   // 3ms probe
        budget -= 3;
        if (n <= 0 || budget <= 0) break;               // buffer drained (or time up)
    }
}

void SerialTransport::sendWire(juce::String wire)
{
    if (wire.isEmpty()) return;
    const juce::ScopedLock sl(qlock);
    // Coalesce rapid same-param sends (knob sweeps): drop any pending LIGHT message that
    // targets the same command so only the latest value is sent. Heavy loads are never
    // coalesced — they keep their order and identity (a rebuild's reset+osc sequence).
    if (! isHeavy(wire))
    {
        const juce::String key = commandKey(wire);
        for (int i = queue.size(); --i >= 0;)
            if (! isHeavy(queue[i]) && commandKey(queue[i]) == key) queue.remove(i);
    }
    queue.add(std::move(wire));
    notify();
}

void SerialTransport::sendReplLine(juce::String line)
{
    if (line.isEmpty()) return;
    if (! line.endsWithChar('\n')) line += "\r\n";
    const juce::ScopedLock sl(qlock);
    // Coalesce repeats (e.g. the periodic latency_ms re-assert) so they don't pile up.
    for (int i = rawQueue.size(); --i >= 0;)
        if (rawQueue[i] == line) rawQueue.remove(i);
    rawQueue.add(std::move(line));
    notify();
}

void SerialTransport::requestVersion()
{
    { const juce::ScopedLock sl(qlock); version.clear(); }   // "" = pending until the thread answers
    versionWanted.store(true);
    notify();
}

juce::String SerialTransport::firmwareVersion() const
{
    const juce::ScopedLock sl(qlock);
    return version;
}

void SerialTransport::queryVersion()
{
    // Read the firmware build over the SAME USB serial REPL that detectAmyboardPort() uses
    // (proven to echo print() output back): the documented accessor is tulip.version() ->
    // "20260627-abc1234". We fence the value in unique <<AMYFW: … >> markers so we can pick it
    // out of the reply; the REPL also echoes our typed source (which contains the markers with
    // quotes/plus around it), so we accept only a fenced value made of build-id characters.
    // We own the port here (sender thread), so this short blocking round-trip is safe.
    drainAvailable(10);   // clear any pending echo so our reply isn't mixed with prior output
    const char* q = "import tulip\r\nprint('<<AMYFW:'+tulip.version()+'>>')\r\n";
    port.write(q, (int) std::strlen(q));

    juce::String reply;
    char buf[512];
    for (int i = 0; i < 25; ++i)   // up to ~1s for the board to answer
    {
        const int n = port.read(buf, sizeof(buf), 40);
        if (n > 0) reply += juce::String(buf, (size_t) n);
        // Stop once we have a fenced value (a '>>' that follows a marker, past the echo).
        if (reply.contains("<<AMYFW:") && reply.fromFirstOccurrenceOf("<<AMYFW:", false, false).contains(">>"))
        {
            // Give a beat for the rest of the line, then bail.
            if (i >= 2) break;
        }
    }

    juce::String v;
    juce::String scan = reply;
    while (v.isEmpty())
    {
        const int m = scan.indexOf("<<AMYFW:");
        if (m < 0) break;
        juce::String rest = scan.substring(m + 8);
        const int e = rest.indexOf(">>");
        if (e < 0) break;
        const juce::String cand = rest.substring(0, e).trim();
        // A real build id is digits/hex/letters and dashes only — the echoed source is not.
        if (cand.isNotEmpty() && cand.containsOnly("0123456789abcdefABCDEF-."))
            v = cand;
        scan = rest.substring(e + 2);   // keep scanning past this (possibly echoed) marker
    }

    const juce::ScopedLock sl(qlock);
    version = v.isNotEmpty() ? v : juce::String("unavailable");
}

void SerialTransport::run()
{
    juce::StringArray batch, raws;
    juce::String lights;   // accumulated light param lines, written in one go

    auto flushLights = [&]
    {
        if (lights.isEmpty()) return;
        port.write(lights.toRawUTF8(), (int) lights.getNumBytesAsUTF8());
        lights.clear();
        drainAvailable(6);   // clear the echo; do NOT wait for a prompt
    };

    while (! threadShouldExit())
    {
        if (port.isOpen() && versionWanted.exchange(false))
            queryVersion();

        batch.clearQuick(); raws.clearQuick();
        {
            const juce::ScopedLock sl(qlock);
            if (queue.size() > 0)    batch.swapWith(queue);
            if (rawQueue.size() > 0) raws.swapWith(rawQueue);
        }
        if ((batch.isEmpty() && raws.isEmpty()) || ! port.isOpen()) { wait(4); continue; }

        // Raw REPL statements (e.g. amy.send(latency_ms=…)) go first, verbatim, ACK-paced.
        for (const auto& r : raws)
        {
            port.write(r.toRawUTF8(), (int) r.getNumBytesAsUTF8());
            drainReplEcho(120);
        }

        // Send the whole pending batch this pass. Light param lines are coalesced into
        // ~240-byte writes and never block (so a rebuild / knob sweep transmits in tens of
        // ms, not hundreds — the board's shared MIDI+serial loop isn't starved). Only heavy
        // ops (patch load 'K' / reset 'S') are ACK-paced, since a burst of THOSE is what
        // knocks the board off the bus.
        for (const auto& msg : batch)
        {
            if (isHeavy(msg))
            {
                flushLights();
                const juce::String line = replLineFor(msg);
                port.write(line.toRawUTF8(), (int) line.getNumBytesAsUTF8());
                drainReplEcho(300);
            }
            else
            {
                lights += replLineFor(msg);
                if (lights.getNumBytesAsUTF8() > 240) flushLights();
            }
        }
        flushLights();
    }
}

juce::String SerialTransport::detectAmyboardPort()
{
    // Send a tiny Python probe and look for its echoed token. A live MicroPython REPL
    // echoes the typed line (and evaluates it), so the token comes back; a non-REPL port
    // (e.g. the dock's billboard CDC) does not. More reliable than the boot banner, which
    // an already-running board won't reprint.
    const char* probe = "\r\nprint('AMYPROBE_OK')\r\n";
    char buf[1024];
    for (const auto& p : SerialPort::availablePorts())
    {
        SerialPort sp;
        if (! sp.open(p)) continue;
        sp.write(probe, (int) std::strlen(probe));
        const int n = sp.read(buf, sizeof(buf), 400);
        const juce::String reply(buf, (size_t) juce::jmax(0, n));
        sp.close();
        if (reply.contains("AMYPROBE_OK") || reply.containsIgnoreCase("AMYboard")
            || reply.containsIgnoreCase("MicroPython"))
            return p;
    }
    return {};
}
} // namespace amyplug
