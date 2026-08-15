/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string>
#include <sstream>
#include <map>
#include <mutex>
#include <chrono>
#include <thread>
#include <algorithm>
#include "napi/native_api.h"
#include "hilog/log.h"
#include "multimodalinput/oh_input_manager.h"
#include "protocol/deskflow_client.h"

// ===== Deskflow 协议客户端桥接 =====
static constexpr unsigned int DF_LOG_DOMAIN = 0xD002;
static constexpr const char* DF_LOG_TAG = "DeskflowPoC";
#define DF_LOGI(...) OH_LOG_Print(LOG_APP, LOG_INFO, DF_LOG_DOMAIN, DF_LOG_TAG, __VA_ARGS__)
#define DF_LOGW(...) OH_LOG_Print(LOG_APP, LOG_WARN, DF_LOG_DOMAIN, DF_LOG_TAG, __VA_ARGS__)
#define DF_LOGE(...) OH_LOG_Print(LOG_APP, LOG_ERROR, DF_LOG_DOMAIN, DF_LOG_TAG, __VA_ARGS__)

static dfpoc::DeskflowClient g_deskflowClient;
static napi_threadsafe_function g_statusTsfn = nullptr;
static napi_threadsafe_function g_clipboardTsfn = nullptr;

// 保护 g_statusTsfn / g_tsfn 跨线程访问的互斥锁
// （worker 线程/MMI 回调线程 与 JS 线程并发）
static std::mutex g_tsfnMutex;

void DeskflowStatusCallJs(napi_env env, napi_value js_cb, void* context, void* data)
{
    std::string* value = static_cast<std::string*>(data);
    napi_value event;
    napi_create_object(env, &event);
    napi_value value_js;
    napi_create_string_utf8(env, value->c_str(), value->length(), &value_js);
    napi_set_named_property(env, event, "value", value_js);
    napi_value global;
    napi_get_global(env, &global);
    napi_value ret;
    napi_call_function(env, global, js_cb, 1, &event, &ret);
    delete value;
}

// 由 DeskflowClient 工作线程调用：投递状态到 JS 线程
// 注意：必须用 nonblocking —— 若 JS 线程繁忙（例如正在处理注入事件引发的
// 窗口焦点/生命周期回调），blocking 模式会让协议消息循环线程永久卡死，
// 导致服务端写失败并拉回鼠标。
// 并发安全：对 g_statusTsfn 的读写加锁，避免 worker 线程与 JS 线程竞态。
void DeskflowStatusCb(const std::string& status)
{
    napi_threadsafe_function tsfn = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_tsfnMutex);
        tsfn = g_statusTsfn;
    }
    if (tsfn == nullptr) {
        DF_LOGW("DeskflowStatusCb: status callback not registered yet, drop '%{public}s'", status.c_str());
        return;
    }
    napi_call_threadsafe_function(tsfn, new std::string(status), napi_tsfn_nonblocking);
}

static napi_value OnDeskflowStatus(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    std::lock_guard<std::mutex> lock(g_tsfnMutex);
    if (g_statusTsfn != nullptr) {
        // ArkTS 侧通常只在 aboutToAppear 注册一次；若重复注册，先收队释放旧的
        napi_release_threadsafe_function(g_statusTsfn, napi_tsfn_release);
        g_statusTsfn = nullptr;
    }
    // Harmony/OpenHarmony 的 napi_create_threadsafe_function 要求 async_resource_name 为合法
    // 字符串值（不能传 NULL），否则返回 napi_invalid_arg（status=1），导致状态回调永远发不到 JS。
    napi_value resourceName = nullptr;
    std::string resName = "deskflow.status";
    napi_create_string_utf8(env, resName.c_str(), resName.size(), &resourceName);
    napi_create_threadsafe_function(env, args[0], nullptr, resourceName, 0, 1,
        nullptr, nullptr, nullptr, DeskflowStatusCallJs, &g_statusTsfn);
    return nullptr;
}

// ===== 剪贴板同步桥接 =====
// worker 线程收到对端 DCLP 剪贴板文本时，投递到 JS（供 ArkTS 写入系统剪贴板）。
void ClipboardDataCallJs(napi_env env, napi_value js_cb, void* context, void* data)
{
    std::string* value = static_cast<std::string*>(data);
    napi_value event;
    napi_create_object(env, &event);
    napi_value value_js;
    napi_create_string_utf8(env, value->c_str(), value->length(), &value_js);
    napi_set_named_property(env, event, "value", value_js);
    napi_value global;
    napi_get_global(env, &global);
    napi_value ret;
    napi_call_function(env, global, js_cb, 1, &event, &ret);
    delete value;
}

