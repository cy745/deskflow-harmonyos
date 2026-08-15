/*
 * DeskflowPoC -- HarmonyOS Deskflow protocol client
 * POSIX TCP socket stream implementation
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "socket_stream.h"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace dfpoc {

SocketStream::SocketStream() : m_fd(-1), m_bufferPos(0) {}

SocketStream::~SocketStream()
{
    close();
}

bool SocketStream::connect(const std::string& host, uint16_t port, int timeoutMs)
{
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* result = nullptr;
    std::string portStr = std::to_string(port);
    int rc = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result);
    if (rc != 0) {
        return false;
    }

    for (struct addrinfo* ai = result; ai != nullptr; ai = ai->ai_next) {
        int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            continue;
        }
        // non-blocking connect with timeout
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        rc = ::connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (rc != 0 && errno == EINPROGRESS) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(fd, &wfds);
            struct timeval tv;
            tv.tv_sec = timeoutMs / 1000;
            tv.tv_usec = (timeoutMs % 1000) * 1000;
            rc = select(fd + 1, nullptr, &wfds, nullptr, &tv);
            if (rc > 0) {
                int err = 0;
                socklen_t len = sizeof(err);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
                rc = (err == 0) ? 0 : -1;
            } else {
                rc = -1;
            }
        }
        if (rc == 0) {
            fcntl(fd, F_SETFL, flags);
            m_fd = fd;
            freeaddrinfo(result);
            return true;
        }
        ::close(fd);
    }
    freeaddrinfo(result);
    return false;
}

bool SocketStream::readExact(uint8_t* data, size_t n)
{
    size_t got = 0;
    // consume buffered data first
    while (got < n && m_bufferPos < m_buffer.size()) {
        data[got++] = m_buffer[m_bufferPos++];
    }
    while (got < n) {
        ssize_t rc = ::recv(m_fd, data + got, n - got, 0);
        if (rc > 0) {
            got += static_cast<size_t>(rc);
        } else if (rc == 0) {
            return false; // EOF
        } else {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
    }
    return true;
}

bool SocketStream::writeAll(const uint8_t* data, size_t n)
{
    size_t sent = 0;
    while (sent < n) {
        ssize_t rc = ::send(m_fd, data + sent, n - sent, MSG_NOSIGNAL);
        if (rc > 0) {
            sent += static_cast<size_t>(rc);
        } else if (rc < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

bool SocketStream::readFrame(std::vector<uint8_t>& out)
{
    uint8_t lenb[4] = {0, 0, 0, 0};
    if (!readExact(lenb, 4)) {
        return false;
    }
    uint32_t len = (static_cast<uint32_t>(lenb[0]) << 24) |
                   (static_cast<uint32_t>(lenb[1]) << 16) |
                   (static_cast<uint32_t>(lenb[2]) << 8) |
                   static_cast<uint32_t>(lenb[3]);
    if (len > 4 * 1024 * 1024) {
        return false;
    }
    out.resize(len);
    if (len > 0 && !readExact(out.data(), len)) {
        return false;
    }
    return true;
}

bool SocketStream::writeFrame(const uint8_t* data, size_t n)
{
    if (n > 4 * 1024 * 1024) {
        return false;
    }
    uint8_t lenb[4] = {
        static_cast<uint8_t>((n >> 24) & 0xFF),
        static_cast<uint8_t>((n >> 16) & 0xFF),
        static_cast<uint8_t>((n >> 8) & 0xFF),
        static_cast<uint8_t>(n & 0xFF)
    };
    if (!writeAll(lenb, 4)) {
        return false;
    }
    return data == nullptr || n == 0 || writeAll(data, n);
}

void SocketStream::pushToBuffer(const uint8_t* data, size_t n)
{
    m_buffer.insert(m_buffer.end(), data, data + n);
}

void SocketStream::close()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_buffer.clear();
    m_bufferPos = 0;
}

} // namespace dfpoc
