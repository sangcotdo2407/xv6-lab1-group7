#include "syscall.h"

.globl start
start:
        la a0, init
        la a1, argv
        li a7, SYS_exec
        ecall

exit:
        li a7, SYS_exit
        ecall
        jal exit

init:
  .string "/init\0"

.p2align 2
argv:
  .long init
  .long 0
