# 实验七：文件系统

文件列表如下：
```text
.
├── kernel
│   ├── defs.h
│   ├── types.h
│   ├── param.h
│   ├── entry.S
│   ├── printf.h
│   ├── printf.c
│   ├── console.c
│   ├── start.c
│   ├── main.c
│   ├── memlayout.h
│   ├── kalloc.c
│   ├── vm.h
│   ├── vm.c
│   ├── string.c
│   ├── assert.h
│   ├── riscv.h
│   ├── spinlock.c
│   ├── spinlock.h
│   ├── kernelvec.S
│   ├── trampoline.S
│   ├── swtch.S
│   ├── trap.c
│   ├── proc.h
│   ├── proc.c
│   ├── process_api.c
│   ├── syscall.h
│   ├── syscall.c
│   ├── sync_test.c
│   ├── uart.c
│   ├── bio.c
│   ├── fs.h
│   ├── fs.c
│   └── file.c
├── kernel.ld
├── Makefile
└── results/
```

## 1、新增各模块简介
本实验在内存中模拟磁盘与缓冲缓存（buffer cache），完成最小可用的文件系统：
- **块缓存与模拟磁盘**：[kernel/bio.c](kernel/bio.c)
  - 提供 `binit()`、`bread()`、`bwrite()`、`brelse()` 以及命中/读写统计接口。
  - 用一个大小为 `NBLOCKS * BSIZE` 的数组模拟磁盘，缓冲区上维持 16 个 `buf` 条目。
- **磁盘/内存 inode 与超级块**：[kernel/fs.h](kernel/fs.h)、[kernel/fs.c](kernel/fs.c)
  - 定义 `struct superblock`、磁盘 inode `struct dinode`、内存 inode `struct inode` 与目录项 `struct dirent`。
  - 通过 `iinit()` 初始化超级块与内存 inode 表，`ialloc()` 分配 inode，`readi()/writei()` 完成文件内容读写。
  - `bmap()` 实现逻辑块到物理块的映射（直接块与一级间接块），必要时进行块分配。
- **文件层（file table）**：[kernel/file.c](kernel/file.c)
  - 定义 `struct file` 与文件表，提供 `fileinit()`、`filealloc()`、`fileclose()`、`fileread()`、`filewrite()` 等。
  - 封装对 `inode` 的读写并维护文件偏移 `off`。
- **系统调用与测试**：[kernel/syscall.c](kernel/syscall.c)、[kernel/start.c](kernel/start.c)
  - 维持最小调用集（`write/getpid/exit/fork/wait`），文件系统测试通过内核接口直接进行。

- **写前日志（WAL）/事务模块**：[kernel/log.c](kernel/log.c)
  - 本次已在 `kernel` 下添加一个简化的 WAL 模块 `log.c`，并把它集成进构建流程（已在 `Makefile` 中添加 `kernel/log.c`）。
  - 主要接口：`log_init()`、`begin_op()`、`end_op()`、`log_write(struct buf *b)`。
  - 集成要点：
    - 在 `fs_init()` 中调用 `log_init()`；超级块 `sb.nlog` 已设置为 30，日志区由 `sb.logstart` 开始。
    - 在 `fs.c` 中，间接表更新使用 `log_write()` 登记，`writei()` 的数据写入已用 `begin_op()`/`end_op()` 包裹并通过 `log_write()` 登记数据块，从而在 `end_op()` 时按 WAL 顺序提交。
    - 提交顺序严格保证：先写日志块 -> 写日志头并刷盘 -> 将日志内容写回目标块 -> 清理日志头，以保证崩溃后可重放已提交事务恢复一致性。
  - 实现说明：当前为简化版本，针对内存模拟磁盘场景做了保守的日志配额与去重登记，适合作为教学与实验用途；后续可扩展为循环日志、事务合并或更精细的配额策略。

## 2、核心数据结构（fs.h）
- **常量**：
  - **`BSIZE`**: 块大小，4096；**`NBLOCKS`**: 模拟磁盘总块数，1024。
  - **`NDIRECT`**: 12 个直接块；**`NINDIRECT`**: 一级间接块可索引的块数；**`MAXFILE`**: 最大文件块数。
- **超级块 `superblock`**：记录总体布局（魔数、总块数、inode/日志/位图起始等）。
- **磁盘 inode `dinode`**：类型、主次设备号、链接计数、大小与数据块地址数组（含一个间接块入口）。
- **内存 inode `inode`**：缓存在内存中的 inode，包含 `lock/valid/ref` 等元数据与同样的地址数组。
- **缓冲块 `buf`**：
  - 字段：`valid/disk/dev/blockno/refcnt/lock/data[BSIZE]`。
  - `valid` 表示缓存中有内容；`disk` 表示脏；`refcnt` 表示被引用次数。

