# HarmonyOS 输入注入（OH_Input）实战与坑

> 本文档记录在华为鸿蒙电脑（HarmonyOS 6 / API 20+，PC/2in1）上，通过 Input Kit C API
> 实现**全局鼠标/键盘/滚轮注入**的完整过程与踩坑经验。所有结论均来自本工程的实机验证
> （含 `devecocli` 抓取真实事件序列做对比）。

---

## 1. 前置条件

### 1.1 权限声明

在 `module.json5` 的 `requestPermissions` 中声明：

```json5
{
  "name": "ohos.permission.INTERCEPT_INPUT_EVENT"
}
```

这是一个 **restricted 权限**（`availableLevel` 为 `system_basic` 以上），普通应用需要
**ACL 申请**并由开发者签名授权后才可生效。本工程运行于 PC/2in1，该权限用于注册输入事件拦截器（诊断）。

### 1.2 注入授权（`OH_Input_RequestInjection`）

全局注入除权限外，还要求运行时**用户授权**。`OH_Input_RequestInjection` 是**异步**接口：

```cpp
Input_InjectionStatus status = /* 授权状态 */;
Input_Result ret = OH_Input_RequestInjection(OnInjectionAuthorizeCallback);
```

授权回调携带的状态值与含义：

| 值 | 枚举 | 含义 |
|---|---|---|
| `0` | `UNAUTHORIZED` | 未授权 |
| `1` | `AUTHORIZING` | 授权流程进行中 |
| `2` | `AUTHORIZED` | 已授权，可以注入 |

- 返回 `INPUT_SUCCESS` 仅表示"授权流程已启动"，**不代表已经被授权**；真正的授权要等回调 / 查询状态。
- 查询当前状态：`OH_Input_QueryAuthorizedStatus(&status)`。
- 撤销：`OH_Input_CancelInjection()`。
- **仅 PC/2in1 生效**；非 2in1 设备通常返回错误码（如 `801`）。
- 连接建立后建议在协议线程**自动申请一次授权**，否则 `CINN` 之后的注入事件不生效。

> 坑：若在授权回调内直接调用 JSBridge 同步回调原生 JS，容易在 MMI IPC 线程崩溃（`SIGABRT`）。
> 必须用 NAPI **线程安全函数**投递到 UI/JS 线程。见 `TROUBLESHOOTING.md`。

---

## 2. 坐标系：使用 vp（逻辑像素）而非物理像素 ⚠️

这是本项目最容易踩的坑，反复出现。

### 2.1 现象

- 用 `display.getDefaultDisplaySync()` 在 2in1 上取到的分辨率是 **物理像素**（例如 `3120 × 2080`），而注入接口内部按 **vp（逻辑像素）** 解释坐标。
- 直接把物理像素坐标塞进注入，鼠标落点会明显偏移，且不同缩放比下偏移量不同。

### 2.2 根因

`OH_Input_InjectMouseEventGlobal` / `InjectKeyEvent` 等注入接口的坐标空间是 **vp 逻辑像素**。
而 `display` 模块的 `getDefaultDisplaySync()` 默认上报物理像素。两者在系统缩放比（`densityPixels`）下不一致。

### 2.3 解决方案

不要乘 `densityPixels`，直接使用 vp 坐标：

```ts
const d = display.getDefaultDisplaySync();
const w = Math.round(d.width);   // 屏幕宽（注入用 vp）
const h = Math.round(d.height);  // 屏幕高
this.result = testNapi.connectDeskflow(host, port, name, w, h);
```

在协议侧，`DINF` 上报屏幕尺寸、`DMMV` 的绝对坐标、以及 `CINN` 移交时的指针坐标，**全部按 vp 处理**，保持与注入空间一致。ETS 侧计算目标点（如屏幕中心）也用 `d.width / 2` 而非物理像素。

---

## 3. displayX/displayY 与 globalX/globalY 必须同时设置 ⚠️

### 3.1 现象

只设置 `setMouseEventGlobalX/Y`（全局坐标）而不设置 `setMouseEventDisplayX/Y`，鼠标会**跳到屏幕左上角 (0,0)**。

