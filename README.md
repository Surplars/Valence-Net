# Valence-Iperf RX256K / N57B

独立 WSL 工程 `/home/openion/Valence-Iperf-Rx256K`，当前位流：`E:/VM/Share/Valence-rtl/valence-iperf-20261025T071436Z/valence_iperf.bit`。
原 `/home/openion/Valence-Iperf-Net57c6984` 与已测 N57A 位流保留。

**完整功能回归、Vivado实现和独立物理审计通过：WNS +0.035 ns / WHS +0.010 ns / WPWS +0.437 ns。**
setup/hold/pulse-width失败端点、路由错误均为0，四项bus-skew通过，DRC无Error/Critical Warning；UART与448-bit RX诊断邮箱逐位物理审计通过。
bit SHA256：`4871542713694d7d25553c67d46c0a6a56c961e8f53513583e9942118301f8b0`。
**本候选尚未烧录，没有新实板成绩。** 时序余量仅对应这份已封存的具体布局布线。

## 本次改变

- 实际接收环每连接128 KiB扩为256 KiB（8192→16384个128-bit字），容量和指针/信用计数使用匹配宽度。
- TCP窗口缩放2→3，完整256 KiB窗口可准确编码。独立线上对照最大通告窗口131032→262104 B、scale2→3。
- TX保留缓存仍为64段，MSS1460时93440 B；发送拥塞上限仍为131072 B。ACK1策略、SACK/Reno、PMTU和真实完成账本保留。
- IP192.168.137.10/24、GW192.168.137.1、MAC02:00:00:00:00:80、TCP5201、UART460800 8N1、核心125 MHz、DDR250 MHz、RX45均保持原值。
- 原生Windows TCP_INFO_v1在N57A上报告多数时间Receiver Limited、零重传。这支持进行真实窗口扩容对照，不保证新位流必然达到线速。无溢出/零窗口不能排除通告窗口上限的影响。

原N57A用户实测反向-w1M为944.3 Mbit/s，首秒后平均948.4；正向32K/N约723–729。原生Windows诊断发送端仍约646–758，说明不能只归因Cygwin；只读TCP_INFO显示接收窗口受限约88%–99%。这些均为**旧位流**的数据。
统计定义：[Microsoft TCP_INFO_v1](https://learn.microsoft.com/en-us/windows/win32/api/mstcpip/ns-mstcpip-tcp_info_v1)。原始摘要见`baseline-window-limit.json`；原生诊断客户端与iperf3.exe成绩分别标记。

## 手动烧录和对照

用户自行烧录。`program_volatile.ps1`默认只检查JTAG，显式加-Program才临时加载PL，不写启动Flash；代理未调用烧录。
启动行应包含 **BUILD=TX64-RX256K-N57B**，UART为460800 8N1、无流控。
电脑有线口保持192.168.137.1/24，直连板卡RJ2/RTL8211F。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\measure_iperf3.ps1
```

脚本用现有`D:\TOOLS\ENV\iperf-3.22-win64\iperf3.exe`依次测默认正向、32K/N正向、默认反向、32K/N/1M窗口正向、1M窗口反向，各10秒；保留JSON、stderr、客户端版本/哈希、socket实际缓冲、CPU和汇总。
保存启动至测试结束的UART日志。BUILD由人核对，脚本本身不读回JTAG固件。单个客户端、单条数据流；不支持UDP、多流-P>1、bidir、omit、认证或限速。
可用`diagnose_tcpinfo.ps1`补充原生Windows诊断，输出明确标为诊断客户端，不冒充iperf3.exe成绩。

## 实现与验证

Valence MAC固定`bf021b52fe67f3d4c8c33d94a7e928559f55c729`；Valence-Net固定`57c698496c55f3afe6c882f7a8ab9fb041f6b66b`，两个依赖副本工作区干净。
无CPU、PS软件、Linux、DMA、DDR。两个工程覆盖文件为TcpReceiveQueue/TcpReceiveRing，接收环源码源自0273，57中该文件未改变；仅在工程副本放宽容量并沿用已验证ACK策略。
工具锁定Mill1.1.7、Scala2.13.17、Chisel7.3、GSIM93b8cd23与Clang19 ASAN/UBSAN，不使用Verilator。

7项应用、11项非法请求、3项全MAC/TCP互通、原Windows3.22正反各65536 B、13项传输、三轮256KiB满环排空/回绕786432 B、16 B零窗口重开、完整窗口恢复、SACK回放、新UART标识、真实Xilinx板级原语和1024帧±250ppm RX桥均通过。
212个网络FIR模块在明确源信息/Reset归一化后与板级网络匹配；非形式等价证明。
512帧线上对照ACK仍513、有效载荷747520 B；TX64三档ACK延迟均约949 Mbit/s为**模型结果**，非实板速率。
发送边界19个寄存器与5个逻辑LUT固定，B5/B6、D5/D6、E6实际配对和DONT_TOUCH逐项核验；检查点保留，不放宽时钟/IO约束。

复现：`make compile`、`make test-app`、`python3 sim/build.py gmii`、root运行`sim/test_tap.py`、`python3 sim/build.py rxqueue`、`build/rxqueue/test`、`python3 sim/test_transport.py`、`python3 fpga/prepare.py`。
`fpga/run.ps1 -RunDirectory <冻结目录> -Phase bit`使用Vivado2025.2完整实现，负余量阻止交付；独立审计后由`scripts/seal_physical.py`绑定报告、RTL和位流哈希。


## 实板测试补充（2026-10-25）

已通过JTAG加载并测量，当前板上TX64-RX256K-N57B。默认正向约751Mbps，反向w1M约938Mbps；-N组合仍有回退，未解决Windows正向满速。详细报告：`/mnt/e/VM/Share/Valence-rtl/valence-iperf-20261025T071436Z/BOARD-RESULTS.md`；不要把前文封存前的“尚未烧录”当作最新实板状态。全部网卡高级属性已恢复、COM4已释放。补充极短段压力测试出现1次忙丢弃，TCP重传16B后恢复完整载荷；原始失败与恢复记录均保留。
