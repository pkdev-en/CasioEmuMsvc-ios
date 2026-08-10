# 📲 在 iOS 上使用 SideStore 安装 CasioEmuMsvc 的教程

> 本教程适用于希望在 **iPhone/iPad** 上安装卡西欧模拟器的用户，**无需越狱**。

---

## 📋 前提条件

- iPhone 或 iPad，运行 **iOS 14 或更高版本**
- **Apple ID**（免费账号即可）
- 已安装并配置好 **SideStore**
- **CasioEmuMsvc.ipa** 文件（从 [GitHub Releases](https://github.com/pkdev-en/CasioEmuMsvc-ios/releases) 下载）

---

## 🔧 第一步 — 安装 SideStore（若尚未安装）

> 如果你已经安装了 SideStore，请直接跳到第二步。

前往 [sidestore.io](https://sidestore.io) 按照官方说明操作。

> ⚠️ **中国大陆用户注意**：访问 sidestore.io 及后续连接 Anisette 服务器时需要**科学上网（代理/VPN）**。
> 安装完成后，SideStore 使用 **LocalDevVPN（回环 VPN）** 进行续签，与代理 VPN 不能同时运行，请在续签时**暂时关闭科学上网工具**，并确保设备连接 **Wi-Fi**（使用移动数据时无法续签）。

---

## 📥 第二步 — 下载 IPA 文件

1. 访问项目的 **Releases** 页面：  
   👉 [github.com/pkdev-en/CasioEmuMsvc-ios/releases](https://github.com/pkdev-en/CasioEmuMsvc-ios/releases)

2. 下载最新版本的 **`CasioEmuMsvc.ipa`** 文件。

3. 将文件保存到 **文件（Files）→ 我的 iPhone** 或 iCloud 云盘中。

---

## 📲 第三步 — 使用 SideStore 安装 IPA

1. 打开 iPhone 上的 **SideStore** 应用。

2. 开启 SideStore 的 **LocalDevVPN**（点击界面中的连接按钮），确保已连接。

3. 切换到底部导航栏的 **My Apps**（我的应用）选项卡。

4. 点击右上角的 **＋** 号。

5. 选择刚才下载的 **CasioEmuMsvc.ipa** 文件 → 点击 **打开**。

6. 若提示登录 Apple ID，输入账号信息后点击 **Install（安装）**。

7. 等待几秒钟，应用图标将出现在主屏幕上。

---

## ✅ 第四步 — 首次启动

1. 从主屏幕打开 **CasioEmuMsvc**。

2. 点击 **Import（导入）** 按钮。

3. 选择你要导入的卡西欧模拟器包（`.package` 格式）。

4. 输入密码。

5. 输入密码后点击 **all**，即可看到你的模拟器列表。

6. 点击你想使用的模拟器，然后点击 **Launch（启动）**。

7. 尽情享用吧！🥳

---

## 🔄 续签应用（避免 7 天后失效）

免费 Apple ID 签名的应用仅有效 **7 天**，到期后应用将无法打开。续签方法：

- 打开 **SideStore**，连接 **LocalDevVPN**，进入 **My Apps（我的应用）**。
- 点击 **Refresh All（全部刷新）**，或单独点击应用名称后选择 **Refresh（刷新）**。
- 在 SideStore 设置中开启 **后台刷新（Background Refresh）** 可实现自动续签。

> **注意**：续签时请确保设备已连接 **Wi-Fi**，移动数据下无法续签。  
> **提示**：如果想突破免费账号 3 个应用的限制，可以配合使用 **LiveContainer** :)

---

## ❗ 常见问题

| 错误 | 解决方案 |
|------|----------|
| 应用 7 天后被删除 | 在到期前通过 SideStore 续签 |
| SideStore VPN 无法连接 | 检查 SideStore 设置中的 **配对文件（Pairing File）** |
| 提示"App could not be installed" | 前往**设置 → 通用 → VPN 与设备管理**，信任对应的开发者证书 |
| 使用流量时无法续签 | SideStore 续签需要 **Wi-Fi 连接**，请切换至 Wi-Fi 后重试 |

---

## 💬 支持与社区

- 💬 **Discord**：[discord.gg/hCEBVQMcmX](https://discord.gg/hCEBVQMcmX)
- 💬 **Discord（备用服务器）**：[discord.gg/tEMNCgBGeB](https://discord.gg/tEMNCgBGeB)
- 📧 **邮箱**：[khaiphu2015@gmail.com](mailto:khaiphu2015@gmail.com)
- 🐛 **问题反馈**：[GitHub Issues](https://github.com/pkdev-en/CasioEmuMsvc-ios/issues)

---

> ⚠️ **法律声明**：本应用依据 **GPL-3.0** 协议授权。用户须自行承担其所在地区使用 ROM 文件的法律责任。

---

> If anything is wrong, please let me know because I am not Chinese.
