# xv6 Lab 实验报告：util

## 1. 实验基本信息

- 课程/项目：MIT 6.S081 / xv6 2021
- 实验名称：util — Xv6 and Unix utilities
- 实验分支：`util`
- 完成日期：2026-08-08
- 官方题目：[Lab: Xv6 and Unix utilities](https://pdos.csail.mit.edu/6.828/2021/labs/util.html)
- 代码仓库：[xv6-labs-2021-do](https://github.com/xswt442-cmd/xv6-labs-2021-do)
- 对应代码提交：`402fd7bc9430ca8fad5fb3c7f69f6913115d0719`

## 2. 实验目标

- 熟悉 xv6 用户程序的编译、链接和文件系统镜像生成过程。
- 使用 `fork`、`pipe`、`read`、`write`、`exec` 和 `wait` 理解进程与进程间通信。
- 使用 xv6 的目录项和文件状态接口实现递归文件查找。
- 理解管道 EOF、文件描述符关闭和父子进程回收对程序正确性的影响。

## 3. 实验环境

- 宿主系统：WSL2，Ubuntu 24.04.4 LTS
- 编译工具链：`riscv64-unknown-elf-gcc 13.2.0`
- 模拟器：`QEMU emulator version 8.2.2`
- xv6 起始版本：`xv6-labs-2021` 的 `util` 分支，起始提交 `f654383cdec479c9d53a02bffa1ab5526f6c3ca4`

## 4. 实现内容与相关代码

| 功能 | 修改/新增文件 | 实现说明 |
| --- | --- | --- |
| 用户程序注册和工具链兼容 | [`Makefile`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/util/Makefile) | 将五个程序加入 `UPROGS`；针对 GCC 13 将 xv6 起始代码的有意递归诊断降为普通警告。 |
| sleep | [`user/sleep.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/util/user/sleep.c) | 校验 tick 参数，转换后调用已有 `sleep` 系统调用。 |
| pingpong | [`user/pingpong.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/util/user/pingpong.c) | 使用两个单向管道在父子进程之间往返传递一个字节。 |
| primes | [`user/primes.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/util/user/primes.c) | 使用逐级创建的进程和管道实现 2 到 35 的并发素数筛。 |
| find | [`user/find.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/util/user/find.c) | 读取目录项并递归遍历目录树，输出名称匹配的完整路径。 |
| xargs | [`user/xargs.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/util/user/xargs.c) | 逐行读取标准输入，分割附加参数，并通过 `fork`/`exec` 执行命令。 |
| 实验用时 | [`time.txt`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/util/time.txt) | 按官方评分脚本要求记录整数小时。 |

### 4.1 关键实现说明

#### 管道端点与 EOF

`pingpong` 为两个方向分别建立管道。父子进程在通信前关闭不属于自己的读写端，子进程收到字节后输出 `ping`，再向反向管道写回；父进程读取返回字节后输出 `pong` 并回收子进程。

`primes` 的每一级进程从输入管道取得第一个整数作为本级素数，再把不能被该素数整除的整数写入下一级。进程必须关闭不再使用的写端，否则下一级无法观察到 EOF；父进程还要逐级 `wait`，避免遗留僵尸进程。

#### 目录递归

`find` 参考 xv6 的 `ls` 实现，通过 `open`、`fstat` 和 `read` 读取固定长度的 `struct dirent`。复制目录名后显式补 NUL，并跳过 `.` 和 `..`，避免无限递归。拼接路径前检查 512 字节缓冲区容量，离开当前目录时关闭文件描述符。

#### 参数组织

`xargs` 保留命令行中已有参数，再逐行读取标准输入并按空白切分附加参数。每一行独立 `fork` 和 `exec`，父进程等待该行完成后再处理下一行；同时检查 `MAXARG`、输入行长度和无换行 EOF 尾行。

## 5. 测试过程、结果与分析

### 5.1 测试命令

```bash
make grade
```

评分脚本会分别启动 xv6/QEMU，验证 `sleep`、`pingpong`、`primes`、`find` 和 `xargs`，并检查 `time.txt`。

### 5.2 测试结果

| 测试项 | 命令/场景 | 实际结果 | 结论 |
| --- | --- | --- | --- |
| sleep 参数与系统调用 | `grade-lab-util` 的三个 sleep 测试 | 全部 OK | 通过 |
| 父子进程通信 | `pingpong` | OK | 通过 |
| 并发素数筛 | `primes` | OK | 通过 |
| 当前目录与递归查找 | 两个 find 测试 | 全部 OK | 通过 |
| 按行执行命令 | `xargs` | OK，输出中出现三次 `hello` | 通过 |
| 自动评分 | `make grade` | `Score: 100/100` | 通过 |

关键评分输出：

```text
sleep, no arguments: OK
sleep, returns: OK
sleep, makes syscall: OK
pingpong: OK
primes: OK
find, in current directory: OK
find, recursive: OK
xargs: OK
time: OK
Score: 100/100
```

### 5.3 结果分析

评分覆盖了五个程序的主要语义。`sleep` 的断点测试证明程序确实调用内核系统调用；`pingpong` 的输出顺序证明双向同步成立；`primes` 的完整素数序列证明多级过滤和 EOF 传递正确；两个 `find` 测试覆盖当前目录与多层目录；`xargs` 测试证明管道输入被逐行转化为命令参数。最终评分为 100/100，说明必做测试均通过。

## 6. 遇到的问题与解决方法

| 问题 | 原因分析 | 解决方法 |
| --- | --- | --- |
| QEMU 在受限沙箱内无法启动评分 | 评分使用本地 GDB socket，受限运行环境禁止创建对应 socket | 在保持仓库范围不变的情况下，以允许本地 QEMU socket 的权限运行 `make grade`。 |
| GCC 13 将 `user/sh.c` 的递归报告为错误 | 2021 起始代码面向较旧工具链；新版 GCC 的 `-Winfinite-recursion` 与全局 `-Werror` 组合导致构建中止 | 在 Makefile 中仅将该诊断从错误降为警告，不改变 shell 行为。 |
| 初次评分为 99/100 | 官方评分脚本要求仓库根目录存在整数格式的 `time.txt` | 按实际投入向上取整记录 `1`，复测得到 100/100。 |
| 管道程序可能等待不结束 | 任一进程保留无用写端时，下游读端无法收到 EOF | 每次 `fork` 后立即关闭无用端，并在写入完成后关闭本级写端。 |

## 7. 原理理解与总结

本实验展示了 Unix 将少量基础机制组合成实用工具的方式。`fork` 复制执行上下文，`pipe` 提供有序字节流，`exec` 用新程序替换当前进程映像，`wait` 负责同步和资源回收。管道不仅传输数据，写端全部关闭产生的 EOF 还是进程间的重要完成信号。

并发素数筛把每个过滤阶段映射为一个进程，体现了通过进程和管道构造数据流。`find` 则说明用户程序可以借助简单的目录项和文件状态接口实现递归文件系统操作。完成这些程序后，对 xv6 用户态 API、进程生命周期和文件描述符继承关系有了更具体的理解。

## 8. 代码与复现说明

- 仓库：[xv6-labs-2021-do](https://github.com/xswt442-cmd/xv6-labs-2021-do)
- 分支：`util`
- 代码提交：`402fd7bc9430ca8fad5fb3c7f69f6913115d0719`
- 报告提交：以包含本文件的提交为准，可执行 `git log -1 --format=%H -- reports/report-lab-util.md` 获取
- 复现步骤：切换到 `util` 分支后执行 `make grade`
