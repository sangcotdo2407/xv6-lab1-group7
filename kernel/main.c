#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

void
main()
{
  if(cpuid() == 0){
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();
    kvminit();
    kvminithart();
    procinit();
    trapinit();
    trapinithart();
    plicinit();
    plicinithart();
    binit();
    iinit();
    fileinit();
    virtio_disk_init();
    userinit();
    __sync_synchronize();
    started = 1;
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();
    trapinithart();
    plicinithart();
  }

  scheduler();
}
