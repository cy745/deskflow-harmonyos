# 工程架构说明

> 本文档描述 Deskflow HarmonyOS PoC 客户端的整体架构：分层、数据流与关键类职责。
> 配套文档：`PROTOCOL.md`（协议编解码）、`INPUT_INJECTION.md`（注入实战）、`TROUBLESHOOTING.md`（踩坑记录）。

---

## 1. 技术栈与模块

- **运行时 / 设备**：HarmonyOS 6（API 24，`compileSdkVersion: 6.1.1(24)`），PC/2in1。
- **UI 层**：ArkTS（ETS），页面 `entry/src/main/ets/pages/Index.ets`。
- **Native 层**：C++ NAPI，编译为 `libentry.so`，链接 `libohinput.so`（Input Kit C API）。
- **网络**：POSIX socket（阻塞 + `select` 超时连接），无第三方依赖。
- 权限：`ohos.permission.INTERCEPT_INPUT_EVENT`、`ohos.permission.INTERNET`。

---

## 2. 分层架构

```
┌─────────────────────────────────────────────────────────────┐
│ ETS UI 层                                                    │
│   Index.ets                                                  │
│   • 连接配置输入 / 状态回显                                  │
│   • 调用 NAPI 导出的连接、注入、拦截、授权接口                │
│   • 监听 onDeskflowStatus / onChange 事件回显                 │
└──────────────────────────┬──────────────────────────────────┘
                           │ NAPI 方法调用 / 线程安全函数回投
┌──────────────────────────▼──────────────────────────────────┐
│ NAPI 桥接层 (napi_init.cpp)                                 │
│   • 导出 JS 接口：connectDeskflow / disconnectDeskflow /      │
│     requestInjection / queryAuthorizedStatus / cancelInjection│
│     injectMouseClickGlobal / injectMouseTrail / injectKey /   │
│     add/remove KeyEventInterceptor / add/remove EventInterceptor│
│     onChange / onDeskflowStatus                              │
│   • 线程安全函数 (tsfn) 做 C++ 线程 → JS 线程回投             │
│   • 诊断：输入事件拦截器（打 hilog）                         │
└──────────────────────────┬──────────────────────────────────┘
                           │ 持有唯一 DeskflowClient
┌──────────────────────────▼──────────────────────────────────┐
│ 协议层 (protocol/)                                          │
│   deskflow_client.{h,cpp}   连接状态机 · 握手 · 消息循环 · 注入│
│   protocol_types.h          消息 key / 格式 / KeyID / 按钮 id  │
│   proto_util.{h,cpp}        writef / readf 格式串编解码        │
│   socket_stream.{h,cpp}     TCP 帧封拆 · 缓冲读写 · select 连接│
└─────────────────────────────────────────────────────────────┘
```

### 2.1 分层职责划分

| 层 | 文件 | 职责 |
|---|---|---|
| **ETS UI** | `ets/pages/Index.ets` | 参数输入、接口调用、状态展示（`connStatus` / `result`） |
| **NAPI 桥接** | `cpp/napi_init.cpp` | 导出 JS 接口、管理线程安全函数、事件拦截诊断、注入授权/注入演示 |
| **协议客户端** | `cpp/protocol/deskflow_client.{h,cpp}` | 连接、握手、消息循环、消息分发、调用注入接口 |
| **协议编解码** | `cpp/protocol/proto_util.{h,cpp}` `protocol_types.h` | 格式串读写、消息常量、键码/按钮定义 |
| **传输** | `cpp/protocol/socket_stream.{h,cpp}` | 4 字节 NBO 长度前缀帧封拆、`readExact`/`writeAll`/`select` 连接 |

---

## 3. NAPI 桥接层（`napi_init.cpp`）

### 3.1 生命周期与全局单例

- 全局唯一 `dfpoc::DeskflowClient g_deskflowClient;`。
- 模块初始化（`Init`）通过 `napi_module_register` 注册到 `"entry"` 模块，导出 `desc[]` 中的全部方法。

### 3.2 线程安全函数（关键）

C++ 协议工作线程 / MMI IPC 回调线程不能直接操作 JS。桥接层用 **NAPI 线程安全函数**回投：

- `g_statusTsfn`：由 `nest thread` 的 `DeskflowStatusCb` 投递**状态字符串**到 JS 线程，更新 `onDeskflowStatus` 回调（连接/握手/进入屏幕等状态）。
- `g_tsfn`：由事件拦截回调（按键/鼠标/触摸/轴）与授权回调投递**诊断事件**，更新 `onChange`。
- 两者都用 `napi_tsfn_nonblocking` 投递：**JS 线程繁忙时不阻塞协议/回调线程**，避免死锁（见 `TROUBLESHOOTING.md` §8 / `INPUT_INJECTION.md` §8）。

### 3.3 诊断拦截器

`AutoRegisterEventInterceptor` 把真实鼠标 / 触摸 / 轴事件打到 `hilog`，用于与注入事件做对比，定位注入差异（如 actionTime 坑）。

### 3.4 注入演示接口（独立于协议注入）

`InjectMouseClickGlobal` / `InjectMouseTrail` / `InjectKey` / `RequestInjection` / `QueryAuthorizedStatus` / `CancelInjection`
用于在没有 Deskflow 服务端时独立验证 HarmonyOS 注入能力（本工程 PoC 的验证手段之一）。

---

## 4. 数据流

### 4.1 上线流程

