# HC-05 BLE 与 VOFA+

本机配对的 HC-05 是 BLE 模块：18:2B:B6:EE:0E:CE，提供 FFE0 服务和 FFE1 通知。
它没有生成 Windows SPP COM 口。COM9 属于另一只 JDY-31-SP，不能用于这只 HC-05。

当前已移植抽水按键流程：上电停机，按 KEY1 后按圈数定位；操作和参数见工程根目录 `PUMP_CONTROL.md`。
当前定位已采用 S 型速度规划，最高 200 RPM，原独立线性软启动已由规划器替代。
下文的 500 RPM 启动实测是此前独立速度控制模式的历史记录，不是本次位置控制实测。

## 已验证的通信路径

STM32 USART1（38400、8N1） -> HC-05 BLE -> Python 转发 -> 本机 TCP -> VOFA+。

- STM32 PA9/TX -> 模块 RXD，模块 TXD -> PA10/RX，双方共地。
- 本模块实际透传波特率为 38400，已经通过连续有效波形帧确认；9600 收到的是乱码。
- 桥接只监听 127.0.0.1:8765。
- 每 50ms 上报一次：三个小端 float32，随后为帧尾 00 00 80 7F。
- CH0：位置环给出的输出轴速度指令，RPM；停止时为零，运动时随剩余行程变化，限幅 ±200 RPM。
- CH1：输出轴实际转速，RPM。
- CH2：有符号 PI 输出，当前转轴限幅 ±250/1000，不含前馈。
- 新增独立模块 `hareware/bt_control.c/.h`。手机蓝牙指令见根目录 `BLUETOOTH_PROTOCOL.md`，先发送 `APP\n` 进入文本模式；`STOP\n` 无需握手也有效。
- 电脑 TCP 桥接仍仅上行遥测，不转发 VOFA+ 命令。手机控制请直连 BLE 模块；停机后发 `VOFA\n` 可恢复 JustFloat。

## 转速换算

按用户提供的 JGA25-370-4.4K 参数：11 PPR、减速比 4.4；TIM2 现在使用 TI12 模式，AB 两相 x4。
每输出轴转一圈计数为 11 × 4 × 4.4 = 193.6。
输出轴 RPM = 每 50ms 计数 × 1200 / 193.6。
500 RPM 对应约 80.67 计数/50ms。早期 x2 版本中的 500 计数/50ms 相当于约 6198 RPM。
内部 PI 按计数运算；目标与 VOFA+ 遥测统一为 RPM。
从 x2 改到 x4 时增益减半：原 Kp=2、Ki=0.05 对应现在 Kp=1、Ki=0.025。
调试器中的 motor_a_target 使用 RPM，由按键/位置环更新；复位停机。
单个计数现在对应约 6.2 RPM，因此相邻采样点仍会有量化跳动。
若以后改变减速比、编码器 PPR 或 x2/x4 模式，必须同步修改换算常数。

## 启动与当前控制参数

目标仍为 500 RPM。给 PI 的内部参考按 500 RPM/s 上升，约 1 秒达到 500 RPM。
加入 0.72 PWM/RPM 的前馈估计：500 RPM 时基础输出约 360/1000，再由 PI 修正负载偏差。
前馈与 PI 总输出共同限幅，饱和时阻止积分继续向饱和方向累积。
目标置零时清除积分并令总输出为零。
这组前馈根据本台架约 500 RPM 时所需的 PWM 估算，其他负载下需要复核。
最终两轮从静止启动实测见 startup-ff072.json、startup-ff072-repeat.json 及对应 CSV。
两轮均约 1 秒达到 475 RPM，分别约 1.95 秒和 1 秒后持续进入 485～515 RPM 范围。
最高转速分别为 508.26 和 514.46 RPM；末 10 秒均值分别为 501.79 和 499.31 RPM。
时间按 50ms 帧周期计算，CSV 同时保存主机接收时间。结果限于当前台架负载与编码器换算，未用独立转速计校准。
早期 0.70 前馈的第二轮未满足验收，记录保留在 startup-feedforward-repeat.json。

## 启动和连接

1. 确认模块供电并已配对。重新启动桥接时运行 Start-HC05Bridge.ps1。
2. 在 VOFA+ 1.3.10 中载入 HC05-BLE-TCP-JustFloat.json。
3. 核对数据接口为 TCP Client / TCP客户端，服务器 127.0.0.1，端口 8765。
4. 数据引擎选择 JustFloat，连接后显示 CH0～CH2。每个采样点对应 0.05 秒。

提供的是独立配置文件，未覆盖现有 VOFA+ 应用配置；仍需在界面载入并点击连接。
VOFA+ 中不使用 COM 口和波特率设置；38400 是 MCU 与模块之间的 UART 参数。

bridge-status.json 记录 BLE 状态、数据是否新鲜、有效帧数和最新通道值。
bridge.log / bridge-error.log 保存运行日志。
服务模式下 BLE 掉线或连接超时会自动重试（2 秒起，最长间隔 10 秒）。
本机 TCP 服务始终保留；VOFA+ 已连接时不需要因为 BLE 掉线而重新打开 TCP。
断线期间不发送旧帧，状态中的 last_channels 清空，避免误认为旧读数仍在更新。
telemetry_fresh=false 表示没有最近 5 秒的有效遥测；若调试器暂停了 MCU，也可能没有新遥测。
重启电脑或手动结束转发后，需要重新运行启动脚本；它不会改 Windows 蓝牙配置或自动取消配对。

## 环境

本机使用 C:\Program Files\LibreOffice\program\python.exe（Python 3.12）。
依赖安装在工程 .tools/ble，没有修改系统 Python 包。
重建依赖时可用 Python 3.12 执行：

    python -m pip install --target .tools/ble -r vofa/requirements-ble.txt

仅检查 GATT 服务：

    python vofa/ble_vofa_bridge.py --paired --mode inspect

限时读取 5 秒：

    python vofa/ble_vofa_bridge.py --paired --mode capture --duration 5

上述命令和运行中的桥接应轮流连接模块，不要同时建立多个 BLE 会话。
