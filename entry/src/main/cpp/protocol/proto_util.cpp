/*
 * DeskflowPoC -- HarmonyOS Deskflow protocol client
 * Simplified ProtocolUtil implementation
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "proto_util.h"
#include "socket_stream.h"

#include <cstdarg>
#include <cstring>

namespace dfpoc {

static void putU16(std::vector<uint8_t>& out, uint16_t v)
{
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

static void putU32(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

void ProtoUtil::writef(std::vector<uint8_t>& out, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            ++fmt;
            uint32_t len = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                len = len * 10 + static_cast<uint32_t>(*fmt - '0');
                ++fmt;
            }
            switch (*fmt) {
                case 'i': {
                    int32_t v = va_arg(args, int32_t);
                    switch (len) {
                        case 1:
                            out.push_back(static_cast<uint8_t>(v & 0xFF));
                            break;
                        case 2:
                            putU16(out, static_cast<uint16_t>(v & 0xFFFF));
                            break;
                        case 4:
                            putU32(out, static_cast<uint32_t>(v));
                            break;
                        default:
                            break;
                    }
                    break;
                }
                case 'I': {
                    // vector of integers (uint32_t values stored per element size)
                    std::vector<uint32_t>* vec = va_arg(args, std::vector<uint32_t>*);
                    putU32(out, static_cast<uint32_t>(vec->size()));
                    for (auto v : *vec) {
                        switch (len) {
                            case 1:
                                out.push_back(static_cast<uint8_t>(v & 0xFF));
                                break;
                            case 2:
                                putU16(out, static_cast<uint16_t>(v & 0xFFFF));
                                break;
                            case 4:
                                putU32(out, v);
                                break;
                            default:
                                break;
                        }
                    }
                    break;
                }
                case 's': {
                    // fixed-width string (e.g. %7s) or length-prefixed string
                    std::string* s = va_arg(args, std::string*);
                    if (len > 0) {
                        // fixed width: pad/truncate to len bytes, no length prefix
                        for (uint32_t i = 0; i < len; ++i) {
                            out.push_back(i < s->size() ? static_cast<uint8_t>((*s)[i]) : 0);
                        }
                    } else {
                        putU32(out, static_cast<uint32_t>(s->size()));
                        out.insert(out.end(), s->begin(), s->end());
                    }
                    break;
                }
                case 'S': {
                    uint32_t n = va_arg(args, uint32_t);
                    const uint8_t* data = va_arg(args, const uint8_t*);
                    putU32(out, n);
                    out.insert(out.end(), data, data + n);
                    break;
                }
                case '%':
                    out.push_back('%');
                    break;
                default:
                    break;
            }
        } else {
            out.push_back(static_cast<uint8_t>(*fmt));
        }
        ++fmt;
    }
    va_end(args);
}

bool ProtoUtil::readf(SocketStream& stream, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    bool ok = true;
    while (*fmt && ok) {
        if (*fmt == '%') {
            ++fmt;
            uint32_t len = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                len = len * 10 + static_cast<uint32_t>(*fmt - '0');
                ++fmt;
            }
            switch (*fmt) {
                case 'i': {
                    void* dest = va_arg(args, void*);
                    switch (len) {
                        case 1: {
                            uint8_t v = 0;
                            ok = stream.readExact(&v, 1);
                            *static_cast<uint8_t*>(dest) = v;
                            break;
                        }
                        case 2: {
                            uint8_t b[2] = {0, 0};
                            ok = stream.readExact(b, 2);
                            uint16_t v = static_cast<uint16_t>((b[0] << 8) | b[1]);
                            *static_cast<uint16_t*>(dest) = v;
                            break;
                        }
                        case 4: {
                            uint8_t b[4] = {0, 0, 0, 0};
                            ok = stream.readExact(b, 4);
                            uint32_t v = (static_cast<uint32_t>(b[0]) << 24) |
                                         (static_cast<uint32_t>(b[1]) << 16) |
                                         (static_cast<uint32_t>(b[2]) << 8) |
                                         static_cast<uint32_t>(b[3]);
                            *static_cast<uint32_t*>(dest) = v;
                            break;
                        }
                        default:
                            ok = false;
                            break;
                    }
                    break;
                }
                case 'I': {
                    std::vector<uint32_t>* vec = va_arg(args, std::vector<uint32_t>*);
                    vec->clear();
                    uint8_t lenb[4] = {0, 0, 0, 0};
                    ok = stream.readExact(lenb, 4);
                    if (!ok) break;
                    uint32_t count = (static_cast<uint32_t>(lenb[0]) << 24) |
                                     (static_cast<uint32_t>(lenb[1]) << 16) |
                                     (static_cast<uint32_t>(lenb[2]) << 8) |
                                     static_cast<uint32_t>(lenb[3]);
                    if (count > 1048576) { ok = false; break; }
                    for (uint32_t i = 0; i < count && ok; ++i) {
                        uint32_t v = 0;
                        switch (len) {
                            case 1: {
                                uint8_t b = 0;
                                ok = stream.readExact(&b, 1);
                                v = b;
                                break;
                            }
                            case 2: {
                                uint8_t b[2] = {0, 0};
                                ok = stream.readExact(b, 2);
                                v = static_cast<uint16_t>((b[0] << 8) | b[1]);
                                break;
                            }
                            case 4: {
                                uint8_t b[4] = {0, 0, 0, 0};
                                ok = stream.readExact(b, 4);
                                v = (static_cast<uint32_t>(b[0]) << 24) |
                                    (static_cast<uint32_t>(b[1]) << 16) |
                                    (static_cast<uint32_t>(b[2]) << 8) |
                                    static_cast<uint32_t>(b[3]);
                                break;
                            }
                            default:
                                ok = false;
                                break;
                        }
                        if (ok) vec->push_back(v);
                    }
                    break;
                }
                case 's': {
                    std::string* s = va_arg(args, std::string*);
                    s->clear();
                    if (len > 0) {
                        // fixed-width string
                        std::vector<uint8_t> buf(len);
                        ok = stream.readExact(buf.data(), len);
                        if (ok) {
                            s->assign(reinterpret_cast<char*>(buf.data()), len);
                        }
                    } else {
                        uint8_t lenb[4] = {0, 0, 0, 0};
                        ok = stream.readExact(lenb, 4);
                        if (!ok) break;
                        uint32_t n = (static_cast<uint32_t>(lenb[0]) << 24) |
                                     (static_cast<uint32_t>(lenb[1]) << 16) |
                                     (static_cast<uint32_t>(lenb[2]) << 8) |
                                     static_cast<uint32_t>(lenb[3]);
                        if (n > 1024 * 1024) { ok = false; break; }
                        std::vector<uint8_t> buf(n);
                        ok = stream.readExact(buf.data(), n);
                        if (ok) {
                            s->assign(reinterpret_cast<char*>(buf.data()), n);
                        }
                    }
                    break;
                }
                case 'S': {
                    // raw bytes with 4-byte length prefix
                    std::vector<uint8_t>* v = va_arg(args, std::vector<uint8_t>*);
                    v->clear();
                    uint8_t lenb[4] = {0, 0, 0, 0};
                    ok = stream.readExact(lenb, 4);
                    if (!ok) break;
                    uint32_t n = (static_cast<uint32_t>(lenb[0]) << 24) |
                                 (static_cast<uint32_t>(lenb[1]) << 16) |
                                 (static_cast<uint32_t>(lenb[2]) << 8) |
                                 static_cast<uint32_t>(lenb[3]);
                    if (n > 4 * 1024 * 1024) { ok = false; break; }
                    v->resize(n);
                    ok = stream.readExact(v->data(), n);
                    break;
                }
                case '%':
                    break;
                default:
                    ok = false;
                    break;
            }
        } else {
            // literal character: read and verify
            uint8_t ch = 0;
            ok = stream.readExact(&ch, 1);
            if (ok && ch != static_cast<uint8_t>(*fmt)) {
                ok = false;
            }
        }
        ++fmt;
    }
    va_end(args);
    return ok;
}

} // namespace dfpoc
