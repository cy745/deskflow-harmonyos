# 签名配置说明（本地机密，勿提交）

本工程的 HarmonyOS 应用签名（证书、profile、密钥库及密码）属于**本地机密**，存放在
用户主目录 `~/.ohos/config/` 下，**不随仓库分发**。

## 签名文件位置

| 类型 | 符号名 | 位置 |
|---|---|---|
| 应用证书 | `*.cer` | `~/.ohos/config/` |
| 签名 profile | `*.p7b` | `~/.ohos/config/` |
| 密钥库 | `*.p12` | `~/.ohos/config/` |
| 构建配置 | `build-profile.json5` | 仓库根（已移除签名段） |

`build-profile.json5` 在上传前已移除 `signingConfigs`，避免把 `.p12` 绝对路径与口令哈希带入仓库。

## 本地配置步骤（每位开发者各做一次）

1. 用 **DevEco Studio** 打开工程（`File → Open` 选择仓库根目录）。
2. 菜单 `File → Project Structure → Signing Configs`：
   - 登录你的华为开发者账号。
   - 勾选 **Automatically generate signature**。
   - 连接已开启「开发者模式」的鸿蒙电脑（设置 → 关于本机 → 连点版本号 → 开发者选项 → USB 调试），绑定设备后生成调试证书。
3. DevEco 会把 `signingConfigs` 写回本机的 `build-profile.json5`。
4. 点击 **Run** 部署到设备；或命令行：

   ```bat
   hvigorw assembleHap --mode module -p product=default
   hdc install -r entry\build\default\outputs\default\entry-default-signed.hap
   ```

## 提交前检查

把下面的指令加到你的 `.git/hooks/pre-commit`（或记得手动执行）：

```bash
grep -rn --include='*.json5' -E '\.(p12|p7b|cer)|storePassword|keyPassword|\.ohos/config' . || true
```

确保输出为空再 `git add -A`。

## CI / 无头构建（可选）

如需在 CI 无交互签名构建，把签名材料经 CI Secret 注入，并在构建前生成
`build-profile.json5` 的签名段（不要存仓库）。示例（仅结构，值来自 CI 变量）：

```json5
"signingConfigs": [
  {
    "name": "release",
    "type": "HarmonyOS",
    "material": {
      "certpath": "${CERT_PATH}",
      "keyAlias": "${KEY_ALIAS}",
      "keyPassword": "${KEY_PASSWORD}",
      "profile": "${PROFILE_PATH}",
      "signAlg": "SHA256withECDSA",
      "storeFile": "${STORE_PATH}",
      "storePassword": "${STORE_PASSWORD}"
    }
  }
]
```
