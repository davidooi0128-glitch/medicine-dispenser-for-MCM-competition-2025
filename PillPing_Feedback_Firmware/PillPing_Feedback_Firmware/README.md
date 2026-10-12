# Pill Ping — Feedback Web UI Firmware

这套版本是在原有 Arduino Nano + ESP8266 medicine dispenser 流程上加入网页反馈，不重写原本的伺服出药、激光确认、Hall pulse 药水泵、水位监测、加水开关和提醒闹钟逻辑。

## 文件

- `PillPing_ArduinoNano.ino` — 运行原有出药/水位/提醒逻辑，并向网页回传配置、模拟时间和出药事件。
- `PillPing_ESP8266.ino` — Wi-Fi AP、重新设计的三页面网页、配置提交/确认、每次开机的临时日志。

请分别把对应 `.ino` 文件上传到对应 MCU，不要把两份程序合并到同一个 Arduino 工程。

## 网页功能

1. **Home**：两个入口 — `Data & Logs`、`Configuration`。
2. **Data & Logs**：展示 Nano EEPROM 中读回的每一剂设置，以及实际出药历史。
3. **Configuration**：读取并预填已保存的剂量设置；保存后等待 Nano 发回 `ACK,SAVED`，而不是单纯在网页上显示“已保存”。
4. **日志**：D4 触发出药时记录 `Day / Dose / 开始时间`；药水泵停止后补上完成时间、实际检测到的药丸数量和 Hall pulse 数。网页显示开始和完成时间，方便区分操作开始和流程完成。
5. **模拟 Day**：Day 只存在 RAM 中，不写入 Nano EEPROM；设备重启后会重新从 Day 1 开始。每个模拟日约为 5 分钟真实时间，和原本约 288 倍的模拟时钟速度一致。
6. **日志保留**：日志只存在 ESP8266 RAM 中，ESP8266/整机断电或重启后会清空；本次开机最多保留最近 40 条。剂量设置仍由 Nano EEPROM 保存，断电后可重新读回。

## 串口接线（必须按新版接线）

| Arduino Nano | ESP8266 NodeMCU/Wemos | 说明 |
|---|---|---|
| D2 (SoftwareSerial RX) | D6 / GPIO12 (SoftwareSerial TX) | ESP → Nano |
| D3 (SoftwareSerial TX) | D5 / GPIO14 (SoftwareSerial RX)，**中间加 5V→3.3V 分压** | Nano → ESP |
| GND | GND | 两板共地 |

Nano D3 是 5V 逻辑，不能直接接 ESP8266 RX。可用电阻分压，例如 Nano D3 经 1 kΩ 接到信号节点，信号节点接 ESP D5，同时信号节点经 2 kΩ 接 GND。ESP D6 的 3.3V TX 可直接进 Nano D2。

ESP 的 D5/D6 是 **ESP8266 自己的板载 pin 标签**；它们和 Nano 的 D5/D6 是不同的物理引脚。Nano 原有 D2/D3 软串口定义保留。此版不再使用 ESP 硬件 UART TX 发送网页数据，因此 USB 调试串口和 MCU 间链路分开。

> 原贴文里的 ESP 代码注释写着“ESP Hardware TX 接 Arduino A1”，但 Nano 程序实际在 D2 接收，而且 A1 已被 Nano 用作 DHT22 数据脚。这两者不一致。新版按 Nano 程序中的 D2/D3 串口定义工作，**不要把 ESP 串口接到 Nano A1**。

## 连接网页

1. 上传两份程序并按上面的串口方式接线、共地。
2. 手机或电脑连接 Wi-Fi：`Medical_Device_WiFi`，密码：`12345678`。
3. 浏览器打开 `http://192.168.4.1`。
4. 进入 `Configuration` 检查已读回的设置，保存后等到网页显示 Nano 确认。

## 保留的 Nano 引脚映射（以原代码为准）

- A0：药水水位传感器。
- A3：饮水水位传感器。
- D6：饮水加水开关，读取 A3 水位。
- D7：药水加水开关，读取 A0 水位。
- D4：主出药按钮；D9：取水按钮；D8：Hall 流量传感器；D10：激光接收头。

用户描述中 D6/D7 对应哪一类加水开关有过混用，因此这里严格按原 Nano 代码的 pin mapping 保留。

## 验证与限制

- 已对嵌入网页的 3 段 JavaScript 执行语法检查。
- 此环境没有实际连接硬件，也没有 Arduino/ESP8266 编译工具链，所以我不能声称已通过真实板子编译或硬件测试；上传前请用 Arduino IDE 选择正确板卡、安装原本所需库，并在台架上验证串口和执行器。
- 日志中的时间是**模拟时钟**的 `HH:MM`，不是现实世界时间，也没有 RTC/日期。
- “1 pulse ≈ 1 mL”只是你原本的估算。网页按 pulse 数显示估算 mL；实际体积需要用量筒或称重法校准后才能视为可靠。
- 系统只能记录出药机构何时开始及何时完成，不能证明用户真的吞服了药物。
- 为尽量不改动原工作逻辑，原本液体泵的 `while (hallPulseCount < targetPulses)` 阻塞循环仍保留。如果 Hall 传感器不再产生 pulse，流程可能一直停在“处理中”，网页也会显示进行中；这是原代码现有的风险，台架使用时应留意。
- 配置表单会拒绝跨午夜时间窗、倒序/重叠时间窗、提醒时间在时间窗外以及不合理的零/超范围用量。这与原有 `getActiveDoseIndex()` 的比较方式相匹配。
