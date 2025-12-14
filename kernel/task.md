什么是系统调用？
p用户程序请求操作系统内核服务的接口
p是用户态和内核态之间的桥梁
p提供了安全、可控的内核功能访问机制
Ø 实验目的：
p机制分析：深入理解 xv6 的系统调用分发机制。
p交互理解：掌握用户态与内核态的数据传输与特权级切换。
p框架实现：设计并实现一套完整的系统调用（Process, File, Memory）。
操作系统概念 第2章：操作系统结构
Ø RISC-V特权级规范 第12.1节：Supervisor Trap Handling
Ø xv6手册 第2.5节和第4章：系统调用和陷阱
文件 
路径 
说明
系统调用入口 
kernel/syscall.c 
系统调用分发表
陷阱处理 
kernel/trap.c 
中断和异常处理
用户态封装 
user/usys.S 
系统调用汇编存根
系统调用定义 
kernel/syscall.h 
系统调用号定义
操作系统概念 第2章：操作系统结构
Ø RISC-V特权级规范 第12.1节：Supervisor Trap Handling
Ø xv6手册 第2.5节和第4章：系统调用和陷阱
文件 
路径 
说明
系统调用入口 
kernel/syscall.c 
系统调用分发表
陷阱处理 
kernel/trap.c 
中断和异常处理
用户态封装 
user/usys.S 
系统调用汇编存根
系统调用定义 
kernel/syscall.h 
系统调用号定义
调用约定：
p ecall指令
ü 功能：从用户态陷入到监督模式（S-mode）
ü 硬件行为：
ü pc → sepc（保存返回地址）
ü 异常原因 → scause（值为8：来自U-mode
的ecall）
ü stvec → pc（跳转到陷阱处理程序）
ü 特权级：U-mode → S-mode
p寄存器使用
p参数传递
寄存器 
用途 
说明
a7 
系统调用号 
标识要调用的系统
调用
a0-a5 
参数1-6 传递最多6个参数
a0 
返回值 
系统调用的返回结果
调用约定：
p特权级切换：用户模式到监督模式的转换过程
用户态 (U-mode)
    ↓ ecall指令
【硬件自动操作】
    • sepc ← pc
    • scause ← 8
    • sstatus.SPP ← U-mode
    • sstatus.SPIE ← sstatus.SIE
    • sstatus.SIE ← 0（关中断）
    • pc ← stvec
    ↓
监督态 (S-mode)
    • 执行 trampoline.S (uservec)
    • 保存用户寄存器到 trapframe
    • 切换到内核页表
    • 调用 usertrap() → syscall()
    • 执行具体系统调用
    • 准备返回（userret）
    ↓ sret指令
【硬件自动操作】
    • pc ← sepc
    • 特权级 ← sstatus.SPP
    • sstatus.SIE ← sstatus.SPIE
    ↓