示意定义：
```c
#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
struct inode { uint dev, inum; int ref; struct spinlock lock; int valid; short type; short major, minor; short nlink; uint size; uint addrs[NDIRECT+1]; };
struct buf { int valid, disk; uint dev, blockno; int refcnt; struct spinlock lock; uchar data[BSIZE]; };
```

## 3、块缓存与磁盘模拟（bio.c）
- **初始化**：`binit()` 清空 16 个 `buf` 条目，并初始化自旋锁。
- **读块**：`bread(dev, blockno)`
  - 命中：返回已有 `buf`、增加 `refcnt` 与命中计数。
  - 未命中：从模拟磁盘拷贝到空闲 `buf`，增加未命中与读盘计数。
- **写块**：`bwrite(b)` 将 `b->data` 写回模拟磁盘并记一次写盘。
- **释放**：`brelse(b)` 递减引用计数；缓冲保持有效以增加后续命中概率。
- **统计接口**：`buffer_cache_hits()`、`buffer_cache_misses()`、`disk_read_count()`、`disk_write_count()`。

## 4、inode 操作与块映射（fs.c）
- **初始化 `fs_init()/iinit()`**：设置超级块（`size/ninodes/...`）并初始化内存 inode 表；必要时创建根 inode（`ROOTINO=1`）。
- **分配与引用**：
  - `ialloc(dev, type)`：线性扫描找到未使用的 inode，设定 `type/nlink/size` 与清空 `addrs[]`。
  - `iget(dev, inum)` 与 `iput(ip)`：增加/减少引用计数。
- **加锁/解锁**：`ilock(ip)` 与 `iunlock(ip)` 用于保护对 inode 的并发访问。
- **逻辑块映射 `bmap(ip, bn)`**：
  - 直接块：`bn < NDIRECT`，若地址为空则分配一个“全零块”。
  - 一级间接块：当 `bn >= NDIRECT`，先为 `ip->addrs[NDIRECT]` 分配间接表块，再在表中为目标项分配全零块并写回。
  - 分配策略：通过扫描从 `bmapstart+1` 到 `sb.size` 的块，检测“全零块”作为空闲块；超限或耗尽时触发 `panic()`。
- **读写数据**：
  - `readi(ip, dst, off, n)`：按块读取，洞洞（未分配块）部分以 0 填充。
  - `writei(ip, src, off, n)`：必要时通过 `bmap()` 分配块，写入并更新 `ip->size`。
- **调试统计**：`read_superblock()`、`count_free_inodes()`、`count_free_blocks()`、`fs_inode_count()`、`fs_inode_at(idx)`。

## 5、文件层（file.c）
- **结构与表**：`struct file { ref/readable/writable/off/ip; }` 与固定大小的 `filetable[50]`。
- **接口**：
  - `fileinit()`：初始化文件表。
  - `filealloc()`：分配一个空闲文件描述结构（非用户态 fd，这里是内核内部用途）。
  - `fileclose(f)`：减少引用，必要时 `iput(f->ip)`。
  - `fileread(f, addr, n)` / `filewrite(f, addr, n)`：委托 `readi()/writei()` 并维护偏移。

## 6、测试结果
在 QEMU 运行下观察文件系统的读写与一致性：
```bash
make clean
make run
```
- **文件系统一致性测试**
  ```c
  void test_filesystem_integrity(void) {
      consoleinit();
      printf("Testing filesystem integrity...\n");
      fs_init();
      fileinit();
      struct inode *ip = ialloc(0, 1); // 分配一个文件/目录 inode
      char wbuf[] = "Hello, filesystem!";
      int wlen = strlen(wbuf);
      int written = writei(ip, wbuf, 0, wlen);
      char rbuf[64];
      int read = readi(ip, rbuf, 0, sizeof(rbuf)-1);
      rbuf[read] = '\0';
      assert(written == wlen && strcmp(wbuf, rbuf) == 0);
      ip->valid = 0; iput(ip);
      printf("Filesystem integrity test passed\n");
  }
  ```
  结果示意：
  ![lab7测试1.png](results/lab7测试1.png)