// 注册 onClipboardData 回调：收到远程剪贴板文本时在 JS 线程触发
static napi_value OnClipboardData(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    std::lock_guard<std::mutex> lock(g_tsfnMutex);
    if (g_clipboardTsfn != nullptr) {
        napi_release_threadsafe_function(g_clipboardTsfn, napi_tsfn_release);
        g_clipboardTsfn = nullptr;
    }
    napi_value resourceName = nullptr;
    std::string resName = "deskflow.clipboard";
    napi_create_string_utf8(env, resName.c_str(), resName.size(), &resourceName);
    napi_create_threadsafe_function(env, args[0], nullptr, resourceName, 0, 1, nullptr, nullptr, nullptr,
        ClipboardDataCallJs, &g_clipboardTsfn);
    return nullptr;
}

// 服务端剪贴板文本到达（worker 线程）——转发到 JS
static void PublishClipboard(const std::string& text)
{
    napi_threadsafe_function tsfn = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_tsfnMutex);
        tsfn = g_clipboardTsfn;
    }
    if (tsfn == nullptr) {
        return;
    }
    napi_call_threadsafe_function(tsfn, new std::string(text), napi_tsfn_nonblocking);
}

// setClipboardCallback + pushClipboard 在建立连接时由 NAPI 绑定：
// 我们用 DeskflowClipboardCb 绑定到 client 的 clipboardCallback。
static void DeskflowClipboardCb(const std::string& text)
{
    if (text.empty()) {
        return;
    }
    PublishClipboard(text);
}

// pushClipboard(text): 把本机剪贴板文本推送到对端（ArkTS 调用）
static napi_value PushClipboard(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    char buf[1024 * 1024] = {0};
    size_t len = 0;
    napi_status st = napi_get_value_string_utf8(env, args[0], buf, sizeof(buf), &len);
    if (st != napi_ok) {
        DF_LOGE("PushClipboard: read string fail %{public}d", (int)st);
    }
    std::string text(buf, len);
    g_deskflowClient.pushClipboard(text);
    napi_value result;
    napi_create_string_utf8(env, "ok", 2, &result);
    return result;
}

// setClipboardSync(enable): 开启/关闭剪贴板同步
static napi_value SetClipboardSync(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    bool enable = false;
    napi_get_value_bool(env, args[0], &enable);
    g_deskflowClient.setClipboardSync(enable);
    napi_value result;
    napi_create_string_utf8(env, enable ? "clipboard sync ON" : "clipboard sync OFF",
        enable ? 17 : 18, &result);
    return result;
}

