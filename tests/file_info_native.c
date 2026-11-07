// Native sanitizer regression: include the real collector, never a copy.
// The real spinlocks are exercised separately by the QEMU user tests.
#include "kernel/file.c"

struct proc proc[NPROC];
static struct {
  uint64 before;
  struct sysinfo info;
  uint64 after;
} guarded;
static struct spinlock *active;
static int failures;

void
acquire(struct spinlock *lock)
{
  if(active)
    __builtin_abort();
  active = lock;
}

void
release(struct spinlock *lock)
{
  if(active != lock)
    __builtin_abort();
  active = 0;
}

static void
check(int ok, char *message)
{
  if(!ok){
    __builtin_printf("FAIL: %s\n", message);
    failures++;
  }
}

static int
zeroed(void *data, int size)
{
  uchar *p = data;
  for(int i = 0; i < size; i++)
    if(p[i])
      return 0;
  return 1;
}

static void
reset(void)
{
  memset(&ftable, 0, sizeof(ftable));
  memset(proc, 0, sizeof(proc));
  memset(&guarded, 0xa5, sizeof(guarded));
  guarded.before = 0x123456789abcdef0UL;
  guarded.after = 0xfedcba9876543210UL;
}

static void
preserved(void)
{
  check(guarded.before == 0x123456789abcdef0UL &&
        guarded.after == 0xfedcba9876543210UL, "buffer guards");
  uchar *p = (uchar *)&guarded.info.process_count;
  for(int i = 0; i < sizeof(guarded.info)-__builtin_offsetof(struct sysinfo, process_count); i++)
    check(p[i] == 0xa5, "entire process portion preserved");
  check(active == 0, "all locks released");
}

static void
bad_pointer(struct file *f)
{
  reset();
  ftable.file[0].ref = 1;
  proc[0].state = RUNNING;
  proc[0].pid = 7;
  proc[0].ofile[0] = f;
  check(file_index_of(f) == -1, "invalid helper argument rejected");
  check(collect_file_info(&guarded.info) == -1, "invalid descriptor pointer rejected");
  check(guarded.info.open_file_count == 0 &&
        zeroed(guarded.info.files, sizeof(guarded.info.files)), "failure clears all file rows");
  preserved();
}

int
main(void)
{
  reset();
  check(collect_file_info(0) == -1, "NULL info");
  check(file_index_of(0) == -1, "NULL file");
  for(int i = 0; i < NFILE; i++)
    check(file_index_of(&ftable.file[i]) == i, "every valid index");
  check(collect_file_info(&guarded.info) == 0 &&
        guarded.info.open_file_count == 0 &&
        zeroed(guarded.info.files, sizeof(guarded.info.files)), "empty table and zero tail");
  preserved();

  reset();
  ftable.file[0].readable = 1;
  ftable.file[0].writable = 1;
  struct file *allocated = filealloc();
  check(allocated == &ftable.file[0], "reuse first free slot");
  check(collect_file_info(&guarded.info) == 0 &&
        guarded.info.files[0].type == FD_NONE &&
        guarded.info.files[0].readable == 0 &&
        guarded.info.files[0].writable == 0,
        "allocating object cannot expose previous owner's mode");
  filesetinfo(allocated, FD_PIPE, 1, 0);
  check(collect_file_info(&guarded.info) == 0 &&
        guarded.info.files[0].type == FD_PIPE &&
        guarded.info.files[0].readable == 1 &&
        guarded.info.files[0].writable == 0 && active == 0,
        "publish complete type/mode and release table lock");

  reset();
  ftable.file[0].ref = 3;
  ftable.file[0].type = FD_INODE;
  ftable.file[0].readable = 1;
  ftable.file[NFILE-1].ref = 1;
  ftable.file[NFILE-1].type = FD_PIPE;
  ftable.file[NFILE-1].writable = 1;
  proc[0].state = RUNNING; proc[0].pid = 7;
  proc[0].ofile[0] = proc[0].ofile[NOFILE-1] = ftable.file;
  proc[1].state = SLEEPING; proc[1].pid = 8;
  proc[1].ofile[0] = ftable.file;
  proc[1].ofile[1] = &ftable.file[NFILE-1];
  check(collect_file_info(&guarded.info) == 0, "sparse snapshot");
  check(guarded.info.open_file_count == 2 &&
        guarded.info.files[0].file_index == 0 &&
        guarded.info.files[1].file_index == NFILE-1, "packed rows retain true indices");
  check(guarded.info.files[0].ref == 3 &&
        guarded.info.files[0].process_count == 2 &&
        guarded.info.files[0].pids[0] == 7 &&
        guarded.info.files[0].pids[1] == 8, "ref differs from unique PID count");
  check(guarded.info.files[1].type == FD_PIPE &&
        guarded.info.files[1].writable == 1, "type and mode");
  check(zeroed(&guarded.info.files[2], (NFILE-2)*sizeof(struct sysinfo_file)), "unused rows cleared");
  preserved();

  reset();
  for(int i = 0; i < NFILE; i++){
    ftable.file[i].ref = 1;
    ftable.file[i].type = FD_INODE;
  }
  for(int i = 0; i < NPROC; i++){
    proc[i].state = RUNNABLE;
    proc[i].pid = i+1;
    for(int j = 0; j < NOFILE; j++)
      proc[i].ofile[j] = ftable.file;
  }
  check(collect_file_info(&guarded.info) == 0 &&
        guarded.info.open_file_count == NFILE &&
        guarded.info.files[0].process_count == NPROC, "full NFILE/NPROC/NOFILE");
  for(int i = 0; i < NPROC; i++)
    check(guarded.info.files[0].pids[i] == i+1, "every PID exactly once at limits");
  preserved();

  for(int i = 1; i < sizeof(struct file); i++)
    bad_pointer((struct file *)((uint64)ftable.file+i));
  bad_pointer((struct file *)((uint64)ftable.file-1));
  bad_pointer(ftable.file+NFILE);
  bad_pointer((struct file *)~0UL);
  __builtin_printf("FILE_INFO_NATIVE %s\n", failures ? "FAILED" : "ALL PASSED");
  return failures != 0;
}
