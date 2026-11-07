#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

void
ramdiskinit(void)
{
}

void
ramdiskrw(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("ramdiskrw: buf not locked");
  if((b->flags & (B_VALID|B_DIRTY)) == B_VALID)
    panic("ramdiskrw: nothing to do");

  if(b->blockno >= FSSIZE)
    panic("ramdiskrw: blockno too big");

  uint64 diskaddr = b->blockno * BSIZE;
  char *addr = (char *)RAMDISK + diskaddr;

  if(b->flags & B_DIRTY){

    memmove(addr, b->data, BSIZE);
    b->flags &= ~B_DIRTY;
  } else {

    memmove(b->data, addr, BSIZE);
    b->flags |= B_VALID;
  }
}
