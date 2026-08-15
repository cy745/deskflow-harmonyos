# Deskflow / Barrier 协议编解码规范

> 本文档描述 Deskflow（以及其前身 Barrier / Synergy 1.x）客户端与服务端之间使用的**私有二进制协议**。
> 内容基于本工程对桌面端 Deskflow fork（`fdeasey/deskflow`，版本 1.26.x 线）与 Barrier 服务端的联调实践总结，
> 并结合原生 `ProtocolTypes.cpp` / `ProtocolUtil.cpp` 的实现还原。适合作为 HarmonyOS 客户端移植的编解码参考。

---

## 1. 传输层：TCP + PacketStreamFilter 帧封装

Deskflow 协议承载在 **纯 TCP** 之上（默认端口 `24800`），没有应用层加密与鉴权。

所有消息都经过服务端的 `PacketStreamFilter` 做帧化：

```
┌──────────────────────────────┬──────────────────────────┐
│  4 字节 长度前缀（NBO）       │      payload（帧体）      │
│  big-endian uint32 = N       │          N 字节          │
└──────────────────────────────┴──────────────────────────┘
```

要点（本工程实战总结）：

- **长度前缀为 4 字节网络字节序（NBO / big-endian）**，只包含帧体长度，**不含**前缀本身。
- 帧体 = `4 字节消息类型 key` + `消息格式化参数`。
- 客户端收帧必须**先解析 4 字节长度前缀**，再读取对应字节的帧体；否则第一帧 `Hello` 就会解析错位。这也是本工程踩到解析阻塞的根因之一，详见 `TROUBLESHOOTING.md`。
- 帧大小上限：协议约定最大消息长度约 `4 MB`（`kProtocolMaxMessageLength`），字符串最大约 `1 MB`。实现应做护栏，避免恶意/异常帧拖垮内存。

`SocketStream::readFrame` / `writeFrame` 在本工程 `protocol/socket_stream.cpp` 中实现，负责在 buffered 读之上完成帧封拆。

---

## 2. writef / readf 格式符

Deskflow 用一种类似 C `printf` 的格式串描述消息参数布局（`ProtocolUtil::writef` / `readf`）。本工程在 `protocol/proto_util.cpp` 重新实现。

格式符一览：

| 格式符 | 编码（写）/ 解码（读） | 说明 |
|---|---|---|
| `%%` | 字面量 `%` | 转义，匹配字节 `%` |
| `%1i` | 1 字节有符号整数 | 取低 8 位 |
| `%2i` | **2 字节 NBO** 有符号整数 | `int16` big-endian |
| `%4i` | **4 字节 NBO** 有符号整数 | `int32` big-endian |
| `%s` | 4 字节 NBO 长度前缀 + `std::string` bytes（**无终止符**） | 长度为核心字符串长度 |
| `%S` | 4 字节 NBO 长度前缀 + 原始 bytes | 用于二进制/剪贴板数据（write-only 语义常见） |
| `%7s` 等定宽 | **定宽字符串**：写时按固定字节宽填充/截断，读时读固定字节数，**无长度前缀** | 典型用途：`Hello` 协议名（7 字符） |
| `%4I` | 4 字节 NBO count + N 个定宽整数元素 | 元素本身用前缀数字决定字节宽（如 `%2I`） |
| 字面字符（如 `C`、`I`、`N`） | 逐字节原样；读时**校验**匹配 | 不匹配即解析失败 |

### 2.1 整数编码细节（`i` / `I`）

- `i` 前面的数字是**字节宽度**：`%1i` = 1 字节、`%2i` = 2 字节、`%4i` = 4 字节。
- 2 字节/4 字节均为 **NBO（big-endian）**。写入时按位移入；读取时 `(b[0] << 8) | b[1]`（2 字节）、`(b[0]<<24)|(b[1]<<16)|(b[2]<<8)|b[3]`（4 字节）。
- `%I`（大写）用于"整数数组"：先写一个 **4 字节 NBO 的 count**，再连续写 `count` 个定宽整数。`DSOP`（设置选项）即用 `%4I` 表达一组序列化后的选项值。

