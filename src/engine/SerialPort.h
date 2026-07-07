// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#pragma once
//
// SerialPort — a minimal blocking-with-timeout serial port over POSIX termios
// (macOS/Linux). Used to talk to the AMYboard's USB-CDC MicroPython `amy` REPL.
//
// USB-CDC ignores the baud rate, but we set a sane value anyway. Native-USB CDC has
// no auto-reset circuit, so opening the port does NOT reboot the board.
//
// Windows (M6, later) would need a COM-port backend; there the methods are no-ops so
// the plugin still compiles.

#include <juce_core/juce_core.h>

namespace amyplug
{
class SerialPort
{
public:
    SerialPort() = default;
    ~SerialPort();

    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    bool open(const juce::String& devicePath);   // e.g. "/dev/cu.usbmodem11201"
    void close();
    bool isOpen() const noexcept { return fd >= 0; }

    int  write(const char* data, int len);        // bytes written, -1 on error
    // Read up to maxLen bytes, waiting up to timeoutMs for data to appear. Returns
    // bytes read (0 on timeout, -1 on error). Keeps reading while bytes keep arriving.
    int  read(char* buf, int maxLen, int timeoutMs);

    juce::String path() const { return devicePath; }

    // Candidate USB serial devices (/dev/cu.usbmodem*, /dev/cu.usbserial*). macOS uses
    // the cu.* (callout) nodes so open() doesn't block waiting for carrier detect.
    static juce::StringArray availablePorts();

private:
    int fd = -1;
    juce::String devicePath;
};
} // namespace amyplug
