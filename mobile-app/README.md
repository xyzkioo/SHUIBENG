# 水泵控制 · Android APP

安装文件：`dist/PumpControl.apk`。这是原生 Android APP，适用 Android 6.0 及以上，不需要 App Inventor、网页服务器或互联网连接。安装包使用本机开发签名，用于当前机构的本地安装测试。

## 安装和使用

1. 将 `PumpControl.apk` 通过数据线或文件传输发送到安卓手机，打开安装；如系统询问，允许当前文件管理器安装此 APK。
2. 给 STM32 和蓝牙模块上电。先断开电脑上的 HC-05 BLE 连接，模块同一时间可能只允许一个手机/电脑连接。
3. 打开“水泵控制”，点击“搜索并连接”，允许“附近设备”权限，选择 HC-05。安卓 11 及以前需要定位权限，部分手机还需打开系统定位开关才能扫描。
4. 等界面显示“已连接”并出现实际状态后，填写圈数和最高速度，点击“伸出”或“收回”。APP 会先保存圈数、速度，收到各自回复后再启动。
5. 抽水时长填秒数，0 表示手动停止；点击“开始抽水”或“停止抽水”。底部红色“全部停止”控制转轴和水泵。

当前设备默认 5 圈、最高 200 RPM、最低爬行 60 RPM；APP 允许圈数 0.1～10、最高速度 60～200 RPM。运动中禁止修改参数/叠加运动命令；停止转轴不会改变已开始的水泵定时。
“保存参数”只更新本次设备运行配置，单片机复位后仍恢复固件默认值。

## 自动处理的事情

- 扫描 BLE，发现 FFE0 服务，订阅 FFE1；优先使用 FFE2 写通道，必要时使用可写的 FFE1。
- 自动发送 `APP\n`，等待 `OK APP V1` 后再显示可操作状态。
- 按完整文本行缓冲 BLE 通知，支持拆包/粘包，并丢弃握手前的 VOFA 二进制尾帧。
- 一次只发送一条业务指令，等待设备回复后再发下一条。运动指令不会自动重发或重连重放。
- 前台保持屏幕亮起，每秒发送心跳；解析 MCU 的 DATA/STATE 更新圈数、速度、水泵及故障。
- 定时抽水结束发送停泵命令；离开页面/切到后台时尝试发送全部停止并断开连接。若停止指令未送达，当前固件还有约 5 秒的心跳超时停机机制。
- 回复超时、状态长期不更新、连接中断时禁用动作按钮，要求重新连接确认，不用旧数据显示“仍正常运行”。

APP 应保持在前台使用；本版不提供后台持续运行或锁屏抽水。它也不是硬件急停装置。首次实际连接请确认按键方向与机械实际一致。

## 文件和构建

- `src/cn/pump/control/MainActivity.java`：原生界面、BLE 生命周期、串行指令队列、心跳及抽水定时。
- `src/cn/pump/control/Protocol.java`：无 Android 依赖的指令校验、文本重组和状态解析。
- `tests/ProtocolTest.java`：34 项协议断言。
- `AndroidManifest.xml`：Android 23～35 配置、BLE 权限，无 INTERNET 权限。
- `build.py`：通过官方 Android 35 SDK 的 aapt2 / D8 / zipalign / apksigner 构建并签名 APK。
- `dist/apk-info.txt`：从最终 APK 读取的包名、版本、权限和启动入口。

本机运行：

```powershell
& 'C:/Program Files/LibreOffice/program/python.exe' mobile-app/build.py
```

JDK 默认使用 `D:/Tools/PyCharm 2025.1.3.1/jbr`，可用 `PUMP_JDK` 环境变量指定其他 JDK。
构建工具缓存于工程 `.tools/android/sdk-selected`，不在源码压缩包中。`select_sdk.py` 从 Google 官方 SDK ZIP 按需提取工具并验证 ZIP CRC，原始地址和版本写在脚本中；首次需先取得官方 `repository.xml`，或复用本机已下载的缓存。
`keystore/local-debug.p12` 保留在本机供后续 APK 更新使用，不包含在源码包；换签名时需要卸载手机上的旧版本才能安装，签名密码仅用于本地开发。

## 已完成验证与限制

- Java 代码编译、Android 资源打包、D8 转换成功。
- 34 项协议测试通过：数值范围、换行、非法输入、通知分包/粘包、握手、状态及重连清空。
- APK v1/v2/v3 签名验证及 zipalign 校验通过，Manifest 可解析，存在可启动 Activity。
- 尚未接入真实安卓手机或模拟器执行 UI/BLE 联调；未声称真实手机到机构的端到端操作已经通过。

手机端与当前 `BLUETOOTH_PROTOCOL.md`、`hareware/bt_control.c` 配套，不支持经典 SPP 版 HC-05，也不使用电脑 COM 口。
