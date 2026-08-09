# xv6 Lab 实验报告：mmap

## 1. 实验基本信息

- 课程/项目：MIT 6.S081 / xv6 2021
- 实验名称：mmap — Mmap
- 实验分支：`mmap`
- 完成日期：2026-08-09
- 官方题目：https://pdos.csail.mit.edu/6.828/2021/labs/mmap.html
- 代码仓库：https://github.com/xswt442-cmd/xv6-labs-2021-do
- 对应代码提交：`a1f01395005f9a3256a8e7df54690b458ce03814`

## 2. 实验目标

- 为每个进程维护文件映射区域（VMA），实现实验规定子集的 `mmap()` 与 `munmap()`。
- 在首次访问时才分配物理页并从文件读取内容，理解文件、页表和缺页异常之间的联系。
- 正确处理共享写回、部分解除映射、进程退出、`exec()` 和 `fork()` 时的页面与文件引用生命周期。

## 3. 实验环境

- 宿主系统：WSL2 / Linux
- 编译工具链：`riscv64-unknown-elf-gcc 13.2.0`
- 模拟器：`QEMU 8.2.2`
- xv6 起始版本：上游 `mmap` 分支，提交 `2255a4ca32a31faeacd902855b77080e3ccbdd8f`

## 4. 实现内容与相关代码

以下链接固定到代码完成提交 `a1f0139`，可直接查看实验相关代码。

| 功能 | 修改/新增文件 | 实现说明 |
| --- | --- | --- |
| VMA 数据结构与生命周期 | [`kernel/proc.h`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/a1f01395005f9a3256a8e7df54690b458ce03814/kernel/proc.h)、[`kernel/param.h`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/a1f01395005f9a3256a8e7df54690b458ce03814/kernel/param.h)、[`kernel/proc.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/a1f01395005f9a3256a8e7df54690b458ce03814/kernel/proc.c) | 每进程保存 16 个 VMA，记录地址、页对齐长度、请求字节数、权限、模式、偏移和文件引用。 |
| mmap/munmap 系统调用 | [`kernel/sysfile.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/a1f01395005f9a3256a8e7df54690b458ce03814/kernel/sysfile.c)、[`kernel/syscall.h`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/a1f01395005f9a3256a8e7df54690b458ce03814/kernel/syscall.h)、[`kernel/syscall.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/a1f01395005f9a3256a8e7df54690b458ce03814/kernel/syscall.c) | 校验受限 ABI、文件访问模式和数值边界，并分配或缩减 VMA。 |
| 惰性缺页加载 | [`kernel/trap.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/a1f01395005f9a3256a8e7df54690b458ce03814/kernel/trap.c)、[`kernel/proc.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/a1f01395005f9a3256a8e7df54690b458ce03814/kernel/proc.c) | 捕获指令、读和写缺页，按 VMA 权限分配清零页，从文件对应位置读取后建立用户页表映射。 |
| 共享写回与资源回收 | [`kernel/proc.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/a1f01395005f9a3256a8e7df54690b458ce03814/kernel/proc.c)、[`kernel/exec.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/a1f01395005f9a3256a8e7df54690b458ce03814/kernel/exec.c) | 解除可写共享映射时按页写回；完整解除、退出或成功 exec 时释放页面和文件引用。 |
| fork 与地址空间边界 | [`kernel/proc.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/a1f01395005f9a3256a8e7df54690b458ce03814/kernel/proc.c) | 子进程复制 VMA、文件引用和已驻留页面；映射从高地址向下放置，并阻止 `sbrk()` 与 VMA 重叠。 |
| 用户接口与构建 | [`user/user.h`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/a1f01395005f9a3256a8e7df54690b458ce03814/user/user.h)、[`user/usys.pl`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/a1f01395005f9a3256a8e7df54690b458ce03814/user/usys.pl)、[`Makefile`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/a1f01395005f9a3256a8e7df54690b458ce03814/Makefile)、[`time.txt`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/a1f01395005f9a3256a8e7df54690b458ce03814/time.txt) | 增加用户存根，把 `mmaptest` 加入镜像，并保留评分所需用时文件。 |

### 4.1 VMA 分配与惰性加载

`mmap()` 只接受由内核选择地址、文件偏移为 0、`MAP_SHARED` 或 `MAP_PRIVATE` 的实验 ABI。请求长度保留为精确字节数，同时向上取整为页数；这样最后一页可以补零，又不会在解除映射时把请求范围之外的数据写入文件。映射在 `TRAPFRAME` 下方寻找最高可用空洞，不计入 `p->sz`，并用最低 VMA 地址限制堆增长，避免二者静默重叠。

首次访问映射地址时，`usertrap()` 把页面异常交给 `vma_fault()`。后者确认地址和访问类型符合 VMA 权限，分配并清零一个物理页，只读取文件中属于请求范围的字节，然后设置相应的 RISC-V PTE 权限。尚未访问的页面不占用物理内存。

### 4.2 解除映射与共享写回

`munmap()` 支持从一个 VMA 的头部、尾部或整体解除映射，并相应调整虚拟地址、文件偏移、页对齐长度和剩余请求字节数。只有已驻留的 `MAP_SHARED | PROT_WRITE` 页面才写回；`MAP_PRIVATE` 和只读共享映射不会改写文件。写回直接调用 `writei()` 并使用 VMA 相对文件偏移，不改变打开文件的 `f->off`，同时按日志容量分块。

写回长度先使用 64 位整数计算，再限制为单页和当前文件大小。这个顺序避免超大映射长度窄化成负 `int` 后突破单页边界，也避免越过原文件末尾扩展文件。

