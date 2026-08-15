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
#include <utility>
#include <algorithm>

#include "hilog/log.h"
#include "multimodalinput/oh_input_manager.h"

namespace dfpoc {

namespace {

constexpr unsigned int DF_LOG_DOMAIN = 0xD002;
constexpr const char* DF_LOG_TAG = "DeskflowPoC";

// 诊断日志开关：编译期宏，1 输出每条消息的 INFO 级 recv 日志（高频，调协议时用），
// 0 仅保留 ERROR / 有限 INFO，避免正常使用时刷屏。
#define DF_VERBOSE_MESSAGE_LOG 0

#define DF_LOGI(...) OH_LOG_Print(LOG_APP, LOG_INFO, DF_LOG_DOMAIN, DF_LOG_TAG, __VA_ARGS__)
#define DF_LOGD(...)                                                     \
    do {                                                                 \
        if (DF_VERBOSE_MESSAGE_LOG) {                                    \
            OH_LOG_Print(LOG_APP, LOG_DEBUG, DF_LOG_DOMAIN, DF_LOG_TAG, __VA_ARGS__); \
        }                                                                \
    } while (0)
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

// Map X11 keysym (deskflow KeyID) to HarmonyOS KeyCode.
// deskflow KeyID: U+E000..U+EFFF for control keys (= X11 keysym - 0x1000),
// printable ASCII are their Unicode codepoints. Values from deskflow KeyTypes.h.
// HarmonyOS KeyCode values from oh_key_code.h. Returns -1 if no equivalent.
int32_t keysymToKeycode(uint32_t keysym)
{
    // deskflow 对 0xEF00~0xEFFF 区间的键以 16 位有符号值传输，到达时可能被符号扩展
    // 成 0xFFFFEFxx。必须掩码回 16 位才能命中下面的常量（例：Tab 0xEF09->0xFFFFEF09）。
    keysym &= 0xFFFF;

    // 可打印 ASCII：字母（大小写）/ 数字 / 空格与符号 -> 对应物理键
    // （Shift 等作为独立修饰键注入，字母可能带大写 keysym，统一映射到同一物理键）
    if ((keysym >= 'a' && keysym <= 'z') || (keysym >= 'A' && keysym <= 'Z')) {
        return KEYCODE_A + static_cast<int32_t>((keysym & 0x1F) - 1); // KEYCODE_A=2017
    }
    if (keysym >= '0' && keysym <= '9') {
        return KEYCODE_0 + static_cast<int32_t>(keysym - '0'); // KEYCODE_0=2000
    }
    switch (keysym) {
        case ' ':       return KEYCODE_SPACE;            // 2050
        case ',':       return KEYCODE_COMMA;            // 2043
        case '.':       return KEYCODE_PERIOD;           // 2044
        case '-':       return KEYCODE_MINUS;            // 2057
        case '=':       return KEYCODE_EQUALS;           // 2058
        case '[':       return KEYCODE_LEFT_BRACKET;     // 2059
        case ']':       return KEYCODE_RIGHT_BRACKET;    // 2060
        case '\\':      return KEYCODE_BACKSLASH;        // 2061
        case ';':       return KEYCODE_SEMICOLON;        // 2062
        case '\'':      return KEYCODE_APOSTROPHE;       // 2063
        case '/':       return KEYCODE_SLASH;            // 2064
        case '`':       return KEYCODE_GRAVE;            // 2056
        case '@':       return KEYCODE_AT;               // 2065
        case '+':       return KEYCODE_PLUS;             // 2066
        case 0x002a:    return KEYCODE_STAR;             // '*' (=2010)
        case 0x0023:    return KEYCODE_POUND;            // '#' (=2011)
    }

    // 光标/编辑键
    switch (keysym) {
        case kKeyBackSpace: return KEYCODE_DEL;          // 2055
        case kKeyTab:       return KEYCODE_TAB;          // 2049
        case kKeyReturn:    return KEYCODE_ENTER;        // 2054
        case kKeyEscape:    return KEYCODE_ESCAPE;       // 2070
        case kKeyHome:      return KEYCODE_MOVE_HOME;    // 2081
        case kKeyEnd:       return KEYCODE_MOVE_END;     // 2082
        case kKeyLeft:      return KEYCODE_DPAD_LEFT;    // 2014
        case kKeyUp:        return KEYCODE_DPAD_UP;      // 2012
        case kKeyRight:     return KEYCODE_DPAD_RIGHT;   // 2015
        case kKeyDown:      return KEYCODE_DPAD_DOWN;    // 2013
        case kKeyPageUp:    return KEYCODE_PAGE_UP;      // 2068
        case kKeyPageDown:  return KEYCODE_PAGE_DOWN;    // 2069
        case kKeyInsert:    return KEYCODE_INSERT;       // 2083
        case kKeyDelete:    return KEYCODE_FORWARD_DEL;  // 2071
        case kKeyClear:     return KEYCODE_MOVE_HOME;    // 用 HOME 近似清屏
        case kKeyLinefeed:  return KEYCODE_LINEFEED;     // 2609
        case kKeyLeftTab:   return KEYCODE_TAB;          // 反向 Tab
        case kKeySelect:    return KEYCODE_FORWARD;      // 2084
        case kKeyExecute:   return KEYCODE_MENU;         // 2067 近似
        case kKeyMenu:      return KEYCODE_MENU;         // 2067
        case kKeyFind:      return KEYCODE_FIND;         // 2623
        case kKeyCancel:    return KEYCODE_CANCEL;       // 2648
        case kKeyHelp:      return KEYCODE_HELP;         // 2625
        case kKeyUndo:      return KEYCODE_UNDO;         // 2619
        case kKeyRedo:      return KEYCODE_REDO;         // 2641
    }

    // 锁定/修饰键
    switch (keysym) {
        case kKeyCapsLock:    return KEYCODE_CAPS_LOCK;      // 2074
        case kKeyNumLock:     return KEYCODE_NUM_LOCK;       // 2102
        case kKeyScrollLock:  return KEYCODE_SCROLL_LOCK;    // 2075
        case kKeyShiftL:      return KEYCODE_SHIFT_LEFT;     // 2047
        case kKeyShiftR:      return KEYCODE_SHIFT_RIGHT;    // 2048
        case kKeyControlL:    return KEYCODE_CTRL_LEFT;      // 2072
        case kKeyControlR:    return KEYCODE_CTRL_RIGHT;     // 2073
        case kKeyAltL:        return KEYCODE_ALT_LEFT;       // 2045
        case kKeyAltR:        return KEYCODE_ALT_RIGHT;      // 2046
        case kKeyAltGr:       return KEYCODE_ALT_RIGHT;      // 用右 Alt 近似
        case kKeyMetaL:       return KEYCODE_META_LEFT;      // 2076
        case kKeyMetaR:       return KEYCODE_META_RIGHT;     // 2077
        case kKeySuperL:      return KEYCODE_META_LEFT;      // Super=Meta
        case kKeySuperR:      return KEYCODE_META_RIGHT;
    }

    // 系统键
    switch (keysym) {
        case kKeyPause:        return KEYCODE_BREAK;         // 2080
        case kKeySysReq:       return KEYCODE_SYSRQ;         // 2079
        case kKeyPrint:        return KEYCODE_PRINT;         // 2645
        case kKeyBreak:        return KEYCODE_BREAK;         // 2080
        case kKeySleep:        return KEYCODE_SLEEP;         // 2600
        case kKeyEject:        return KEYCODE_MEDIA_EJECT;   // 2088
        case kKeyZenkaku:      return KEYCODE_ZENKAKU_HANKAKU; // 2601
        case kKeyKana:         return KEYCODE_KATAKANA;      // 2604
        case kKeyHiraganaKatakana: return KEYCODE_KATAKANA_HIRAGANA; // 2607
        case kKeyHenkan:       return KEYCODE_HENKAN;        // 2606
        case kKeyHangul:       return KEYCODE_HANGUEL;       // 2613
        case kKeyHanja:        return KEYCODE_HANJA;         // 2614
    }

    // 功能键 F1..F35（HarmonyOS 只到 F24=2827）
    if (keysym >= kKeyF1 && keysym <= kKeyF24) {
        return KEYCODE_F1 + static_cast<int32_t>(keysym - kKeyF1); // 2090+
    }

    // 媒体/亮度/应用键（extended 0xE0xx，从 KeyTypes.h）
    switch (keysym) {
        case kKeyAudioMute:     return KEYCODE_VOLUME_MUTE;     // 22
        case kKeyAudioDown:     return KEYCODE_VOLUME_DOWN;       // 17
        case kKeyAudioUp:       return KEYCODE_VOLUME_UP;         // 16
        case kKeyAudioNext:     return KEYCODE_MEDIA_NEXT;        // 12
        case kKeyAudioPrev:     return KEYCODE_MEDIA_PREVIOUS;    // 13
        case kKeyAudioStop:     return KEYCODE_MEDIA_STOP;        // 11
        case kKeyAudioPlay:     return KEYCODE_MEDIA_PLAY_PAUSE;  // 10
        case kKeyAppMail:       return KEYCODE_ENVELOPE;          // 2053
        case kKeyAppMedia:      return KEYCODE_MEDIA_PLAY_PAUSE;  // 10 近似
        case kKeyBrightnessDown: return KEYCODE_BRIGHTNESS_DOWN;  // 41
        case kKeyBrightnessUp:  return KEYCODE_BRIGHTNESS_UP;     // 40
        case kKeyWWWBack:       return KEYCODE_FORWARD;           // 2084 近似
        case kKeyWWWForward:    return KEYCODE_FORWARDMAIL;       // 2654 近似
        case kKeyWWWHome:       return KEYCODE_HOME;              // 1 近似
        case kKeyWWWRefresh:    return KEYCODE_REFRESH;           // 2635
    }

    // 小键盘数字 0-9 与符号（Deskflow KeyID 0xEFB0..=KP_0，映射到 NUMPAD）
    if (keysym >= kKeyKP_0 && keysym <= kKeyKP_9) {
        return KEYCODE_NUMPAD_0 + static_cast<int32_t>(keysym - kKeyKP_0); // 2103+
    }
    switch (keysym) {
        case kKeyKP_Enter:     return KEYCODE_NUMPAD_ENTER;   // 2119
        case kKeyKP_Multiply:  return KEYCODE_NUMPAD_MULTIPLY;// 2114
        case kKeyKP_Add:       return KEYCODE_NUMPAD_ADD;     // 2116
        case kKeyKP_Separator: return KEYCODE_NUMPAD_COMMA;   // 2118
        case kKeyKP_Subtract:  return KEYCODE_NUMPAD_SUBTRACT;// 2115
        case kKeyKP_Decimal:   return KEYCODE_NUMPAD_DOT;     // 2117
        case kKeyKP_Divide:    return KEYCODE_NUMPAD_DIVIDE;  // 2113
        case kKeyKP_Equal:     return KEYCODE_NUMPAD_EQUALS;  // 2120
        case kKeyKP_Home:      return KEYCODE_MOVE_HOME;      // 2081
        case kKeyKP_End:       return KEYCODE_MOVE_END;       // 2082
        case kKeyKP_PageUp:    return KEYCODE_PAGE_UP;        // 2068
        case kKeyKP_PageDown:  return KEYCODE_PAGE_DOWN;      // 2069
        case kKeyKP_Insert:    return KEYCODE_INSERT;         // 2083
        case kKeyKP_Delete:    return KEYCODE_FORWARD_DEL;    // 2071
    }

    return -1;
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

// 把纯文本封装为 IClipboard marshalled 容器（供 DCLP 上传）：
// 4B 格式数(=1) + 4B 格式(Text=0) + 4B 长度 + 文本，全部大端。
std::string DeskflowClient::marshalText(const std::string& utf8Text)
{
    std::string out;
    auto be32 = [&out](uint32_t v) {
        out.push_back(static_cast<char>((v >> 24) & 0xff));
        out.push_back(static_cast<char>((v >> 16) & 0xff));
        out.push_back(static_cast<char>((v >> 8) & 0xff));
        out.push_back(static_cast<char>(v & 0xff));
    };
    be32(1);            // numFormats
    be32(0);            // Format::Text
    be32(static_cast<uint32_t>(utf8Text.size()));
    out += utf8Text;
    return out;
}

// 从 IClipboard marshalled 容器中提取 format 0 (Text) 的负载；无则返回空串。
std::string DeskflowClient::unmarshalText(const std::string& container)
{
    if (container.size() < 4) {
        return "";
    }
    auto rd = [&container](size_t off) -> uint32_t {
        if (off + 4 > container.size()) {
            return 0;
        }
        const auto* p = reinterpret_cast<const unsigned char*>(container.data() + off);
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
    };
    size_t off = 0;
    uint32_t numFormats = rd(off);
    off += 4;
    for (uint32_t i = 0; i < numFormats && off + 8 <= container.size(); ++i) {
        uint32_t format = rd(off);
        off += 4;
        uint32_t size = rd(off);
        off += 4;
        if (off + size > container.size()) {
            break;
        }
        if (format == 0) {   // Format::Text
            return container.substr(off, size);
        }
        off += size;
    }
    return "";
}

// 以分块方式发送 DCLP 给对端（barrier/deskflow: mark=1 Start(ASCII 大小) -> 2 Data -> 3 End）。
// seq 使用当前 CINN 活跃会话序号（与官方客户端一致）。
bool DeskflowClient::sendClipboard(const std::string& text)
{
    if (!m_handshakeDone || text.empty()) {
        return false;
    }
    // 先封装为 marshalled 容器（格式 0 文本），再分块上传
    std::string payload = marshalText(text);   // 4B numFormats + 4B format + 4B size + text
    constexpr uint8_t kDataStart = 1;
    constexpr uint8_t kDataChunk = 2;
    constexpr uint8_t kDataEnd = 3;
    uint8_t id = 0;            // kClipboardClipboard（主剪贴板）
    uint32_t seq = m_seqNum;   // 用活跃 CINN 的会话序号，勿手动递增
    {
        std::lock_guard<std::mutex> lock(m_clipboardMutex);
        m_lastClipboardText = text;
    }
    const size_t kChunk = 512 * 1024;  // 与服务端 StreamChunker 一致（512KB）
    bool ok = true;
    // 1) Start：payload = 期望总大小（ASCI十进制）
    {
        std::vector<uint8_t> start;
        std::string sizeStr = std::to_string(payload.size());
        ProtoUtil::writef(start, kMsgDClipboard, id, seq, kDataStart, &sizeStr);
        ok = ok && m_stream.writeFrame(start.data(), start.size());
    }
    // 2) DataChunk（按 512KB 切块）
    for (size_t off = 0; off < payload.size() && ok; off += kChunk) {
        std::string chunk = payload.substr(off, std::min(kChunk, payload.size() - off));
        std::vector<uint8_t> dc;
        ProtoUtil::writef(dc, kMsgDClipboard, id, seq, kDataChunk, &chunk);
        ok = ok && m_stream.writeFrame(dc.data(), dc.size());
    }
    // 3) End
    {
        std::vector<uint8_t> end;
        std::string empty;
        ProtoUtil::writef(end, kMsgDClipboard, id, seq, kDataEnd, &empty);
        ok = ok && m_stream.writeFrame(end.data(), end.size());
    }
    DF_LOGI("DCLP push id=%{public}u seq=%{public}u text=%{public}zu container=%{public}zu ok=%{public}d",
        id, seq, text.size(), payload.size(), ok ? 1 : 0);
    return ok;
}

// 向对端抓取剪贴板所有权（CCLP id, seq）。仅在上传方向：本机剪贴板变化后、
// 先把所有权声明给自己、再把 DCLP 内容广播出去。官方客户端用活跃 CINN 的 seq。
bool DeskflowClient::grabClipboard()
{
    if (!m_handshakeDone) {
        return false;
    }
    std::vector<uint8_t> out;
    uint8_t id = 0;
    uint32_t seq = m_seqNum;   // 活跃 CINN 的会话序号
    ProtoUtil::writef(out, kMsgCClipboard, id, seq);
    bool ok = m_stream.writeFrame(out.data(), out.size());
    DF_LOGI("CCLP grab id=%{public}u seq=%{public}u ok=%{public}d", id, seq, ok ? 1 : 0);
    return ok;
}

// 把本机剪贴板文本推送/广播给对端（ArkTS 调用；可能来自 worker 或 JS 线程）
// 官方客户端流程：先 CCLP 声明所有权，再分块 DCLP 上传内容。
void DeskflowClient::pushClipboard(const std::string& text)
{
    if (!m_clipboardSync.load() || !m_handshakeDone || text.empty()) {
        return;
    }
    grabClipboard();
    sendClipboard(text);
}

// 保留（不再被 CINN 调用）：进入屏幕时由服务端主动下推 DCLP，客户端无需 CCLP 抓取。
// 若仍想主动刷新一次可用。此方法对接收方向无副作用，但注意 CCLP 会让服务端
// 将本端 m_dirty 清 false 从而抑制下推，故默认不调用。
void DeskflowClient::requestClipboard()
{
    // no-op：见 sendClipboard 与 CINN 处理说明（CCLP 不再用于接收方向）
    if (m_clipboardSync.load()) {
        DF_LOGI("requestClipboard: no-op (server auto-pushes DCLP on CINN)");
    }
}

void DeskflowClient::run()
{
    // 外层循环：负责重连。run() 由 start() 起的 worker 线程执行一次，
    // 内部在断开后按需重连，直到 stop() 被调用或收到不可重连指示。
    while (!m_stopRequested.load()) {
        m_shouldReconnect = true;  // 每次新连接开始时重置

        // 执行一次"连接→握手→消息循环"。返回值为"断开后是否应继续重连"。
        bool shouldContinue = runOnce();

        if (!m_autoReconnect.load() || !shouldContinue || m_stopRequested.load()) {
            break;
        }

        // 退避重连
        setStatus("reconnecting in " + std::to_string(m_reconnectIntervalMs.load()) + "ms");
        DF_LOGI("reconnect in %{public}d ms", m_reconnectIntervalMs.load());
        int32_t waitMs = m_reconnectIntervalMs.load();
        for (int32_t waited = 0; waited < waitMs && !m_stopRequested.load(); waited += 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    m_running = false;
    if (!m_stopRequested.load()) {
        // 循环因不再重连而退出（如收到 CBYE/EICV）
        setStatus("disconnected");
    }
}

// 单次连接生命周期：连接 → 握手 → 消息循环。运行期断开返回 true（可重连），否则 false。
bool DeskflowClient::runOnce()
{
    setStatus("connecting to " + m_host + ":" + std::to_string(m_port));
    DF_LOGI("connecting to %{public}s:%{public}d", m_host.c_str(), m_port);
    if (!m_stream.connect(m_host, m_port, 5000)) {
        DF_LOGE("connect failed");
        setStatus("error: connect failed");
        return true;  // 可重连（例如服务端暂不可达）
    }
    DF_LOGI("tcp connected");
    setStatus("connected, handshake...");
    if (!handshake()) {
        DF_LOGE("handshake failed");
        setStatus("error: handshake failed");
        m_stream.close();
        // 版本不兼容时 handshake() 会把 m_shouldReconnect 置 false
        return m_shouldReconnect.load();
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
        DF_LOGD("recv: %{public}s (frame %{public}zu, bufRemain %{public}zu)",
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
    // 断开后是否重连：未被 stop，且未被置为不重连（CBYE/EICV）
    return !m_stopRequested.load() && m_shouldReconnect.load();
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
        // 协议名/主版本对不上：重连也无效，放弃
        m_shouldReconnect = false;
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
        DF_LOGD("CALV heartbeat replied=%{public}d", ok ? 1 : 0);
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
        // 服务端在收到 CINN 后会自动推送 DCLP 剪贴板（分块：size→data→end）。
        // 注意：此处绝不能发 CCLP 去"抓取"——据 deskflow Server::handleClipboardGrabbed，
        // 收到 CCLP 会把本客户端剪贴板标记为 m_dirty=false 并清空服务端缓存，
        // 从而抑制服务端后续的 DCLP 推送。故进入本机屏幕时静默等待 DCLP 即可。
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
        // 服务端主动说再见（CBYE）：不重连
        m_shouldReconnect = false;
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
        if (!ProtoUtil::readf(m_stream, kMsgCScreenSaver + 4, &on)) {
            return false;
        }
        return true;
    }
    if (key == "CCLP") {
        uint8_t id = 0;
        uint32_t seq = 0;
        if (!ProtoUtil::readf(m_stream, kMsgCClipboard + 4, &id, &seq)) {
            return false;
        }
        return true;
    }
    if (key == "DSOP") {
        std::vector<uint32_t> options;
        if (!ProtoUtil::readf(m_stream, kMsgDSetOptions + 4, &options)) {
            return false;
        }
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
        if (!ProtoUtil::readf(m_stream, kMsgDClipboard + 4, &id, &seq, &mark, &data)) {
            return false;
        }
        // DCLP 是按块传输：mark=1 DataStart(payload=ASCII 期望大小) →
        // mark=2 DataChunk(payload=内容分块) → mark=3 DataEnd(完成)。见 deskflow ClipboardChunk。
        constexpr uint8_t kDataStart = 1;
        constexpr uint8_t kDataChunk = 2;
        constexpr uint8_t kDataEnd = 3;
        if (mark == kDataStart) {
            m_clipChunk.active = true;
            m_clipChunk.buffer.clear();
            m_clipChunk.clipSeq = seq;
            try {
                m_clipChunk.expectedSize = static_cast<size_t>(std::stoull(data));
            } catch (...) {
                m_clipChunk.expectedSize = 0;
            }
            // 大小即内容长度，直接校验上限，避免越界
            if (m_clipChunk.expectedSize > kProtocolMaxStringLength) {
                DF_LOGE("DCLP start size too large: %{public}zu", m_clipChunk.expectedSize);
                m_clipChunk.active = false;
                m_clipChunk.buffer.clear();
            }
            return true;
        }
        if (mark == kDataChunk) {
            if (!m_clipChunk.active) {
                DF_LOGI("DCLP data chunk before start");
                return true;
            }
            m_clipChunk.buffer.append(data);
            return true;
        }
        if (mark == kDataEnd) {
            if (!m_clipChunk.active) {
                return true;
            }
            m_clipChunk.active = false;
            std::string assembled = std::move(m_clipChunk.buffer);
            m_clipChunk.buffer.clear();
            // 反序列化容器，提取纯文本（format 0 / Text）
            std::string text = unmarshalText(assembled);
            if (m_clipboardSync.load() && !text.empty()) {
                ClipboardCallback cb = nullptr;
                {
                    std::lock_guard<std::mutex> lock(m_clipboardMutex);
                    cb = m_clipboardCb;
                    m_lastClipboardText = text;   // 记录，用于忽略后续本机同源回环
                }
                if (cb) {
                    cb(text);
                }
                DF_LOGI("DCLP: assembled %{public}zu bytes, text %{public}zu -> JS", assembled.size(), text.size());
            } else {
                DF_LOGD("DCLP: assembled %{public}zu bytes, text %{public}zu, ignored (sync=%{public}d)",
                    assembled.size(), text.size(), m_clipboardSync.load() ? 1 : 0);
            }
            return true;
        }
        DF_LOGD("DCLP: unknown mark %{public}u", mark);
        return true;
    }
    if (key == "DFTR") {
        uint8_t mark = 0;
        std::string data;
        if (!ProtoUtil::readf(m_stream, kMsgDFileTransfer + 4, &mark, &data)) {
            return false;
        }
        return true;
    }
    if (key == "DDRG") {
        int16_t fileCount = 0;
        std::string info;
        if (!ProtoUtil::readf(m_stream, kMsgDDragInfo + 4, &fileCount, &info)) {
            return false;
        }
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
        if (!ProtoUtil::readf(m_stream, kMsgDSecureInputNotification + 4, &data)) {
            return false;
        }
        return true;
    }

    // ---- errors ----
    if (key == "EICV") {
        int16_t a = 0, b = 0;
        if (!ProtoUtil::readf(m_stream, kMsgEIncompatible + 4, &a, &b)) {
            return false;
        }
        // 版本不兼容：重连也无效，放弃
        m_shouldReconnect = false;
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
    // 反转滚轮方向：垂直滚轮取反（作用于 yDelta）
    if (m_invertScroll.load() && yDelta != 0) {
        yDelta = -yDelta;
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
        DF_LOGI("injectKeyEvent: unmapped keysym=0x%{public}x", keysym);
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
    DF_LOGI("injectKeyEvent keysym=0x%{public}x keyCode=%{public}d %{public}s ret=%{public}d",
        keysym, keyCode, down ? "DOWN" : "UP", ret);
    return ret == INPUT_SUCCESS;
}

} // namespace dfpoc