### 3.2 根因

服务端的 `HandleMouseProperty` / 注入路径每次都会读取 `displayX` / `displayY`（显示器相对坐标）来决定注入位置；只填 `globalX/Y` 时 display 坐标保持初始 0，指针被定位回左上角。

### 3.3 解决方案

`MOVE`、`BUTTON_DOWN`、`BUTTON_UP`、`AXIS_*` 全部同时设置两套坐标，且值一致：

```cpp
OH_Input_SetMouseEventDisplayX(ev, x);
OH_Input_SetMouseEventDisplayY(ev, y);
OH_Input_SetMouseEventGlobalX(ev, x);
OH_Input_SetMouseEventGlobalY(ev, y);
```

---

## 4. actionTime 必须传 -1（关键坑）⚠️

这是本项目注入能否"像人手一样"工作的**决定性细节**。

### 4.1 现象

注入的鼠标点击/拖动"事件发了但没效果"：桌面图标拖不动、窗口拖不动、长按不触发。

### 4.2 根因

`actionTime` 如果传入 **Unix 毫秒时间戳**（约 `1.75e12`），与系统内部时钟基准（`GetSysClockTime()`，从开机计时约 `1e10` 量级）**数量级不一致**。系统手势识别依赖 `downTime` / 按住时长做比较，量级错乱后会**误判按住时间**，甚至认为`down` 与 `up` 时间倒退，导致拖动/长按类手势识别器不触发。

### 4.3 解决方案

把 `actionTime` 设为 **`-1`**，让系统自己用 `GetSysClockTime()` 生成与真实事件同源的时间基准：

```cpp
OH_Input_SetMouseEventActionTime(ev, -1);   // MOVE / BUTTON_* / AXIS_*
OH_Input_SetKeyEventActionTime(ke, -1);     // KeyEvent 同理
```

### 4.4 证据（devecocli 抓包对比）

用 `devecocli` 抓取"真实拖动"事件流，与注入事件流对比：

```
真实拖动（成功）：
  MOUSE action=DOWN x=.. y=.. button=1 (LEFT)
  MOUSE action=MOVE x=.. y=.. button=1
  MOUSE action=MOVE x=.. y=.. button=1
  MOUSE action=UP   x=.. y=.. button=1

注入拖动（只有 actionTime 传 -1 后才匹配）：
  同上，actionTime 由系统时钟生成
```

结论：**成功拖动是纯 MOUSE 事件流 `DOWN → MOVE(button=LEFT) → UP`**，而非注入触摸事件。本工程因此移除了触摸事件注入方案，统一走鼠标事件。

---

## 5. MOVE 事件必须携带当前按下的按钮

仅发送 `MOUSE_ACTION_MOVE` 且不设置 `button`，系统无法识别这是一次"正在拖动的移动"，拖动会断掉。

注入 MOVE 前，遍历当前按下状态，把仍按下的按钮设置进事件：

```cpp
for (auto& [buttonId, down] : m_mouseButtonsDown) {
    if (down) {
        int32_t harmony = buttonToHarmony(buttonId);
        if (harmony >= 0) { OH_Input_SetMouseEventButton(ev, harmony); break; }
    }
}
```

工作线程维护 `m_mouseButtonsDown[buttonId] = down` 以记录真实按下状态；离开屏幕（`COUT`）时清空所有按下标记（同时注入抬起，避免粘滞按钮）。

---

## 6. 滚轮需要完整 AXIS_BEGIN → AXIS_UPDATE → AXIS_END 序列

只发一次轴更新（`MOUSE_ACTION_AXIS_UPDATE`）系统不识别滚动，必须发三连：

