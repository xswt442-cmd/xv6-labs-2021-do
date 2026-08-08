# xv6 Lab 实验报告：net

## 1. 实验基本信息

- 课程/项目：MIT 6.S081 / xv6 2021
- 实验名称：net — Network driver
- 实验分支：`net`
- 完成日期：2026-08-08
- 官方题目：https://pdos.csail.mit.edu/6.828/2021/labs/net.html
- 代码仓库：https://github.com/xswt442-cmd/xv6-labs-2021-do
- 对应代码提交：`8a03d6b21efef76bc32dbf105b1dcc21631531c6`

## 2. 实验目标

- 完成 E1000 网卡发送与接收路径，理解 DMA 描述符环和 MMIO 寄存器。
- 正确管理 TX/RX 描述符与 `mbuf` 的所有权转换和生命周期。
- 理解中断驱动网络接收、环形队列回绕及并发发送时的同步要求。

## 3. 实验环境

- 宿主系统：WSL2 / Linux
- 编译工具链：`riscv64-unknown-elf-gcc 13.2.0`
- 模拟器：`QEMU 8.2.2`
- xv6 起始版本：上游 `net` 分支，提交 `1bd9c80b1ef5eb70b91bbd1dbea9c1d953cdc555`

## 4. 实现内容与相关代码

| 功能 | 修改/新增文件 | 实现说明 |
| --- | --- | --- |
| E1000 发送 | `kernel/e1000.c` | 从 `TDT` 获取可用 TX 槽，检查 `DD`、回收旧 mbuf、填写描述符并推进尾指针。 |
| E1000 接收 | `kernel/e1000.c` | 从 `RDT+1` 排空已完成 RX 槽，更换 DMA buffer、交付旧 mbuf 并归还描述符。 |
| 并发和内存顺序 | `kernel/e1000.c` | 用自旋锁保护描述符环，用内存屏障约束描述符与 MMIO doorbell 的发布顺序。 |
| 工具链兼容 | `Makefile` | 针对 GCC 13 的 `infinite-recursion` 诊断增加兼容选项，保留其余 `-Werror`。 |
| 实验用时 | `time.txt` | 按评分脚本格式记录用时。 |

### 4.1 发送描述符环

发送函数在 `e1000_lock` 保护下读取 `TDT` 指向的下一槽。只有硬件已写回 `E1000_TXD_STAT_DD` 时才能复用该槽并释放上一轮保存的 mbuf；若描述符仍忙，则返回 `-1`，传入 mbuf 的所有权仍归调用者。可用时填写缓冲区地址、长度以及 `EOP | RS` 命令并清除旧状态，执行内存屏障后推进 `TDT`，把描述符和 mbuf 所有权交给设备。

### 4.2 接收描述符环

`RDT` 表示最后一个已归还硬件的 RX 槽，因此软件从 `(RDT + 1) % RX_RING_SIZE` 开始检查连续的 `DD` 描述符。对无错误、完整且长度合法的帧，先分配替换 mbuf，再将旧 mbuf 的长度设为硬件写回值；这样旧缓冲区交给协议栈前，设备已经获得新的 DMA 目标。坏帧或内存不足时复用原缓冲区，避免环槽永久丢失。清除写回字段并执行屏障后，把当前索引写入 `RDT`。

### 4.3 锁与递归发送

发送进程和接收中断可能并发访问 E1000 状态，因此描述符处理使用同一自旋锁串行化。接收函数只在锁内替换和归还 RX 描述符，把待处理 mbuf 暂存起来，然后释放锁再调用 `net_rx()`。这是必要的，因为 ARP 请求的接收路径会同步构造回复并再次进入 `e1000_transmit()`；若持锁调用协议栈会发生递归加锁死锁。

## 5. 测试过程、结果与分析

### 5.1 测试命令

```bash
# 终端一
make server

# 终端二
make qemu
# xv6 shell 中执行
nettests

# 官方评分
make grade
```

### 5.2 测试结果

| 测试项 | 实际结果 | 结论 |
| --- | --- | --- |
| 基础 ping | 宿主 UDP echo 回复正确 | 通过 |
| 单进程压力 | 100 次 ping 全部成功 | 通过 |
| 多进程压力 | 10 个并发进程全部成功 | 通过 |
| DNS | 收到并解析 `pdos.csail.mit.edu` 的 A 记录 | 通过 |
| 官方评分 | `make grade` 得分 `100/100` | 通过 |

手工测试关键输出：

```text
testing ping: OK
testing single-process pings: OK
testing multi-process pings: OK
testing DNS
DNS OK
all tests passed.
```

官方评分关键输出：

```text
nettest: ping: OK
nettest: single process: OK
nettest: multi-process: OK
nettest: DNS: OK
time: OK
Score: 100/100
```

### 5.3 结果分析

基础 ping 同时覆盖了 UDP 发送、宿主回包、E1000 接收中断及协议栈投递。100 次单进程测试远超过 16 个描述符，证明 TX/RX 环能够正确回绕并回收 mbuf。多进程测试验证发送锁在并发调用下保护了尾指针和槽状态。DNS 测试进一步验证较完整的外部网络收发路径。测试生成的 `packets.pcap` 仅用于诊断，测试后已恢复，未作为源码提交。

## 6. 遇到的问题与解决方法

| 问题 | 原因分析 | 解决方法 |
| --- | --- | --- |
| 首次自动评分停在第一个 ping | 评分脚本启动 UDP server 后立即启动 QEMU，在当前 WSL 环境出现服务就绪竞态 | 先确认 `make server` 已监听，再执行手工端到端测试和官方评分，最终获得 100/100 |
| GCC 13 无法编译上游 `user/sh.c` | 编译器新增递归分析并被 xv6 的 `-Werror` 提升为错误 | 仅抑制 `infinite-recursion` 诊断，保留其他警告检查 |
| RX 交付缓冲区后设备仍需 DMA 空间 | 若先交付旧 mbuf再分配，分配失败会造成空槽或 DMA 使用已转移内存 | 先成功分配替换 mbuf，再转换所有权；失败时复用原 buffer |
| 接收路径可能触发同步 ARP 回复 | 持有 E1000 锁调用 `net_rx()` 会递归进入发送函数 | 锁内完成描述符操作，锁外逐个调用 `net_rx()` |

## 7. 原理理解与总结

网卡驱动的核心不是简单复制字节，而是协调 CPU 与设备对一组共享描述符和缓冲区的所有权。状态位 `DD` 是硬件到软件的完成信号，`TDT`/`RDT` 写入则是软件把描述符交还硬件的 doorbell；内存屏障保证描述符内容在门铃之前可见。发送缓冲区必须保留到硬件完成，接收缓冲区则必须先补充替代品才能把旧包交给协议栈。环形结构提供固定空间和持续复用能力，锁、中断和严格的所有权规则共同保证并发下不重复使用或提前释放内存。

## 8. 代码与复现说明

- 仓库：https://github.com/xswt442-cmd/xv6-labs-2021-do
- 分支：`net`
- 代码提交：`8a03d6b21efef76bc32dbf105b1dcc21631531c6`
- 报告提交：包含本文件的 `lab(net): 提交实验报告` 提交
- 复现步骤：切换到 `net` 分支；先运行 `make server`，再于另一终端运行 `make qemu` 并执行 `nettests`，或在服务已就绪时运行 `make grade`。
