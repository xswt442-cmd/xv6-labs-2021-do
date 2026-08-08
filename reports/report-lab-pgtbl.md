# xv6 Lab 实验报告：pgtbl

## 1. 实验基本信息

- 课程/项目：MIT 6.S081 / xv6 2021
- 实验名称：pgtbl — Page tables
- 实验分支：`pgtbl`
- 完成日期：2026-08-08
- 官方题目：[Lab: Page tables](https://pdos.csail.mit.edu/6.828/2021/labs/pgtbl.html)
- 代码仓库：[xv6-labs-2021-do](https://github.com/xswt442-cmd/xv6-labs-2021-do)
- 对应代码提交：`75bfec9c20e1ba465bf47a6231d09a1b0de09a12`

## 2. 实验目标

- 理解 RISC-V Sv39 三级页表、叶 PTE、物理页号和访问权限。
- 通过只读共享页让用户态无须陷入内核即可读取 PID。
- 实现递归页表打印，观察进程虚拟地址空间的实际结构。
- 使用 PTE Accessed 位实现页面访问查询，并正确清除已消费的状态。

## 3. 实验环境

- 宿主系统：WSL2，Ubuntu 24.04.4 LTS
- 编译工具链：`riscv64-unknown-elf-gcc 13.2.0`
- 模拟器：`QEMU emulator version 8.2.2`
- xv6 起始版本：`pgtbl` 分支，起始提交 `1e6b2dec7d5ca49571e426ccc6cd686d009b6d07`

## 4. 实现内容与相关代码

| 功能 | 修改/新增文件 | 实现说明 |
| --- | --- | --- |
| USYSCALL 生命周期 | [`kernel/proc.h`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/pgtbl/kernel/proc.h)、[`kernel/proc.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/pgtbl/kernel/proc.c) | 为每个进程分配独立共享页，写入 PID，以用户只读权限映射并在回收时释放。 |
| 页表打印 | [`kernel/vm.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/pgtbl/kernel/vm.c)、[`kernel/defs.h`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/pgtbl/kernel/defs.h)、[`kernel/exec.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/pgtbl/kernel/exec.c) | 递归打印三级有效 PTE，并在 PID 1 成功 exec 后触发。 |
| 页面访问查询 | [`kernel/sysproc.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/pgtbl/kernel/sysproc.c)、[`kernel/riscv.h`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/pgtbl/kernel/riscv.h) | 定义 `PTE_A`，实现 32 位访问位图、PTE 校验、清位、TLB 刷新与安全 copyout。 |
| 题目回答 | [`answers-pgtbl.txt`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/pgtbl/answers-pgtbl.txt) | 解释共享页用途、映射长度、Sv39 输出、page 0/1/2 和 USYSCALL 权限。 |
| 构建与用时 | [`Makefile`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/pgtbl/Makefile)、[`time.txt`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/pgtbl/time.txt) | 兼容 GCC 13 的递归诊断并满足评分元数据要求。 |

### 4.1 USYSCALL 映射与所有权

`allocproc` 在取得 PID 后分配并清零一页物理内存，将 PID 写入 `struct usyscall`。`proc_pagetable` 把它映射到固定虚拟地址 `USYSCALL`，权限只有 `PTE_U | PTE_R`，因此用户态可以读取但不能改写。

`exec` 创建新页表时仍映射当前进程的同一共享页，旧页表销毁时只解除映射。最终 `freeproc` 才释放物理页。各失败路径沿相同所有权规则清理，避免泄漏和双重释放；`fork` 创建的新进程则得到写有自己 PID 的独立页。

### 4.2 vmprint 与 Sv39 层次

`vmprint` 以索引升序遍历 512 个 PTE，只输出有效项。没有 R/W/X 位的有效 PTE 是指向下一层页表的非叶项，递归深度限制为 Sv39 的三级；叶项打印 PTE 值和由 PPN 还原的物理地址。PID 1 的新用户页表提交后调用该函数，使输出对应 init 的最终地址空间。

页表低地址区域包括程序、保护页和用户栈；高地址区域包括只读 USYSCALL、仅内核可访问的 trapframe 以及 trampoline。更具体的 PTE 与 page 0/1/2 分析记录在 `answers-pgtbl.txt`。

### 4.3 pgaccess 的状态消费

`pgaccess` 将 `malloc` 可能返回的页内地址向下对齐到所在页，限制查询为 1 到 32 页，并在改变任何状态前验证整个范围内的 PTE 都是有效用户叶项。第一次遍历生成 32 位 mask；只有 `copyout` 成功后才清除相应 PTE 的 A 位，因此坏目标地址不会消费访问状态。发生清位后执行 `sfence_vma`，保证后续访问重新经过页表并设置 A 位。

## 5. 测试过程、结果与分析

### 5.1 测试命令

```bash
make grade
```

### 5.2 测试结果

| 测试项 | 实际结果 | 结论 |
| --- | --- | --- |
| `pgtbltest: ugetpid` | OK | 通过 |
| `pgtbltest: pgaccess` | OK | 通过 |
| `pte printout` | 输出结构、PTE 与 PA 关系匹配 | 通过 |
| `answers-pgtbl.txt` | OK | 通过 |
| 完整 `usertests` | `ALL TESTS PASSED` | 通过 |
| 自动评分 | `Score: 46/46` | 通过 |

关键输出：

```text
pgtbltest: ugetpid: OK
pgtbltest: pgaccess: OK
pte printout: OK
answers-pgtbl.txt: OK
usertests: all tests: OK
time: OK
Score: 46/46
```

### 5.3 结果分析

`ugetpid` 在 64 次 fork 中与系统调用结果逐次比较，覆盖了共享页的分配和 PID 初始化。`pgaccess` 在首次查询后访问第 1、2、30 页，第二次查询仅返回对应位，证明 A 位读取、清除和重新设置成立。页表打印测试不仅比较层次和索引，还验证 `PTE2PA` 关系。完整 usertests 通过说明新增固定映射没有破坏 fork、exec、sbrk 或内存回收。

## 6. 遇到的问题与解决方法

| 问题 | 原因分析 | 解决方法 |
| --- | --- | --- |
| 初版 pgaccess 未识别测试访问 | `malloc` 返回值不保证页对齐 | 按“从地址所在页开始”的语义用 `PGROUNDDOWN` 获取首个虚拟页。 |
| 非法 mask 地址可能提前清除 A 位 | 先清位再 copyout 会产生失败副作用 | 先生成并成功复制位图，再清 A 位和刷新 TLB。 |
| 共享页容易双重释放 | 页表映射与物理页所有权混淆 | 页表销毁只解除 USYSCALL 映射，`freeproc` 统一释放物理页。 |
| 完整 usertests 时间较长 | 评分包含大量进程、文件系统和异常路径回归测试 | 保持 QEMU 运行到明确输出 `ALL TESTS PASSED`，不以中间日志替代结果。 |

## 7. 原理理解与总结

页表既完成地址翻译，也承载权限和硬件维护的状态。USYSCALL 说明同一物理页可以在内核直接映射和用户页表中以不同权限出现，从而在维持只读边界的同时减少系统调用开销。vmprint 把 Sv39 的三级索引和叶/非叶 PTE 关系直接呈现出来。

Accessed 位是硬件与操作系统协作的例子：硬件记录访问，内核读取并清除，之后必须处理 TLB 缓存才能观察新的访问周期。实现中还需关注失败原子性，不能因为用户输出指针无效就悄悄消费原有状态。

## 8. 代码与复现说明

- 仓库：[xv6-labs-2021-do](https://github.com/xswt442-cmd/xv6-labs-2021-do)
- 分支：`pgtbl`
- 代码提交：`75bfec9c20e1ba465bf47a6231d09a1b0de09a12`
- 报告提交：执行 `git log -1 --format=%H -- reports/report-lab-pgtbl.md` 获取
- 复现步骤：切换到 `pgtbl` 分支后执行 `make grade`
