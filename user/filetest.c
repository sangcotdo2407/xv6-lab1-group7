// filetest.c - tests for the file part of sysinfo() (collect_file_info).
// Author: Mai Khắc Hoàng Vũ (24120183).
//
// Needs: SYS_sysinfo + user stub "int sysinfo(struct sysinfo *)" (Thi),
// and collect_process_info() (Sang), because the test maps a local fd to
// its file_index through this process's own entry in info.processes[].
//
// Makefile: add  $U/_filetest\  to UPROGS.   Run in xv6:  $ filetest

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/sysinfo.h"
#include "user/user.h"

#define T_PIPE   1   // enum value of FD_PIPE   in kernel/file.h
#define T_INODE  2   // enum value of FD_INODE  in kernel/file.h
#define T_DEVICE 3

static struct sysinfo si;   // ~40 KB: must be static, not on the user stack
static int failures;

#define CHECK(cond, msg) do { \
  if(cond){ printf("  ok   %s\n", msg); } \
  else    { printf("  FAIL %s (line %d)\n", msg, __LINE__); failures++; } \
} while(0)

static int
snap(void)
{
  if(sysinfo(&si) < 0){
    printf("  FAIL sysinfo() returned -1\n");
    failures++;
    return -1;
  }
  return 0;
}

// file_index of local fd in process pid, or -1.
static int
fd_to_index(int pid, int fd)
{
  for(int i = 0; i < si.process_count; i++){
    struct sysinfo_process *p = &si.processes[i];
    if(p->pid != pid)
      continue;
    for(int j = 0; j < p->open_file_count; j++)
      if(p->open_files[j].fd == fd)
        return p->open_files[j].file_index;
  }
  return -1;
}

static struct sysinfo_file *
file_by_index(int idx)
{
  for(int i = 0; i < si.open_file_count; i++)
    if(si.files[i].file_index == idx)
      return &si.files[i];
  return 0;
}

static int
has_pid(struct sysinfo_file *f, int pid)
{
  for(int i = 0; i < f->process_count; i++)
    if(f->pids[i] == pid)
      return 1;
  return 0;
}

