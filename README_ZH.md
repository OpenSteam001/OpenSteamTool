<div align="center">
  <img src="docs/bst.png" width="180" alt="BetterSteamTools logo">

  <h1>OpenSteamTool</h1>

  <p>
    <strong>开源 Steam 解锁工具</strong>
  </p>

  <p>
    <img src="https://img.shields.io/badge/C%2B%2B-20%2B-2ea44f?logo=cplusplus&logoColor=white" alt="C++ 20+">
    <img src="https://img.shields.io/badge/CMake-3.20%2B-2ea44f?logo=cmake&logoColor=white" alt="CMake 3.20+">
    <img src="https://img.shields.io/badge/Windows-only-d73a49?logo=windows&logoColor=white" alt="仅 Windows">
    <a href="https://deepwiki.com/OpenSteam001/OpenSteamTool">
      <img src="https://deepwiki.com/badge.svg" alt="Ask DeepWiki">
    </a>
  </p>

  <p>
    <a href="README.md">
      <img src="https://flagcdn.com/w40/us.png" width="22" alt="United States flag">
      English
    </a>
    &nbsp;|&nbsp;
    <a href="README_ES.md">
      <img src="https://flagcdn.com/w40/es.png" width="22" alt="Spain flag">
      Español
    </a>
    &nbsp;|&nbsp;
    <a href="README_ZH.md">
      <img src="https://flagcdn.com/w40/cn.png" width="22" alt="China flag">
      中文
    </a>
  </p>
</div>

## 功能特性

