# xv6 Lab 实验报告：cow

## 1. 实验基本信息

- 课程/项目：MIT 6.S081 / xv6 2021
- 实验名称：cow — Copy-on-Write fork
- 实验分支：`cow`
- 完成日期：2026-08-08
- 官方题目：[Lab: Copy-on-Write Fork](https://pdos.csail.mit.edu/6.828/2021/labs/cow.html)
- 代码仓库：[xv6-labs-2021-do](https://github.com/xswt442-cmd/xv6-labs-2021-do)
- 对应代码提交：`66c7dcdf719abf3707271ae4a9d2a5a7ed16d3f1`

## 2. 实验目标

- 让 `fork` 通过共享物理页避免立即复制全部用户内存。
- 使用 PTE 权限和 store page fault 延迟执行实际页面复制。
- 用引用计数保证共享物理页只在最后一个映射消失后释放。
- 让内核 `copyout` 与用户写缺页遵守同一 COW 和写权限语义。

## 3. 实验环境

- 宿主系统：WSL2，Ubuntu 24.04.4 LTS
- 编译工具链：`riscv64-unknown-elf-gcc 13.2.0`
- 模拟器：`QEMU emulator version 8.2.2`
- xv6 起始版本：`cow` 分支，起始提交 `c9818915934504523e52a33e2755b7aff54c495e`

## 4. 实现内容与相关代码

| 功能 | 修改/新增文件 | 实现说明 |
| --- | --- | --- |
| COW PTE 标志 | [`kernel/riscv.h`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/cow/kernel/riscv.h) | 使用 RSW 软件保留位定义 `PTE_COW`。 |
| 物理页引用计数 | [`kernel/kalloc.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/cow/kernel/kalloc.c)、[`kernel/defs.h`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/cow/kernel/defs.h) | 为可分配物理页维护受锁保护的引用计数。 |
| 共享 fork 与缺页复制 | [`kernel/vm.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/cow/kernel/vm.c) | 重写 `uvmcopy`，实现统一 `cowfault`，并修改 `copyout`。 |
| 用户写缺页处理 | [`kernel/trap.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/cow/kernel/trap.c) | 仅把 store page fault 交给 COW 解析，非法写终止进程。 |
| 构建与评分 | [`Makefile`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/cow/Makefile)、[`time.txt`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/cow/time.txt) | 兼容 GCC 13 递归诊断并记录实验用时。 |

### 4.1 共享映射与失败回滚

`uvmcopy` 不再分配和复制每个用户页。第一阶段为子页表建立指向同一物理页的映射并增加引用计数；原本可写的页在子映射中清除 `PTE_W` 并标记 `PTE_COW`，原本只读的代码页保持普通只读，不能因 fork 获得写权限。

只有全部子映射成功后，第二阶段才降低父页权限并刷新 TLB。若中途建立映射失败，`uvmunmap(..., do_free=1)` 撤销已经建立的子映射和相应引用；此时父 PTE 尚未改变，因此无需复杂的权限回滚。

### 4.2 引用计数

引用数组按 `(pa-KERNBASE)/PGSIZE` 索引。初始化空闲页时先赋予一个临时引用，再通过 `kfree` 降到 0 并加入 freelist；`kalloc` 摘下页面后设置为 1。共享映射增加引用，解除映射或替换旧页时调用 `kfree` 递减，只有 1 到 0 的转换才真正填充垃圾并归还 freelist。所有计数访问受 `reflock` 保护。

### 4.3 COW fault 与 copyout

`cowfault` 要求地址在进程范围内，PTE 同时具有 V、U、COW 且不具有 W。引用数为 1 时可以直接恢复写权限并清除 COW；共享时先分配新页、复制完整内容、原子替换 PTE、刷新 TLB，最后减少旧页引用。分配失败不会改变旧映射。

用户态只有 store page fault 会进入该路径。`copyout` 在每个目标页上检查真实 PTE，遇到 COW 时调用同一函数，随后重新取 PTE；普通只读页仍返回失败，避免内核复制绕过用户页写权限。

## 5. 测试过程、结果与分析

### 5.1 测试命令

```bash
make grade
```

### 5.2 测试结果

| 测试项 | 实际结果 | 结论 |
| --- | --- | --- |
| cowtest simple | 两次 `simple: ok` | 通过 |
| cowtest three | 三次 `three: ok` | 通过 |
| cowtest file | `file: ok` | 通过 |
| usertests copyin/copyout | 均 OK | 通过 |
| 完整 usertests | `ALL TESTS PASSED` | 通过 |
| 自动评分 | `Score: 110/110` | 通过 |

```text
simple: OK
three: OK
file: OK
usertests: copyin: OK
usertests: copyout: OK
usertests: all tests: OK
time: OK
Score: 110/110
```

### 5.3 结果分析

simple 验证父子写入后内容隔离；three 让三个进程共同写共享内存，覆盖引用计数和多次复制；file 专门验证 `read` 等内核路径通过 `copyout` 写入 COW 用户页。完整 usertests 还覆盖 fork/exit、内存耗尽、copyin/copyout 和只读地址等回归场景。

## 6. 遇到的问题与解决方法

| 问题 | 原因分析 | 解决方法 |
| --- | --- | --- |
| fork 部分失败时父页可能被错误降权 | 边建立子映射边修改父 PTE 难以回滚 | 使用两阶段 `uvmcopy`，全部映射成功后才修改父权限。 |
| 共享页可能过早释放 | 原分配器不知道一个物理页有多个 PTE | 增加锁保护的物理页引用计数，最后一个引用才入 freelist。 |
| `copyout` 可绕过 COW 或写普通只读页 | 原实现只通过 `walkaddr` 获取物理地址 | 显式检查 PTE，解析 COW 后仍要求 V/U/W。 |
| TLB 可能保留旧写权限 | 修改 PTE 不会自动清除当前 hart 的缓存 | 父页降权和缺页换页后执行 `sfence_vma`。 |

## 7. 原理理解与总结

COW 把 fork 的成本从“复制所有页”改为“复制实际发生写入的页”。关键不只是清除写位，还包括识别哪些只读页可以永远共享、为共享物理页建立可靠生命周期，以及确保用户缺页和内核写用户内存使用一致的权限规则。

引用计数与 PTE 更新存在严格顺序：新映射发布前先保证物理页存活，旧映射替换后才能减少旧引用；OOM 或映射失败必须保持原地址空间有效。两阶段提交和先新后旧的换页顺序使这些失败路径可验证。

## 8. 代码与复现说明

- 仓库：[xv6-labs-2021-do](https://github.com/xswt442-cmd/xv6-labs-2021-do)
- 分支：`cow`
- 代码提交：`66c7dcdf719abf3707271ae4a9d2a5a7ed16d3f1`
- 报告提交：执行 `git log -1 --format=%H -- reports/report-lab-cow.md` 获取
- 复现步骤：切换到 `cow` 分支后执行 `make grade`