### 2.2 字符串编码细节（`s` / `S`）

- `%s`：**4 字节 NBO 长度** + `length` 字节内容，**末尾没有 `\0` 终止符**。读取方必须显式按长度截取，不能依赖 `\0`。
- `%S`：同上，但语义面向"任意字节"（raw），常用于剪贴板图片等二进制负载。
- 定宽如 `%7s`：读写都按 **固定字节宽度** 处理，无长度前缀。写超出截断、不足填 `0`；读固定读满字节数，填充到字符串中。用于 `Hello` 协议名（Synergy / Barrier 协议名固定 7 字符）。

### 2.3 一个典型的写入示例

```cpp
// 发送 HelloBack: "Barrier" 定宽7 + major(2) + minor(2) + screenName(%s)
ProtoUtil::writef(out, "%7s%2i%2i%s",
    &protocolName,   // 写入时按 7 字节定宽处理
    major, minor,
    &screenName);    // %s：4B NBO 长度 + bytes
```

---

## 3. 握手过程（Hello / HelloBack）

客户端连接 `24800` 后，服务端立刻发送第一帧 `Hello`：

| 阶段 | 方向 | 消息 | 格式 | 说明 |
|---|---|---|---|---|
| 1 | S → C | `Hello` | `%7s` + `%2i` + `%2i` | 协议名(7) + major + minor |
| 2 | C → S | `HelloBack` | `%7s` + `%2i` + `%2i` + `%s` | 协议名(7) + major + minor + 客户端屏幕名 |
| 3 | 后续 | 运行期校验消息序列 | — | 版本不匹配则发 `EICV` |

握手细节（实战要点）：

- `Hello` 由 **服务端** 先发，协议名解析出的值是 `"Barrier"`（7 字符，来自 `%7s`，固定宽度、无长度前缀）。**直接用关键字符串比对是陷阱**：那是 `%7s` 的 7 字节 raw，不是带前缀的长度字符串。
- 客户端的 `HelloBack` **协议名必须原样回传**（即服务端的 `"Barrier"`），随后是客户端协议版本 `1.x` 与客户端屏幕名（`%s`）。
- 主版本必须匹配；次要版本客户端取**双方较小值**（向下兼容）。
- 版本不兼容时服务端发 `EICV`（见下），`=%2i%2i` 携带期望的 major/minor。

---

## 4. 消息类型一览

> 帧体首 4 字节为消息类型 key（ASCII），其后才是格式化的参数部分。
> 因此 `readf` 通常用 `kMsgXxx + 4` 跳过 key 直接解析参数。消息 key 与参数之间**没有**分隔符。

### 4.1 握手 / 控制类

| key | 名称 | 格式（含 key） | 方向 | 说明 |
|---|---|---|---|---|
| `Hello` | Hello | `%7s%2i%2i` | S→C | 首帧，协议名 + 版本 |
| `HelloBack` | HelloBack | `%7s%2i%2i%s` | C→S | 回执 |
| `EICV` | Incompatible | `%2i%2i` | S→C | 版本不兼容，带期望版本 |
| `EBSY` | Busy | — | S→C | 服务忙 |
| `EUNK` | Unknown | — | S→C | 未知客户端 |
| `EBAD` | Bad | — | S→C | 协议违规 |
| `QINF` | QueryInfo | — | S→C | 查询屏幕信息，客户端答 `DINF` |
| `DINF` | DeviceInfo | `%2i%2i%2i%2i%2i%2i%2i` | C→S | x, y, w, h, 0, mx(鼠标x), my(鼠标y) |
| `DSOP` | SetOptions | `%4I` | S→C | 设置一组选项；**收到即代表握手完成，进入运行期** |
| `CROP` | ResetOptions | — | S→C | 重置选项 |
| `CIAK` | InfoAck | — | S→C | 信息确认 |
| `CBYE` | Close | — | S→C | 关闭连接 |
| `CNOP` | Noop | — | 双向 | 空操作 |