```
[ETs] 用户填 host/port/screenName
  → connectDeskflow(host, port, name, screenW, screenH)
     ├─ g_deskflowClient.setScreenSize(w,h)    # vp 尺寸
     ├─ g_deskflowClient.setStatusCallback(...) # → g_statusTsfn 回投
     └─ g_deskflowClient.start(...)             # 启动后台工作线程
           │
           ▼ (工作线程 run())
   1. connect(host:24800) 非阻塞+select 超时
   2. readFrame → Hello("%7s%2i%2i") 校验协议名/版本
   3. writeFrame(HostBack)                # 携带 screenName
   4. OH_Input_RequestInjection(...)      # 自动申请注入授权
   5. 进入消息循环
```

### 4.2 消息循环与分发

```
while (not stopped):
    frame = readFrame()          # 4B NBO 长度 + payload
    key   = frame[0..4]          # 消息类型
    handled = handleMessage(key)  # 按字面量 key 分发，按格式读参
    if handshakeDone: writeFrame("CNOP")   # 运行期回 CNOP
```

- `handleMessage` 先读 4 字节 key，再依各消息格式读取参数（`kMsgXxx + 4` 跳过 key）。
- `QINF → DINF`、`CALV → CALV`、`CINN → 移动指针 + 同步修饰键`、`DMMV/DMRM/DMDN/DMUP/DMWM/DKDN/DKUP/DKRP/DKDL` 做真实注入；
- 剪贴板/文件/拖拽/屏保/选项等消息**安全忽略**（读清负载，保持流对齐）。
- 错误消息（`EICV` / `EBSY` / `EUNK` / `EBAD`）置状态并终止循环。

### 4.3 运行期确认

收到 `DSOP` 后 `m_handshakeDone = true`，此后每条消息处理完回一次 `CNOP`，绕开服务端"BSD 延迟 ACK"等待（`PROTOCOL.md` §5）。

### 4.4 注入数据流

```
DMMV(x,y) → injectMouseMove → Input_MouseEvent(MOVE, display+global=x,y, button=当前按下, actionTime=-1)
DMDN/DMUP → injectMouseButton → 维护 m_mouseButtonsDown + DOWN/UP 事件
DMWM      → injectMouseWheel → AXIS_BEGIN→UPDATE→END
DKDN/DKUP → syncModifiers(mask) + injectKeyEvent(keysym→keyCode)
```

---

## 5. 关键类职责

### 5.1 `DeskflowClient`

- **构造/析构**：析构调用 `stop()` 关闭 socket。
- `start(host, port, name)`：防重入（`m_running`），后台 `std::thread` 脱离运行 `run()`。
- `stop()`：置 `m_stopRequested` 并 `close()` 以解除 `recv` 阻塞。
- `run()`：连接 → 握手 → 申请授权 → 消息循环。
- `handshake()`：Hello 校验 + HostBack 发送。
- `handleMessage(key)`：消息分发（返回 `false` 终止循环）。
- `setScreenSize(w,h)`：设置 vp 屏尺寸（用于 `DINF` 与坐标钳制）。
- `setStatusCallback(cb)`：注册状态回调（工作线程调用，桥接层回投 JS）。

内部状态：
- `m_screenW/H`：屏尺寸（vp）。
- `m_pointerX/Y`：最近绝对指针位置。
- `m_active`：是否处于控制中（进入/离开屏幕）。
- `m_handshakeDone`：是否进入运行期（控制是否回 CNOP）。
- `m_seqNum`：最近 `CINN` 的会话序号。
- `m_keyStates`：keysym → 是否按下（按键去重 + 修饰键同步依据）。
- `m_mouseButtonsDown`：按钮 id → 是否按下（拖动 MOVE 携带按钮 + COUT 清理）。

### 5.2 `ProtoUtil`（`proto_util.*`）

- `writef(out, fmt, ...)`：按格式串把参数追加到字节容器。
- `readf(stream, fmt, ...)`：从流按格式读参，格式/EOF 不符返回 `false`。
- 支持 `i / I / s / S / 定宽 / %%`（见 `PROTOCOL.md` §2）。

### 5.3 `SocketStream`（`socket_stream.*`）

- `connect(host, port, timeout)`：`getaddrinfo` + `fcntl` 非阻塞 + `select` 超时。
- `readExact / writeAll`：严格读写足量字节（处理 `EINTR`）。
- `readFrame / writeFrame`：4 字节 NBO 长度前缀封装，>4MB 拒绝。
- 内置读缓冲 `m_buffer`，供 `pushToBuffer` 把已读帧回灌以继续解析参数。

### 5.4 `napi_init.cpp` 全局函数

- `ConnectDeskflow` / `DisconnectDeskflow`：生命周期入口。
- `OnDeskflowStatus` / `OnChange`：注册/重建 tsfn。
- `DeskflowStatusCb` / `NotifyValueChange`：`napi_tsfn_nonblocking` 投递。
- `RequestInjection` / `QueryAuthorizedStatus` / `CancelInjection`：注入授权管理。
- `InjectMouseClickGlobal` / `InjectMouseTrail` / `InjectKey`：独立注入演示。
- 事件拦截回调（`OnKeyEventCallback` / `OnMouseEventCallback` / `OnTouchEventCallback` / `OnAxisEventCallback`）：诊断打点。

---

## 6. 目录/文件总览

```
entry/src/main/
  ├─ ets/pages/Index.ets          # UI
  ├─ module.json5                 # 权限 / deviceTypes / abilities
  └─ cpp/
      ├─ napi_init.cpp            # NAPI 桥接 + 注入 + 拦截诊断
      └─ protocol/
          ├─ deskflow_client.{h,cpp}
          ├─ protocol_types.h
          ├─ proto_util.{h,cpp}
          └─ socket_stream.{h,cpp}
```

---

*由本项目实战总结。*
