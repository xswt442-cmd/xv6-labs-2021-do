# xv6 Lab 实验报告：syscall

## 1. 实验基本信息

- 课程/项目：MIT 6.S081 / xv6 2021
- 实验名称：syscall — System calls
- 实验分支：`syscall`
- 完成日期：2026-08-08
- 官方题目：[Lab: System calls](https://pdos.csail.mit.edu/6.828/2021/labs/syscall.html)
- 代码仓库：[xv6-labs-2021-do](https://github.com/xswt442-cmd/xv6-labs-2021-do)
- 对应代码提交：`18e5c1aed5b356ec3747958661a86bbdfce77f66`

## 2. 实验目标

- 理解用户态封装、汇编桩、系统调用号和内核分发表构成的完整系统调用路径。
- 实现按位掩码控制的系统调用跟踪，并理解进程属性在 `fork` 和 `exec` 中的传播。
- 在正确锁保护下统计空闲物理内存和进程数量，并安全复制到用户地址。

## 3. 实验环境

- 宿主系统：WSL2，Ubuntu 24.04.4 LTS
- 编译工具链：`riscv64-unknown-elf-gcc 13.2.0`
- 模拟器：`QEMU emulator version 8.2.2`
- xv6 起始版本：`syscall` 分支，起始提交 `1e6e6cafbb26a898b8c3f90e819fc5e7227dc8af`

## 4. 实现内容与相关代码

| 功能 | 修改/新增文件 | 实现说明 |
| --- | --- | --- |
| 系统调用接口 | [`kernel/syscall.h`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/syscall/kernel/syscall.h)、[`user/user.h`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/syscall/user/user.h)、[`user/usys.pl`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/syscall/user/usys.pl) | 增加 `trace`、`sysinfo` 的编号、用户原型和汇编桩。 |
| trace 分发与输出 | [`kernel/syscall.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/syscall/kernel/syscall.c) | 注册处理函数和名称表，在调用返回后按掩码打印 PID、名称及返回值。 |
| trace 进程状态 | [`kernel/proc.h`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/syscall/kernel/proc.h)、[`kernel/proc.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/syscall/kernel/proc.c) | 保存跟踪掩码，分配/释放时清零，并在 `fork` 时继承。 |
| 内核处理函数 | [`kernel/sysproc.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/syscall/kernel/sysproc.c) | 实现 `sys_trace` 和 `sys_sysinfo`，使用 `copyout` 写用户空间。 |
| 内存与进程统计 | [`kernel/kalloc.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/syscall/kernel/kalloc.c)、[`kernel/proc.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/syscall/kernel/proc.c)、[`kernel/defs.h`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/syscall/kernel/defs.h) | 在对应锁保护下统计 freelist 字节数和非 `UNUSED` 进程数。 |
| trace 启动程序 | [`user/trace.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/syscall/user/trace.c) | 检查参数容量并为传给 `exec` 的 argv 补空指针结尾。 |
| 构建与评分 | [`Makefile`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/syscall/Makefile)、[`time.txt`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/syscall/time.txt) | 注册测试程序、兼容 GCC 13 递归诊断并记录实验用时。 |

### 4.1 trace 调用链

用户程序先调用 `trace(mask)`，生成的汇编桩把系统调用号放入 `a7` 并执行 `ecall`。`sys_trace` 将掩码保存到当前 `struct proc`。此字段在进程槽分配和释放时清零，防止复用槽位泄漏旧设置；`fork` 显式复制父进程掩码，而 `exec` 不重建 `struct proc`，因此跟踪设置自然保留。

`syscall()` 先调用分发表中的处理函数，把结果写回 `trapframe->a0`，随后检查 `tracemask & (1 << num)`。因此输出的是本次调用的真实返回值，而不是调用前参数。名称表和处理函数表都以系统调用号为索引，避免编号错位。

### 4.2 sysinfo 的同步与用户内存访问

空闲内存统计持有 `kmem.lock` 遍历物理页 freelist，每个节点计为 `PGSIZE` 字节。进程统计逐项获取 `proc[i].lock`，把状态不是 `UNUSED` 的槽位计入结果，并且不会同时持有多个进程锁。

`sys_sysinfo` 先在内核栈构造完整的 `struct sysinfo`。两个统计函数返回时已释放内部锁，之后才执行 `copyout`。这样既避免直接解引用不可信用户指针，也避免在可能访问页表的复制过程中持有统计锁；非法地址会返回 `-1`。

## 5. 测试过程、结果与分析

### 5.1 测试命令

```bash
make grade
```

### 5.2 测试结果

| 测试项 | 场景 | 实际结果 | 结论 |
| --- | --- | --- | --- |
| 指定 read 跟踪 | `trace 32 grep hello README` | read 返回值序列匹配 | 通过 |
| 跟踪全部调用 | `trace 2147483647 ...` | trace、exec、open、read、close 均匹配 | 通过 |
| 默认关闭跟踪 | 直接执行 `grep` | 无 syscall 跟踪输出 | 通过 |
| fork 继承 | `trace 2 usertests forkforkfork` | 子进程持续输出 fork 跟踪，测试通过 | 通过 |
| 系统信息 | `sysinfotest` | `sysinfotest: OK` | 通过 |
| 自动评分 | `make grade` | `Score: 35/35` | 通过 |

关键输出：

```text
trace 32 grep: OK
trace all grep: OK
trace nothing: OK
trace children: OK
sysinfotest: OK
time: OK
Score: 35/35
```

### 5.3 结果分析

不同 mask 的测试同时证明了位编号、名称表和返回值输出正确；“trace nothing”证明未设置掩码时不会产生副作用；子进程测试证明 `fork` 继承成立。`sysinfotest` 会耗尽并恢复可用页面、创建和回收进程，还会传入非法用户地址，因此覆盖了统计单位、状态语义、资源变化和 `copyout` 错误处理。

## 6. 遇到的问题与解决方法

| 问题 | 原因分析 | 解决方法 |
| --- | --- | --- |
| trace 的 argv 可能没有终止标记 | 起始 `trace.c` 复制参数后未写入空指针 | 检查 `MAXARG` 并设置 `nargv[i-2] = 0`。 |
| sysinfo 需要一致但不能长期持锁 | freelist 和进程状态会被其他 CPU 修改 | 分别在 `kmem.lock` 和单个进程锁下统计，统计完成立即释放。 |
| 非法用户指针不能直接写 | 用户传入地址可能未映射或无写权限 | 在内核栈组装结果并通过 `copyout` 验证和复制。 |
| 新 GCC 阻止官方起始 shell 编译 | GCC 13 的 `-Winfinite-recursion` 与 `-Werror` 组合 | 仅将该项降为警告，保留其他警告作为错误。 |

## 7. 原理理解与总结

系统调用不是单一函数，而是用户声明、汇编入口、调用号、trap、内核分发和返回寄存器共同形成的 ABI。`trace` 利用这一集中分发点，在不修改每个处理函数的前提下观察调用结果，也说明进程级策略应存放在 `struct proc` 并明确规定创建、继承和回收语义。

`sysinfo` 展示了“读取统计信息”同样需要并发控制。统计必须遵守原数据结构的锁规则，但把结果复制到用户空间前应缩短临界区。`copyout` 则建立了内核数据与不可信用户地址之间的安全边界。

## 8. 代码与复现说明

- 仓库：[xv6-labs-2021-do](https://github.com/xswt442-cmd/xv6-labs-2021-do)
- 分支：`syscall`
- 代码提交：`18e5c1aed5b356ec3747958661a86bbdfce77f66`
- 报告提交：执行 `git log -1 --format=%H -- reports/report-lab-syscall.md` 获取包含本文件的提交
- 复现步骤：切换到 `syscall` 分支后执行 `make grade`
