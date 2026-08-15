/*
 * DeskflowPoC -- HarmonyOS Deskflow protocol client
 * POSIX TCP socket stream with buffered reads
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dfpoc {

class SocketStream
{
public:
    SocketStream();
    ~SocketStream();

    // Connect to host:port. Returns true on success.
    bool connect(const std::string& host, uint16_t port, int timeoutMs = 5000);

    // Read exactly n bytes. Returns true on success, false on EOF/error.
    bool readExact(uint8_t* data, size_t n);

    // Write bytes. Returns true on success.
    bool writeAll(const uint8_t* data, size_t n);

    // Read one length-prefixed frame (4-byte NBO length + payload).
    // Returns false on EOF/error or oversize frame.
    bool readFrame(std::vector<uint8_t>& out);

    // Write one length-prefixed frame.
    bool writeFrame(const uint8_t* data, size_t n);

    // Push bytes into the internal read buffer (consumed by readExact first).
    void pushToBuffer(const uint8_t* data, size_t n);

    // Bytes still waiting in the internal read buffer (for diagnostics).
    size_t bufferRemaining() const { return m_buffer.size() - m_bufferPos; }

    void close();

    bool isConnected() const { return m_fd >= 0; }

private:
    int m_fd;
    std::vector<uint8_t> m_buffer;
    size_t m_bufferPos;
};

} // namespace dfpoc
