# xv6 Lab 实验报告：fs

## 1. 实验基本信息

- 课程/项目：MIT 6.S081 / xv6 2021
- 实验名称：fs — File system
- 实验分支：`fs`
- 完成日期：2026-08-09
- 官方题目：https://pdos.csail.mit.edu/6.828/2021/labs/fs.html
- 代码仓库：https://github.com/xswt442-cmd/xv6-labs-2021-do
- 对应代码提交：`85ccaeb06b4eeed056821cd4af4135b616634d98`

## 2. 实验目标

- 为 xv6 inode 增加双重间接块，使单文件容量从 268 个块扩展到 65,803 个块。
- 实现符号链接系统调用、`O_NOFOLLOW` 标志及 `open()` 的链接跟随逻辑。
- 保证磁盘 inode 格式、内存 inode、mkfs、块分配和截断释放逻辑保持一致。

## 3. 实验环境

- 宿主系统：WSL2 / Linux
- 编译工具链：`riscv64-unknown-elf-gcc 13.2.0`
- 模拟器：`QEMU 8.2.2`
- xv6 起始版本：上游 `fs` 分支，提交 `46bcbaf1f2b638ede67e3dd1fcccee226a772fa5`

## 4. 实现内容与相关代码

以下链接固定到代码完成提交 `85ccaeb`，可直接查看实验相关代码。

