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

// KeyID constants (Deskflow KeyID = X11 keysym - 0x1000 for control keys; 0xE000..0xEFFF).
// Canonical values from deskflow KeyTypes.h. Printable ASCII are their Unicode codepoints.
constexpr uint32_t kKeyNone          = 0x0000;
constexpr uint32_t kKeyBackSpace     = 0xEF08;
constexpr uint32_t kKeyTab           = 0xEF09;
constexpr uint32_t kKeyLinefeed      = 0xEF0A;
constexpr uint32_t kKeyClear         = 0xEF0B;
constexpr uint32_t kKeyReturn        = 0xEF0D;
constexpr uint32_t kKeyPause         = 0xEF13;
constexpr uint32_t kKeyScrollLock    = 0xEF14;
constexpr uint32_t kKeySysReq        = 0xEF15;
constexpr uint32_t kKeyEscape        = 0xEF1B;
constexpr uint32_t kKeyCompose       = 0xEF20;
constexpr uint32_t kKeyHenkan        = 0xEF23;
constexpr uint32_t kKeyKana          = 0xEF26;
constexpr uint32_t kKeyHiraganaKatakana = 0xEF27;
constexpr uint32_t kKeyZenkaku       = 0xEF2A;
constexpr uint32_t kKeyHangul        = 0xEF31;
constexpr uint32_t kKeyHanja         = 0xEF34;
constexpr uint32_t kKeyDelete        = 0xEFFF;
constexpr uint32_t kKeyHome          = 0xEF50;
constexpr uint32_t kKeyLeft          = 0xEF51;
constexpr uint32_t kKeyUp            = 0xEF52;
constexpr uint32_t kKeyRight         = 0xEF53;
constexpr uint32_t kKeyDown          = 0xEF54;
constexpr uint32_t kKeyPageUp        = 0xEF55;
constexpr uint32_t kKeyPageDown      = 0xEF56;
constexpr uint32_t kKeyEnd           = 0xEF57;
constexpr uint32_t kKeyBegin         = 0xEF58;
constexpr uint32_t kKeySelect        = 0xEF60;
constexpr uint32_t kKeyPrint         = 0xEF61;
constexpr uint32_t kKeyExecute       = 0xEF62;
constexpr uint32_t kKeyInsert        = 0xEF63;
constexpr uint32_t kKeyUndo          = 0xEF65;
constexpr uint32_t kKeyRedo          = 0xEF66;
constexpr uint32_t kKeyMenu          = 0xEF67;
constexpr uint32_t kKeyFind          = 0xEF68;
constexpr uint32_t kKeyCancel        = 0xEF69;
constexpr uint32_t kKeyHelp          = 0xEF6A;
constexpr uint32_t kKeyBreak         = 0xEF6B;
constexpr uint32_t kKeyAltGr         = 0xEF7E;
constexpr uint32_t kKeyNumLock       = 0xEF7F;
constexpr uint32_t kKeyKP_Space      = 0xEF80;
constexpr uint32_t kKeyKP_Tab        = 0xEF89;
constexpr uint32_t kKeyKP_Enter      = 0xEF8D;
constexpr uint32_t kKeyKP_F1         = 0xEF91;
constexpr uint32_t kKeyKP_F2         = 0xEF92;
constexpr uint32_t kKeyKP_F3         = 0xEF93;
constexpr uint32_t kKeyKP_F4         = 0xEF94;
constexpr uint32_t kKeyKP_Home       = 0xEF95;
constexpr uint32_t kKeyKP_Left       = 0xEF96;
constexpr uint32_t kKeyKP_Up         = 0xEF97;
constexpr uint32_t kKeyKP_Right      = 0xEF98;
constexpr uint32_t kKeyKP_Down       = 0xEF99;
constexpr uint32_t kKeyKP_PageUp     = 0xEF9A;
constexpr uint32_t kKeyKP_PageDown   = 0xEF9B;
constexpr uint32_t kKeyKP_End        = 0xEF9C;
constexpr uint32_t kKeyKP_Begin      = 0xEF9D;
constexpr uint32_t kKeyKP_Insert     = 0xEF9E;
constexpr uint32_t kKeyKP_Delete     = 0xEF9F;
constexpr uint32_t kKeyKP_Multiply   = 0xEFAA;
constexpr uint32_t kKeyKP_Add        = 0xEFAB;
constexpr uint32_t kKeyKP_Separator  = 0xEFAC;
constexpr uint32_t kKeyKP_Subtract   = 0xEFAD;
constexpr uint32_t kKeyKP_Decimal    = 0xEFAE;
constexpr uint32_t kKeyKP_Divide     = 0xEFAF;
constexpr uint32_t kKeyKP_0          = 0xEFB0;
constexpr uint32_t kKeyKP_1          = 0xEFB1;
constexpr uint32_t kKeyKP_2          = 0xEFB2;
constexpr uint32_t kKeyKP_3          = 0xEFB3;
constexpr uint32_t kKeyKP_4          = 0xEFB4;
constexpr uint32_t kKeyKP_5          = 0xEFB5;
constexpr uint32_t kKeyKP_6          = 0xEFB6;
constexpr uint32_t kKeyKP_7          = 0xEFB7;
constexpr uint32_t kKeyKP_8          = 0xEFB8;
constexpr uint32_t kKeyKP_9          = 0xEFB9;
constexpr uint32_t kKeyKP_Equal      = 0xEFBD;
constexpr uint32_t kKeyF1            = 0xEFBE;   // F1..F35 连续
constexpr uint32_t kKeyF12           = 0xEFC9;
constexpr uint32_t kKeyF24           = 0xEFD5;
constexpr uint32_t kKeyF35           = 0xEFE0;
constexpr uint32_t kKeyShiftL        = 0xEFE1;
constexpr uint32_t kKeyShiftR        = 0xEFE2;
constexpr uint32_t kKeyControlL      = 0xEFE3;
constexpr uint32_t kKeyControlR      = 0xEFE4;
constexpr uint32_t kKeyCapsLock      = 0xEFE5;
constexpr uint32_t kKeyShiftLock     = 0xEFE6;
constexpr uint32_t kKeyMetaL         = 0xEFE7;
constexpr uint32_t kKeyMetaR         = 0xEFE8;
constexpr uint32_t kKeyAltL          = 0xEFE9;
constexpr uint32_t kKeyAltR          = 0xEFEA;
constexpr uint32_t kKeySuperL        = 0xEFEB;
constexpr uint32_t kKeySuperR        = 0xEFEC;
constexpr uint32_t kKeyHyperL        = 0xEFED;
constexpr uint32_t kKeyHyperR        = 0xEFEE;
constexpr uint32_t kKeyLeftTab       = 0xEE20;
constexpr uint32_t kKeyNextGroup     = 0xEE08;
constexpr uint32_t kKeyPrevGroup     = 0xEE0A;
// extended/media/app 键 (0xE0xx / 0xE001)
constexpr uint32_t kKeyEject         = 0xE001;
constexpr uint32_t kKeySleep         = 0xE05F;
constexpr uint32_t kKeyWWWBack       = 0xE0A6;
constexpr uint32_t kKeyWWWForward    = 0xE0A7;
constexpr uint32_t kKeyWWWRefresh    = 0xE0A8;
constexpr uint32_t kKeyWWWStop       = 0xE0A9;
constexpr uint32_t kKeyWWWSearch     = 0xE0AA;
constexpr uint32_t kKeyWWWFavorites  = 0xE0AB;
constexpr uint32_t kKeyWWWHome       = 0xE0AC;
constexpr uint32_t kKeyAudioMute     = 0xE0AD;
constexpr uint32_t kKeyAudioDown     = 0xE0AE;
constexpr uint32_t kKeyAudioUp       = 0xE0AF;
constexpr uint32_t kKeyAudioNext     = 0xE0B0;
constexpr uint32_t kKeyAudioPrev     = 0xE0B1;
constexpr uint32_t kKeyAudioStop     = 0xE0B2;
constexpr uint32_t kKeyAudioPlay     = 0xE0B3;
constexpr uint32_t kKeyAppMail       = 0xE0B4;
constexpr uint32_t kKeyAppMedia      = 0xE0B5;
constexpr uint32_t kKeyBrightnessDown = 0xE0B8;
constexpr uint32_t kKeyBrightnessUp  = 0xE0B9;

// Mouse button ids (protocol)
constexpr int32_t kButtonLeft   = 1;
constexpr int32_t kButtonMiddle = 2;
constexpr int32_t kButtonRight  = 3;
constexpr int32_t kButtonWheelUp   = 4;
constexpr int32_t kButtonWheelDown = 5;

} // namespace dfpoc