```cpp
const int32_t actions[3] = {
    MOUSE_ACTION_AXIS_BEGIN,
    MOUSE_ACTION_AXIS_UPDATE,
    MOUSE_ACTION_AXIS_END,
};
for (int32_t action : actions) {
    Input_MouseEvent* ev = OH_Input_CreateMouseEvent();
    OH_Input_SetMouseEventAction(ev, action);
    OH_Input_SetMouseEventAxisType(ev, axisType);
    OH_Input_SetMouseEventAxisValue(ev, axisValue);
    OH_Input_SetMouseEventDisplayX(ev, x);
    OH_Input_SetMouseEventDisplayY(ev, y);
    OH_Input_SetMouseEventGlobalX(ev, x);
    OH_Input_SetMouseEventGlobalY(ev, y);
    OH_Input_SetMouseEventActionTime(ev, -1);
    OH_Input_InjectMouseEventGlobal(ev);
    OH_Input_DestroyMouseEvent(&ev);
}
```

- 轴类型：`MOUSE_AXIS_SCROLL_VERTICAL`（垂直）或 `MOUSE_AXIS_SCROLL_HORIZONTAL`（水平）。
- 垂直滚轮时 `yDelta` 上滚 `+1`、下滚 `-1`；协议按钮 `4`（滚轮上）/`5`（滚轮下）在注入层转换为该轴事件（见 `protocol_types.h` 的 `kButtonWheelUp/Down`）。

---

## 7. 键盘注入

```cpp
Input_KeyEvent* ke = OH_Input_CreateKeyEvent();
OH_Input_SetKeyEventAction(ke, KEY_ACTION_DOWN);
OH_Input_SetKeyEventKeyCode(ke, keyCode);
OH_Input_SetKeyEventActionTime(ke, -1);   // 同 actionTime 坑
OH_Input_InjectKeyEvent(ke);
OH_Input_DestroyKeyEvent(&ke);
```

- `keyCode` 为 HarmonyOS `Input_KeyCode` 枚举（非 X11 keysym），需做映射：`keysymToKeycode()`。
- 同样必须 `actionTime = -1`。
- 修饰键：协议掩码 → 维护本地按下状态，`syncModifiers()` 按需补发/抬起，保证本地修饰键状态与协议 mask 一致（见 `PROTOCOL.md` §7.2）。

---

## 8. 注入授权回调与线程安全（SIGABRT 坑）⚠️

- `OH_Input_RequestInjection` 的回调（`OnInjectAuthCallback`）在 **MMI IPC 线程**执行。
- 该线程不能直接调用 JS（`napi_call_function`）——会导致 `SIGABRT` 崩溃。
- 必须创建 **NAPI 线程安全函数（tsfn）** 接收投递，由 JS 线程执行回调。

本工程 `napi_init.cpp` 中，状态/事件上报统一用 `napi_create_threadsafe_function` + `napi_tsfn_nonblocking` 投递（详见 `ARCHITECTURE.md` 与 `TROUBLESHOOTING.md`）。

---

## 9. 建议封装：统一注入入口

本工程在 `deskflow_client.cpp` 暴露四个注入原语，供各消息处理器调用：

| 函数 | 对应协议消息 | 内部行为 |
|---|---|---|
| `injectMouseMove(x, y)` | `DMMV` / `DMRM` / `CINN` | 更新指针，MOVE 事件（携带按下按钮，actionTime=-1） |
| `injectMouseButton(id, down)` | `DMDN` / `DMUP` / `COUT` 清理 | 维护按下状态，DOWN/UP 事件 |
| `injectMouseWheel(dx, dy)` | `DMWM` | BEGIN→UPDATE→END 三连 |
| `injectKeyEvent(keysym, down)` | `DKDN` / `DKUP` / `DKRP` / 修饰键同步 | keysym→keyCode 映射后 DOWN/UP |

---

## 10. 关键经验清单（TL;DR）

1. **用 vp 坐标，不乘 `densityPixels`。**
2. `displayX/Y` 与 `globalX/Y` **都要设**，且值一致。
3. **`actionTime` 恒为 `-1`**，交给系统时钟——这是拖动/长按命中的关键。
4. MOVE 拖动必须携带当前按下的 `button`。
5. 滚轮必须 BEGIN→UPDATE→END。
6. 授权/拦截回调在 MMI IPC 线程，回调 JS 必须走线程安全函数。
7. 连接后自动申请注入授权。
8. 真实拖动 = 纯鼠标事件流（非触摸），以 `devecocli` 抓包为准。

---

*由本项目实战总结。*