- **并发测试**
  ```c
  void concurrent_worker(void)
    {
        // each worker will allocate/write/read/free inodes repeatedly
        struct proc *p = myproc();
        int pid = p ? p->pid : 0;
        printf("worker %d: started\n", pid);
        for (int j = 0; j < 100; j++)
        {
            struct inode *ip = ialloc(0, 1);
            if (!ip)
            {
                // allocation failed, back off and continue
                for (volatile int w = 0; w < 1000; w++)
                    ;
                continue;
            }
            // write an int
            int val = j;
            int wrote = writei(ip, (char *)&val, 0, sizeof(val));
            if (wrote != sizeof(val))
            {
                printf("worker %d: write error at iter %d wrote=%d\n", pid, j, wrote);
            }
            // read it back
            int r = 0;
            int rd = readi(ip, (char *)&r, 0, sizeof(r));
            if (rd == sizeof(r) && r != val)
            {
                printf("worker %d: mismatch iter %d %d!=%d\n", pid, j, r, val);
            }
            // simulate unlink
            ip->valid = 0;
            iput(ip);
        }
        printf("worker %d: done\n", pid);
        exit_process(0);
    }

    void test_concurrent_access(void)
    {
        consoleinit();
        printf("Testing concurrent file access...\n");

        pmem_init();
        procinit();
        // trap_init();
        // enable_interrupts();

        const int nworkers = 4;
        for (int i = 0; i < nworkers; i++)
        {
            int pid = create_process(concurrent_worker);
            if (pid <= 0)
                printf("test_concurrent_access: create_process failed for worker %d\n", i);
        }

        // enter scheduler to run workers; this does not return
        scheduler();

        // unreachable
    }
    ```
    测试结果如下：
    ![lab7测试2.png](results/lab7测试2.png)
- **文件系统状态检查**
    ```c
    void debug_filesystem_state(void)
    {
        printf("=== Filesystem Debug Info ===\n");
        // 初始化必要子系统以确保 API 可用
        fs_init();
        fileinit();

        // 超级块信息
        struct superblock sb;
        read_superblock(&sb);
        printf("Total blocks: %d\n", (int)sb.size);

        // 统计空闲块与空闲 inode
        printf("Free blocks: %d\n", count_free_blocks());
        printf("Free inodes: %d\n", count_free_inodes());

        // 缓存命中统计（读取器会在使用 bread 时变化）
        printf("Buffer cache hits: %d\n", buffer_cache_hits());
        printf("Buffer cache misses: %d\n", buffer_cache_misses());
    }
    ```
    测试结果如下：
    ![lab7测试3.png](results/lab7测试3.png)
- **磁盘I/O统计**：
  ```c
  void debug_disk_io(void)
    {
        printf("=== Disk I/O Statistics ===\n");
        // 可选：确保子系统初始化
        fs_init();
        fileinit();
        printf("Disk reads: %d\n", disk_read_count());
        printf("Disk writes: %d\n", disk_write_count());
    }
  ```
  测试结果如下：
  ![lab7测试4.png](results/lab7测试4.png)
- **inode 使用情况检查**
  ```c
  void fs_inode_usage(void)
    {
        printf("=== Inode Usage ===\n");
        // 确保文件系统初始化
        fs_init();
        fileinit();
        int n = fs_inode_count();
        for (int i = 0; i < n; i++)
        {
            struct inode *ip = fs_inode_at(i);
            if (!ip)
                continue;
            if (ip->ref > 0)
            {
                printf("Inode %d: ref=%d, type=%d, size=%d\n",
                    (int)ip->inum, (int)ip->ref, (int)ip->type, (int)ip->size);
            }
        }
    }
    ```
    测试结果如下：
    ![lab7测试5.png](results/lab7测试5.png)

- **大小文件分配**
  ```c
  void test_filesystem_performance(void)
  {
      consoleinit();
      printf("Testing filesystem performance...\n");

      // 初始化文件系统与文件层
      fs_init();
      fileinit();

      // 大量小“文件”（inode）测试：每次分配一个 inode，写入 4 字节，再释放
      uint64 start_time = get_time();
      const int small_n = 1000;
      const char small_data[4] = {'t', 'e', 's', 't'};
      for (int i = 0; i < small_n; i++)
      {
          struct inode *ip = ialloc(0, 1); // type=1：文件
          if (!ip)
          {
              // 分配失败则跳过，避免测试中断
              continue;
          }
          // 写入 4 字节
          int wrote = writei(ip, (char *)small_data, 0, sizeof(small_data));
          (void)wrote; // 测试场景不强制校验返回值
          // 释放并模拟“unlink”
          ip->valid = 0;
          iput(ip);
      }
      uint64 small_files_time = get_time() - start_time;

      // 大文件测试：同一个 inode 连续写入 4KB * 1024 = 4MB
      start_time = get_time();
      struct inode *large = ialloc(0, 1);
      if (large)
      {
          char *large_buffer = (char *)alloc_page();
          if (large_buffer)
          {
              // 缓冲区内容无需特定值，保持未初始化即可
              for (int i = 0; i < 1024; i++)
              {
                  // 每次写 4KB，偏移递增
                  writei(large, large_buffer, i * BSIZE, BSIZE);
              }
              free_page(large_buffer);
          }
          // 释放并模拟“unlink”
          large->valid = 0;
          iput(large);
      }
      uint64 large_file_time = get_time() - start_time;

      printf("Small files (1000x4B): %d cycles\n", (int)small_files_time);
      printf("Large file (1x4MB): %d cycles\n", (int)large_file_time);
  }
  ```
  测试结果如下：  
  ![lab7测试6.png](results/lab7测试6.png)
