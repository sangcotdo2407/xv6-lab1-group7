//
// Support functions for system calls that involve file descriptors.
//

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

extern struct proc proc[NPROC];   // process table, defined in proc.c

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

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
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

// Close file f.  (Decrement ref count, close when reaches 0.)
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

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
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

// Read from file f.
// addr is a user virtual address.
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

// Write to file f.
// addr is a user virtual address.
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
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r != n1){
        // error from writei
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

// Index of f in ftable.file[], or -1 if f does not point into the table.
int
file_index_of(struct file *f)
{
  if(f < ftable.file || f >= ftable.file + NFILE)
    return -1;
  return (int)(f - ftable.file);
}

// Fill info->open_file_count and info->files[] with a snapshot of every
// open file object (ftable.file[i].ref > 0), plus the distinct PIDs that
// reference each object through some ofile[] slot.
//
// info is provided (and not otherwise touched) by sys_sysinfo(); it must
// not live on the kernel stack.  Only the file part of *info is written.
//
// Locking: the ftable lock and a proc lock are never held at the same time.
//   phase 1: hold ftable.lock, copy the file objects, release it.
//   phase 2: for each non-UNUSED proc, hold p->lock only, scan p->ofile[].
// The snapshot is therefore not atomic: a file opened/closed between the two
// phases may be missed or may show a pid list that disagrees with ref.
// Returns 0 on success, -1 on failure.
int
collect_file_info(struct sysinfo *info)
{
  int slot_of[NFILE];   // ftable index -> position in info->files[], or -1
  int i, n;
  struct proc *p;

  if(info == 0)
    return -1;

  info->open_file_count = 0;
  memset(info->files, 0, sizeof(info->files));
  for(i = 0; i < NFILE; i++)
    slot_of[i] = -1;

  // phase 1: snapshot the file objects.
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

  // phase 2: collect distinct referencing PIDs.
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->state == UNUSED){
      release(&p->lock);
      continue;
    }
    int pid = p->pid;
    for(i = 0; i < NOFILE; i++){
      struct file *f = p->ofile[i];
      if(f < ftable.file || f >= ftable.file + NFILE)
        continue;                       // null or not a ftable entry
      int slot = slot_of[f - ftable.file];
      if(slot < 0)
        continue;                       // opened after the snapshot
      struct sysinfo_file *sf = &info->files[slot];
      // all fds of one process are visited consecutively, so comparing
      // with the last stored pid is enough to keep each pid once.
      if(sf->process_count > 0 && sf->pids[sf->process_count - 1] == pid)
        continue;
      if(sf->process_count < NPROC)
        sf->pids[sf->process_count++] = pid;
    }
    release(&p->lock);
  }
  return 0;
}
