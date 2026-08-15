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

    DeskflowClient();
    ~DeskflowClient();

    // Set callback invoked from worker thread with status strings
    void setStatusCallback(StatusCallback cb) { m_statusCb = cb; }

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