| 功能 | 修改/新增文件 | 实现说明 |
| --- | --- | --- |
| inode 寻址布局 | [`kernel/fs.h`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/85ccaeb06b4eeed056821cd4af4135b616634d98/kernel/fs.h)、[`kernel/file.h`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/85ccaeb06b4eeed056821cd4af4135b616634d98/kernel/file.h) | 改为 11 个直接块、1 个单重间接块和 1 个双重间接根块，磁盘 inode 仍保持 13 个地址槽。 |
| 大文件映射与回收 | [`kernel/fs.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/85ccaeb06b4eeed056821cd4af4135b616634d98/kernel/fs.c) | 扩展 `bmap()` 和 `itrunc()`，分配、记录并释放双重间接树。 |
| 文件系统镜像格式 | [`mkfs/mkfs.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/85ccaeb06b4eeed056821cd4af4135b616634d98/mkfs/mkfs.c) | 使宿主 mkfs 的 `iappend()` 使用相同的双重间接布局和磁盘端序。 |
| symlink ABI | [`kernel/stat.h`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/85ccaeb06b4eeed056821cd4af4135b616634d98/kernel/stat.h)、[`kernel/fcntl.h`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/85ccaeb06b4eeed056821cd4af4135b616634d98/kernel/fcntl.h)、[`kernel/syscall.h`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/85ccaeb06b4eeed056821cd4af4135b616634d98/kernel/syscall.h)、[`kernel/syscall.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/85ccaeb06b4eeed056821cd4af4135b616634d98/kernel/syscall.c)、[`user/user.h`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/85ccaeb06b4eeed056821cd4af4135b616634d98/user/user.h)、[`user/usys.pl`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/85ccaeb06b4eeed056821cd4af4135b616634d98/user/usys.pl) | 增加 inode 类型、打开标志、系统调用号、内核分发表和用户存根。 |
| symlink 创建与跟随 | [`kernel/sysfile.c`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/85ccaeb06b4eeed056821cd4af4135b616634d98/kernel/sysfile.c) | 原子创建链接，`open()` 最多跟随 10 层，并正确处理循环、悬空链接和 `O_NOFOLLOW`。 |
| 构建与评分输入 | [`Makefile`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/85ccaeb06b4eeed056821cd4af4135b616634d98/Makefile)、[`time.txt`](https://github.com/xswt442-cmd/xv6-labs-2021-do/blob/85ccaeb06b4eeed056821cd4af4135b616634d98/time.txt) | 把 `symlinktest` 加入镜像，兼容 GCC 13，并提供评分脚本要求的整数用时。 |

### 4.1 双重间接块

磁盘 inode 的 13 个地址槽没有增加：`addrs[0..10]` 是直接块，`addrs[11]` 是单重间接块，`addrs[12]` 是双重间接根块。由此 `MAXFILE = 11 + 256 + 256×256 = 65803`，同时保持 `struct dinode` 为 64 字节，避免破坏磁盘格式和 `IPB` 计算。

`bmap()` 在双重间接区间先计算外层和内层索引，按需分配根块、二级索引块和数据块。每次修改索引块后调用 `log_write()`，并在进入下一层前释放当前 buffer。`itrunc()` 反向遍历所有二级索引，依次释放数据块、二级索引块和根块，最后清空 inode 地址及大小。立即释放的索引块不额外写日志，从而避免截断大文件时超过日志容量。

### 4.2 符号链接创建

`sys_symlink()` 在一个文件系统事务中锁住父目录，分配 `T_SYMLINK` inode，先写入以 NUL 结尾的目标路径，再设置链接计数并插入目录项。写入或 `dirlink()` 失败时把链接计数恢复为 0，并由 `iunlockput()` 回收 inode 和已分配块，避免暴露空链接或泄漏资源。

并发创建同一路径时，`dirlookup()` 返回的是未加 inode 锁的引用，因此命中路径必须使用 `iput()`。错误地调用 `iunlockput()` 会触发 `panic: iunlock`；修复后并发测试通过。

### 4.3 链接跟随

`open()` 仅对最终路径分量跟随 symlink。每轮先读取目标路径，再释放当前 inode 的锁和引用，之后调用 `namei()` 获取下一 inode，因此自链接和两节点循环不会形成 inode 自锁。解析最多 10 层；超过深度、目标不存在或 payload 非法时失败。指定 `O_NOFOLLOW` 时直接打开 symlink inode，以便 `fstat()` 观察 `T_SYMLINK`。目录仍拒绝写、创建和截断模式，但允许无害的附加标志。

## 5. 测试过程、结果与分析

### 5.1 测试命令

```bash
make grade
./grade-lab-fs symlinktest
./grade-lab-fs usertests
```

### 5.2 测试结果

| 测试项 | 命令/场景 | 实际结果 | 结论 |
| --- | --- | --- | --- |
| 大文件 | `make grade` 中的 `bigfile` | 两次完整复验均通过，最终一轮耗时 170.0 秒 | 通过 |
| symlink 基础 | `symlinktest` | `symlinktest: symlinks: OK` | 通过 |
| symlink 并发 | 两个进程反复创建/删除同一链接 | 修复后定向与完整评分均通过 | 通过 |
| 回归测试 | 完整 `usertests` | 完整评分中 243.0 秒通过；最终语义修正后 244.1 秒再次通过 | 通过 |
| 自动评分 | `make grade` | `Score: 100/100` | 通过 |

关键评分输出如下：

```text
running bigfile: OK (170.0s)
symlinktest: symlinks: OK
symlinktest: concurrent symlinks: OK
usertests: OK (243.0s)
time: OK
Score: 100/100
```

### 5.3 结果分析

`bigfile` 写读全部 65,803 个块，覆盖直接、单重间接和完整双重间接范围，也验证边界计算及日志记录。完整 `usertests` 中的大文件写入、删除和常规文件系统操作进一步覆盖 `itrunc()` 的释放路径。symlink 基础测试覆盖 `O_NOFOLLOW`、悬空链接、多层链接和循环，独立并发测试则证明目录锁、inode 引用和失败清理可以在竞争下正确配合。

实现与完整 Unix 语义仍有差异：实验只要求 `open()` 跟随最终路径分量，相对目标按当前工作目录解析；没有扩展到所有系统调用和中间路径分量。

## 6. 遇到的问题与解决方法

| 问题 | 原因分析 | 解决方法 |
| --- | --- | --- |
| 并发 symlink 出现 `panic: iunlock` | `dirlookup()` 命中返回未锁 inode，却调用了要求已持锁的 `iunlockput()` | 改用 `iput()` 释放未锁引用，父目录仍由 `iunlockput()` 释放 |
| 双重间接边界容易错一位 | 直接、单重和双重区间需要依次扣除容量 | 统一使用 `NDIRECT`、`NINDIRECT`、`NDINDIRECT` 计算外层与内层索引 |
| 截断大文件可能耗尽日志 | 若清零并记录每个即将释放的索引块，会产生大量日志项 | 只记录 bitmap 与 inode 更新，索引块直接释放 |
| 目录附加标志语义回退 | 仅检查读写位会接受 `O_CREATE` 或 `O_TRUNC` | 显式拒绝目录的写、创建和截断标志，同时允许 `O_NOFOLLOW` |
| GCC 13 构建失败 | 新编译器把 xv6 shell 的有意递归诊断提升为错误 | 仅把 `infinite-recursion` 保留为非致命警告，不关闭其他 `-Werror` |

## 7. 原理理解与总结

大文件支持体现了文件系统元数据的分层索引：增加一层间接块能以很小的 inode 固定开销换取平方级容量，但分配、日志和释放必须沿同一棵树严格对称。符号链接则体现了“目录项指向 inode”和“inode 内容描述另一个路径”之间的组合；正确性不仅在于能找到目标，还依赖每轮解析时的锁释放、引用计数和事务边界。并发测试暴露的 `iunlock` panic 说明，xv6 中“持有引用”和“持有 inode 锁”是两个独立状态，清理函数必须与实际状态精确匹配。

## 8. 代码与复现说明

- 仓库：https://github.com/xswt442-cmd/xv6-labs-2021-do
- 分支：`fs`
- 代码提交：`85ccaeb06b4eeed056821cd4af4135b616634d98`
- 报告提交：包含本文件的 `lab(fs): 提交实验报告` 提交
- 复现步骤：切换到 `fs` 分支后执行 `make grade`；可用 `./grade-lab-fs symlinktest` 或 `./grade-lab-fs usertests` 单独复验对应项目。
