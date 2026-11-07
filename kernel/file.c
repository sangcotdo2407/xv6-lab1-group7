#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"
#include "sysinfo.h"

extern struct proc proc[NPROC];

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->type = FD_NONE;
      f->readable = 0;
      f->writable = 0;
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

void
filesetinfo(struct file *f, int type, int readable, int writable)
{
  acquire(&ftable.lock);
  f->type = type;
  f->readable = readable != 0;
  f->writable = writable != 0;
  release(&ftable.lock);
}

struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE){
    pipeclose(ff.pipe, ff.writable);
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;

  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_PIPE){
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}

int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0)
    return -1;

  if(f->type == FD_PIPE){
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){

    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r != n1){

        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  } else {
    panic("filewrite");
  }

  return ret;
}

int
file_index_of(struct file *f)
{
  uint64 addr = (uint64)f;
  uint64 base = (uint64)ftable.file;

  if(addr < base || addr - base >= sizeof(ftable.file) ||
     (addr - base) % sizeof(struct file) != 0)
    return -1;
  return (addr - base) / sizeof(struct file);
}

int
collect_file_info(struct sysinfo *info)
{
  int slot_of[NFILE];
  int i, n;
  struct proc *p;

  if(info == 0)
    return -1;

  info->open_file_count = 0;
  memset(info->files, 0, sizeof(info->files));

  for(i = 0; i < NFILE; i++)
    slot_of[i] = -1;

  n = 0;

  acquire(&ftable.lock);

  for(i = 0; i < NFILE; i++){
    struct file *f = &ftable.file[i];

    if(f->ref <= 0)
      continue;

    struct sysinfo_file *sf = &info->files[n];

    sf->file_index = i;
    sf->type = f->type;
    sf->readable = f->readable;
    sf->writable = f->writable;
    sf->ref = f->ref;
    sf->process_count = 0;

    slot_of[i] = n;
    n++;
  }

  release(&ftable.lock);

  info->open_file_count = n;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);

    if(p->state == UNUSED){
      release(&p->lock);
      continue;
    }

    int pid = p->pid;

    for(i = 0; i < NOFILE; i++){
      struct file *f = p->ofile[i];

      if(f == 0)
        continue;

      int index = file_index_of(f);
      if(index < 0 || index >= NFILE){
        release(&p->lock);
        info->open_file_count = 0;
        memset(info->files, 0, sizeof(info->files));
        return -1;
      }

      int slot = slot_of[index];

      if(slot < 0)
        continue;

      struct sysinfo_file *sf = &info->files[slot];

      if(sf->process_count > 0 &&
          sf->pids[sf->process_count - 1] == pid)
        continue;

      if(sf->process_count < NPROC)
        sf->pids[sf->process_count++] = pid;
    }

    release(&p->lock);
  }

  return 0;
}