### 核心解锁
- 解锁任意数量未拥有的游戏
- 解锁未拥有游戏的所有 DLC
- 支持从 Lua 配置自动加载仓库（depot）解密密钥
- 支持通过 `opensteamtool` / `steamrun` / `wudrm` 上游 API 自动下载 manifest（默认为 `opensteamtool`），或通过自定义 Lua 端点（参见 [通过 Lua 获取 Manifest](#通过-lua-获取-manifest)）
- 支持下载需要访问令牌的保护游戏或 DLC
- 支持绑定 manifest 以防止特定游戏被更新

### 热重载
- 在任何监视目录中添加、修改、删除或覆盖 `.lua` 文件会自动触发重载。无需重启，无需切换离线/在线模式

### 注入
- 通过 `opensteamtool.toml` 中的 `[inject]` 添加可选的游戏进程库注入
- 配置 `enabled`、`library_x64` 和 `library_x86`；注入的库必须与目标进程架构匹配。路径可以是绝对路径，也可以是相对于 Steam 根目录的相对路径

### 家庭共享和远程同乐
- 绕过 Steam 家庭共享限制，适用于通过 Lua 中 `addappid` 添加到库的游戏。参与共享的 Steam 家庭中的所有账户都必须使用 OpenSteamTool 才能生效

### 兼容 Denuvo 和 SteamStub 保护的游戏
- 仅 SteamStub 保护的游戏不需要配置 `AppTicket`。OpenSteamTool 可以重用 Steam 本地 ConfigStore 令牌，通过 SteamDRMP 令牌解析漏洞伪造请求的 AppId，无需注入游戏进程
- Denuvo 保护的游戏仍需要显式令牌数据。OpenSteamTool 通过平台凭据存储保存 `AppTicket` 和 `ETicket`
- 在 Lua 配置中使用 `setAppTicket(appid, "hex")` 和 `setETicket(appid, "hex")` 自动将这些值写入平台凭据存储
- Denuvo 验证有 30 分钟有效窗口。过期后授权可能失败，显示 Denuvo 错误代码 `88500005`；重试前请刷新令牌数据
- AppTicket 优先级：显式令牌优先级最高，包括通过 `setAppTicket` 配置的令牌和已缓存的 `AppTicket` 凭据值。若无可用显式 AppTicket，OpenSteamTool 回退到伪造的本地 ConfigStore 令牌路径
- SteamID 优先级：优先读取缓存的 `SteamID`；若缺失，则从显式 `AppTicket` 解析。在 Windows 上，凭据存储后端当前使用 `HKCU\Software\Valve\Steam\Apps\<AppId>`。Linux 后端尚未实现

#### 使用 `extract_tickets` 提取令牌

`extract_tickets` 工具会转储你需要的 `AppTicket` 和 `ETicket` 十六进制字符串，用于 `setAppTicket` / `setETicket`。在 Steam 运行并登录到**拥有**目标游戏的账户的机器上运行它。

1. 构建工具（参见 [构建](#构建)）；二进制文件位于 `build/tools/Release/extract_tickets.exe`
2. 使用目标 AppId 运行它（或不带参数运行，在提示时输入 AppId）：
   ```powershell
   extract_tickets.exe 1361510
   ```
3. 它从注册表读取 Steam 安装路径，加载 `steamclient64.dll`，并将所有内容写入可执行文件旁边的 `<appid>/` 文件夹：
   - `appticket.bin` — 原始应用所有权令牌（二进制）
   - `eticket.bin` — 原始加密应用令牌（二进制）
   - `tickets.txt` — 包含十六进制字符串的纯文本摘要：
     ```
     appid:1361510
     appticket(184 bytes):14000000...
     eticket(143 bytes):...
     ```
   无法获取的令牌报告为 `appticket:null` / `eticket:null`
4. 将 `tickets.txt` 中的十六进制字符串粘贴到你的 Lua 配置中：
   ```lua
   setAppTicket(1361510, "14000000...")
   setETicket(1361510, "...")
   ```

> **注意：** 令牌仅当从**真正拥有**游戏的账户提取时才有效

### 统计和成就
- 为未拥有的游戏启用统计和成就
- 使用 `setStat(appid, "steamid")` 配置拉取哪个 SteamID 的成就数据
- 如果某个应用未配置 `setStat`，当 `[stats] enable_api = true`（默认）时，OpenSteamTool 查询 `https://stats.opensteamtool.com/{appid}`
- 优先级：`setStat` > stats API（启用且有效时）> 硬编码预设 SteamID `76561198028121353`

### 在线修复
- 在 Steam 启动参数中添加 `-onlinefix` 以在需要使用大厅匹配的游戏里启用基于 480 的在线游戏。当前限制是同一时间只能运行一个这样的游戏。要撤销，只需从启动参数中移除 `-onlinefix` — 下次启动时在线游戏恢复正常

## 未来计划
- Steam 云同步支持。（这是个超级大工程）

## 使用方法

1. 在项目根目录运行 `build.bat` 构建项目
2. 将生成的 `dwmapi.dll`、`xinput1_4.dll` 和 `OpenSteamTool.dll` 复制到 Steam 根目录
3. 创建 Lua 目录（例如 `C:\steam\config\lua`）并将 Lua 脚本放在那里。DLL 会自动加载并执行它们
4. Lua 示例：
```lua
addappid(1361510) -- 解锁 appid 为 1361510 的游戏

addappid(1361511, 0,"5954562e7f5260400040a818bc29b60b335bb690066ff767e20d145a3b6b4af0") -- 解锁 appid 为 1361511 的游戏，depotKey 为 "5954562e7f5260400040a818bc29b60b335bb690066ff767e20d145a3b6b4af0" 

addtoken(1361510,"2764735786934684318") -- 为 appid 为 1361510 的游戏添加访问令牌 ("2764735786934684318") 
-- 不再支持：
--pinApp(1361510) -- 固定 appid 为 1361510 的游戏以防止其被更新

setManifestid(1361511,"5656605350306673283") -- 固定 depotid:1361511 manifest_gid:5656605350306673283，大小默认为 0
setManifestid(1361511,"5656605350306673283", 12345678) -- 同上，但指定明确大小

setAppTicket(1361510,"0100000000000000...") -- 将 AppTicket 写入凭据存储；在 Windows 上：HKCU\Software\Valve\Steam\Apps\1361510\AppTicket

setETicket(1361510,"0100000000000000...") -- 将 ETicket 写入凭据存储；在 Windows 上：HKCU\Software\Valve\Steam\Apps\1361510\ETicket

setStat(1361510, "76561197960287930") -- 使用指定 SteamID 的成就数据用于 appid 1361510
-- 若未配置，启用时使用 stats API；否则使用默认 SteamID 76561198028121353
```

所有函数名**不区分大小写**。`setAppTicket`、`setappticket`、`SetAppticket`、`SETAPPTICKET` 等都是等价的。每个注册的函数都适用（`addAppId`、`AddToken`、`SETManifestid` 等）。

### 配置（可选）

将 `opensteamtool.example.toml` 重命名为 `opensteamtool.toml` 并放在 Steam 根目录（与 `steam.exe` 同级）。
若找不到配置文件，则使用内置默认值——不会自动创建。
文件在 Steam 运行时被监视；有效更改会热重载，无需重启 Steam。

```toml
[log]
# 仅调试构建。级别：trace、debug、info、warn、error
level = "info"

[manifest]
# 仓库 manifest 请求码的上游 API。选项："opensteamtool"、"steamrun"、"wudrm"
url = "opensteamtool"

# manifest 请求的 HTTP 超时（毫秒）
timeout_resolve_ms = 5000
timeout_connect_ms = 5000
timeout_send_ms    = 10000
timeout_recv_ms    = 10000

[stats]
# 当没有 Lua setStat 覆盖时查询 https://stats.opensteamtool.com/{appid}
# 优先级：setStat > stats API > 硬编码预设 SteamID
enable_api = true

# 额外的 Lua 配置目录（可选）
# 文件在默认 <Steam>/config/stplug-in 文件夹之后加载
# 默认文件夹总是最后加载，因此用户文件优先级更高
[lua]
paths = []

[inject]
# 可选的游戏进程库注入
# 注入的库必须与目标进程架构匹配
enabled = false
# library_x64 = "OpenSteamTool.GameHook.x64.dll"
# library_x86 = "OpenSteamTool.GameHook.x86.dll"

# 可选元数据镜像。参见下面的"Steam 版本兼容性"
[remote]
# url_template = "https://your.server/{channel}/{component}/{sha256}.toml"
```

### 通过 Lua 获取 Manifest

支持两个 manifest 码函数：

#### `fetch_manifest_code(gid)`

基础函数，只接收 manifest GID

#### `fetch_manifest_code_ex(app_id, depot_id, gid)` *（推荐）*

扩展函数，接收 `app_id`、`depot_id` 和 `gid`。允许构造需要应用识别的 API 端点

C++ 运行时提供两个 Lua 辅助函数：

| 函数 | 签名 | 返回值 |
|------|------|--------|
| `http_get`  | `http_get(url [, headers])`       | `body, status_code` |
| `http_post` | `http_post(url, body [, headers])` | `body, status_code` |

`headers` 是可选表：`{["Key"]="Value", ...}`

### Steam 版本兼容性

OpenSteamTool 不再在 DLL 中内置字节模式签名。相反，每次启动时它计算磁盘上 `steamclient64.dll` 和 `steamui.dll` 的 SHA-256，并从上游跟踪器 [`OpenSteam001/steam-monitor`](https://github.com/OpenSteam001/steam-monitor)（`pattern` 分支）查找匹配的模式文件

查找顺序（每次启动）：

1. **GitHub raw** — `https://raw.githubusercontent.com/OpenSteam001/steam-monitor/pattern/...`。规范来源
2. **jsDelivr CDN** — 如果 GitHub raw 无法访问（连接拒绝/超时/5xx）时自动回退。无需配置。在 `raw.githubusercontent.com` 被封锁但 jsDelivr 可访问的地区很有用（如中国大陆）
3. **本地缓存** — `<Steam>\opensteamtool\pattern\<subdir>\<sha256>.toml`。仅当远程不可达时使用。每次成功远程获取后覆盖缓存

每次启动都会咨询远程，因此用户自动获取上游重新发布（例如机器人添加新签名或修复现有签名），无需清除任何缓存

如果某步返回 **HTTP 404**，镜像循环立即停止——所有镜像提供相同内容，因此 404 意味着上游机器人尚未为此 Steam 版本发布 TOML。代码然后回退到本地缓存（如果存在）；否则出现一次性弹窗，显示不匹配的 DLL 名称、其 SHA-256、预期缓存路径和上游 URL。仅禁用与该 DLL 相关的钩子——OpenSteamTool 的其余部分继续工作

如果你知道给定版本的布局，也可以手动将模式 TOML 放入缓存目录；文件名必须为 `<sha256>.toml`。下次远程不可达时缓存回退会拾取它

> 每次启动执行简短的出站 HTTPS 请求（每个 DLL 一个：`steamclient64.dll`、`steamui.dll`）。下载的内容很小（每个约 10 KB），工作在线程上运行，因此永远不会阻塞 Steam 加载器

#### 使用不同的镜像

对大多数用户来说，内置的 **GitHub -> jsDelivr** 回退已经足够。要使用私有镜像或内网服务器，配置完整的 URL 模板。自定义镜像替换内置远程源；本地缓存回退仍然可用

模板必须包含 `{channel}`、`{component}` 和 `{sha256}`。当前使用的通道是 `pattern` 和 `ipc`

```toml
[remote]
url_template = "https://your.server/{channel}/{component}/{sha256}.toml"
# url_template = "https://fast.jsdelivr.net/gh/OpenSteam001/steam-monitor@{channel}/{component}/{sha256}.toml"
```

### 调试日志

调试构建在 `<Steam>/opensteamtool/` 下写入每个模块的日志文件：

| 文件 | 来源 | 内容 |
|------|------|------|
| `main.log` | 通用 | 初始化、配置加载、Lua 解析、工具 |
| `ipc.log` | `LOG_IPC_*` | IPC 命令、InterfaceCall 分发、欺骗 |
| `netpacket.log` | `LOG_NETPACKET_*` | 网络包发送/接收、eMsg 分发 |
| `manifest.log` | `LOG_MANIFEST_*` | Manifest 下载、`fetch_manifest_code`、manifest 绑定 |
| `decryptionkey.log` | `LOG_DECRYPTIONKEY_*` | 仓库解密密钥注入 |
| `keyvalue.log` | `LOG_KEYVALUE_*` | KeyValues 补丁（manifest 绑定） |
| `misc.log` | `LOG_MISC_*` | 引擎指针捕获、AppId 提示 |
| `achievement.log` | `LOG_ACHIEVEMENT_*` | UserStats 请求/响应、steamid 欺骗 |
| `pics.log` | `LOG_PICS_*` | PICS 访问令牌注入 |
| `package.log` | `LOG_PACKAGE_*` | 包注入、FileWatcher 事件 |
| `onlinefix.log` | `LOG_ONLINEFIX_*` | 在线修复（480 AppId 欺骗） |
| `richpresence.log` | `LOG_RICHPRESENCE_*` | 丰富状态包构造和注入 |
| `steamui.log` | `LOG_STEAMUI_*` | SteamUI 钩子诊断 |
| `pipe.log` | `LOG_PIPE_*` | 管道握手、进程检查、Denuvo 授权、库注入 |
| `platform.log` | `LOG_PLATFORM_*` | 平台助手诊断，包括远程进程操作 |

日志级别由 `opensteamtool.toml` 中的 `[log] level` 控制

## 构建

### 要求
- Windows 10/11
- CMake 3.20+
- 带有 MSVC（x64 工具链）的 Visual Studio 2022

### 运行时要求
- Steam 更新后首次启动需要访问 `raw.githubusercontent.com` 的出站 HTTPS（参见 [Steam 版本兼容性](#steam-版本兼容性)）。之后会缓存

### 快速构建
```powershell
build.bat
```

### 输出
- Debug：`build/Debug/OpenSteamTool.dll`、`build/Debug/dwmapi.dll`、`build/Debug/xinput1_4.dll`
- Release：`build/Release/OpenSteamTool.dll`、`build/Release/dwmapi.dll`、`build/Release/xinput1_4.dll`

## 免责声明
本项目仅供研究和教育目的使用。你负责遵守当地法律、平台服务条款和软件许可证。
