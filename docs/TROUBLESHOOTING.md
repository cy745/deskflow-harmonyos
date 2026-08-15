# 踩坑记录与解决方案

> 本文档汇总本工程在开发 Deskflow（Barrier）HarmonyOS 客户端过程中**真实踩过并解决**的坑。
> 每条按「症状 → 根因 → 解决」组织。涉及协议层细节的可配合 `PROTOCOL.md` 阅读。

---

## 1. Deskflow 服务端崩溃 0xc0000409（ucrtbase.dll）

**症状**
- 客户端从鸿蒙电脑向 Windows Deskflow 服务端发起剪贴板同步时，服务端进程在 `ucrtbase.dll` 崩溃退出，崩溃码 `0xc0000409`（`STATUS_STACK_BUFFER_OVERRUN` / fast-fail）。

**根因**
- 穿越（cross-screen）剪贴板发送图像（DIB）数据时，服务端处理 **负 `biHeight`**（自顶向下的位图）发生越界/缓冲问题，属 Deskflow 上游缺陷。链接到上游 issue：[deskflow #9869](https://github.com/deskflow/deskflow/issues/9869)。

**解决**
- 服务端配置关闭剪贴板共享：
  ```
  clipboardSharing=false
  ```
- 客户端在 `DCLP` / `CCLP` 上只做"安全忽略"（读清负载、不上报、不触发服务端 DIB 处理），从而绕过崩溃路径。

---

## 2. 客户端收到 CINN 后连接被断开

**症状**
- `CINN`（进入屏幕）消息到达后，客户端解析异常并被服务端断开 / 控制权拉回。

**根因**
- 消息分发用 `kMsgCEnter = "CINN%2i%2i%4i%2i"` 这种**带格式常量的字符串**，与 4 字节消息 key `"CINN"` 用 `std::string ==` 比较**永远不会相等**，从而落到 `unknown message` 分支被拒。

**解决**
- 分发比较一律用**字面量 4 字节 key**（如 `"CINN"`），解析参数时才用 `kMsgCEnter + 4` 跳过 key：

```cpp
if (key == "CINN") {
    ProtoUtil::readf(m_stream, kMsgCEnter + 4, &x, &y, &seq, &mask);
    ...
}
```

> 同理，`DMMV`、`DMDN`、`DKDN` 等分发统一用字面量 key，格式只用于读参。

---

## 3. 注入鼠标跳到左上角

**症状**
- 调用注入移动/点击后，指针跑到屏幕左上角 (0,0)。

**根因**
- 只设置了 `globalX/globalY`，未设置 `displayX/displayY`。注入路径读取 `display` 坐标为 0。

**解决**
- MOVE / DOWN / UP / AXIS 事件**同时设置** `displayX/displayY` 与 `globalX/globalY`，且值相同。详见 `INPUT_INJECTION.md` §3。

---

## 4. 鼠标位置不对（物理像素 vs vp）

**症状**
- 鼠标落点与实际目标有明显偏移，且随缩放比变化。

**根因**
- 注入接口使用 **vp（逻辑像素）**，而 `display.getDefaultDisplaySync()` 在 2in1 上返回**物理像素**（如 `3120×2080`）。

**解决**
- 全程使用 vp：ETS 侧取 `Math.round(d.width/height)` 传屏，注入坐标不乘 `densityPixels`。详见 `INPUT_INJECTION.md` §2。

---

## 5. 注入授权回调 SIGABRT 崩溃

**症状**
- 调用 `OH_Input_RequestInjection` 后，授权回调执行时进程 `SIGABRT` 崩溃。

**根因**
- 授权回调在 **MMI IPC 线程**执行；在该线程内直接 `napi_call_function` 调用 JS，违反 NAPI 线程约束。

**解决**
- 用 **NAPI 线程安全函数（tsfn）** 把数据投递到 JS 线程再调用回调；投递方式用 `napi_tsfn_nonblocking`。见 `INPUT_INJECTION.md` §8 与 `ARCHITECTURE.md` §3。

---

## 6. 连接握手解析失败 / 协议名对不上

**症状**
- 客户端连上 24800 后握手失败，报"协议名不匹配"或解析不到服务端版本。

**根因**
- 服务端首帧 `Hello` 协议名是 `"Barrier"` —— 由 `%7s` **定宽 7 字节、无长度前缀**编码。若客户端按 `%s`（长度前缀）读取，会吞掉后面的版本字段，导致后续全部错位。
- 另一个维度：连接后第一帧必须先**解析 4 字节长度前缀**取出帧体，再格式化解析；直接按消息 key 读会错位。

**解决**
- 按 `%7s%2i%2i` 精确读 Hello；用 `readFrame` 先解帧（4 字节 NBO 长度前缀 + payload）再进 `readf`。参考 `PROTOCOL.md` §3 / §2。

---

## 7. LSYN / DKRP / SECN 解析格式错导致阻塞

**症状**
- 某些消息（如语言同步 `LSYN`、按键重复 `DKRP`、安全输入 `SECN`）到达后，客户端停在 `recv` 不再处理后续消息，甚至连接被判定超时。

**根因**
- 这些消息**不是"无负载"消息**，而是带有字符串/多个参数的负载。若实现方把它们当作无参数 key 直接忽略、**没有按格式读走负载字节**，剩余字节会污染下一帧的 key 对齐，令后续每一帧都解析错位、`readExact` 永远差几个字节 → 看起来像"阻塞在 recv"。

**解决**
- 对每条消息**如实按其格式把负载读干净**（哪怕功能上忽略），保持流对齐：

```cpp
if (key == "LSYN") {
    std::string data;
    ProtoUtil::readf(m_stream, kMsgDLanguageSynchronisation + 4, &data); // 必须读走
    ...
}
```

---

## 8. 动作不生效：移动窗口 / 拖桌面图标永不触发

**症状**
- 注入"按住左键 + 移动"后，窗口拖不动、桌面图标拖不动、长按不触发；但单纯点击/移动正常。

**根因**
- 两个叠加问题：
  1. `actionTime` 传了 Unix 毫秒时间戳（约 `1.75e12`），与系统时钟（`GetSysClockTime()`，约 `1e10`）量级错乱，导致 `downTime`/按住时长判定失效；
  2. `MOUSE_ACTION_MOVE` 没有携带当前按下的 `button`，无法构成拖动事件。

**解决**
- ① `actionTime` 一律传 `-1`，交系统时钟生成（关键）。
- ② MOVE 前查按下状态表，为事件设置仍按下的按钮。
- 证据：`devecocli` 抓包确认成功拖动是纯 MOUSE `DOWN → MOVE(button=LEFT) → UP` 流。详见 `INPUT_INJECTION.md` §4 / §5。

---

## 9. 运行时被服务端拉回主屏 / 写事件超时

**症状**
- 进入鸿蒙屏控制后，偶尔刚操作就被服务端把控制权拉回主屏，或服务端侧报写事件超时。

**根因**
- Deskflow/Barrier 服务端依赖客户端的"响应"维持交互节奏；其底层有与 **BSD 延迟 ACK** 相关的处理逻辑，客户端不回确认时服务端容易超时并放弃该屏。

**解决**
- **收到 `DSOP`（握手完成）后进入运行期**，对每条处理完的消息回一条 `CNOP` 帧，把服务端"延迟 ACK 等待"绕过去。见 `PROTOCOL.md` §5。

---

## 跨文件索引

| 主题 | 参考文档 |
|---|---|
| 帧封装 / 格式符 / 消息格式 / 心跳 / KeyID | `PROTOCOL.md` |
| vp 坐标 / actionTime=-1 / 坐标双设 / 滚轮序列 / 授权回调线程 | `INPUT_INJECTION.md` |
| 分层 / NAPI / 数据流 / 类职责 | `ARCHITECTURE.md` |

---

*均由本项目实战总结。链接的 upstream issue 仅为缺陷追溯参考。*
