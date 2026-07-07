// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// SerialTransport — drives the AMYboard's MicroPython `amy` REPL over the USB-CDC
// serial port. This is the ONLY path that actually edits the board's sound: MIDI
// carries note events, but AMY wire-over-SysEx is inert on real hardware (deferred to
// the board's MicroPython handler). See docs/HARDWARE_MODE.md.
//
// Each queued wire string is sent as:   amy.send_raw("<wire>")\r\n
// and the REPL echoes ">>>" per line, which we drain as a soft ACK for flow-control —
// a fast unpaced burst of patch-loads can knock the board off the USB bus, so heavy
// sends must wait for the prompt.

#include "SerialPort.h"
#include <juce_core/juce_core.h>
#include <atomic>

namespace amyplug
{
class SerialTransport final : private juce::Thread
{
public:
    SerialTransport();
    ~SerialTransport() override;

    // Open a specific device path (e.g. "/dev/cu.usbmodem11201"). Handshakes the REPL
    // (import amy) and starts the sender thread. Returns false if the port won't open.
    bool open(const juce::String& devicePath);
    void close();
    bool isOpen() const noexcept { return connected.load(); }
    juce::String portName() const;

    // Queue one AMY wire string for the board. Thread-safe; does no serial I/O itself
    // (the sender thread drains the queue), so it's safe to call from any thread.
    void sendWire(juce::String wire);

    // Queue a raw MicroPython REPL statement, sent AS-IS (not wrapped in amy.send_raw).
    // Needed for calls the wire protocol can't express — notably amy.send(latency_ms=…),
    // since the wire 'N' form does NOT set the board's global latency. Thread-safe.
    void sendReplLine(juce::String line);

    static juce::StringArray availablePorts() { return SerialPort::availablePorts(); }

    // Probe each candidate serial port for the AMYboard's MicroPython banner and return
    // the first match ("" if none). Opens/closes each briefly — call off the audio thread.
    static juce::String detectAmyboardPort();

private:
    void run() override;                       // sender thread
    bool handshake();                          // wake REPL, ensure `amy` imported
    void drainReplEcho(int timeoutMs);         // read until ">>>" or timeout (heavy ACK pace)
    void drainAvailable(int maxMs);            // read+discard whatever's buffered, then return

    SerialPort port;
    juce::CriticalSection qlock;
    juce::StringArray queue;                   // pending wire strings (wrapped in send_raw)
    juce::StringArray rawQueue;                // pending raw REPL statements (sent as-is)
    std::atomic<bool> connected { false };
    juce::String name;
};
} // namespace amyplug
