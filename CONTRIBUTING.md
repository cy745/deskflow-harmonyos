# 参与贡献指南

感谢你对 Deskflow for HarmonyOS 的关注！欢迎提交 Issue、Pull Request、改进文档或参与讨论。

## 项目现状

这是一个 **Deskflow fork**，聚焦于让鸿蒙电脑（HarmonyOS 6 / API 20+，PC/2in1）作为
Deskflow/Barrier 协议客户端接入服务端。当前核心链路（握手、消息循环、鼠标/键盘/滚轮注入、
双向切换）已可用；**剪贴板同步、断线自动重连、错误分类**等尚未实现，是很好的切入点。

## 如何开始

1. 阅读 `README.md` 与 `docs/` 下的四篇文档：
   - `docs/ARCHITECTURE.md` — 了解分层与数据流
   - `docs/PROTOCOL.md` — 协议编解码规范
   - `docs/INPUT_INJECTION.md` — 输入注入实战与坑
   - `docs/TROUBLESHOOTING.md` — 踩坑记录与解决方案
2. 用 DevEco Studio 打开工程，配置本地签名（见 `signingConfigs/README.md`），
   在鸿蒙 2in1 设备上跑通连接与穿越。

## 提交 Issue

- **Bug 报告**：请描述设备型号与系统版本（如 MOR-W52 / HarmonyOS 6.1）、Deskflow 服务端版本、
  复现步骤、期望与实际行为、以及 `hilog` 中 `DeskflowPoC` 域日志（`hdc shell "hilog | grep DeskflowPoC"`）。
- **功能建议**：说明使用场景与预期收益。

## 提交 PR

1. Fork 本仓库并克隆到本地。
2. 新建分支：`git checkout -b feat/your-feature`。
3. 编码时注意：
   - **协议层**（`entry/src/main/cpp/protocol/`）保持与 deskflow 一致的
     `GPL-2.0-only WITH LicenseRef-OpenSSL-Exception` 文件头，并保留来源版权致谢。
   - **NAPI/UI**（`napi_init.cpp`、`index.ets`）保留 `Apache-2.0` 文件头。
   - 新增消息处理统一检查 `readf` 返回值，读取失败即返回 `false`，避免负载残留污染下一帧对齐
     （详见 `docs/TROUBLESHOOTING.md` §7）。
   - 注入事件 `actionTime` 一律传 `-1`，坐标用 vp 且 `displayX/Y` 与 `globalX/Y` 双设
     （详见 `docs/INPUT_INJECTION.md`）。
   - 不加个人/机器相关绝对路径，不提交签名文件与密码。
4. 运行 `hvigorw assembleHap` 确保构建通过，在鸿蒙 2in1 上验证无回归。
5. 提交并推送到你的 fork，开 PR 到 `main`。PR 描述请说明改动动机与验证结果。

## 编码约定

- C++ 使用 `std::atomic` 保护跨线程（worker 线程 ↔ JS/MMI 线程）状态。
- 全局注入回调（MMI IPC 线程）不得直接调 JS，必须经线程安全函数（tsfn）。
- 日志：关键路径用 `DF_LOGI`，诊断/高频路径用 `DF_LOGD`（编译期 `DF_VERBOSE_MESSAGE_LOG`）。
- 提交信息遵循 Conventional Commits（`feat:` / `fix:` / `docs:` / `refactor:` 等）。

## 行为准则

保持友善、尊重、专业。不发布与问题无关的个人信息。
