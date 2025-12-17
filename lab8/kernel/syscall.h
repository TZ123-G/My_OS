// Minimal syscall numbers for this教学项目
#ifndef _SYSCALL_H_
#define _SYSCALL_H_

#define SYS_write 1
#define SYS_getpid 2
#define SYS_exit 3
#define SYS_fork 4
#define SYS_wait 5
#define SYS_klog 6

#endif

// Provide syscall() prototype for in-kernel tests that call it directly
void syscall(void);
