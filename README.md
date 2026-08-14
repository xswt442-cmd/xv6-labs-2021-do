# xv6labs-2021 十项实验

本仓库完成 MIT 6.S081 / xv6 2021 的十项实验，内容涵盖 Unix 用户程序、系统调用、页表、陷阱处理、写时复制、线程与同步、网卡驱动、锁优化、文件系统和内存映射。

本分支即 `main` 分支只提供项目导航；每项实验的源码和实验报告位于对应的独立分支。十个实验分支均直接基于 MIT 上游同名起始分支，不继承或合并其他实验的实现。

## 实验导航

| 序号 | 实验 | 主要内容 | 得分 | 源码分支 | 实验报告 |
| ---: | --- | --- | ---: | --- | --- |
| 1 | util | `sleep`、`pingpong`、并发素数筛、`find`、`xargs` | 100/100 | [`util`](../../tree/util) | [查看报告](../../blob/util/reports/report-lab-util.md) |
| 2 | syscall | 系统调用跟踪与系统信息统计 | 35/35 | [`syscall`](../../tree/syscall) | [查看报告](../../blob/syscall/reports/report-lab-syscall.md) |
| 3 | pgtbl | 页表打印、每进程内核页表、用户映射 | 46/46 | [`pgtbl`](../../tree/pgtbl) | [查看报告](../../blob/pgtbl/reports/report-lab-pgtbl.md) |
| 4 | traps | 栈回溯与用户级周期告警 | 85/85 | [`traps`](../../tree/traps) | [查看报告](../../blob/traps/reports/report-lab-traps.md) |
| 5 | cow | 写时复制 `fork()` 与物理页引用计数 | 110/110 | [`cow`](../../tree/cow) | [查看报告](../../blob/cow/reports/report-lab-cow.md) |
| 6 | thread | 用户线程切换、并发哈希表与 barrier | 60/60 | [`thread`](../../tree/thread) | [查看报告](../../blob/thread/reports/report-lab-thread.md) |
| 7 | net | E1000 发送与接收驱动 | 100/100 | [`net`](../../tree/net) | [查看报告](../../blob/net/reports/report-lab-net.md) |
| 8 | lock | 每 CPU 空闲页链表与分桶块缓存 | 70/70 | [`lock`](../../tree/lock) | [查看报告](../../blob/lock/reports/report-lab-lock.md) |
| 9 | fs | 双重间接块、大文件与符号链接 | 100/100 | [`fs`](../../tree/fs) | [查看报告](../../blob/fs/reports/report-lab-fs.md) |
| 10 | mmap | VMA、惰性缺页、共享写回与 `fork()` | 140/140 | [`mmap`](../../tree/mmap) | [查看报告](../../blob/mmap/reports/report-lab-mmap.md) |

## 分支与提交结构

每个实验分支均采用以下历史结构：

```text
MIT upstream/<实验名>
└── lab(<实验名>): 完成……
    └── lab(<实验名>): 提交实验报告
```

代码提交包含实验实现、题目要求的 `time.txt` 以及相应的 `answers-*.txt`；随后独立提交 `reports/report-lab-<实验名>.md`。报告固定记录真实代码提交 SHA、实现分析和实际测试结果。

## 实验环境

- 宿主环境：WSL2 / Linux
- RISC-V 工具链：`riscv64-unknown-elf-gcc 13.2.0`
- 模拟器：`QEMU 8.2.2`
- 构建工具：GNU Make
- 基础代码：MIT `xv6-labs-2021`

检查本机环境：

```bash
riscv64-unknown-elf-gcc --version
qemu-system-riscv64 --version
make --version
```

## 获取、运行与测试

克隆仓库后切换到需要查看的实验分支：

```bash
git clone https://github.com/xswt442-cmd/xv6-labs-2021-do.git
cd xv6-labs-2021-do
git switch mmap
```

启动 xv6：

```bash
make qemu
```

退出 QEMU 时先按 `Ctrl-a`，再按 `x`。

执行当前分支的完整自动评分：

```bash
make grade
```

不同实验的评分时间差异较大，其中 `fs` 的大文件与完整回归测试耗时最长。各实验的定向测试命令、关键输出、问题排查过程和设计取舍均记录在对应报告中。

## 完成情况

十项实验的官方评分脚本和规定回归测试均已通过，总分以各实验独立评分上限计为：

```text
100 + 35 + 46 + 85 + 110 + 60 + 100 + 70 + 100 + 140 = 846 / 846
```

除官方测试外，部分实验还进行了额外边界与并发验证，例如符号链接并发创建、网络收发、VMA 容量边界以及超大共享映射的单页写回检查。具体证据见各分支实验报告。

## 资料来源

- [MIT 6.S081 / 6.828 课程主页](https://pdos.csail.mit.edu/6.828/2021/)
- [xv6 book](https://pdos.csail.mit.edu/6.828/2021/xv6/book-riscv-rev2.pdf)
- [xv6 labs 指引](https://pdos.csail.mit.edu/6.828/2021/labs/)

本仓库用于操作系统实验、学习记录和项目展示。
