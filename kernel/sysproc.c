#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "sysinfo.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// Shared snapshot: too large for the one-page kernel stack. Collectors
// release their spinlocks before copyout; this sleeplock serializes callers.
static struct sysinfo kinfo;
static struct sleeplock kinfo_lock = { .lk = { .name = "sysinfo" }, .name = "sysinfo" };

uint64
sys_sysinfo(void)
{
  uint64 uaddr;      // địa chỉ ảo của struct sysinfo trong user
  int ret = 0;

  argaddr(0, &uaddr);

  acquiresleep(&kinfo_lock);
  memset(&kinfo, 0, sizeof(kinfo));
  if(collect_file_info(&kinfo) < 0 ||
     collect_process_info(&kinfo) < 0 ||
     copyout(myproc()->pagetable, uaddr, (char *)&kinfo, sizeof(kinfo)) < 0)
    ret = -1;       // collector lỗi thì bỏ qua copyout nhờ cơ chế ngắt `||`
  releasesleep(&kinfo_lock);   // chỉ một điểm nhả khóa, mọi đường đều đi qua
  return ret;
}
