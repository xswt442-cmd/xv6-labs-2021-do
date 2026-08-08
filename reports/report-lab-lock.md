# xv6 Lab 实验报告：lock

## 1. 实验基本信息

- 课程/项目：MIT 6.S081 / xv6 2021
- 实验名称：lock — Parallelism/locking
- 实验分支：`lock`
- 完成日期：2026-08-08
- 官方题目：https://pdos.csail.mit.edu/6.828/2021/labs/lock.html
- 代码仓库：https://github.com/xswt442-cmd/xv6-labs-2021-do
- 对应代码提交：`8a9f3d985896e4f21597b4b0a05cd4d04229423c`

## 2. 实验目标

- 将物理页分配器拆分为每 CPU 空闲链表，降低 `kmem` 锁竞争。
- 将 buffer cache 拆分为哈希桶，降低无关磁盘块之间的锁竞争。
- 在提高并行度的同时维持页唯一归属、缓存块唯一性、引用计数和日志 pin 语义。

## 3. 实验环境

- 宿主系统：WSL2 / Linux
- 编译工具链：`riscv64-unknown-elf-gcc 13.2.0`
- 模拟器：`QEMU 8.2.2`
- xv6 起始版本：上游 `lock` 分支，提交 `281b66cf19660eb15c4542b63693c74a9ced0467`

## 4. 实现内容与相关代码

| 功能 | 修改/新增文件 | 实现说明 |
| --- | --- | --- |
| 每 CPU 页分配器 | `kernel/kalloc.c` | 每个 CPU 使用独立 freelist 与锁，本地耗尽时从其他 CPU 批量 steal。 |
| 哈希 buffer cache | `kernel/bio.c`、`kernel/buf.h` | 13 个桶分别加锁，命中仅访问目标桶，miss 由淘汰锁串行。 |
| 低争用淘汰 | `kernel/bio.c` | 目标桶保持锁定，逐桶扫描 victim，并在迁移前重新验证候选状态。 |
| 工具链兼容 | `Makefile` | 仅在 GCC 13 下把 `infinite-recursion` 保留为非致命警告。 |
| 实验用时 | `time.txt` | 按评分脚本格式记录整数。 |

### 4.1 每 CPU freelist

`kfree()` 在关闭中断后读取 `cpuid()`，把页归还当前 CPU 的链表。`kalloc()` 优先从本地链表弹出；本地为空时逐个获取其他 CPU 的锁，拆出一批页，再把除返回页以外的页挂到本地。读取 CPU 编号到完成链表操作期间保持中断关闭，避免进程迁移后操作错误链表；steal 任意时刻只持有一把 `kmem` 锁，避免 CPU 间反向窃取形成死锁。

### 4.2 分桶缓存与块唯一性

缓存按 `(dev, blockno)` 哈希到 13 个桶。命中路径只持目标桶锁，增加 `refcnt` 后释放自旋锁，再获取 buffer sleeplock。miss 路径先获取全局 `evictlock`，再锁住目标桶并二次查找；从二次查找到插入结束始终保持目标桶锁，因此并发 miss 不会创建同一块的多个副本。

### 4.3 低争用 victim 选择

初版 miss 同时获取全部 13 个桶锁，功能正确但 `bcachetest` 的失败交换总数为 948，超过 500 阈值。优化后，在 `evictlock` 串行 miss 的基础上逐桶短暂扫描最旧的 `refcnt==0` 候选；获取候选所属桶锁后重新验证桶号、身份、引用计数和时间戳，候选失效则重扫。任意时刻最多持目标桶和 victim 桶两把锁，显著降低无关桶阻塞，同时保留原子迁移和唯一性。

`brelse()`、`bpin()`、`bunpin()` 均在 buffer 所属桶锁下更新引用计数；只有 `refcnt==0` 的 buffer 才能迁移，所以持有引用的调用者使用 `b->bucket` 是安全的。所有 sleeplock 获取均发生在自旋锁释放之后。

## 5. 测试过程、结果与分析

### 5.1 测试命令

```bash
make grade
```

官方套件覆盖 `kalloctest`、`usertests sbrkmuch`、`bcachetest`、完整 `usertests` 和 `time.txt`。

### 5.2 测试结果

| 测试项 | 实际结果 | 结论 |
| --- | --- | --- |
| `kalloctest` test1 | 锁争用满足阈值 | 通过 |
| `kalloctest` test2 | 可用页数量稳定，无丢页 | 通过 |
| `usertests sbrkmuch` | 大规模分配、释放、重分配正常 | 通过 |
| `bcachetest` test0 | 优化前 `tot=948` 失败；优化后满足 `<500` 阈值 | 通过 |
| `bcachetest` test1 | 超过缓存容量的文件访问正常 | 通过 |
| 完整 `usertests` | `ALL TESTS PASSED` | 通过 |
| 官方评分 | `Score: 70/70` | 通过 |

关键输出：

```text
kalloctest: test1: OK
kalloctest: test2: OK
kalloctest: sbrkmuch: OK
bcachetest: test0: OK
bcachetest: test1: OK
usertests: OK
time: OK
Score: 70/70
```

### 5.3 结果分析

`kalloctest` 同时证明本地页链表降低了锁争用，批量 steal 没有复制或遗失页。`bcachetest` test0 对锁统计的硬阈值证明命中和淘汰路径的粒度达到要求；test1 验证持续淘汰与迁桶。完整 `usertests` 进一步覆盖文件系统、日志和内存分配回归，说明新的引用计数与 pin 语义没有破坏既有功能。

## 6. 遇到的问题与解决方法

| 问题 | 原因分析 | 解决方法 |
| --- | --- | --- |
| 初版 bcache 功能通过但争用为 948 | 每次 miss 同时持有全部 13 个桶锁 | 改为逐桶扫描、候选加锁重验证，最多同时持两把桶锁 |
| 并发 miss 可能生成重复缓存块 | 查找与插入若不原子，两个 CPU 可同时认为目标不存在 | 用 `evictlock` 串行 miss，并从二次查找到插入持续持目标桶锁 |
| `cpuid()` 后可能迁移 CPU | 开中断状态下进程可被调度到另一 CPU | 用 `push_off()/pop_off()` 覆盖 CPU 本地链表操作 |
| GCC 13 将 xv6 shell 递归警告视为错误 | 新编译器静态分析触发上游代码诊断 | 仅对 GCC 13 的该诊断取消 `-Werror` |

## 7. 原理理解与总结

锁的正确性只是并发设计的底线，实验的重点还在于缩小共享范围。每 CPU freelist 把高频分配/释放变成本地操作，只有资源不平衡时才跨 CPU 协调；哈希桶把缓存命中限制在相关块的锁域。优化锁粒度后，必须用清晰的不变量弥补全局锁被拆除后的复杂度：页只能属于一个链表，缓存块只能有一个副本，引用不为零的块不能淘汰，锁顺序不能形成环。性能测试中的失败交换次数把这些抽象设计转化成了可测量结果。

## 8. 代码与复现说明

- 仓库：https://github.com/xswt442-cmd/xv6-labs-2021-do
- 分支：`lock`
- 代码提交：`8a9f3d985896e4f21597b4b0a05cd4d04229423c`
- 报告提交：包含本文件的 `lab(lock): 提交实验报告` 提交
- 复现步骤：切换到 `lock` 分支后执行 `make grade`。
