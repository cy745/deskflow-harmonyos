/*
 * DeskflowPoC -- HarmonyOS Deskflow protocol client
 * Client implementation: handshake + message loop + injection
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow_client.h"
#include "proto_util.h"
#include "protocol_types.h"

#include <cstdarg>
#include <cstring>
#include <sstream>

#include "hilog/log.h"
#include "multimodalinput/oh_input_manager.h"

namespace dfpoc {

namespace {

constexpr unsigned int DF_LOG_DOMAIN = 0xD002;
constexpr const char* DF_LOG_TAG = "DeskflowPoC";

#define DF_LOGI(...) OH_LOG_Print(LOG_APP, LOG_INFO, DF_LOG_DOMAIN, DF_LOG_TAG, __VA_ARGS__)
#define DF_LOGE(...) OH_LOG_Print(LOG_APP, LOG_ERROR, DF_LOG_DOMAIN, DF_LOG_TAG, __VA_ARGS__)

// 注入授权异步回调（MMI IPC 线程）
void OnInjectAuthCallback(Input_InjectionStatus status)
{
    const char* text = "Unknown";
    switch (status) {
        case UNAUTHORIZED: text = "UNAUTHORIZED(0)"; break;
        case AUTHORIZING:  text = "AUTHORIZING(1)"; break;
        case AUTHORIZED:   text = "AUTHORIZED(2)"; break;
        default: break;
    }
    DF_LOGI("injection auth callback: %{public}s", text);
}

// Map X11 keysym (deskflow KeyID) to HarmonyOS KeyCode. Returns -1 if unmapped.
int32_t keysymToKeycode(uint32_t keysym)
{
    if (keysym >= 0x61 && keysym <= 0x7A) {
        return 2017 + static_cast<int32_t>(keysym - 0x61); // KEYCODE_A .. KEYCODE_Z
    }
    if (keysym >= 0x30 && keysym <= 0x39) {
        return 2000 + static_cast<int32_t>(keysym - 0x30); // KEYCODE_0 .. KEYCODE_9
    }
    if (keysym >= kKeyF1 && keysym <= kKeyF12) {
        return 2090 + static_cast<int32_t>(keysym - kKeyF1);
    }
    switch (keysym) {
        case kKeyReturn:    return 2054; // KEYCODE_ENTER
        case kKeyBackSpace: return 2055; // KEYCODE_DEL (backspace)
        case kKeyTab:       return 2049;
        case kKeySpace:     return 2050;
        case kKeyEscape:    return 2070;
        case kKeyLeft:      return 2014; // KEYCODE_DPAD_LEFT
        case kKeyUp:        return 2012; // KEYCODE_DPAD_UP
        case kKeyRight:     return 2015; // KEYCODE_DPAD_RIGHT
        case kKeyDown:      return 2013; // KEYCODE_DPAD_DOWN
        case kKeyHome:      return 2081; // KEYCODE_MOVE_HOME
        case kKeyEnd:       return 2082; // KEYCODE_MOVE_END
        case kKeyPageUp:    return 2068;
        case kKeyPageDown:  return 2069;
        case kKeyInsert:    return 2083;
        case kKeyDelete:    return 2071; // KEYCODE_FORWARD_DEL
        case kKeyCapsLock:  return 2074;
        case kKeyShiftL:    return 2047;
        case kKeyShiftR:    return 2048;
        case kKeyControlL:  return 2072;
        case kKeyControlR:  return 2073;
        case kKeyAltL:      return 2045;
        case kKeyAltR:      return 2046;
        case kKeyMetaL:     return 2076; // KEYCODE_META_LEFT
        case kKeySuperL:    return 2076;
        case kKeySuperR:    return 2077;
        case kKeyNumLock:   return 2102;
        case kKeyScrollLock: return 2075;
        case kKeyPrintScreen: return 2079; // KEYCODE_SYSRQ
        case 0x2C: return 2043; // comma
        case 0x2E: return 2044; // period
        case 0x2D: return 2057; // minus
        case 0x3D: return 2058; // equals
        case 0x5B: return 2059; // left bracket
        case 0x5D: return 2060; // right bracket
        case 0x5C: return 2061; // backslash
        case 0x3B: return 2062; // semicolon
        case 0x27: return 2063; // apostrophe
        case 0x2F: return 2064; // slash
        case 0x60: return 2056; // grave
        default: return -1;
    }
}

int32_t buttonToHarmony(int32_t buttonId)
{
    switch (buttonId) {
        case kButtonLeft:   return MOUSE_BUTTON_LEFT;
        case kButtonMiddle: return MOUSE_BUTTON_MIDDLE;
        case kButtonRight:  return MOUSE_BUTTON_RIGHT;
        default: return -1;
    }
}

} // namespace

DeskflowClient::DeskflowClient() = default;

DeskflowClient::~DeskflowClient()
{
    stop();
}

bool DeskflowClient::start(const std::string& host, uint16_t port, const std::string& screenName)
{
    if (m_running.load()) {
        return false;
    }
    m_host = host;
    m_port = port;
    m_screenName = screenName;
    m_stopRequested = false;
    m_running = true;
    m_thread = std::thread([this] { run(); });
    m_thread.detach();
    return true;
}

void DeskflowClient::stop()
{
    m_stopRequested = true;
    m_stream.close(); // unblock recv
}

void DeskflowClient::setStatus(const std::string& s)
{
    if (m_statusCb) {
        m_statusCb(s);
    }
}

void DeskflowClient::run()
{
    setStatus("connecting to " + m_host + ":" + std::to_string(m_port));
    DF_LOGI("connecting to %{public}s:%{public}d", m_host.c_str(), m_port);
    if (!m_stream.connect(m_host, m_port, 5000)) {
        DF_LOGE("connect failed");
        setStatus("error: connect failed");
        m_running = false;
        return;
    }
    DF_LOGI("tcp connected");
    setStatus("connected, handshake...");
    if (!handshake()) {
        DF_LOGE("handshake failed");
        setStatus("error: handshake failed");
        m_stream.close();
        m_running = false;
        return;
    }
    DF_LOGI("handshake ok");
    setStatus("handshake ok, waiting for enter (move mouse to this screen)");

    // 自动申请注入授权（否则 CINN 后的注入事件不会生效）
    Input_Result authRet = OH_Input_RequestInjection(OnInjectAuthCallback);
    DF_LOGI("auto request injection auth, ret=%{public}d", authRet);
    if (authRet != INPUT_SUCCESS && authRet != INPUT_INJECTION_AUTHORIZED) {
        DF_LOGE("auto injection auth request failed: %{public}d", authRet);
    }

    // message loop -- every message is a length-prefixed frame
    bool keepGoing = true;
    while (keepGoing && !m_stopRequested.load()) {
        std::vector<uint8_t> frame;
        if (!m_stream.readFrame(frame)) {
            if (!m_stopRequested.load()) {
                DF_LOGE("connection lost while reading frame");
                setStatus("error: connection lost");
            } else {
                setStatus("disconnected");
            }
            break;
        }
        // parse message key + payload from frame buffer
        m_stream.pushToBuffer(frame.data(), frame.size());
        uint8_t head[4] = {0, 0, 0, 0};
        if (!m_stream.readExact(head, 4)) {
            DF_LOGE("frame too short");
            break;
        }
        std::string key(reinterpret_cast<char*>(head), 4);
        DF_LOGI("recv: %{public}s (frame %{public}zu, bufRemain %{public}zu)",
            key.c_str(), frame.size(), m_stream.bufferRemaining());
        keepGoing = handleMessage(key);
        // 运行期每条消息处理完后回 CNOP（deskflow client 的 BSD 延迟 ACK 绕开 hack）
        if (keepGoing && m_handshakeDone) {
            if (!m_stream.writeFrame(reinterpret_cast<const uint8_t*>("CNOP"), 4)) {
                DF_LOGE("write CNOP failed");
                keepGoing = false;
            }
        }
    }

    DF_LOGI("message loop exited (stopRequested=%{public}d)", m_stopRequested.load() ? 1 : 0);

    m_stream.close();
    m_running = false;
    if (!m_stopRequested.load()) {
        setStatus("disconnected");
    }
}

bool DeskflowClient::handshake()
{
    // Server -> Client: framed Hello = "%7s%2i%2i"
    std::vector<uint8_t> frame;
    if (!m_stream.readFrame(frame)) {
        DF_LOGE("read hello frame failed");
        return false;
    }
    m_stream.pushToBuffer(frame.data(), frame.size());

    std::string protocolName;
    int16_t major = 0;
    int16_t minor = 0;
    if (!ProtoUtil::readf(m_stream, kMsgHello, &protocolName, &major, &minor)) {
        DF_LOGE("read hello failed");
        return false;
    }
    DF_LOGI("hello received: name='%{public}s' len=%{public}zu ver=%{public}d.%{public}d",
        protocolName.c_str(), protocolName.size(), major, minor);
    if (protocolName.size() != kHelloNameLength || major != kProtocolMajorVersion) {
        std::ostringstream oss;
        oss << "protocol mismatch: name='" << protocolName << "' ver=" << major << "." << minor;
        DF_LOGE("protocol mismatch: name='%{public}s' ver=%{public}d.%{public}d",
            protocolName.c_str(), major, minor);
        setStatus(oss.str());
        return false;
    }
    int16_t helloBackMinor = (minor < kProtocolMinorVersion) ? minor : kProtocolMinorVersion;

    // Client -> Server: framed HelloBack = name(7) + "%2i%2i%s" (name = screenName)
    std::vector<uint8_t> out;
    ProtoUtil::writef(out, kMsgHelloBack, &protocolName, kProtocolMajorVersion, helloBackMinor,
        &m_screenName);
    if (!m_stream.writeFrame(out.data(), out.size())) {
        DF_LOGE("write hello back failed");
        return false;
    }
    DF_LOGI("hello back sent: %{public}zu bytes, name='%{public}s'", out.size(), m_screenName.c_str());
    return true;
}

bool DeskflowClient::handleMessage(const std::string& key)
{
    bool handled = true;
    if (key == kMsgQInfo) {
        // QINF -> respond DINF: x, y, w, h, 0, mx, my
        std::vector<uint8_t> out;
        ProtoUtil::writef(out, kMsgDInfo, 0, 0, m_screenW, m_screenH, 0, m_pointerX.load(),
            m_pointerY.load());
        bool ok = m_stream.writeFrame(out.data(), out.size());
        DF_LOGI("QINF -> DINF sent=%{public}d (%{public}dx%{public}d)", ok ? 1 : 0, m_screenW, m_screenH);
        return ok;
    }
    if (key == kMsgCKeepAlive) {
        bool ok = m_stream.writeFrame(reinterpret_cast<const uint8_t*>("CALV"), 4);
        DF_LOGI("CALV heartbeat replied=%{public}d", ok ? 1 : 0);
        return ok;
    }
    if (key == "CINN") {
        int16_t x = 0, y = 0;
        uint32_t seq = 0;
        int16_t mask = 0;
        if (!ProtoUtil::readf(m_stream, kMsgCEnter + 4, &x, &y, &seq, &mask)) {
            DF_LOGE("CINN: readf failed (bufRemain=%{public}zu)", m_stream.bufferRemaining());
            return false;
        }
        DF_LOGI("CINN: x=%{public}d y=%{public}d seq=%{public}u mask=0x%{public}x", x, y, seq, mask);
        m_active = true;
        m_seqNum = seq;
        // 规格：x=-1 且 y=-1 表示仅切换修饰键、不移动指针
        if (x >= 0 && y >= 0) {
            m_pointerX = x;
            m_pointerY = y;
            injectMouseMove(x, y);
        }
        syncModifiers(static_cast<uint32_t>(mask));
        setStatus("active: control acquired, pointer at " + std::to_string(x) + "," + std::to_string(y));
        return true;
    }
    if (key == "COUT") {
        m_active = false;
        // 离开屏幕：清空按钮按下状态（防止粘滞按键/按钮）
        for (auto& [buttonId, down] : m_mouseButtonsDown) {
            if (down) {
                injectMouseButton(buttonId, false);
            }
        }
        m_mouseButtonsDown.clear();
        setStatus("inactive: moved back to primary screen");
        return true;
    }
    if (key == kMsgCClose) {
        setStatus("closed by server");
        return false;
    }
    if (key == "DMMV") {
        int16_t x = 0, y = 0;
        if (!ProtoUtil::readf(m_stream, "%2i%2i", &x, &y)) {
            return false;
        }
        m_pointerX = x;
        m_pointerY = y;
        injectMouseMove(x, y);
        return true;
    }
    if (key == "DMRM") {
        int16_t dx = 0, dy = 0;
        if (!ProtoUtil::readf(m_stream, "%2i%2i", &dx, &dy)) {
            return false;
        }
        int32_t x = m_pointerX.load() + dx;
        int32_t y = m_pointerY.load() + dy;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= m_screenW) x = m_screenW - 1;
        if (y >= m_screenH) y = m_screenH - 1;
        m_pointerX = x;
        m_pointerY = y;
        injectMouseMove(x, y);
        return true;
    }
    if (key == "DMDN") {
        uint8_t button = 0;
        if (!ProtoUtil::readf(m_stream, "%1i", &button)) {
            return false;
        }
        injectMouseButton(button, true);
        return true;
    }
    if (key == "DMUP") {
        uint8_t button = 0;
        if (!ProtoUtil::readf(m_stream, "%1i", &button)) {
            return false;
        }
        injectMouseButton(button, false);
        return true;
    }
    if (key == "DMWM") {
        int16_t xDelta = 0, yDelta = 0;
        if (!ProtoUtil::readf(m_stream, "%2i%2i", &xDelta, &yDelta)) {
            return false;
        }
        injectMouseWheel(xDelta, yDelta);
        return true;
    }
    if (key == "DKDN") {
        int16_t id = 0, mask = 0, button = 0;
        if (!ProtoUtil::readf(m_stream, kMsgDKeyDown + 4, &id, &mask, &button)) {
            return false;
        }
        syncModifiers(static_cast<uint32_t>(mask));
        uint32_t k = static_cast<uint32_t>(id);
        if (!m_keyStates[k]) {
            injectKeyEvent(k, true);
            m_keyStates[k] = true;
        }
        return true;
    }
    if (key == "DKUP") {
        int16_t id = 0, mask = 0, button = 0;
        if (!ProtoUtil::readf(m_stream, kMsgDKeyUp + 4, &id, &mask, &button)) {
            return false;
        }
        syncModifiers(static_cast<uint32_t>(mask));
        uint32_t k = static_cast<uint32_t>(id);
        if (m_keyStates[k]) {
            injectKeyEvent(k, false);
            m_keyStates[k] = false;
        }
        return true;
    }
    if (key == "DKRP") {
        int16_t id = 0, mask = 0, count = 0, button = 0;
        std::string lang;
        if (!ProtoUtil::readf(m_stream, kMsgDKeyRepeat + 4, &id, &mask, &count, &button, &lang)) {
            return false;
        }
        syncModifiers(static_cast<uint32_t>(mask));
        uint32_t k = static_cast<uint32_t>(id);
        if (!m_keyStates[k]) {
            injectKeyEvent(k, true);
            m_keyStates[k] = true;
        }
        return true;
    }
    if (key == "DKDL") {
        int16_t id = 0, mask = 0, button = 0;
        std::string lang;
        if (!ProtoUtil::readf(m_stream, kMsgDKeyDownLang + 4, &id, &mask, &button, &lang)) {
            return false;
        }
        syncModifiers(static_cast<uint32_t>(mask));
        uint32_t k = static_cast<uint32_t>(id);
        if (!m_keyStates[k]) {
            injectKeyEvent(k, true);
            m_keyStates[k] = true;
        }
        return true;
    }

    // ---- ignored messages (consume payload to keep stream in sync) ----
    if (key == kMsgCNoop) {
        return true;
    }
    if (key == kMsgCResetOptions) {
        return true;
    }
    if (key == kMsgCInfoAck) {
        DF_LOGI("CIAK received (info ack)");
        return true;
    }
    if (key == "CSEC") {
        uint8_t on = 0;
        ProtoUtil::readf(m_stream, kMsgCScreenSaver + 4, &on);
        return true;
    }
    if (key == "CCLP") {
        uint8_t id = 0;
        uint32_t seq = 0;
        ProtoUtil::readf(m_stream, kMsgCClipboard + 4, &id, &seq);
        return true;
    }
    if (key == "DSOP") {
        std::vector<uint32_t> options;
        ProtoUtil::readf(m_stream, kMsgDSetOptions + 4, &options);
        // DSOP 是握手完成的标志：此后进入运行期（每条消息后回 CNOP）
        m_handshakeDone = true;
        DF_LOGI("DSOP received, handshake complete, entering runtime");
        return true;
    }
    if (key == "DCLP") {
        uint8_t id = 0;
        uint32_t seq = 0;
        uint8_t mark = 0;
        std::string data;
        ProtoUtil::readf(m_stream, kMsgDClipboard + 4, &id, &seq, &mark, &data);
        return true;
    }
    if (key == "DFTR") {
        uint8_t mark = 0;
        std::string data;
        ProtoUtil::readf(m_stream, kMsgDFileTransfer + 4, &mark, &data);
        return true;
    }
    if (key == "DDRG") {
        int16_t fileCount = 0;
        std::string info;
        ProtoUtil::readf(m_stream, kMsgDDragInfo + 4, &fileCount, &info);
        return true;
    }
    if (key == "LSYN") {
        std::string data;
        if (!ProtoUtil::readf(m_stream, kMsgDLanguageSynchronisation + 4, &data)) {
            return false;
        }
        DF_LOGI("LSYN ignored (lang data %{public}zu bytes)", data.size());
        return true;
    }
    if (key == "SECN") {
        std::string data;
        ProtoUtil::readf(m_stream, kMsgDSecureInputNotification + 4, &data);
        return true;
    }

    // ---- errors ----
    if (key == "EICV") {
        int16_t a = 0, b = 0;
        ProtoUtil::readf(m_stream, kMsgEIncompatible + 4, &a, &b);
        setStatus("error: incompatible version, server wants " + std::to_string(a) + "." + std::to_string(b));
        return false;
    }
    if (key == kMsgEBusy) {
        setStatus("error: server busy");
        return false;
    }
    if (key == kMsgEUnknown) {
        setStatus("error: unknown client");
        return false;
    }
    if (key == kMsgEBad) {
        setStatus("error: protocol violation");
        return false;
    }

    setStatus("error: unknown message '" + key + "'");
    return false;
}

// ---------- modifier key synchronization ----------

// mask bit -> keysym of the modifier key to inject
uint32_t DeskflowClient::maskModifierKeysym(uint32_t maskBit) const
{
    switch (maskBit) {
        case kKeyModifierShift:   return kKeyShiftL;
        case kKeyModifierControl: return kKeyControlL;
        case kKeyModifierAlt:     return kKeyAltL;
        case kKeyModifierMeta:    return kKeyMetaL;
        case kKeyModifierSuper:   return kKeySuperL;
        case kKeyModifierAltGr:   return kKeyAltR;
        default: return kKeyNone;
    }
}

// 使本地修饰键按下状态与协议 mask 一致：
// mask 置位但未按下的 -> 注入按下；mask 未置位但按下的 -> 注入抬起
void DeskflowClient::syncModifiers(uint32_t mask)
{
    static const uint32_t kModBits[] = {
        kKeyModifierShift, kKeyModifierControl, kKeyModifierAlt,
        kKeyModifierMeta, kKeyModifierSuper, kKeyModifierAltGr
    };
    for (uint32_t bit : kModBits) {
        uint32_t keysym = maskModifierKeysym(bit);
        if (keysym == kKeyNone) {
            continue;
        }
        bool wantDown = (mask & bit) != 0;
        bool isDown = m_keyStates.count(keysym) > 0 && m_keyStates[keysym];
        if (wantDown && !isDown) {
            injectKeyEvent(keysym, true);
            m_keyStates[keysym] = true;
        } else if (!wantDown && isDown) {
            injectKeyEvent(keysym, false);
            m_keyStates[keysym] = false;
        }
    }
}

// ---------- injection helpers ----------

bool DeskflowClient::injectMouseMove(int32_t x, int32_t y)
{
    struct Input_MouseEvent* ev = OH_Input_CreateMouseEvent();
    if (ev == nullptr) {
        return false;
    }
    OH_Input_SetMouseEventAction(ev, MOUSE_ACTION_MOVE);
    OH_Input_SetMouseEventDisplayX(ev, x);
    OH_Input_SetMouseEventDisplayY(ev, y);
    OH_Input_SetMouseEventGlobalX(ev, x);
    OH_Input_SetMouseEventGlobalY(ev, y);
    // 拖动：MOVE 事件必须携带当前按下的按钮（真实事件流如此，devecocli 抓包确认）
    for (auto& [buttonId, down] : m_mouseButtonsDown) {
        if (down) {
            int32_t harmony = buttonToHarmony(buttonId);
            if (harmony >= 0) {
                OH_Input_SetMouseEventButton(ev, harmony);
                break;
            }
        }
    }
    // actionTime 传 -1：让系统使用 GetSysClockTime()（与真实事件同源的时间基准）。
    // 之前传 Unix 毫秒时间戳（~1.75e12），与系统时钟（~1e10）量级错乱，
    // 导致手势识别（按住时长/downTime 比较）失效 —— 拖动/长按永不触发
    OH_Input_SetMouseEventActionTime(ev, -1);
    int32_t ret = OH_Input_InjectMouseEventGlobal(ev);
    OH_Input_DestroyMouseEvent(&ev);
    if (ret != INPUT_SUCCESS) {
        DF_LOGE("injectMouseMove(%{public}d,%{public}d) failed ret=%{public}d", x, y, ret);
    }
    return ret == INPUT_SUCCESS;
}

bool DeskflowClient::injectMouseButton(int32_t buttonId, bool down)
{
    int32_t harmony = buttonToHarmony(buttonId);
    if (harmony < 0) {
        // wheel buttons 4/5 handled via axis injection
        if (buttonId == kButtonWheelUp || buttonId == kButtonWheelDown) {
            return injectMouseWheel(0, buttonId == kButtonWheelUp ? 1 : -1);
        }
        return false;
    }
    m_mouseButtonsDown[buttonId] = down;
    int32_t x = m_pointerX.load();
    int32_t y = m_pointerY.load();
    struct Input_MouseEvent* ev = OH_Input_CreateMouseEvent();
    if (ev == nullptr) {
        return false;
    }
    OH_Input_SetMouseEventAction(ev, down ? MOUSE_ACTION_BUTTON_DOWN : MOUSE_ACTION_BUTTON_UP);
    OH_Input_SetMouseEventButton(ev, harmony);
    OH_Input_SetMouseEventDisplayX(ev, x);
    OH_Input_SetMouseEventDisplayY(ev, y);
    OH_Input_SetMouseEventGlobalX(ev, x);
    OH_Input_SetMouseEventGlobalY(ev, y);
    // actionTime 传 -1：使用系统时钟（GetSysClockTime），与真实事件时间基准一致
    OH_Input_SetMouseEventActionTime(ev, -1);
    int32_t ret = OH_Input_InjectMouseEventGlobal(ev);
    OH_Input_DestroyMouseEvent(&ev);
    if (ret != INPUT_SUCCESS) {
        DF_LOGE("injectMouseButton(%{public}d,%{public}d) failed ret=%{public}d", buttonId, down ? 1 : 0, ret);
    }
    return ret == INPUT_SUCCESS;
}

// 触摸事件注入已移除：devecocli 抓包证明成功拖动是纯 MOUSE 事件流
// （BUTTON_DOWN -> MOVE(button=LEFT) -> BUTTON_UP，actionTime 由系统时钟生成）

bool DeskflowClient::injectMouseWheel(int32_t xDelta, int32_t yDelta)
{
    if (yDelta == 0 && xDelta == 0) {
        return true;
    }
    int32_t x = m_pointerX.load();
    int32_t y = m_pointerY.load();
    int32_t axisType = (yDelta != 0) ? MOUSE_AXIS_SCROLL_VERTICAL : MOUSE_AXIS_SCROLL_HORIZONTAL;
    float axisValue = (yDelta != 0) ? static_cast<float>(yDelta) : static_cast<float>(xDelta);
    // 滚轮需要完整 AXIS_BEGIN -> AXIS_UPDATE -> AXIS_END 序列（官方 Action 枚举 4/5/6），
    // 只发 UPDATE 系统不识别
    const int32_t actions[3] = { MOUSE_ACTION_AXIS_BEGIN, MOUSE_ACTION_AXIS_UPDATE, MOUSE_ACTION_AXIS_END };
    for (int32_t action : actions) {
        struct Input_MouseEvent* ev = OH_Input_CreateMouseEvent();
        if (ev == nullptr) {
            return false;
        }
        OH_Input_SetMouseEventAction(ev, action);
        OH_Input_SetMouseEventAxisType(ev, axisType);
        OH_Input_SetMouseEventAxisValue(ev, axisValue);
        OH_Input_SetMouseEventDisplayX(ev, x);
        OH_Input_SetMouseEventDisplayY(ev, y);
        OH_Input_SetMouseEventGlobalX(ev, x);
        OH_Input_SetMouseEventGlobalY(ev, y);
        OH_Input_SetMouseEventActionTime(ev, -1);
        int32_t ret = OH_Input_InjectMouseEventGlobal(ev);
        OH_Input_DestroyMouseEvent(&ev);
        if (ret != INPUT_SUCCESS) {
            DF_LOGE("injectMouseWheel(action=%{public}d) failed ret=%{public}d", action, ret);
            return false;
        }
    }
    return true;
}

bool DeskflowClient::injectKeyEvent(uint32_t keysym, bool down)
{
    int32_t keyCode = keysymToKeycode(keysym);
    if (keyCode < 0) {
        return false;
    }
    struct Input_KeyEvent* ke = OH_Input_CreateKeyEvent();
    if (ke == nullptr) {
        return false;
    }
    OH_Input_SetKeyEventAction(ke, down ? KEY_ACTION_DOWN : KEY_ACTION_UP);
    OH_Input_SetKeyEventKeyCode(ke, keyCode);
    OH_Input_SetKeyEventActionTime(ke, -1);
    int32_t ret = OH_Input_InjectKeyEvent(ke);
    OH_Input_DestroyKeyEvent(&ke);
    if (ret != INPUT_SUCCESS) {
        DF_LOGE("injectKeyEvent(keysym=0x%{public}x,%{public}d) failed ret=%{public}d", keysym, down ? 1 : 0, ret);
    }
    return ret == INPUT_SUCCESS;
}

} // namespace dfpoc