### 4.2 屏幕进入/离开（焦点切换）

| key | 名称 | 格式（含 key） | 说明 |
|---|---|---|---|
| `CINN` | Enter | `%2i%2i%4i%2i` | 进入屏幕：x, y, 会话序号, 修饰键 mask。**x=-1 且 y=-1 表示只切修饰键、不移动指针** |
| `COUT` | Leave | — | 离开屏幕（回到主屏） |

### 4.3 键盘事件

| key | 名称 | 格式（含 key） | 说明 |
|---|---|---|---|
| `DKDN` | KeyDown | `%2i%2i%2i` | id(KeyID), mask, button |
| `DKDN1.0` | KeyDown1_0 | `%2i%2i` | 老版本无 button |
| `DKUP` | KeyUp | `%2i%2i%2i` | id, mask, button |
| `DKUP1.0` | KeyUp1_0 | `%2i%2i` | 老版本 |
| `DKRP` | KeyRepeat | `%2i%2i%2i%2i%s` | id, mask, count, button, lang |
| `DKDL` | KeyDownLang | `%2i%2i%2i%s` | id, mask, button, lang |
| `LSYN` | LanguageSync | `%s` | 语言同步 |

### 4.4 鼠标事件

| key | 名称 | 格式（含 key） | 说明 |
|---|---|---|---|
| `DMMV` | MouseMove | `%2i%2i` | 绝对位移到 (x, y) |
| `DMRM` | MouseRelMove | `%2i%2i` | 相对位移 (dx, dy) |
| `DMDN` | MouseDown | `%1i` | 按钮 id 按下 |
| `DMUP` | MouseUp | `%1i` | 按钮 id 抬起 |
| `DMWM` | MouseWheel | `%2i%2i` | xDelta, yDelta |
| `DMWM1.0` | MouseWheel1_0 | `%2i` | 老版本只有 yDelta |

### 4.5 剪贴板 / 拖拽 / 安全

| key | 名称 | 格式（含 key） | 说明 |
|---|---|---|---|
| `CINN` 相关 `seq` | 会话序号 | — | 剪贴板事务用 |
| `CCLP` | ClipboardChipset | `%1i%4i` | id, seq |
| `DCLP` | ClipboardData | `%1i%4i%1i%s` | id, seq, mark, data |
| `DFTR` | FileTransfer | `%1i%s` | mark, data |
| `DDRG` | DragInfo | `%2i%s` | fileCount, info |
| `CSEC` | ScreenSaver | `%1i` | 屏保开关 |
| `SECN` | SecureInput | `%s` | 安全输入通知 |

---

## 5. 运行期确认（回 CNOP）

Deskflow / Barrier 的服务端在收到客户端消息后会依赖"客户端响应"来维持交互节奏。为避免服务端阻塞等待（其底层有"BSD 延迟 ACK"相关的处理逻辑），本工程的客户端在**握手完成（收到 `DSOP`）之后**，对每一条处理完成的消息都回一条 `CNOP` 帧：

```
if (m_handshakeDone) {
    writeFrame("CNOP", 4);   // 4 字节 key，无额外参数
}
```

这是踩坑后的补强：**不回 `CNOP` 时，Barrier 服务端较容易在写事件时超时并把控制权拉回主屏**。详见 `TROUBLESHOOTING.md` 中"运行时回 CNOP"条目。

---

## 6. 心跳机制

- 服务端每 **3 秒** 发送一次 `CALV`（KeepAlive）。
- 客户端必须在 **9 秒**（约 3 次心跳间隔）内收到并响应，否则视为 **flatline（心跳超时）**，连接会被服务端判定为死亡。
- 客户端响应仍是退一个 `CALV` 帧：

```
if (key == "CALV") writeFrame("CALV", 4);
```

