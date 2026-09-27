# DeepSeek Usage Plugin for TrafficMonitor

基于 TrafficMonitor 插件接口实现的 DeepSeek 余额监控插件。插件通过 DeepSeek 官方
`GET https://api.deepseek.com/user/balance` 接口查询账户余额，并把结果显示在
TrafficMonitor 主窗口或任务栏窗口中。

作者：[@928475053](https://github.com/928475053)

仓库：[928475053/DeepSeekUsagePlugin](https://github.com/928475053/DeepSeekUsagePlugin)

## 功能

- 在插件设置中填写 DeepSeek API Key。
- 支持自定义轮询间隔，范围为 10 到 86400 秒，默认 60 秒。
- 支持自动、CNY、USD 三种币种显示策略。
- 网络请求在后台线程执行，不阻塞 TrafficMonitor 界面。
- 鼠标提示中显示总余额、赠送余额、充值余额、更新时间、轮询间隔和最近错误。
- 提供“立即刷新 DeepSeek 余额”插件命令。
- API Key 使用当前 Windows 用户的 DPAPI 加密后保存，不写入明文配置。

显示状态示例：

| 状态 | 显示文本 |
| --- | --- |
| 未配置 | `未配置` |
| 正在首次查询 | `查询中...` |
| 查询成功 | `¥110.00` |
| 账户不可用或余额不足 | `!¥0.00` |
| 刷新失败，保留上次结果 | `¥110.00?` |
| 查询失败且无历史结果 | `查询失败` |

## 安装

1. 确认 TrafficMonitor 是 32 位还是 64 位版本。
2. 将对应 DLL 放入 TrafficMonitor 程序目录下的 `plugins` 目录：
   - 64 位：`dist\x64\DeepSeekUsagePlugin.dll`
   - 32 位：`dist\x86\DeepSeekUsagePlugin.dll`
3. 重启 TrafficMonitor。
4. 在 TrafficMonitor 的“插件管理”中打开“DeepSeek 余额监控”的“选项”。
5. 填写 DeepSeek API Key，设置轮询间隔和显示币种。
6. 在任务栏窗口或主窗口的“显示设置”中勾选“DeepSeek 余额”。

配置文件由 TrafficMonitor 的插件配置目录保存，文件名通常为：

```text
DeepSeekUsagePlugin.dat
```

配置内容使用 Windows DPAPI 和当前用户范围加密。将配置文件复制到其他 Windows
用户下后无法直接解密，这是预期行为。

## 构建

环境要求：

- Visual Studio 2022，安装“使用 C++ 的桌面开发”
- Windows 10/11 SDK

使用 Visual Studio 打开 `DeepSeekUsagePlugin.sln`，选择 `Release|x64` 或
`Release|x86` 后构建。

也可以在 Developer PowerShell 中运行：

```powershell
msbuild .\DeepSeekUsagePlugin.sln /m /p:Configuration=Release /p:Platform=x64
msbuild .\DeepSeekUsagePlugin.sln /m /p:Configuration=Release /p:Platform=x86
```

构建输出位于：

```text
build\x64\Release\plugins\DeepSeekUsagePlugin.dll
build\Win32\Release\plugins\DeepSeekUsagePlugin.dll
```

## 测试

默认测试覆盖 JSON 解析、DPAPI 配置往返，以及通过 `LoadLibrary` 加载插件 DLL
并调用 `TMPluginGetInstance` 的冒烟测试。

```powershell
.\build\tests\x64\Release\DeepSeekUsagePluginTests.exe
```

可选的在线测试会使用无效 API Key 请求 DeepSeek 官方接口，并验证返回 HTTP 401：

```powershell
.\build\tests\x64\Release\DeepSeekUsagePluginTests.exe --test-network
```

## 目录结构

```text
include\PluginInterface.h        TrafficMonitor 官方插件接口
src\BalanceClient.cpp            WinHTTP 请求与 HTTP 错误处理
src\JsonParser.cpp               DeepSeek 响应解析
src\ConfigStore.cpp              DPAPI 配置加密、保存和读取
src\OptionsDialog.cpp            Win32 设置界面
src\Plugin.cpp                   插件生命周期、后台轮询和显示文本
tests\                           单元测试与插件加载测试
```

## 参考

- [TrafficMonitor 插件开发指南](https://github.com/zhongyang219/TrafficMonitor/wiki/%E6%8F%92%E4%BB%B6%E5%BC%80%E5%8F%91%E6%8C%87%E5%8D%97)
- [DeepSeek Get User Balance API](https://api-docs.deepseek.com/api/get-user-balance)