用户态 (U-mode)
Ø 每个环节的作用是什么？
Ø 1. 用户程序调用：应用程序调用系统调用封装函数（如 write()），传入参数。
Ø 2. usys.S 桩代码：将系统调用号加载到 a7 寄存器，执行 ecall 指令触发陷阱。
Ø 3. 执行 ecall 指令：CPU 保存当前 PC 到 sepc，设置异常原因到 scause，切换到内核态并
跳转到 stvec 指向的 trampoline。
Ø 4. uservec (trampoline.S)：保存所有用户寄存器到 trapframe，切换到内核页表和内核栈，
跳转到 usertrap()。
Ø 5. usertrap (trap.c)：检查 scause 确认是系统调用，调用 syscall() 进行分发处理。
Ø 6. syscall (syscall.c)：从 trapframe->a7 获取系统调用号，通过函数指针数组调用对应的
系统调用实现函数。
Ø 7. 系统调用实现：执行具体的系统调用功能（如 sys_write() 执行写操作），返回结果。
Ø 8. usertrapret (trap.c)：准备返回用户态，设置相关寄存器状态，调用 userret()
每个环节的作用是什么？
Ø 9. userret (trampoline.S)：切换回用户页表，从 trapframe 恢复所有用户寄存器，执行 
sret 返回用户态。
Ø 10. 返回用户态：sret 指令恢复 PC 和特权级，用户程序从系统调用处继续执行。
Ø 参数是如何传递的？
Ø 用户程序将参数放入寄存器 a0-a5（最多6个参数），uservec 将这些寄存器保存到 
trapframe，内核通过 argint()、argaddr() 等函数从 trapframe 中读取参数。对于指针参
数，内核使用 copyin()/copyout() 安全地访问用户内存空间。
Ø 返回值如何返回？
Ø 系统调用实现函数返回结果后，syscall() 将返回值写入 trapframe->a0，userret 从 
trapframe 恢复 a0 寄存器到用户态，用户程序通过 a0 获得返回值。成功时返回 ≥ 0，失败
时返回 -1。
研究RISC-V的ecall机制：
p ecall指令的作用：RISC-V 的环境调用指令，用于触发从用户态到内核态的切换。执行
时硬件自动完成：保存当前 PC 到 sepc，设置异常原因码到 scause（系统调用为8），
关闭中断，切换特权级到 S-mode，并跳转到 stvec 指向的陷阱处理入口。
p scause寄存器中系统调用的编码：scause 寄存器记录陷阱发生的原因。当用户态执行 
ecall 触发系统调用时，scause 被硬件设置为 8（Environment call from U-mode）。
内核通过读取 scause 的值来区分是系统调用、中断还是其他异常，从而进行相应处理。
p sepc寄存器的作用和更新：sepc (Supervisor Exception Program Counter) 保存触发
陷阱时的指令地址。执行 ecall 时，硬件将当前 PC 值保存到 sepc。系统调用处理完毕
后，内核将 sepc + 4（跳过 ecall 指令），然后执行 sret 指令，硬件自动将 sepc 的值
恢复到 PC，使程序从系统调用的下一条指令继续执行。
08 任务1：理解系统调用的实现原理
Ø 理解特权级切换：
p用户栈到内核栈的转换：系统调用触发时，CPU 从用户栈切换到内核栈。sscratch 寄存器保
存 trapframe 地址，uservec 通过它获取内核栈指针 sp，完成栈的切换，确保内核操作不污
染用户栈空间。
p寄存器状态的保存和恢复
• 保存阶段：uservec 将所有用户寄存器（32个通用寄存器 + PC）保存到 trapframe 结构
中。
• 恢复阶段：userret 在返回前从 trapframe 恢复所有寄存器，确保用户程序状态完整恢复，
就像系统调用从未发生过一样。
p页表的切换时机
• 进入内核：uservec 中从用户页表切换到内核页表（写 satp 寄存器），使内核能访问内
核地址空间。
• 返回用户：userret 中切换回用户页表，执行 sret 后程序运行在用户地址空间。
为什么需要陷阱帧(trapframe)？
p保存用户态上下文 - 用户的32个寄存器、PC等状态必须完整保存，否则系统调用返
回后程序无法继续执行
p实现状态隔离 - 内核和用户使用不同的栈和寄存器，trapframe 是两者之间的"中转
站"，避免相互污染
p支持参数传递和返回 - 系统调用参数（a0-a7）和返回值都通过 trapframe 在用户
态和内核态之间传递
p系统调用和中断处理有什么相同和不同？
p系统调用是程序主动请求服务，中断是硬件强制打断程序执行。
研读 syscall.c 中的核心分发逻辑：
Ø 系统调用号是如何传递的？
Ø 返回值存储在哪里？
Ø 错误处理机制是什么？
void syscall(void) {
    int num;
    struct proc *p = myproc();
    num = p->trapframe->a7; // 系统调用号
    if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
        p->trapframe->a0 = syscalls[num](); // 调用并保存返回值
    } else {
        // 处理无效系统调用
    }
}
分析参数提取函数：
Ø 参数是从哪里提取的？
Ø 如何处理不同类型的参数？
Ø 边界检查是如何实现的？
int argint(int n, int *ip); // 获取整数参数
int argaddr(int n, uint64 *ip); // 获取地址参数
int argstr(int n, char *buf, int max); // 获取字符串参数
08 任务2：分析xv6的系统调用分发机制
Ø 理解用户内存访问：
p copyout() 和 copyin() 的作用
• copyout() - 将数据从内核空间复制到用户空间，copyin() - 将数据从用户空间复制
到内核空间，这两个函数在复制过程中会进行安全检查：验证用户地址是否合法、
是否越界、页表映射是否存在，防止内核访问非法内存。
p为什么不能直接访问用户内存？
• 地址空间隔离 - 内核和用户使用不同页表，用户虚拟地址在内核页表中无效或映射
到错误位置，安全风险 - 用户可能传递恶意指针（如内核地址、空指针），直接访
问会导致内核崩溃或安全漏洞，权限检查 - 需要验证用户是否有权限访问该内存区
域（读/写权限、地址范围）
p如何防止用户传递恶意指针？
• 地址范围检查，页表验证，权限检查，边界检查
设计要求：
Ø 1. 定义系统调用表结构
Ø 2. 设计参数传递机制
Ø 3. 实现错误处理策略
Ø 核心组件设计：
p如何验证用户提供的指针？
p如何处理系统调用失败？
p如何支持可变参数的系统调
用？
p如何实现系统调用的权限检
查？
// 系统调用描述符
struct syscall_desc {
    int (*func)(void); // 实现函数
    char *name; // 系统调用名称
    int arg_count; // 参数个数
    // 可选：参数类型描述
};
// 系统调用表
extern struct syscall_desc syscall_table[];
// 系统调用分发器
void syscall_dispatch(void);
// 参数提取辅助函数
int get_syscall_arg(int n, long *arg);
int get_user_string(const char __user *str, char *buf, int max);
int get_user_buffer(const void __user *ptr, void *buf, int size);
设计要求：
Ø 1. 定义系统调用表结构
Ø 2. 设计参数传递机制
Ø 3. 实现错误处理策略
Ø 核心组件设计：
p如何验证用户提供的指针？
p如何处理系统调用失败？
p如何支持可变参数的系统调
用？
p如何实现系统调用的权限检
查？
// 系统调用描述符
struct syscall_desc {
    int (*func)(void); // 实现函数
    char *name; // 系统调用名称
    int arg_count; // 参数个数
    // 可选：参数类型描述
};
// 系统调用表
extern struct syscall_desc syscall_table[];
// 系统调用分发器
void syscall_dispatch(void);
// 参数提取辅助函数
int get_syscall_arg(int n, long *arg);
int get_user_string(const char __user *str, char *buf, int max);
int get_user_buffer(const void __user *ptr, void *buf, int size);参考xv6的usys.pl，理解：
Ø 1. 桩代码生成机制：
# 每个系统调用的桩代码格式
.global write
write:
    li a7, SYS_write # 系统调用号加载到a7
    ecall # 陷入内核
    ret # 返回
08 任务5：实现用户态系统调用接口
Ø 参考xv6的usys.pl，理解：
Ø 2. 用户库函数设计：
Ø 实现考虑：
p如何处理系统调用的错误返回？
p是否需要errno机制？
p如何提供用户友好的接口？
// 用户库中的系统调用声明
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int*);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);