实现要求：空闲期 `recv` 不能长时间阻塞导致错过心跳。本工程连接置为 blocking 同时依赖服务端 3s 心跳维持 `recv` 活跃；若需自行实现保活，应把心跳超时纳入连接看门狗。

---

## 7. 键码与修饰键

### 7.1 KeyID

Deskflow 使用 **KeyID** 标识键：**控制键 = X11 keysym − `0x1000`**；可打印字符键 = 对应 Unicode 码点。

| KeyID | 键 | KeyID | 键 |
|---|---|---|---|
| `0xEF08` | BackSpace | `0xEF50` | Home |
| `0xEF09` | Tab | `0xEF51` | Left |
| `0xEF0D` | Return | `0xEF52` | Up |
| `0xEF1B` | Escape | `0xEF53` | Right |
| `0xEF61` | PrintScreen | `0xEF54` | Down |
| `0xEFE1` / `0xEFE2` | ShiftL / ShiftR | `0xEFE3` / `0xEFE4` | ControlL / ControlR |
| `0xEFE7` | MetaL | `0xEFE9` / `0xEFEA` | AltL / AltR |
| `0xEFEB` / `0xEFEC` | SuperL / SuperR | `0xEFBE` … `0xEFC9` | F1 … F12 |

> 原生定义见 `protocol_types.h` 的 `kKey*` 常量。

### 7.2 KeyModifierMask（修饰键掩码位）

| Bit | 值 | 修饰键 |
|---|---|---|
| Shift | `0x0001` | Shift |
| Control | `0x0002` | Ctrl |
| Alt | `0x0004` | Alt |
| Meta | `0x0008` | Meta |
| Super | `0x0010` | Super/Win |
| AltGr | `0x0020` | AltGr |
| CapsLock | `0x1000` | Caps Lock |
| NumLock | `0x2000` | Num Lock |
| ScrollLock | `0x4000` | Scroll Lock |

> 注意：锁键位（`0x1000` 起）与普通修饰键位（`0x0001` 起）**相互独立、不重叠**。

### 7.3 鼠标按钮 ID（协议层）

| ButtonID | 含义 |
|---|---|
| `1` | 左键 |
| `2` | 中键 |
| `3` | 右键 |
| `4` | 滚轮上 |
| `5` | 滚轮下 |

> 前 3 个直接映射 HarmonyOS `MOUSE_BUTTON_*`；`4/5` 在注入层转换为 `MOUSE_AXIS_SCROLL_*` 轴事件（见 `INPUT_INJECTION.md`）。

---

## 8. 本工程编解码实现（`protocol/`）

- `proto_util.{h,cpp}`：`ProtoUtil::writef` / `readf` 格式串实现（含 `i / I / s / S / 定宽 / %%`）。
- `socket_stream.{h,cpp}`：`readFrame` / `writeFrame`（4 字节 NBO 长度前缀）+ buffered `readExact` / `writeAll`。
- `protocol_types.h`：全部消息 key 与格式常量、KeyID、按钮 id、协议版本。
- `deskflow_client.{h,cpp}`：握手、消息分发（`handleMessage`）、注入接口。

消息分发在 `deskflow_client.cpp::handleMessage` 中，先读 4 字节 key 再按 key 路由，参数用 `kMsgXxx + 4` 跳过 key 解析。

---

## 9. 解析模块的健壮性建议（防范阻塞）

- **长度前缀必须校验上限**（如 > 4 MB 视为非法），否则恶意/错误长度会让连接永久挂在 `recv`。
- `readf` 中途 `readExact` 失败要立刻中断并返回失败，把连接判死，避免在错误状态里继续解析后续帧。
- 忽略型消息（如 `LSYN` / `DKRP` / `SECN`）也必须**按格式把负载读干净**，否则剩余字节会污染下一帧的 key 对齐，导致后续消息解析全部错位/阻塞。
- 字符串读取按 4 字节长度截取，**不要信任 `\0` 终止符**。

---

*由本项目实战总结。*