// connectDeskflow(host, port, name, screenW, screenH): 后台线程连接 Deskflow server
static napi_value ConnectDeskflow(napi_env env, napi_callback_info info)
{
    size_t argc = 5;
    napi_value args[5];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    char hostBuf[256] = {0};
    size_t hostLen = 0;
    napi_get_value_string_utf8(env, args[0], hostBuf, sizeof(hostBuf), &hostLen);
    int32_t port = 24800;
    napi_get_value_int32(env, args[1], &port);
    char nameBuf[256] = {0};
    size_t nameLen = 0;
    napi_get_value_string_utf8(env, args[2], nameBuf, sizeof(nameBuf), &nameLen);
    int32_t screenW = 1280;
    int32_t screenH = 800;
    napi_get_value_int32(env, args[3], &screenW);
    napi_get_value_int32(env, args[4], &screenH);

    g_deskflowClient.stop();
    // 等待旧线程退出（最多 1.5 秒），避免 start() 因 m_running 仍为 true 而失败
    for (int i = 0; i < 15 && g_deskflowClient.isRunning(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    DF_LOGI("connectDeskflow: old running=%{public}d, target %{public}s:%{public}d screen %{public}dx%{public}d",
        g_deskflowClient.isRunning() ? 1 : 0, hostBuf, port, screenW, screenH);
    if (g_deskflowClient.isRunning()) {
        // 旧 worker 1.5s 仍未能退出（例如卡在 connect 超时）。不硬启新线程，
        // 明确提示用户稍后重试，避免静默覆盖/误导。
        std::string msg = "previous client is still stopping, please retry in a moment";
        DF_LOGE("%{public}s", msg.c_str());
        napi_value result;
        napi_create_string_utf8(env, msg.c_str(), msg.size(), &result);
        return result;
    }
    g_deskflowClient.setStatusCallback(DeskflowStatusCb);
    g_deskflowClient.setClipboardCallback(DeskflowClipboardCb);
    g_deskflowClient.setScreenSize(screenW, screenH);
    // 是否自动重连由桌面侧通过 setAutoReconnect 控制（默认开启，这里不覆盖）
    bool started = g_deskflowClient.start(hostBuf, static_cast<uint16_t>(port), nameBuf);
    DF_LOGI("connectDeskflow: started=%{public}d", started ? 1 : 0);

    std::string message = started ? "client starting: " + std::string(hostBuf) + ":" + std::to_string(port)
                                  : "client already running";
    napi_value result;
    napi_create_string_utf8(env, message.c_str(), message.size(), &result);
    return result;
}

// disconnectDeskflow(): 断开连接
static napi_value DisconnectDeskflow(napi_env env, napi_callback_info info)
{
    g_deskflowClient.stop();
    std::string message = "disconnect requested";
    napi_value result;
    napi_create_string_utf8(env, message.c_str(), message.size(), &result);
    return result;
}

// setInvertScroll(invert): 反转滚轮方向（垂直滚轮取反）
static napi_value SetInvertScroll(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    bool invert = false;
    napi_get_value_bool(env, args[0], &invert);
    g_deskflowClient.setInvertScroll(invert);
    napi_value result;
    napi_create_string_utf8(env, invert ? "invert scroll ON" : "invert scroll OFF",
        invert ? 15 : 16, &result);
    return result;
}

// setAutoReconnect(enable): 是否在连接意外断开后自动重连（worker 线程实时读取）
static napi_value SetAutoReconnect(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    bool enable = true;
    napi_get_value_bool(env, args[0], &enable);
    g_deskflowClient.setAutoReconnect(enable, 3000);
    napi_value result;
    napi_create_string_utf8(env, enable ? "auto reconnect ON" : "auto reconnect OFF",
        enable ? 17 : 18, &result);
    return result;
}

napi_threadsafe_function g_tsfn = nullptr;
// ThreadSafeFunction 回调：在 JS 线程执行，把事件值传给 ArkTS 侧
void CallJsOnThread(napi_env env, napi_value js_cb, void* context, void* data)
{
    std::string* value = static_cast<std::string*>(data);
    napi_value event;
    napi_create_object(env, &event);
    napi_value value_js;
    napi_create_string_utf8(env, value->c_str(), value->length(), &value_js);
    napi_set_named_property(env, event, "value", value_js);
    napi_value global;
    napi_get_global(env, &global);
    napi_value ret;
    napi_call_function(env, global, js_cb, 1, &event, &ret);
    delete value;
}

// 线程安全地发布一个值到 JS（MMI 回调线程调用）：
// 加锁从 g_tsfn 快照指针并投递，避免与 JS 线程 / 其它回调线程竞态（use-after-free）。
void PublishValue(const std::string& text)
{
    napi_threadsafe_function tsfn = nullptr;
    std::string* value = new std::string(text);  // 先构造，避免在锁内分配
    {
        std::lock_guard<std::mutex> lock(g_tsfnMutex);
        tsfn = g_tsfn;
    }
    if (tsfn == nullptr) {
        delete value;
        return;
    }
    // nonblocking：避免在 JS 线程繁忙时阻塞调用线程
    napi_call_threadsafe_function(tsfn, value, napi_tsfn_nonblocking);
}


napi_value OnChange(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    std::lock_guard<std::mutex> lock(g_tsfnMutex);
    if (g_tsfn != nullptr) {
        napi_release_threadsafe_function(g_tsfn, napi_tsfn_release);
        g_tsfn = nullptr;
    }
    // 创建线程安全函数：允许从任意线程（MMI 回调线程）投递事件到 JS 线程
    napi_create_threadsafe_function(env, args[0], nullptr, nullptr, 0, 1, nullptr, nullptr, nullptr,
        CallJsOnThread, &g_tsfn);
    return nullptr;
}

// [Start key_event_interceptor]
struct KeyEvent {
    int32_t action;
    int32_t keyCode;
    int64_t actionTime { -1 };
};

// 定义按键事件回调函数
void OnKeyEventCallback(const Input_KeyEvent* keyEvent)
{
    KeyEvent event;
    // Input_KeyEvent的生命周期仅限于回调函数内，回调函数执行完毕后会被自动销毁
    event.action = OH_Input_GetKeyEventAction(keyEvent);
    event.keyCode = OH_Input_GetKeyEventKeyCode(keyEvent);
    event.actionTime = OH_Input_GetKeyEventActionTime(keyEvent);
    // [StartExclude key_event_interceptor]
    std::ostringstream oss;
    oss << "Key event detected   action:"  << event.action
    << " keyCode: " << event.keyCode
    << " actionTime: " << event.actionTime;
    PublishValue(oss.str());
    // [EndExclude key_event_interceptor]
}

static napi_value AddKeyEventInterceptor(napi_env env, napi_callback_info info)
{
    Input_Result ret = OH_Input_AddKeyEventInterceptor(OnKeyEventCallback, nullptr);
    // [StartExclude key_event_interceptor]
    std::string message = "";
    if (ret == INPUT_SUCCESS) {
        message = "Key event interceptor added successfully, return " + std::to_string(ret);
    } else {
        message = "Key event interceptor added failed, return " + std::to_string(ret);
    }
    napi_value result;
    napi_create_string_utf8(env, message.c_str(), message.size(), &result);
    return result;
    // [EndExclude key_event_interceptor]
}

static napi_value RemoveKeyEventInterceptor(napi_env env, napi_callback_info info)
{
    Input_Result ret = OH_Input_RemoveKeyEventInterceptor();
    // [StartExclude key_event_interceptor]
    std::string message = "";
    if (ret == INPUT_SUCCESS) {
        message = "Key event interceptor removed successfully, return " + std::to_string(ret);
    } else {
        message = "Key event interceptor removed failed, return " + std::to_string(ret);
    }
    napi_value result;
    napi_create_string_utf8(env, message.c_str(), message.size(), &result);
    return result;
    // [EndExclude key_event_interceptor]
}
// [End key_event_interceptor]

// [Start input_event_interceptor]
struct MouseEvent {
    int32_t action;
    int32_t displayX;
    int32_t displayY;
    int32_t button { -1 };
    int32_t axisType { -1 };
    float axisValue { 0.0f };
    int64_t actionTime { -1 };
};

struct TouchEvent {
    int32_t action;
    int32_t id;
    int32_t displayX;
    int32_t displayY;
    int64_t actionTime { -1 };
};

struct AxisEvent {
    int32_t axisAction;
    float displayX;
    float displayY;
    std::map<int32_t, double> axisValues;
    int64_t actionTime { -1 };
    int32_t sourceType;
    int32_t axisEventType { -1 };
};

// 定义鼠标事件回调函数
void OnMouseEventCallback(const Input_MouseEvent* mouseEvent)
{
    MouseEvent event;
    // Input_MouseEvent的生命周期仅在回调函数内，回调函数结束时被销毁
    event.action = OH_Input_GetMouseEventAction(mouseEvent);
    event.displayX = OH_Input_GetMouseEventDisplayX(mouseEvent);
    event.displayY = OH_Input_GetMouseEventDisplayY(mouseEvent);
    event.button = OH_Input_GetMouseEventButton(mouseEvent);
    event.axisType = OH_Input_GetMouseEventAxisType(mouseEvent);
    event.axisValue = OH_Input_GetMouseEventAxisValue(mouseEvent);
    event.actionTime = OH_Input_GetMouseEventActionTime(mouseEvent);
    // [StartExclude input_event_interceptor]
    int32_t gx = 0, gy = 0, winId = 0, dispId = 0;
    gx = OH_Input_GetMouseEventGlobalX(mouseEvent);
    gy = OH_Input_GetMouseEventGlobalY(mouseEvent);
    winId = OH_Input_GetMouseEventWindowId(mouseEvent);
    dispId = OH_Input_GetMouseEventDisplayId(mouseEvent);
    OH_LOG_Print(LOG_APP, LOG_INFO, DF_LOG_DOMAIN, "EventListen",
        "MOUSE action=%{public}d x=%{public}d y=%{public}d gx=%{public}d gy=%{public}d "
        "button=%{public}d axisType=%{public}d axisValue=%{public}f t=%{public}lld winId=%{public}d disp=%{public}d",
        event.action, event.displayX, event.displayY, gx, gy, event.button, event.axisType,
        event.axisValue, event.actionTime, winId, dispId);
    std::ostringstream oss;
    oss << "Mouse event detected   action："  << event.action
    << " displayX： " << event.displayX
    << " displayY：" << event.displayY
    << " button：" << event.button
    << " axisType：" << event.axisType
    << " axisValue：" << event.axisValue
    << " actionTime：" << event.actionTime;
    PublishValue(oss.str());
    // [EndExclude input_event_interceptor]
}

// 定义触摸事件回调函数
void OnTouchEventCallback(const Input_TouchEvent* touchEvent)
{
    TouchEvent event;
    // Input_TouchEvent的生命周期仅在回调函数内，回调函数结束时被销毁
    event.action = OH_Input_GetTouchEventAction(touchEvent);
    event.id = OH_Input_GetTouchEventFingerId(touchEvent);
    event.displayX = OH_Input_GetTouchEventDisplayX(touchEvent);
    event.displayY = OH_Input_GetTouchEventDisplayY(touchEvent);
    event.actionTime = OH_Input_GetTouchEventActionTime(touchEvent);
    // [StartExclude input_event_interceptor]
    int32_t gx = 0, gy = 0, winId = 0;
    gx = OH_Input_GetTouchEventGlobalX(touchEvent);
    gy = OH_Input_GetTouchEventGlobalY(touchEvent);
    winId = OH_Input_GetTouchEventWindowId(touchEvent);
    int64_t downTime = OH_Input_GetTouchEventDownTime(touchEvent);
    double pressure = OH_Input_GetTouchEventPressure(touchEvent);
    OH_LOG_Print(LOG_APP, LOG_INFO, DF_LOG_DOMAIN, "EventListen",
        "TOUCH action=%{public}d id=%{public}d x=%{public}d y=%{public}d gx=%{public}d gy=%{public}d "
        "t=%{public}lld down=%{public}lld press=%{public}f win=%{public}d",
        event.action, event.id, event.displayX, event.displayY, gx, gy, event.actionTime, downTime,
        pressure, winId);
    std::ostringstream oss;
    oss << "Touch event detected action："  << event.action
    << " displayX： " << event.displayX
    << " displayY：" << event.displayY
    << " id：" << event.id
    << " actionTime：" << event.actionTime;
    PublishValue(oss.str());
    // [EndExclude input_event_interceptor]
}

// 定义轴事件回调函数
void OnAxisEventCallback(const Input_AxisEvent* axisEvent)
{
    AxisEvent event;
    
    // Input_AxisEvent的生命周期仅在回调函数内，回调函数结束时被销毁
    InputEvent_AxisAction action;
    Input_Result ret = OH_Input_GetAxisEventAction(axisEvent, &action);
    event.axisAction = action;
    ret = OH_Input_GetAxisEventDisplayX(axisEvent, &event.displayX);
    ret = OH_Input_GetAxisEventDisplayY(axisEvent, &event.displayY);
    ret = OH_Input_GetAxisEventActionTime(axisEvent, &event.actionTime);
    InputEvent_SourceType sourceType;
    ret = OH_Input_GetAxisEventSourceType(axisEvent, &sourceType);
    event.sourceType = sourceType;
    InputEvent_AxisEventType axisEventType;
    ret = OH_Input_GetAxisEventType(axisEvent, &axisEventType);
    event.axisEventType = axisEventType;
    if (event.axisEventType == AXIS_EVENT_TYPE_PINCH) {
        double value = 0;
        ret = OH_Input_GetAxisEventAxisValue(axisEvent, AXIS_TYPE_PINCH, &value);
        event.axisValues.insert(std::make_pair(AXIS_TYPE_PINCH, value));
        ret = OH_Input_GetAxisEventAxisValue(axisEvent, AXIS_TYPE_ROTATE, &value);
        event.axisValues.insert(std::make_pair(AXIS_TYPE_ROTATE, value));
    } else if (event.axisEventType == AXIS_EVENT_TYPE_SCROLL) {
        double value = 0;
        ret = OH_Input_GetAxisEventAxisValue(axisEvent, AXIS_TYPE_SCROLL_VERTICAL, &value);
        event.axisValues.insert(std::make_pair(AXIS_TYPE_SCROLL_VERTICAL, value));
        ret = OH_Input_GetAxisEventAxisValue(axisEvent, AXIS_TYPE_SCROLL_HORIZONTAL, &value);
        event.axisValues.insert(std::make_pair(AXIS_TYPE_SCROLL_HORIZONTAL, value));
    }
    // [StartExclude input_event_interceptor]
    std::ostringstream oss;
    oss << "Axis event detected axisAction："  << event.axisAction
    << " displayX： " << event.displayX
    << " displayY：" << event.displayY
    << " sourceType：" << event.sourceType
    << " actionTime：" << event.actionTime
    << " axisEventType：" << event.axisEventType
    << "\n axisValues:";
    for (const auto& pair : event.axisValues) {
        oss << " axis" << pair.first << "=" << pair.second;
    }
    PublishValue(oss.str());
    OH_LOG_Print(LOG_APP, LOG_INFO, DF_LOG_DOMAIN, "EventListen",
        "AXIS action=%{public}d x=%{public}f y=%{public}f src=%{public}d type=%{public}d t=%{public}lld "
        "v=%{public}f",
        event.axisAction, event.displayX, event.displayY, event.sourceType, event.axisEventType,
        event.actionTime, event.axisValues.empty() ? 0.0 : event.axisValues.begin()->second);
    // [EndExclude input_event_interceptor]
}

// 输入事件回调函数结构体
Input_InterceptorEventCallback g_eventCallback;

static napi_value AddEventInterceptor(napi_env env, napi_callback_info info)
{
    // 设置鼠标事件回调函数
    g_eventCallback.mouseCallback = OnMouseEventCallback;
    // 设置触摸事件回调函数
    g_eventCallback.touchCallback = OnTouchEventCallback;
    // 设置轴事件回调函数
    g_eventCallback.axisCallback = OnAxisEventCallback;
    Input_Result ret = OH_Input_AddInputEventInterceptor(&g_eventCallback, nullptr);
    // [StartExclude input_event_interceptor]
    std::string message = "";
    if (ret == INPUT_SUCCESS) {
        message = "Input event interception added successfully, return code: " + std::to_string(ret);
    } else {
        message = "Failed to add input event interception, return code: " + std::to_string(ret);
    }
    napi_value result;
    napi_create_string_utf8(env, message.c_str(), message.size(), &result);
    return result;
    // [EndExclude input_event_interceptor]
}

static napi_value RemoveEventInterceptor(napi_env env, napi_callback_info info)
{
    Input_Result ret = OH_Input_RemoveInputEventInterceptor();
    // [StartExclude input_event_interceptor]
    std::string message = "";
    if (ret == INPUT_SUCCESS) {
        message = "Input event interception removed successfully, return code: " + std::to_string(ret);
    } else {
        message = "Failed to remove input event interception, return code: " + std::to_string(ret);
    }
    napi_value result;
    napi_create_string_utf8(env, message.c_str(), message.size(), &result);
    return result;
    // [EndExclude input_event_interceptor]
}
// [End input_event_interceptor]

// ===== Deskflow PoC: 注入授权与全局注入验证 (API 20+, 仅 PC/2in1 生效) =====

// OH_Input_RequestInjection 的异步授权回调
void OnInjectionAuthorizeCallback(Input_InjectionStatus authorizedStatus)
{
    std::string statusText = "Unknown";
    switch (authorizedStatus) {
        case UNAUTHORIZED:
            statusText = "UNAUTHORIZED(0)";
            break;
        case AUTHORIZING:
            statusText = "AUTHORIZING(1)";
            break;
        case AUTHORIZED:
            statusText = "AUTHORIZED(2)";
            break;
        default:
            statusText = std::to_string(static_cast<int>(authorizedStatus));
            break;
    }
    PublishValue("Injection auth callback, status: " + statusText);
}

// 申请注入授权（用户授权弹窗；返回 INPUT_SUCCESS=0 表示申请流程已启动）
static napi_value RequestInjection(napi_env env, napi_callback_info info)
{
    Input_Result ret = OH_Input_RequestInjection(OnInjectionAuthorizeCallback);
    std::string message = "OH_Input_RequestInjection ret: " + std::to_string(ret);
    napi_value result;
    napi_create_string_utf8(env, message.c_str(), message.size(), &result);
    return result;
}

// 查询当前注入授权状态（UNAUTHORIZED=0 / AUTHORIZING=1 / AUTHORIZED=2）
static napi_value QueryAuthorizedStatus(napi_env env, napi_callback_info info)
{
    Input_InjectionStatus status = UNAUTHORIZED;
    Input_Result ret = OH_Input_QueryAuthorizedStatus(&status);
    std::string message = "OH_Input_QueryAuthorizedStatus ret: " + std::to_string(ret) +
        ", status: " + std::to_string(status);
    napi_value result;
    napi_create_string_utf8(env, message.c_str(), message.size(), &result);
    return result;
}

// 取消事件注入并撤销授权
static napi_value CancelInjection(napi_env env, napi_callback_info info)
{
    OH_Input_CancelInjection();
    std::string message = "OH_Input_CancelInjection called";
    napi_value result;
    napi_create_string_utf8(env, message.c_str(), message.size(), &result);
    return result;
}

// 在全局坐标 (x, y) 注入一次鼠标左键点击：移动到目标 -> 按下 -> 抬起
// 注意：displayX/displayY（显示器相对坐标）与 globalX/globalY（主屏全局坐标）
// 必须同时设置——服务端 HandleMouseProperty 每次注入都会读取 displayX/Y，
// 只设置 global 坐标会导致指针被定位到 (0,0) 左上角。
static napi_value InjectMouseClickGlobal(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    int32_t x = 0;
    int32_t y = 0;
    napi_get_value_int32(env, args[0], &x);
    napi_get_value_int32(env, args[1], &y);

    struct Input_MouseEvent* ev = OH_Input_CreateMouseEvent();
    if (ev == nullptr) {
        std::string message = "CreateMouseEvent failed";
        napi_value result;
        napi_create_string_utf8(env, message.c_str(), message.size(), &result);
        return result;
    }
    std::string message;
    // 1) 移动到目标坐标（display + global 都设置）
    OH_Input_SetMouseEventAction(ev, MOUSE_ACTION_MOVE);
    OH_Input_SetMouseEventDisplayX(ev, x);
    OH_Input_SetMouseEventDisplayY(ev, y);
    OH_Input_SetMouseEventGlobalX(ev, x);
    OH_Input_SetMouseEventGlobalY(ev, y);
    // actionTime 传 -1：使用系统时钟（与真实事件同源，确保拖动/长按手势识别）
    OH_Input_SetMouseEventActionTime(ev, -1);
    int32_t ret = OH_Input_InjectMouseEventGlobal(ev);
    message += "move(" + std::to_string(x) + "," + std::to_string(y) + ") ret: " + std::to_string(ret);
    // 2) 左键按下
    OH_Input_SetMouseEventAction(ev, MOUSE_ACTION_BUTTON_DOWN);
    OH_Input_SetMouseEventButton(ev, MOUSE_BUTTON_LEFT);
    OH_Input_SetMouseEventDisplayX(ev, x);
    OH_Input_SetMouseEventDisplayY(ev, y);
    OH_Input_SetMouseEventGlobalX(ev, x);
    OH_Input_SetMouseEventGlobalY(ev, y);
    OH_Input_SetMouseEventActionTime(ev, -1);
    ret = OH_Input_InjectMouseEventGlobal(ev);
    message += ", down ret: " + std::to_string(ret);
    // 3) 左键抬起
    OH_Input_SetMouseEventAction(ev, MOUSE_ACTION_BUTTON_UP);
    OH_Input_SetMouseEventButton(ev, MOUSE_BUTTON_LEFT);
    OH_Input_SetMouseEventDisplayX(ev, x);
    OH_Input_SetMouseEventDisplayY(ev, y);
    OH_Input_SetMouseEventGlobalX(ev, x);
    OH_Input_SetMouseEventGlobalY(ev, y);
    OH_Input_SetMouseEventActionTime(ev, -1);
    ret = OH_Input_InjectMouseEventGlobal(ev);
    message += ", up ret: " + std::to_string(ret);
    OH_Input_DestroyMouseEvent(&ev);

    napi_value result;
    napi_create_string_utf8(env, message.c_str(), message.size(), &result);
    return result;
}

// 只注入一次鼠标移动（不点击），display + global 坐标都设置
static int32_t InjectMouseMoveTo(int32_t x, int32_t y)
{
    struct Input_MouseEvent* ev = OH_Input_CreateMouseEvent();
    if (ev == nullptr) {
        return -1;
    }
    OH_Input_SetMouseEventAction(ev, MOUSE_ACTION_MOVE);
    OH_Input_SetMouseEventDisplayX(ev, x);
    OH_Input_SetMouseEventDisplayY(ev, y);
    OH_Input_SetMouseEventGlobalX(ev, x);
    OH_Input_SetMouseEventGlobalY(ev, y);
    // actionTime 传 -1：使用系统时钟（与真实事件同源）
    OH_Input_SetMouseEventActionTime(ev, -1);
    int32_t ret = OH_Input_InjectMouseEventGlobal(ev);
    OH_Input_DestroyMouseEvent(&ev);
    return ret;
}

// 平滑移动测试线程：带缓动曲线（ease-out），统计单次注入耗时与失败数
void MouseTrailThread(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t steps, int32_t intervalMs)
{
    int64_t totalStepMs = 0;
    int64_t maxStepMs = 0;
    int32_t failCount = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int32_t i = 1; i <= steps; ++i) {
        // ease-out quad 缓动：起速快、收尾慢，接近真实鼠标移动
        double t = static_cast<double>(i) / steps;
        double eased = 1.0 - (1.0 - t) * (1.0 - t);
        int32_t x = x0 + static_cast<int32_t>((x1 - x0) * eased);
        int32_t y = y0 + static_cast<int32_t>((y1 - y0) * eased);
        auto s0 = std::chrono::steady_clock::now();
        int32_t ret = InjectMouseMoveTo(x, y);
        auto s1 = std::chrono::steady_clock::now();
        int64_t stepMs = std::chrono::duration_cast<std::chrono::microseconds>(s1 - s0).count() / 1000;
        totalStepMs += stepMs;
        maxStepMs = std::max(maxStepMs, stepMs);
        if (ret != INPUT_SUCCESS) {
            ++failCount;
        }
        if (intervalMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        }
    }
    int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    std::ostringstream oss;
    oss << "Trail done: " << elapsed << "ms total, " << steps << " steps @ " << intervalMs
        << "ms interval, inject avg " << (steps > 0 ? totalStepMs / steps : 0)
        << "ms, max " << maxStepMs << "ms, fails " << failCount
        << ", end (" << x1 << "," << y1 << ")";
    PublishValue(oss.str());
}

// 启动平滑移动轨迹（后台线程执行，UI 不卡顿）
static napi_value InjectMouseTrail(napi_env env, napi_callback_info info)
{
    size_t argc = 6;
    napi_value args[6];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0, steps = 100, intervalMs = 5;
    napi_get_value_int32(env, args[0], &x0);
    napi_get_value_int32(env, args[1], &y0);
    napi_get_value_int32(env, args[2], &x1);
    napi_get_value_int32(env, args[3], &y1);
    napi_get_value_int32(env, args[4], &steps);
    napi_get_value_int32(env, args[5], &intervalMs);
    std::thread(MouseTrailThread, x0, y0, x1, y1, steps, intervalMs).detach();
    std::string message = "Smooth trail started: (" + std::to_string(x0) + "," + std::to_string(y0) +
        ") -> (" + std::to_string(x1) + "," + std::to_string(y1) + "), " +
        std::to_string(steps) + " steps @ " + std::to_string(intervalMs) + "ms";
    napi_value result;
    napi_create_string_utf8(env, message.c_str(), message.size(), &result);
    return result;
}

// 注入一次按键（按下 -> 抬起），keyCode 为 Input_KeyCode 键值（如 2015=回车）
static napi_value InjectKey(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    int32_t keyCode = 0;
    napi_get_value_int32(env, args[0], &keyCode);

    struct Input_KeyEvent* ke = OH_Input_CreateKeyEvent();
    if (ke == nullptr) {
        std::string message = "CreateKeyEvent failed";
        napi_value result;
        napi_create_string_utf8(env, message.c_str(), message.size(), &result);
        return result;
    }
    std::string message;
    OH_Input_SetKeyEventAction(ke, KEY_ACTION_DOWN);
    OH_Input_SetKeyEventKeyCode(ke, keyCode);
    OH_Input_SetKeyEventActionTime(ke, -1);  // 使用系统时钟
    int32_t ret = OH_Input_InjectKeyEvent(ke);
    message += "key(" + std::to_string(keyCode) + ") down ret: " + std::to_string(ret);
    OH_Input_SetKeyEventAction(ke, KEY_ACTION_UP);
    OH_Input_SetKeyEventActionTime(ke, -1);
    ret = OH_Input_InjectKeyEvent(ke);
    message += ", up ret: " + std::to_string(ret);
    OH_Input_DestroyKeyEvent(&ke);

    napi_value result;
    napi_create_string_utf8(env, message.c_str(), message.size(), &result);
    return result;
}

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] = {
        { "onChange", nullptr, OnChange, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "addKeyEventInterceptor", nullptr, AddKeyEventInterceptor, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "removeKeyEventInterceptor", nullptr, RemoveKeyEventInterceptor, nullptr, nullptr, nullptr,
            napi_default, nullptr },
        { "addEventInterceptor", nullptr, AddEventInterceptor, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "removeEventInterceptor", nullptr, RemoveEventInterceptor, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "requestInjection", nullptr, RequestInjection, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "queryAuthorizedStatus", nullptr, QueryAuthorizedStatus, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "cancelInjection", nullptr, CancelInjection, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "injectMouseClickGlobal", nullptr, InjectMouseClickGlobal, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "injectMouseTrail", nullptr, InjectMouseTrail, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "injectKey", nullptr, InjectKey, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "onDeskflowStatus", nullptr, OnDeskflowStatus, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "onClipboardData", nullptr, OnClipboardData, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "pushClipboard", nullptr, PushClipboard, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "setClipboardSync", nullptr, SetClipboardSync, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "connectDeskflow", nullptr, ConnectDeskflow, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "disconnectDeskflow", nullptr, DisconnectDeskflow, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "setInvertScroll", nullptr, SetInvertScroll, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "setAutoReconnect", nullptr, SetAutoReconnect, nullptr, nullptr, nullptr, napi_default, nullptr },
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}
EXTERN_C_END

static napi_module demoModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "entry",
    .nm_priv = ((void*)0),
    .reserved = { 0 },
};

extern "C" __attribute__((constructor)) void RegisterEntryModule(void)
{
    napi_module_register(&demoModule);
}
