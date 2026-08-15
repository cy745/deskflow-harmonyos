/*
 * DeskflowPoC -- HarmonyOS Deskflow protocol client
 * Client state machine: connect, handshake, message loop, input injection
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "socket_stream.h"

namespace dfpoc {

// Keyboard modifier mask (protocol KeyModifierMask bits)
constexpr uint32_t kKeyModifierShift   = 0x0001;
constexpr uint32_t kKeyModifierControl = 0x0002;
constexpr uint32_t kKeyModifierAlt     = 0x0004;
constexpr uint32_t kKeyModifierMeta    = 0x0008;
constexpr uint32_t kKeyModifierSuper   = 0x0010;
constexpr uint32_t kKeyModifierAltGr   = 0x0020;
constexpr uint32_t kKeyModifierCapsLock = 0x1000;
constexpr uint32_t kKeyModifierNumLock  = 0x2000;
constexpr uint32_t kKeyModifierScrollLock = 0x4000;

class DeskflowClient
{
public:
    using StatusCallback = std::function<void(const std::string&)>;
    using ClipboardCallback = std::function<void(const std::string&)>;

    DeskflowClient();
    ~DeskflowClient();

    // Set callback invoked from worker thread with status strings
    void setStatusCallback(StatusCallback cb) { m_statusCb = cb; }

    // Set callback invoked when remote clipboard text arrives (worker thread)
    void setClipboardCallback(ClipboardCallback cb) { m_clipboardCb = cb; }

    // Enable/disable clipboard sync (thread-safe; read by protocol thread)
    void setClipboardSync(bool enable) { m_clipboardSync = enable; }
    bool clipboardSync() const { return m_clipboardSync.load(); }

    // Start connection in a background thread. Returns false if already running.
    bool start(const std::string& host, uint16_t port, const std::string& screenName);

    // Set local screen size in vp units (reported via DINF, coordinate space for DMMV)
    void setScreenSize(int32_t w, int32_t h)
    {
        m_screenW = w;
        m_screenH = h;
    }

    // 反转滚轮方向：true 时注入的垂直滚轮取反（配合系统滚轮方向偏好）。
    void setInvertScroll(bool invert) { m_invertScroll = invert; }

    // Request stop and disconnect (non-blocking; worker thread detaches)
    void stop();

    bool isRunning() const { return m_running.load(); }

    // 自动重连策略：连接意外断开后自动重连，直到成功或收到不可重连指示/stop。
    void setAutoReconnect(bool enable, int32_t intervalMs = 3000)
    {
        m_autoReconnect = enable;
        m_reconnectIntervalMs = intervalMs;
    }

    // 把本地剪贴板文本推送到对端（若当前处于运行期 & 允许剪贴板同步）。ArkTS 调用。
    void pushClipboard(const std::string& text);

    // 主动向对端声明剪贴板所有权（CCLP，上传方向用）。进入屏幕时勿调用——
    // 服务端在 CINN 后自动下推 DCLP；CCLP 会让服务端清空本端 dirty 从而抑制下推。
    bool grabClipboard();

    // 保留：接收方向无需主动请求（服务端自动下推 DCLP），此方法为 no-op。
    void requestClipboard();

private:
    void run();                              // worker thread: (re)connect loop
    bool runOnce();                          // one connect→handshake→message-loop lifecycle
    bool handshake();                        // Barrier Hello/HelloBack exchange
    bool handleMessage(const std::string& key);  // dispatch one 4-byte protocol message key
    void setStatus(const std::string& s);    // notify status callback (worker thread)

    // injection helpers (verified against HarmonyOS 6.1)
    bool injectMouseMove(int32_t x, int32_t y);
    bool injectMouseButton(int32_t buttonId, bool down);
    bool injectMouseWheel(int32_t xDelta, int32_t yDelta);
    bool injectKeyEvent(uint32_t keysym, bool down);

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    std::thread m_thread;
    SocketStream m_stream;
    StatusCallback m_statusCb;

    // 自动重连
    std::atomic<bool> m_autoReconnect{true};
    std::atomic<int32_t> m_reconnectIntervalMs{3000};
    // 是否反转滚轮方向（垂直滚轮取反）
    std::atomic<bool> m_invertScroll{false};
    // 本次断开是否应重连：连接丢失/协议错误 → true（默认）；
    // 收到 CBYE（服务端主动关闭）或 EICV（版本不兼容）→ false（不重连）
    std::atomic<bool> m_shouldReconnect{true};

    std::string m_host;
    uint16_t m_port;
    std::string m_screenName;

    // screen state (vp units)
    int32_t m_screenW = 1280;
    int32_t m_screenH = 800;
    // last known absolute pointer position
    std::atomic<int32_t> m_pointerX{0};
    std::atomic<int32_t> m_pointerY{0};
    bool m_active = false;
    bool m_handshakeDone = false;   // 收到 DSOP 后进入运行期（此后每条消息回 CNOP）
    uint32_t m_seqNum = 0;          // 最近 CINN 的会话序号（剪贴板用）
    // 剪贴板同步开关（ArkTS 开关控制）
    std::atomic<bool> m_clipboardSync{false};
    // 远程剪贴板文本回调（worker → JS，经 NAPI tsfn 投递）——用下面的 mutex 保护
    ClipboardCallback m_clipboardCb;
    // 剪贴板内容 id / seq（barrier 用区分与失效判定）
    uint32_t m_clipboardSeq = 1;
    // 上一份推送/接收到的剪贴板文本（用于忽略本机回环）
    std::string m_lastClipboardText;
    // 保护 m_clipboardCb / m_clipboardSeq / m_lastClipboardText
    std::mutex m_clipboardMutex;
    // 发送 DCLP 给对端（id, seq, mark, text）
    bool sendClipboard(const std::string& text);

    // ---- 接收侧剪贴板分块组装状态（DCLP: mark=1 size/2 data/3 end）----
    struct ClipChunkState {
        bool active = false;          // 分块传输进行中
        std::string buffer;           // 已收到的数据拼装缓冲
        size_t expectedSize = 0;      // 声明大小
        uint32_t clipSeq = 0;         // 本次分块的 seq
    };
    ClipChunkState m_clipChunk;       // 跨消息保留状态（仅 worker 线程访问，无需锁）

    // 剪贴板容器（marshalled）与纯文本互转（IClipboard marshall/unmarshall 格式）
    // marshall: 4B 格式数 + 每个格式 [4B formID + 4B size + data]（大端）
    static std::string marshalText(const std::string& utf8Text);
    static std::string unmarshalText(const std::string& container);  // 返回 format 0(Text) 的负载
    // 按键状态：keysym -> 是否按下（用于修饰键同步与按键去重）
    std::map<uint32_t, bool> m_keyStates;
    // 鼠标按钮按下状态：protocol ButtonID -> 是否按下
    // MOVE 注入必须携带当前按下的按钮（真实事件流如此，devecocli 抓包确认）
    std::map<int32_t, bool> m_mouseButtonsDown;
    // 同步修饰键状态到 mask（注入缺失的按下/多余的抬起）
    void syncModifiers(uint32_t mask);
    uint32_t maskModifierKeysym(uint32_t maskBit) const;
};

} // namespace dfpoc
