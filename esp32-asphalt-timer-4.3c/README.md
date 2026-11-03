# ESP32 4.3C 沥青试验记录助手

目标板：微雪 ESP32-S3-Touch-LCD-4.3C，800×480，16 MiB Flash、8 MiB OPI PSRAM。

主程序：[asphalt-timer-4.3c.ino](asphalt-timer-4.3c/asphalt-timer-4.3c.ino)。
继续开发前阅读 [CODEX_HANDOFF.md](CODEX_HANDOFF.md)；操作流程见 [数据录入与手机传输说明.md](数据录入与手机传输说明.md)。

## 功能

- 秒表：设置页进入，大号数字，支持开始、暂停、继续、开始/继续/记录共用大按钮、成功短滴、连续分段记录和相邻记录时间差；列表按页跳转。TF卡保存CSV，超过100条仍可继续；无卡暂存100条。切页或手动锁屏继续；运行时不自动锁屏，暂停后恢复30秒无操作锁屏。重启秒表清零，已保存的TF文件保留。
- 自动历史归档：满三天的空闲项目可在往期历史查看，满七天后锁屏空闲时归档到TF；保护正在计时及仍被使用的老化对照，支持历史恢复与单项报告下载。
- USB语音传输：`tools/upload_voice_serial.py` 将语音校验后写入TF，无需取卡；不同内容的同名文件不会覆盖。

- 样品按日期分类；未老化、RTFOT、TFOT阶段使用同一套试验模板。
- 步骤计时、提醒、实际开始/确认完成/跳过时间，RTC断电走时。
- 大按键数字键盘，保留原始小数字符串；每项目4组、每组6条读数。
- 针入度、普通/离析软化点、动力/旋转黏度、质量变化及老化前后比较。
- 国内与澳洲规范选项；澳标软化点需选择实际浴液，联动修约规则。
- TF归档CSV、JSON；选定项目生成Markdown报告。
- 首页“任务”右上角导出：板端日期 → 样品 → 试验逐层选择，支持整日、整样品或单项勾选及跨日期保留选择。
- “手机上选择”开启Wi-Fi热点，手机通过自动登录引导进入分级选择页，勾选后才生成并下载MD。蓝牙及手机版可编辑数据同步不在本版中。
- 显示页数、数字直接跳页；锁屏后需完整横滑解锁；网络处理与触摸采样独立。

## 源码与发布文件

```text
asphalt-timer-4.3c/          Arduino工程及全部驱动、字体、拼音数据
tools/                     字体/拼音生成、语音生成及试听工具
voice-pack/voice/          已生成语音；把该voice目录直接复制到TF根目录
voice-pack/manifest.json    语音项目与文件名映射
release/asphalt-timer-4.3c.bin  当前已烧录的应用固件
release/SHA256SUMS.txt      校验值
```

现成固件不需要重新生成字体或语音。调用语音生成工具需要自行提供服务配置；已有语音包可离线使用。

## 编译

Arduino ESP32开发包版本为 **3.1.1**。建议工程放在纯ASCII路径，避免工具链在中文路径上异常。

```powershell
arduino-cli compile --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=custom,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc,FlashMode=qio" --jobs 2 --build-path C:\esp32-build .\asphalt-timer-4.3c
```

使用工程内的 partitions.csv；实际应用分区容量 **0x300000（3 MiB）**，不能仅以编译器显示的16 MiB上限判断固件是否能放入。

## 烧录与数据

应用固件只写 **0x10000**，不要把应用bin写到0x0，也不要擦除整片Flash。更新前备份并校验用户数据。

```powershell
esptool --port COM5 --before usb-reset --after watchdog-reset write-flash 0x10000 .\release\asphalt-timer-4.3c.bin
```

端口号按实际电脑确定。当前连接板的识别为USB VID303A PID1001；COM3/COM4在原电脑上是蓝牙串口。

样品/计时记录采用兼容读取旧版的双槽压缩格式（版本4），旧固件不能读取此格式，不能直接降级。读数独立保存在 results4c 命名空间，带校验和、无损压缩和读回验证，并为计时写入预留空间。TF的 /records/ 保存CSV/JSON，/history/ 保存可恢复的二进制历史快照，/exports/ 保存报告。当前板内活动容量为32样品、32项目；删除板内项目不会删除卡上已经保存的历史文件。

## 本次验证与待回归

2026-11-02版本已通过编译、应用写入校验、数值自检、隔离NVS重启恢复、TF读写及防误触自检。用户已确认热点自动弹页下载、热点期间导航、跳页和无重叠。

最新补充的板端“日期 → 样品 → 试验”导出层级已编译、烧录，仍需完整实机回归跨日期选择和选定项目导出内容。参见交接说明中的检查项。
