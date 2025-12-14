/* generated minimal user syscall stubs */
.global write
write:
    li a7, SYS_write
    ecall
    ret

.global getpid
getpid:
    li a7, SYS_getpid
    ecall
    ret

.global exit
exit:
    li a7, SYS_exit
    ecall
    ret

.global fork
fork:
    li a7, SYS_fork
    ecall
    ret

.global wait
wait:
    li a7, SYS_wait
    ecall
    ret
