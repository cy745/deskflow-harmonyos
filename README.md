# Deskflow for HarmonyOS

在华为鸿蒙电脑（HarmonyOS 6 / API 20+，PC/2in1）上，让鸿蒙电脑作为 **Deskflow / Barrier
协议客户端**，接入一台 Windows/Linux/macOS 上的 Deskflow 服务端，实现**多机键鼠共享**：
把鼠标与键盘在 Windows 与鸿蒙电脑之间无缝穿越，在鸿蒙屏幕上操作用鸿蒙的应用、文件和界面。

> 这是一个 **Deskflow fork**：协议层参考 [deskflow/deskflow](https://github.com/deskflow/deskflow)
> （GPL-2.0）实现，工程骨架基于 OpenHarmony 官方示例
> [NDKInputEventInterceptor](https://gitcode.com/openharmony/applications_app_samples/tree/master/code/DocsSample/InputKit/NDKInputEventInterceptor)
> 改造。详见文末[许可证](#许可证)。

## 特性

- ✅ **协议互通**：与 Windows Deskflow 1.26 Server（协议名 `Barrier`，v1.8，TCP 24800）完整握手并进入运行期。
- ✅ **鼠标移动 / 点击 / 拖动 / 滚轮**：鸿蒙屏幕上可移动、点击、**拖动窗口与桌面图标**、滚动页面。
- ✅ **键盘输入**：可打印字符、方向键、功能键、修饰键（Shift/Ctrl/Alt）同步。
- ✅ **双向切换**：鼠标从 Windows 右缘进入鸿蒙屏幕，从鸿蒙左缘返回 Windows。
- ✅ **全局注入**：基于 `OH_Input_RequestInjection` + `OH_Input_InjectMouseEventGlobal` / `InjectKeyEvent`，作用于桌面任意应用。
- ✅ **事件拦截诊断**：内置 `OH_Input_AddInputEventInterceptor`，可抓取真实鼠标/触摸/轴事件序列，便于对比与调试（也是本项目还原拖动问题的关键手段）。

## 架构

```
┌─────────────────────────┐     ┌──────────────────────────────────────────┐
│  鸿蒙电脑 (HarmonyOS)   │     │  Windows / Linux  (Deskflow Server)        │
│                         │     │  deskflow-core · Barrier v1.8 · TCP 24800 │
│  ArkUI 页面 (Index.ets) │◄────┼────────────────────────────────────────────│
│  NAPI (napi_init.cpp)   │     │  Hello/HelloBack · QINF/DINF · CINN/COUT   │
│  Protocol 层            │◄────│  DMMV/DMDN/DMUP/DMWM · DKDN/DKUP/DKRP      │
│  (deskflow_client)      │◄────│  CALV 心跳 · DSOP · LSYN · DCLP/CCLP        │
│  OH_Input 注入           │     └──────────────────────────────────────────┘
└─────────────────────────┘
         │
         ▼
  HarmonyOS Input Kit (libohinput.so)
```

- **ETs UI**：`entry/src/main/ets/pages/Index.ets` — 拦截演示、连接配置（host/port/screen name）、注入测试按钮、状态回显。
- **NAPI 桥接**：`entry/src/main/cpp/napi_init.cpp` — 暴露 `onDeskflowStatus` / `connectDeskflow` / `disconnectDeskflow` 给 ArkTS；用线程安全函数（tsfn）把工作线程状态安全投递到 JS 线程。
- **协议层**（`entry/src/main/cpp/protocol/`，GPL-2.0）：
  - `socket_stream` — POSIX TCP + 帧封装（4 字节 NBO 长度前缀 + payload）。
  - `proto_util` — `writef/readf` 实现 Deskflow/Barrier 字节语义。
  - `deskflow_client` — 握手、消息循环、CNOP 延迟 ACK、心跳、鼠标/键盘/滚轮注入。
  - `protocol_types` — 消息格式串、KeyID、KeyModifierMask、按钮 ID 常量。

## 环境要求

| 项目 | 要求 |
|---|---|
| 鸿蒙设备 | HarmonyOS 6 / API 20+，**PC / 2in1**（全局注入仅 2in1 生效） |
| SDK | DevEco Studio，HarmonyOS 6.1.1（API 24） |
| Deskflow 服务端 | 1.26（也可用其它 Barrier/Synergy 服务端，需协议 v1.8） |
| 局域网 | 两台机器同网段，服务端开放 TCP 24800 |

## 构建与部署

```bash
# 1. 打开工程（DevEco Studio File → Open 选择仓库根目录）

# 2. 配置签名（本地机密，不入库）
#    File → Project Structure → Signing Configs → 登录并勾选 Automatically generate signature
#    连接已开「开发者模式」的鸿蒙电脑，绑定设备后生成调试证书。
#    详见 signingConfigs/README.md

# 3. 构建
hvigorw assembleHap --mode module -p product=default

# 4. 安装到设备（设备需 USB/无线调试连接）
hdc install -r entry\build\default\outputs\default\entry-default-signed.hap

# 5. 启动
hdc shell aa start -a EntryAbility -b com.deskflow.ohospoc
```

## 使用

### 鸿蒙端

1. 打开 App，进入注入区，点击 `Request injection authorization`，按系统弹窗授权（API 20+ 仅 PC/2in1）。
2. 在连接区填入 Deskflow 服务端 IP（如 `192.168.3.116`）、端口（`24800`）、屏幕名（需与服务端配置一致），点「连接 Deskflow Server」。

### Deskflow 服务端

在服务端配置文件中，把鸿蒙电脑列入屏幕与链路。示例 `deskflow-server.conf`：

```
section: screens
    WindowsPC:
    HarmonyPC:
end

section: links
    WindowsPC:
        right = HarmonyPC
    HarmonyPC:
        left = WindowsPC
end

section: options
    relativeMouseMoves = false
    clipboardSharing = false   # 见「已知限制」
end
```

> **服务端需 `clipboardSharing = false`**：deskflow 1.26.0.0 在鼠标穿越抓取剪贴板图片
> （负 biHeight 的 DIB）时会崩溃（`0xc0000409`，见 [deskflow#9869](https://github.com/deskflow/deskflow/issues/9869)）。
> 本工程尚未实现剪贴板，关闭不影响现有功能。

穿越：把 Windows 鼠标移到右缘 → 进入鸿蒙屏幕；鸿蒙屏幕鼠标移到左缘 → 回到 Windows。

## 验证结果

在 HUAWEI MateBook Pro S（MOR-W52，HarmonyOS 6.1.0.135）＋ Windows 11 Deskflow 1.26 实测：

| 功能 | 结果 |
|---|---|
| 连接握手（Barrier v1.8） | ✅ |
| 鼠标移动 | ✅ |
| 鼠标点击 | ✅ |
| 鼠标拖动（窗口/图标） | ✅ |
| 滚轮滚动 | ✅ |
| 键盘输入 | ✅ |
| 双向屏幕切换 | ✅ |
| 服务端稳定（穿越不崩） | ✅ |
| 剪贴板同步 | ⏳ 未实现 |

## 文档

- [架构说明](docs/ARCHITECTURE.md)
- [协议编解码规范](docs/PROTOCOL.md)
- [输入注入实战与坑](docs/INPUT_INJECTION.md)
- [踩坑记录与解决方案](docs/TROUBLESHOOTING.md)
- [签名配置说明](signingConfigs/README.md)

## 参与贡献

欢迎报告 Issue、提交 PR 或改进文档。请先阅读[参与贡献指南](CONTRIBUTING.md)
（含本项目特别注意事项：协议层 GPL 头、readf 返回值、actionTime=-1、坐标 vp 等约定）。

## 已知限制

- **剪贴板同步（DCLP/CCLP）未实现**，且服务端需关闭 clipboardSharing 以避免其崩溃 bug。
- **断线自动重连 / 错误分类**未完善。
- 键盘映射覆盖常用键，个别媒体/厂商键未映射。
- 需先手动点连接；暂无开机自启、后台驻留。

## 许可证

本仓库采用**文件级混合许可**（各文件头标注了 SPDX 标识）：

- 协议实现与客户端代码（`entry/src/main/cpp/protocol/`）沿用上游 [deskflow/deskflow](https://github.com/deskflow/deskflow)
  的许可证：**GPL-2.0-only WITH LicenseRef-OpenSSL-Exception**。
- 基于 OpenHarmony 官方示例 [NDKInputEventInterceptor](https://gitcode.com/openharmony/applications_app_samples/tree/master/code/DocsSample/InputKit/NDKInputEventInterceptor)
  衍生/改造的工程骨架与 UI 文件保留其 **Apache License 2.0** 文件头。
- 各文件遵循 REUSE 规范的 SPDX 标识；根目录 `LICENSE` 提供 GPL v2 全文。

HarmonyOS Input Kit（`oh_input_manager.h` 及 `libohinput.so`）为华为/OpenHarmony 提供的
系统能力，遵循其各自许可，非本仓库代码。

## 参考

- Deskflow：https://github.com/deskflow/deskflow
- oh_input_manager.h（HarmonyOS Input Kit C API）：https://developer.huawei.com/consumer/cn/doc/harmonyos-references-V13/oh__input__manager_8h-V13
- OpenHarmony 事件拦截开发指导：https://gitcode.com/openharmony/docs/blob/master/zh-cn/application-dev/device/input/interceptor-guidelines.md
