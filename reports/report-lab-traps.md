# xv6 Lab 实验报告：traps

## 1. 实验基本信息

- 课程/项目：MIT 6.S081 / xv6 2021
- 实验名称：traps — Traps
- 实验分支：`traps`
- 完成日期：2026-08-08
- 官方题目：[Lab: Traps](https://pdos.csail.mit.edu/6.828/2021/labs/traps.html)
- 代码仓库：[xv6-labs-2021-do](https://github.com/xswt442-cmd/xv6-labs-2021-do)
- 对应代码提交：`30f312dcfed12fc9393f864a2ac2477bb0b57918`

## 2. 实验目标

- 结合实际 RISC-V 汇编理解调用约定、参数寄存器、返回地址和小端序。
- 利用帧指针实现可靠的内核栈回溯。
- 理解用户 trapframe 的保存与恢复，实现周期性用户级 alarm。
- 正确处理时钟中断、handler 禁止重入和系统调用返回寄存器。

## 3. 实验环境

- 宿主系统：WSL2，Ubuntu 24.04.4 LTS
- 编译工具链：`riscv64-unknown-elf-gcc 13.2.0`
- 模拟器：`QEMU emulator version 8.2.2`
- xv6 起始版本：`traps` 分支，起始提交 `219a8d7d70b6ac66b1447aeada079a1f8c3027f7`

## 4. 实现内容与相关代码

| 功能 | 修改/新增文件 | 实现说明 |
| --- | --- | --- |
| 汇编与字节序问答 | [`answers-traps.txt`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/traps/answers-traps.txt) | 依据 GCC 13 实际生成的 `user/call.asm` 回答寄存器、地址、内联和端序问题。 |
| 内核栈回溯 | [`kernel/printf.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/traps/kernel/printf.c)、[`kernel/riscv.h`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/traps/kernel/riscv.h)、[`kernel/defs.h`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/traps/kernel/defs.h) | 读取 `s0/fp`，沿栈帧输出返回地址，并检查栈页边界。 |
| alarm 状态 | [`kernel/proc.h`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/traps/kernel/proc.h)、[`kernel/proc.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/traps/kernel/proc.c)、[`kernel/exec.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/traps/kernel/exec.c) | 保存 interval、tick、handler、执行标志和完整 trapframe，管理初始化、fork 与 exec 生命周期。 |
| 时钟触发 | [`kernel/trap.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/traps/kernel/trap.c) | 在用户态时钟中断上累计 tick，保存现场并将 epc 改为 handler。 |
| alarm 系统调用 | [`kernel/sysproc.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/traps/kernel/sysproc.c)、[`kernel/syscall.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/traps/kernel/syscall.c)、[`kernel/syscall.h`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/traps/kernel/syscall.h)、[`user/user.h`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/traps/user/user.h)、[`user/usys.pl`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/traps/user/usys.pl) | 增加 `sigalarm` 和 `sigreturn` 的完整调用链。 |
| 构建与评分 | [`Makefile`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/traps/Makefile)、[`time.txt`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/traps/time.txt) | 注册 alarmtest，兼容 GCC 13 递归诊断并记录用时。 |

### 4.1 栈回溯

RISC-V 编译器在内核函数中用 `s0` 保存帧指针。按照 xv6 的栈帧布局，返回地址位于 `fp-8`，上一帧指针位于 `fp-16`。回溯从当前 fp 开始，只访问 `PGROUNDDOWN(fp)` 所在的内核栈页，并检查 16 字节对齐、地址范围和前一 fp 单调增大，避免损坏栈造成越界或死循环。该函数由 `sys_sleep` 和 `panic` 调用。

### 4.2 alarm 现场保存与恢复

每个进程保存 interval、累计 tick、handler 地址、`in_handler` 标志和一份完整 `struct trapframe`。用户态 timer trap 到达时，只有未处于 handler 且 interval 非零才累计；达到阈值后先复制整个 trapframe，再把当前 epc 改为 handler 地址并设置执行标志。

`sigreturn` 把完整快照复制回活动 trapframe，因此包括 epc、栈指针、临时寄存器、保存寄存器和参数寄存器。它返回快照中的原 `a0`，抵消系统调用分发器随后写回返回值的行为。handler 运行期间不再触发 alarm，防止重入；`sigalarm(0, 0)` 可以禁用后续 alarm，但不会阻止当前 handler 调用 `sigreturn`。

`allocproc` 和 `freeproc` 清理状态；fork 继承配置但从新的计时状态开始；成功 exec 会清除旧地址空间中的 handler 地址。

## 5. 测试过程、结果与分析

### 5.1 测试命令

```bash
make grade
```

### 5.2 测试结果

| 测试项 | 实际结果 | 结论 |
| --- | --- | --- |
| `answers-traps.txt` | OK | 通过 |
| backtrace | 三个地址经 addr2line 对应 sysproc.c、syscall.c、trap.c | 通过 |
| alarmtest test0 | handler 周期触发 | 通过 |
| alarmtest test1 | 中断现场完整恢复 | 通过 |
| alarmtest test2 | handler 不重入 | 通过 |
| 完整 usertests | OK | 通过 |
| 自动评分 | `Score: 85/85` | 通过 |

```text
answers-traps.txt: OK
backtrace test: OK
alarmtest: test0: OK
alarmtest: test1: OK
alarmtest: test2: OK
usertests: OK
time: OK
Score: 85/85
```

### 5.3 结果分析

backtrace 测试把输出地址交给 `addr2line`，证明回溯的不只是格式，而是正确的内核调用链。alarmtest 分别覆盖基本触发、计算期间所有寄存器恢复和慢 handler 禁止重入。完整 usertests 通过说明 trap 路径、fork、exec 和普通系统调用没有因新增状态而回归。

## 6. 遇到的问题与解决方法

| 问题 | 原因分析 | 解决方法 |
| --- | --- | --- |
| 旧版参考汇编地址不适用 | GCC 13 优化会内联和常量折叠 | 以本机生成的 `user/call.asm` 为依据，确认 `printf=0x626`、返回地址 `0x38`。 |
| handler 返回后计算状态可能损坏 | 只保存 epc 或少量寄存器不足 | 保存和恢复完整 trapframe，并让 sigreturn 返回原 a0。 |
| alarm 可能嵌套进入 | 慢 handler 期间仍收到时钟中断 | 使用 `in_handler` 阻止再次累计和触发。 |
| handler 地址在 exec 后失效 | 新程序地址空间与旧 handler 不相关 | 成功提交新地址空间时清除 alarm 配置和快照。 |

## 7. 原理理解与总结

trapframe 是用户态与内核态之间的完整上下文契约。系统调用和中断进入内核时保存用户寄存器，返回时恢复；alarm 的本质是内核有条件地改写返回 PC，同时保留一份稍后可恢复的原始现场。`sigreturn` 因而不是普通函数返回，而是一次完整上下文恢复。

栈回溯则依赖 ABI 对栈帧的约定。帧指针使内核可以在没有高级运行时的情况下还原调用链，但实现必须把不可信或损坏的栈内容限制在当前栈页内。

## 8. 代码与复现说明

- 仓库：[xv6-labs-2021-do](https://github.com/xswt442-cmd/xv6-labs-2021-do)
- 分支：`traps`
- 代码提交：`30f312dcfed12fc9393f864a2ac2477bb0b57918`
- 报告提交：执行 `git log -1 --format=%H -- reports/report-lab-traps.md` 获取
- 复现步骤：切换到 `traps` 分支后执行 `make grade`
