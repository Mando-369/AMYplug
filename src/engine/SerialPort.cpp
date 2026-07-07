// SPDX-License-Identifier: AGPL-3.0-or-later OR MIT
#include "SerialPort.h"

#if ! JUCE_WINDOWS
 #include <termios.h>
 #include <fcntl.h>
 #include <unistd.h>
 #include <sys/select.h>
 #include <errno.h>
#endif

namespace amyplug
{
SerialPort::~SerialPort() { close(); }

#if ! JUCE_WINDOWS

bool SerialPort::open(const juce::String& p)
{
    close();
    const int f = ::open(p.toRawUTF8(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (f < 0)
        return false;

    struct termios t;
    if (tcgetattr(f, &t) != 0) { ::close(f); return false; }

    cfmakeraw(&t);                        // 8N1, no echo, no line processing
    t.c_cflag |= (tcflag_t) (CREAD | CLOCAL | CS8);  // enable receiver, ignore modem lines
    t.c_cflag &= (tcflag_t) ~CRTSCTS;                // no hardware flow control
    cfsetispeed(&t, B115200);
    cfsetospeed(&t, B115200);
    t.c_cc[VMIN]  = 0;                    // non-blocking read semantics (we poll)
    t.c_cc[VTIME] = 0;

    if (tcsetattr(f, TCSANOW, &t) != 0) { ::close(f); return false; }
    tcflush(f, TCIOFLUSH);

    fd = f;
    devicePath = p;
    return true;
}

void SerialPort::close()
{
    if (fd >= 0)
        ::close(fd);
    fd = -1;
    devicePath = {};
}

int SerialPort::write(const char* data, int len)
{
    if (fd < 0) return -1;
    int total = 0;
    while (total < len)
    {
        const ssize_t n = ::write(fd, data + total, (size_t) (len - total));
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EINTR) continue;   // retry transient
            return -1;
        }
        total += (int) n;
    }
    return total;
}

int SerialPort::read(char* buf, int maxLen, int timeoutMs)
{
    if (fd < 0) return -1;
    int total = 0;
    int remainingMs = juce::jmax(0, timeoutMs);
    while (total < maxLen)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv;
        tv.tv_sec  = remainingMs / 1000;
        tv.tv_usec = (remainingMs % 1000) * 1000;

        const int r = ::select(fd + 1, &rfds, nullptr, nullptr, &tv);
        if (r < 0) { if (errno == EINTR) continue; return total > 0 ? total : -1; }
        if (r == 0) break;                              // timed out with no (more) data

        const ssize_t n = ::read(fd, buf + total, (size_t) (maxLen - total));
        if (n > 0) { total += (int) n; remainingMs = 20; } // short grace for trailing bytes
        else break;
    }
    return total;
}

juce::StringArray SerialPort::availablePorts()
{
    juce::StringArray ports;
    juce::File dev("/dev");
    for (const auto& pattern : { "cu.usbmodem*", "cu.usbserial*", "cu.SLAB*", "cu.wchusbserial*" })
        for (auto& f : dev.findChildFiles(juce::File::findFiles | juce::File::ignoreHiddenFiles, false, pattern))
            ports.addIfNotAlreadyThere(f.getFullPathName());
    return ports;
}

#else // JUCE_WINDOWS — COM-port backend not implemented yet (M6)

bool SerialPort::open(const juce::String&) { return false; }
void SerialPort::close() {}
int  SerialPort::write(const char*, int) { return -1; }
int  SerialPort::read(char*, int, int) { return -1; }
juce::StringArray SerialPort::availablePorts() { return {}; }

#endif
} // namespace amyplug
