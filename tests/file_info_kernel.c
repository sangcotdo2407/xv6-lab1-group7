// Included in proc.c only in a temporary test build, after fsinit at boot.
#include "fs.h"
#include "sleeplock.h"
#include "file.h"

static struct {
  uint64 before;
  struct sysinfo info;
  uint64 after;
} fit_buffer;

static int
fit_zero(void *data, int size)
{
  uchar *p = data;
  for(int i = 0; i < size; i++)
    if(p[i])
      return 0;
  return 1;
}

static void
fit_check(int ok, char *message)
{
  if(!ok){
    printf("FILE_INFO_KERNEL FAIL: %s\n", message);
    panic("file info regression");
  }
}

static void
file_info_kernel_test(void)
{
  struct proc *p = myproc();
  struct file *f = filealloc();
  fit_check(f != 0, "allocate file");
  acquire(&p->lock);
  fit_check(p->ofile[0] == 0, "run before init opens console");
  p->ofile[0] = f;
  release(&p->lock);

  fit_buffer.before = 0x123456789abcdef0UL;
  fit_buffer.after = 0xfedcba9876543210UL;
  memset(&fit_buffer.info, 0xa5, sizeof(fit_buffer.info));
  fit_check(collect_file_info(&fit_buffer.info) == 0, "file collector succeeds");
  fit_check(fit_buffer.info.open_file_count == 1 &&
            fit_buffer.info.files[0].file_index == file_index_of(f) &&
            fit_buffer.info.files[0].ref == 1 &&
            fit_buffer.info.files[0].process_count == 1 &&
            fit_buffer.info.files[0].pids[0] == p->pid, "real table index/ref/PID");
  fit_check(fit_buffer.info.process_count == (int)0xa5a5a5a5,
            "file collector preserves process portion");
  fit_check(collect_process_info(&fit_buffer.info) == 0,
            "process collector succeeds with poisoned buffer");
  fit_check(fit_buffer.info.process_count == 1 &&
            fit_buffer.info.processes[0].open_file_count == 1 &&
            fit_buffer.info.processes[0].open_files[0].file_index == file_index_of(f),
            "process descriptor agrees in stable state");
  fit_check(fit_zero(&fit_buffer.info.processes[0].open_files[1],
                    (NOFILE-1)*sizeof(struct sysinfo_fd)) &&
            fit_zero(&fit_buffer.info.processes[1],
                    (NPROC-1)*sizeof(struct sysinfo_process)), "process tails cleared");

  for(int offset = 1; offset < sizeof(struct file); offset++){
    acquire(&p->lock);
    p->ofile[0] = (struct file *)((uint64)f+offset);
    release(&p->lock);
    fit_check(collect_file_info(&fit_buffer.info) == -1,
              "file collector rejects every misalignment");
    fit_check(fit_buffer.info.open_file_count == 0 &&
              fit_zero(fit_buffer.info.files, sizeof(fit_buffer.info.files)),
              "file failure clears rows");
    fit_check(collect_process_info(&fit_buffer.info) == -1,
              "process collector rejects invalid descriptor");
    fit_check(fit_buffer.info.process_count == 0 &&
              fit_zero(fit_buffer.info.processes, sizeof(fit_buffer.info.processes)),
              "process failure clears rows");
    fit_check(!holding(&p->lock) && !holding(&wait_lock), "error releases locks");
  }

  acquire(&p->lock);
  p->ofile[0] = 0;
  release(&p->lock);
  fileclose(f);
  fit_check(collect_file_info(&fit_buffer.info) == 0 &&
            fit_buffer.info.open_file_count == 0, "cleanup and repeat after errors");
  fit_check(fit_buffer.before == 0x123456789abcdef0UL &&
            fit_buffer.after == 0xfedcba9876543210UL, "buffer guards");
  printf("FILE_INFO_KERNEL ALL PASSED\n");
}