### 4.3 fork、exit 与 exec

`fork()` 为子进程复制每个 VMA 并增加文件引用；已缺页加载的页面复制到新的物理页，未加载页面仍保持惰性。失败路径只回收已经复制的页面和引用，不触发共享写回。`exit()` 在关闭普通文件描述符前清理全部 VMA；成功 `exec()` 在新映像已经构造完成后清理旧映射，而失败的 `exec()` 保留原进程映像。

## 5. 测试过程、结果与分析

### 5.1 测试命令

```bash
make grade

# 开发期间临时加入镜像并在 xv6 shell 中执行，验证额外边界：
mmapedge
mmapbig
```

### 5.2 测试结果

| 测试项 | 命令/场景 | 实际结果 | 结论 |
| --- | --- | --- | --- |
| 官方 mmap 功能 | `make grade` 中 8 项 `mmaptest` | `mmap f`、private、read-only、read/write、dirty、not-mapped unmap、two files、fork 全部 OK | 通过 |
| 完整回归 | `make grade` 中 `usertests` | 最终一轮 `usertests: OK (83.4s)` | 通过 |
| 自动评分 | `make grade` | `Score: 140/140` | 通过 |
| 参数与 VMA 边界 | 临时 `mmapedge` | 覆盖非法地址、权限/模式、非零偏移、非页对齐长度、16 个 VMA 上限，输出 `mmapedge: OK` | 通过 |
| 超大映射写回边界 | 临时 `mmapbig` | 2 GiB 共享映射只解除第一页，第二个文件页保持原内容，输出 `mmapbig: OK` | 通过 |

最终评分关键输出如下：

```text
mmaptest: mmap f: OK
mmaptest: mmap private: OK
mmaptest: mmap read-only: OK
mmaptest: mmap read/write: OK
mmaptest: mmap dirty: OK
mmaptest: not-mapped unmap: OK
mmaptest: two files: OK
mmaptest: fork_test: OK
usertests: OK (83.4s)
time: OK
Score: 140/140
```

### 5.3 结果分析

官方测试覆盖私有与共享映射、读写权限、部分解除映射、两个文件、文件描述符关闭后的引用保持及 `fork()`；完整 `usertests` 证明现有进程、文件系统和虚拟内存行为没有明显回归。额外边界测试补充了官方评分未覆盖的错误参数、非页对齐长度、VMA 用尽和超大长度写回，后者直接验证每次写回不会读取单个物理页之外的内存。

实现遵循本实验的受限 mmap 语义，并非完整 POSIX 实现：不支持指定映射地址、非零文件偏移、匿名映射或中间挖洞。RISC-V 叶 PTE 不允许“可写但不可读”的组合，因此单独请求 `PROT_WRITE` 的页面在装入后也具有硬件读权限。另一个未扩展的 xv6 接口限制是，尚未缺页加载的映射不能直接作为系统调用的内核拷贝缓冲区；本实验规定测试不依赖该行为。

## 6. 遇到的问题与解决方法

| 问题 | 原因分析 | 解决方法 |
| --- | --- | --- |
| VMA 页面不在 `p->sz` 内 | 原有 `uvmcopy()`、`uvmfree()` 只处理连续低地址用户区 | 为 VMA 单独实现 fork 复制、解除映射和清理流程，并在释放普通页表前先移除 VMA 页 |
| 非页对齐长度可能多读写文件 | 页表按整页管理，而接口长度按字节给出 | 同时保存页对齐长度和精确请求字节数，读入及写回均按精确范围裁剪 |
| 只读共享映射被无条件写回 | 仅按 `MAP_SHARED` 判断会让未修改的只读映射覆盖文件 | 写回条件增加 `PROT_WRITE`，并限制到当前文件大小 |
| 超大长度可能突破单页写回 | 64 位剩余长度过早赋给 `int` 会溢出，进而失去 `PGSIZE` 上限 | 先用 `uint64` 比较并限制到一页，再安全转换；用 2 GiB 映射定向复验 |
| `fork()` 失败清理可能在进程锁内睡眠 | 通用 VMA 清理包含文件系统写回和关闭操作 | 失败路径直接回收子进程新页和文件引用，不执行共享写回 |
| 负数 `sbrk()` 可能发生无符号下溢 | 原实现直接把有符号增量与无符号地址相加 | 显式计算缩减量并先检查是否超过当前进程大小 |

## 7. 原理理解与总结

`mmap` 把三个原本相对独立的对象连接起来：VMA 描述进程的虚拟地址契约，页表记录已经兑现的页面，文件对象保存后备数据和生命周期。惰性分配意味着“地址有效”不等于“PTE 已存在”，因此缺页不是单纯错误，而是把 VMA 承诺兑现为物理页的正常控制流。

资源回收必须与这种惰性状态对称：只解除实际存在的 PTE，但无论页面是否驻留都要更新 VMA；共享页需要在丢弃前写回，私有页则直接释放；文件描述符可以先关闭，因此 VMA 自己必须持有独立文件引用。`fork()`、`exit()` 和 `exec()` 分别对应继承、终止和替换地址空间，是检验这套生命周期是否完整的关键路径。

## 8. 代码与复现说明

- 仓库：https://github.com/xswt442-cmd/xv6-labs-2021-do
- 分支：`mmap`
- 代码提交：`a1f01395005f9a3256a8e7df54690b458ce03814`
- 报告提交：包含本文件的 `lab(mmap): 提交实验报告` 提交
- 复现步骤：切换到 `mmap` 分支后执行 `make grade`；评分脚本会依次运行完整 `mmaptest`、`usertests` 和 `time` 检查。
