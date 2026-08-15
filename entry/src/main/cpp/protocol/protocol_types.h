/*
 * DeskflowPoC -- HarmonyOS Deskflow protocol client
 * Protocol message constants, ported from Deskflow (GPL-2.0-only)
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstdint>

namespace dfpoc {

// Default port
constexpr uint16_t kDefaultPort = 24800;

// Protocol version
constexpr int16_t kProtocolMajorVersion = 1;
constexpr int16_t kProtocolMinorVersion = 8;

// Hello name length (Synergy/Barrier are both 7 chars)
constexpr uint32_t kHelloNameLength = 7;

// Message formats (from Deskflow ProtocolTypes.cpp)
// Handshake
constexpr const char* kMsgHello       = "%7s%2i%2i";        // Server -> Client
constexpr const char* kMsgHelloArgs   = "%2i%2i";
constexpr const char* kMsgHelloBack   = "%7s%2i%2i%s";      // Client -> Server
constexpr const char* kMsgHelloBackArgs = "%2i%2i%s";

// Commands (C*)
constexpr const char* kMsgCNoop         = "CNOP";
constexpr const char* kMsgCClose        = "CBYE";
constexpr const char* kMsgCEnter        = "CINN%2i%2i%4i%2i"; // x, y, sequence, ? 
constexpr const char* kMsgCLeave        = "COUT";
constexpr const char* kMsgCClipboard    = "CCLP%1i%4i";
constexpr const char* kMsgCScreenSaver  = "CSEC%1i";
constexpr const char* kMsgCResetOptions = "CROP";
constexpr const char* kMsgCInfoAck      = "CIAK";
constexpr const char* kMsgCKeepAlive    = "CALV";

// Data (D*)
constexpr const char* kMsgDKeyDownLang  = "DKDL%2i%2i%2i%s";
constexpr const char* kMsgDKeyDown      = "DKDN%2i%2i%2i";   // id, mask, button
constexpr const char* kMsgDKeyDown1_0   = "DKDN%2i%2i";
constexpr const char* kMsgDKeyRepeat    = "DKRP%2i%2i%2i%2i%s"; // id, mask, count, button, lang
constexpr const char* kMsgDKeyUp        = "DKUP%2i%2i%2i";   // id, mask, button
constexpr const char* kMsgDKeyUp1_0     = "DKUP%2i%2i";
constexpr const char* kMsgDMouseDown    = "DMDN%1i";
constexpr const char* kMsgDMouseUp      = "DMUP%1i";
constexpr const char* kMsgDMouseMove    = "DMMV%2i%2i";
constexpr const char* kMsgDMouseRelMove = "DMRM%2i%2i";
constexpr const char* kMsgDMouseWheel   = "DMWM%2i%2i";
constexpr const char* kMsgDMouseWheel1_0 = "DMWM%2i";
constexpr const char* kMsgDClipboard    = "DCLP%1i%4i%1i%s";
constexpr const char* kMsgDInfo         = "DINF%2i%2i%2i%2i%2i%2i%2i"; // x,y,w,h,0,mx,my
constexpr const char* kMsgDSetOptions    = "DSOP%4I";
constexpr const char* kMsgDFileTransfer  = "DFTR%1i%s";
constexpr const char* kMsgDDragInfo      = "DDRG%2i%s";
constexpr const char* kMsgDSecureInputNotification = "SECN%s";
constexpr const char* kMsgDLanguageSynchronisation = "LSYN%s";

// Queries (Q*)
constexpr const char* kMsgQInfo = "QINF";

// Errors (E*)
constexpr const char* kMsgEIncompatible = "EICV%2i%2i";
constexpr const char* kMsgEBusy         = "EBSY";
constexpr const char* kMsgEUnknown      = "EUNK";
constexpr const char* kMsgEBad          = "EBAD";

// Protocol limits
constexpr uint32_t kProtocolMaxMessageLength = 4 * 1024 * 1024;   // 4 MB
constexpr uint32_t kProtocolMaxStringLength  = 1024 * 1024;       // 1 MB
constexpr uint32_t kMaxHelloLength           = 1024;

// KeyID constants (Deskflow KeyID = X11 keysym - 0x1000 for control keys;
// printable chars are Unicode codepoints). Per KeyTypes.h.
constexpr uint32_t kKeyNone         = 0;
constexpr uint32_t kKeyBackSpace    = 0xEF08;
constexpr uint32_t kKeyTab          = 0xEF09;
constexpr uint32_t kKeyReturn       = 0xEF0D;
constexpr uint32_t kKeyEscape       = 0xEF1B;
constexpr uint32_t kKeySpace        = 0x0020;
constexpr uint32_t kKeyHome         = 0xEF50;
constexpr uint32_t kKeyLeft         = 0xEF51;
constexpr uint32_t kKeyUp           = 0xEF52;
constexpr uint32_t kKeyRight        = 0xEF53;
constexpr uint32_t kKeyDown         = 0xEF54;
constexpr uint32_t kKeyPageUp       = 0xEF55;
constexpr uint32_t kKeyPageDown     = 0xEF56;
constexpr uint32_t kKeyEnd          = 0xEF57;
constexpr uint32_t kKeyInsert       = 0xEF63;
constexpr uint32_t kKeyDelete       = 0xEFFF;
constexpr uint32_t kKeyNumLock      = 0xEF7F;
constexpr uint32_t kKeyPrintScreen  = 0xEF61;
constexpr uint32_t kKeyScrollLock   = 0xEF14;
constexpr uint32_t kKeyCapsLock     = 0xEFE5;
constexpr uint32_t kKeyShiftL       = 0xEFE1;
constexpr uint32_t kKeyShiftR       = 0xEFE2;
constexpr uint32_t kKeyControlL     = 0xEFE3;
constexpr uint32_t kKeyControlR     = 0xEFE4;
constexpr uint32_t kKeyMetaL        = 0xEFE7;
constexpr uint32_t kKeyAltL         = 0xEFE9;
constexpr uint32_t kKeyAltR         = 0xEFEA;
constexpr uint32_t kKeySuperL       = 0xEFEB;
constexpr uint32_t kKeySuperR       = 0xEFEC;
constexpr uint32_t kKeyF1           = 0xEFBE;
constexpr uint32_t kKeyF12          = 0xEFC9;

// Mouse button ids (protocol)
constexpr int32_t kButtonLeft   = 1;
constexpr int32_t kButtonMiddle = 2;
constexpr int32_t kButtonRight  = 3;
constexpr int32_t kButtonWheelUp   = 4;
constexpr int32_t kButtonWheelDown = 5;

} // namespace dfpoc