static int
distinct_pids(struct sysinfo_file *f)
{
  for(int i = 0; i < f->process_count; i++)
    for(int j = i + 1; j < f->process_count; j++)
      if(f->pids[i] == f->pids[j])
        return 0;
  return 1;
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

// Check structural invariants even while other CPUs change descriptors.
// File and process rows are sampled at different times: do not compare
// ref against a later FD total or require every FD in the file snapshot.
static int
snapshot_ok(void)
{
  if(si.open_file_count < 0 || si.open_file_count > NFILE ||
     si.process_count < 0 || si.process_count > NPROC)
    return 0;
  for(int i = 0; i < si.open_file_count; i++){
    struct sysinfo_file *f = &si.files[i];
    if(f->file_index < 0 || f->file_index >= NFILE || f->ref < 1 ||
       f->type < 0 || f->type > T_DEVICE ||
       f->readable < 0 || f->readable > 1 ||
       f->writable < 0 || f->writable > 1 ||
       f->process_count < 0 || f->process_count > NPROC)
      return 0;
    if(!distinct_pids(f))
      return 0;
    for(int j = 0; j < f->process_count; j++)
      if(f->pids[j] <= 0)
        return 0;
    for(int j = i + 1; j < si.open_file_count; j++)
      if(f->file_index == si.files[j].file_index)
        return 0;
    if(!zeroed(&f->pids[f->process_count],
               (NPROC-f->process_count)*sizeof(int)))
      return 0;
  }
  for(int i = 0; i < si.process_count; i++){
    struct sysinfo_process *p = &si.processes[i];
    if(p->open_file_count < 0 || p->open_file_count > NOFILE)
      return 0;
    int last = -1;
    for(int j = 0; j < p->open_file_count; j++){
      struct sysinfo_fd *f = &p->open_files[j];
      if(f->fd <= last || f->fd >= NOFILE ||
         f->file_index < 0 || f->file_index >= NFILE)
        return 0;
      last = f->fd;
    }
    if(!zeroed(&p->open_files[p->open_file_count],
               (NOFILE-p->open_file_count)*sizeof(struct sysinfo_fd)))
      return 0;
  }
  return zeroed(&si.files[si.open_file_count],
                (NFILE-si.open_file_count)*sizeof(struct sysinfo_file)) &&
         zeroed(&si.processes[si.process_count],
                (NPROC-si.process_count)*sizeof(struct sysinfo_process));
}

// Snapshot, then return the file object behind fd (0 if absent).
static struct sysinfo_file *
lookup(int fd)
{
  if(snap() < 0)
    return 0;
  int idx = fd_to_index(getpid(), fd);
  return idx < 0 ? 0 : file_by_index(idx);
}

static int
count_now(void)
{
  return snap() < 0 ? -1 : si.open_file_count;
}

// 1. open a new file
static void
test_open(void)
{
  printf("test 1: open a new file\n");
  int base = count_now();
  int fd = open("README", O_RDONLY);
  struct sysinfo_file *f = lookup(fd);
  CHECK(f != 0, "new fd appears in sysinfo");
  CHECK(si.open_file_count == base + 1, "open_file_count +1");
  if(f){
    CHECK(f->type == T_INODE, "type is FD_INODE");
    CHECK(f->readable == 1 && f->writable == 0, "O_RDONLY: readable only");
    CHECK(f->ref == 1, "ref == 1");
    CHECK(f->process_count == 1 && f->pids[0] == getpid(), "pids = {self}");
  }
  close(fd);
  CHECK(count_now() == base, "close: open_file_count back to base");
}

// 2. open the same path twice -> two distinct struct file
static void
test_open_twice(void)
{
  printf("test 2: open the same path twice\n");
  int base = count_now();
  int fd1 = open("README", O_RDONLY);
  int fd2 = open("README", O_RDONLY);
  // lookup() refreshes the shared buffer si, so copy what is needed
  // out of each snapshot before taking the next one.
  struct sysinfo_file *f1 = lookup(fd1);
  int i1 = f1 ? f1->file_index : -1;
  int ref1 = f1 ? f1->ref : -1;
  struct sysinfo_file *f2 = lookup(fd2);
  int i2 = f2 ? f2->file_index : -1;
  int ref2 = f2 ? f2->ref : -1;
  CHECK(i1 >= 0 && i2 >= 0 && i1 != i2, "two different file_index");
  CHECK(si.open_file_count == base + 2, "open_file_count +2");
  CHECK(ref1 == 1 && ref2 == 1, "each object has ref == 1");
  close(fd1);
  close(fd2);
  CHECK(count_now() == base, "closed both: back to base");
}

// 3. dup -> same struct file, ref 2, still ONE pid
static void
test_dup(void)
{
  printf("test 3: dup\n");
  int base = count_now();
  int fd = open("README", O_RDONLY);
  int fd2 = dup(fd);
  struct sysinfo_file *f = lookup(fd);
  int i1 = f ? f->file_index : -1;
  CHECK(i1 >= 0 && fd_to_index(getpid(), fd2) == i1, "both fds -> same file_index");
  CHECK(si.open_file_count == base + 1, "dup adds no file object");
  if(f){
    CHECK(f->ref == 2, "ref == 2");
    CHECK(f->process_count == 1, "process_count == 1 (ref != process_count)");
  }
  close(fd2);
  f = lookup(fd);
  CHECK(f != 0 && f->ref == 1, "close(dup): ref back to 1, object still open");
  close(fd);
  CHECK(count_now() == base, "close last fd: object released");
}

// 4. fork -> parent and child share the object
static void
test_fork(void)
{
  printf("test 4: fork shares struct file\n");
  int fd = open("README", O_RDONLY);
  int sync[2];
  pipe(sync);
  int parent = getpid();
  int pid = fork();
  if(pid < 0){
    printf("  FAIL fork\n");
    failures++;
    return;
  }
  if(pid == 0){
    char c;
    close(sync[1]);
    read(sync[0], &c, 1);          // block until the parent finished checking
    exit(0);
  }
  struct sysinfo_file *f = lookup(fd);
  CHECK(f != 0 && f->ref == 2, "ref == 2 after fork");
  if(f){
    CHECK(f->process_count == 2, "process_count == 2");
    CHECK(has_pid(f, parent) && has_pid(f, pid), "pids = {parent, child}");
    CHECK(distinct_pids(f), "no duplicated pid");
  }
  CHECK(fd_to_index(pid, fd) == (f ? f->file_index : -2), "child's fd -> same file_index");
  write(sync[1], "x", 1);
  wait(0);
  f = lookup(fd);
  CHECK(f != 0 && f->ref == 1 && f->process_count == 1, "after child exits: ref 1, 1 pid");
  close(fd);
  close(sync[0]);
  close(sync[1]);
}

// 5. same object through two fds in the child as well
static void
test_fork_dup(void)
{
  printf("test 5: fork + dup (many fds, one object)\n");
  int fd = open("README", O_RDONLY);
  int fd2 = dup(fd);
  int sync[2];
  pipe(sync);
  int pid = fork();
  if(pid == 0){
    char c;
    close(sync[1]);
    read(sync[0], &c, 1);
    exit(0);
  }
  struct sysinfo_file *f = lookup(fd);
  CHECK(f != 0 && f->ref == 4, "ref == 4 (2 fds x 2 processes)");
  CHECK(f != 0 && f->process_count == 2, "process_count == 2, not 4");
  CHECK(f != 0 && distinct_pids(f), "pids not duplicated");
  write(sync[1], "x", 1);
  wait(0);
  close(fd);
  close(fd2);
  close(sync[0]);
  close(sync[1]);
}

// 6. pipe objects: type, permissions
static void
test_pipe(void)
{
  printf("test 6: pipe ends\n");
  int p[2];
  pipe(p);
  struct sysinfo_file *r = lookup(p[0]);
  int ri = r ? r->file_index : -1;
  CHECK(r && r->type == T_PIPE && r->readable == 1 && r->writable == 0, "read end");
  struct sysinfo_file *w = lookup(p[1]);
  CHECK(w && w->type == T_PIPE && w->readable == 0 && w->writable == 1, "write end");
  CHECK(w && ri != w->file_index, "two distinct objects");
  close(p[0]);
  close(p[1]);
}

// 7. limits: fill the per-process table, then check consistency
static void
test_limits(void)
{
  printf("test 7: fill NOFILE descriptors\n");
  int fds[NOFILE], n = 0;
  int base = count_now();
  while(n < NOFILE){
    int fd = open("README", O_RDONLY);
    if(fd < 0)
      break;
    fds[n++] = fd;
  }
  CHECK(n > 0 && n < NOFILE, "open() fails once the fd table is full");
  CHECK(count_now() == base + n, "open_file_count == base + successful opens");
  int ok = 1;
  for(int i = 0; i < si.open_file_count; i++){
    struct sysinfo_file *f = &si.files[i];
    if(f->file_index < 0 || f->file_index >= NFILE || f->ref < 1 ||
       f->process_count > NPROC || !distinct_pids(f))
      ok = 0;
  }
  CHECK(ok, "every entry: index < NFILE, ref >= 1, pids distinct");
  for(int i = 0; i < n; i++)
    close(fds[i]);
  CHECK(count_now() == base, "all closed: back to base");
}

// 8. repeated calls give a stable result
static void
test_repeat(void)
{
  printf("test 8: repeated sysinfo() calls\n");
  int c0 = count_now();
  int same = 1;
  for(int i = 0; i < 50; i++)
    if(count_now() != c0)
      same = 0;
  CHECK(same, "50 calls, same open_file_count");
}

// Both fd-allocation rollback paths and both copyout failure paths must
// detach descriptors before fileclose(), without holding a proc lock.
static void
test_pipe_errors(void)
{
  printf("test 9: pipe rollback and invalid descriptors\n");
  int base = count_now(), fds[NOFILE], n = 0, p[2];
  int fd;
  while(n < NOFILE && (fd = dup(0)) >= 0)
    fds[n++] = fd;
  CHECK(n > 0 && n < NOFILE, "filled descriptor table with dup");
  CHECK(pipe(p) < 0, "pipe fails with no descriptor slot");
  CHECK(count_now() == base && snapshot_ok(), "no file leak after first fdalloc fails");
  if(n > 0){
    close(fds[--n]);
    CHECK(pipe(p) < 0, "pipe fails with only one descriptor slot");
    CHECK(count_now() == base && snapshot_ok(), "second fdalloc rollback restores files");
    CHECK(fd_to_index(getpid(), fds[n]) == -1, "rolled-back descriptor is absent");
  }
  for(int i = 0; i < n; i++)
    close(fds[i]);
  CHECK(pipe((int *)(1ULL << 38)) < 0, "pipe rejects unmapped destination");
  CHECK(count_now() == base && snapshot_ok(), "first copyout rollback restores files");

  char *heap = sbrk(8192);
  if(heap != (char *)-1){
    uint64 top = ((uint64)sbrk(0) + 4095) & ~4095ULL;
    CHECK(pipe((int *)(top-sizeof(int))) < 0, "pipe rejects second fd outside mapped memory");
    CHECK(count_now() == base && snapshot_ok(), "second copyout rollback restores files");
    sbrk(-8192);
  } else {
    CHECK(0, "allocate boundary buffer");
  }
  CHECK(dup(-1) < 0 && close(-1) < 0 && close(NOFILE) < 0,
        "invalid descriptors return errors");
  CHECK(open("no-such-file", O_RDONLY) < 0, "open missing file returns an error");
  CHECK(count_now() == base && snapshot_ok(), "error paths preserve snapshot");
}

static void
test_concurrent(void)
{
  enum { WORKERS = 4, SNAPSHOTS = 10000 };
  printf("test 10: descriptor churn with %d snapshots\n", SNAPSHOTS);
  int base = count_now(), ready[2], workers[WORKERS], started = 0;
  if(pipe(ready) < 0){
    CHECK(0, "create worker ready pipe");
    return;
  }
  for(int w = 0; w < WORKERS; w++){
    int pid = fork();
    if(pid < 0)
      break;
    if(pid == 0){
      close(ready[0]);
      if(write(ready[1], "r", 1) != 1)
        exit(1);
      close(ready[1]);
      for(;;){
        int fd = dup(0);
        if(fd < 0 || close(fd) < 0)
          exit(1);
        if(w == 1){
          fd = open("README", O_RDONLY);
          if(fd < 0 || close(fd) < 0)
            exit(1);
        } else if(w == 2){
          int p[2];
          if(pipe(p) < 0 || close(p[0]) < 0 || close(p[1]) < 0)
            exit(1);
        } else if(w == 3){
          if(pipe((int *)(1ULL << 38)) != -1)
            exit(1);
        }
      }
    }
    workers[started++] = pid;
  }
  close(ready[1]);
  int ok = started == WORKERS;
  char c;
  for(int w = 0; w < started; w++)
    if(read(ready[0], &c, 1) != 1)
      ok = 0;
  int snapshots = 0;
  for(; snapshots < SNAPSHOTS; snapshots++){
    if(sysinfo(&si) < 0 || !snapshot_ok()){
      ok = 0;
      break;
    }
  }
  for(int w = 0; w < started; w++)
    kill(workers[w]);
  for(int w = 0; w < started; w++){
    int status;
    if(wait(&status) < 0 || status != -1)
      ok = 0;  // workers normally stop only when killed
  }
  close(ready[0]);
  CHECK(ok && snapshots == SNAPSHOTS,
        "4 workers: dup/close, open/close, pipe and rollback keep bounds and unique PIDs");
  CHECK(count_now() == base && snapshot_ok(), "worker exit releases all files");
}

int
main(void)
{
  test_open();
  test_open_twice();
  test_dup();
  test_fork();
  test_fork_dup();
  test_pipe();
  test_limits();
  test_repeat();
  test_pipe_errors();
  test_concurrent();
  if(failures == 0)
    printf("filetest: ALL PASSED\n");
  else
    printf("filetest: %d FAILED\n", failures);
  exit(failures != 0);
}
