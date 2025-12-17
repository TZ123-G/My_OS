#include "types.h"
#include "param.h"
#include "printf.h"
#include "spinlock.h"
#include "defs.h"
#include <stddef.h>

// 简单的有界缓冲区实现，使用 spinlock + sleep/wakeup 实现同步
#define SBUF_SIZE 1
#define PRODUCE_COUNT 10

static int sbuf[SBUF_SIZE];
static int sbuf_in = 0;
static int sbuf_out = 0;
static int sbuf_count = 0;
static struct spinlock sbuf_lock;
static int sbuf_not_full_chan;
static int sbuf_not_empty_chan;

void shared_buffer_init(void)
{
    initlock(&sbuf_lock, "sbuf_lock");
    sbuf_in = sbuf_out = sbuf_count = 0;
}

static void sbuf_put(int val)
{
    acquire(&sbuf_lock);
    while (sbuf_count == SBUF_SIZE)
    {
        sleep(&sbuf_not_full_chan, &sbuf_lock);
    }
    sbuf[sbuf_in] = val;
    sbuf_in = (sbuf_in + 1) % SBUF_SIZE;
    sbuf_count++;
    wakeup(&sbuf_not_empty_chan);
    release(&sbuf_lock);
}

static int sbuf_get(void)
{
    int val;
    acquire(&sbuf_lock);
    while (sbuf_count == 0)
    {
        sleep(&sbuf_not_empty_chan, &sbuf_lock);
    }
    val = sbuf[sbuf_out];
    sbuf_out = (sbuf_out + 1) % SBUF_SIZE;
    sbuf_count--;
    wakeup(&sbuf_not_full_chan);
    release(&sbuf_lock);
    return val;
}

void producer_task(void)
{
    for (int i = 1; i <= PRODUCE_COUNT; i++)
    {
        sbuf_put(i);
        printf("producer: produced %d\n", i);
        // 让出一些时间片以便消费者运行
        for (volatile int d = 0; d < 10000; d++)
            ;
    }
    printf("producer: exiting\n");
    exit_process(0);
}

void consumer_task(void)
{
    for (int i = 1; i <= PRODUCE_COUNT; i++)
    {
        int v = sbuf_get();
        printf("consumer: consumed %d\n", v);
        for (volatile int d = 0; d < 10000; d++)
            ;
    }
    printf("consumer: exiting\n");
    exit_process(0);
}


// 测试函数：初始化缓冲区与内核子系统，创建生产者/消费者与 watcher，然后进入调度器
void test_synchronization(void)
{
    printf("Starting synchronization test\n");

    // 初始化缓冲区与必要子系统（物理内存、进程表、陷阱/定时器）
    shared_buffer_init();
    pmem_init();
    procinit();
    //trap_init();
    //enable_interrupts();

    // 创建消费者和生产者进程
    int cpid = create_process(consumer_task);
    if (cpid <= 0)
        printf("test_synchronization: create_process failed for consumer\n");

    int ppid = create_process(producer_task);
    if (ppid <= 0)
        printf("test_synchronization: create_process failed for producer\n");


    // 进入调度器以运行创建的进程（scheduler 不返回）
    scheduler();
}
