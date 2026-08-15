/*
 * DeskflowPoC -- HarmonyOS Deskflow protocol client
 * Simplified ProtocolUtil (writef/readf), ported from Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dfpoc {

class SocketStream;

// Minimal re-implementation of Deskflow's ProtocolUtil formatting:
//   %1i / %2i / %4i -- 1/2/4 byte integers (NBO for 2/4)
//   %s              -- std::string with 4-byte NBO length prefix
//   %S              -- N raw bytes with 4-byte NBO length prefix
//   %%              -- literal '%'
//   %7s             -- fixed-width string (no length prefix), used for hello name
class ProtoUtil
{
public:
    // Write formatted data to a byte buffer (append)
    static void writef(std::vector<uint8_t>& out, const char* fmt, ...);

    // Read formatted data from a stream. Returns false on EOF/format error.
    static bool readf(SocketStream& stream, const char* fmt, ...);
};

} // namespace dfpoc
