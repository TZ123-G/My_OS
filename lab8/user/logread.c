// user/logread.c: simple log reader using sys_klog
#include <stddef.h>
extern int klog(char *buf, int n); // usys.S stub name is klog
extern int write(int fd, const void *buf, int n);

int main(void)
{
    char buf[256];
    for (;;)
    {
        int n = klog(buf, sizeof(buf));
        if (n < 0)
        {
            write(2, "klog read error\n", 16);
            break;
        }
        else if (n == 0)
        {
            // no data; spin a bit
            for (volatile int i = 0; i < 100000; i++)
            {
            }
            continue;
        }
        write(1, buf, n);
    }
    return 0;
}
