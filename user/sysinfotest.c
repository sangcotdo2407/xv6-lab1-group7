#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "kernel/riscv.h"
#include "kernel/memlayout.h"
#include "kernel/sysinfo.h"
#include "user/user.h"

static struct sysinfo si;
static int total, failed;
enum { FT_NONE, FT_PIPE, FT_INODE, FT_DEVICE };
enum { PS_UNUSED, PS_USED, PS_SLEEPING, PS_RUNNABLE, PS_RUNNING, PS_ZOMBIE };

#define CHECK(c, m) do { total++; \
  if(c) printf("  [PASS] %s\n", m); \
  else { failed++; printf("  [FAIL] %s\n", m); } } while(0)

static void require(int ok, char *message) {
  if(!ok){ printf("  [FAIL] %s\n", message); exit(1); }
}
static int zeroed(void *data, int size) {
  uchar *bytes = data;
  for(int i = 0; i < size; i++) if(bytes[i]) return 0;
  return 1;
}
static int valid_snapshot(void) {
  if(si.open_file_count < 0 || si.open_file_count > NFILE ||
     si.process_count < 0 || si.process_count > NPROC) return 0;
  for(int i = 0; i < si.open_file_count; i++){
    struct sysinfo_file *f = &si.files[i];
    if(f->file_index < 0 || f->file_index >= NFILE || f->ref <= 0 ||
       f->type < FT_NONE || f->type > FT_DEVICE ||
       f->readable < 0 || f->readable > 1 || f->writable < 0 || f->writable > 1 ||
       f->process_count < 0 || f->process_count > NPROC) return 0;
    for(int j = 0; j < i; j++) if(f->file_index == si.files[j].file_index) return 0;
    for(int j = 0; j < f->process_count; j++){
      if(f->pids[j] <= 0) return 0;
      for(int k = 0; k < j; k++) if(f->pids[k] == f->pids[j]) return 0;
    }
    if(!zeroed(f->pids + f->process_count, (NPROC-f->process_count)*sizeof(int))) return 0;
  }
  for(int i = 0; i < si.process_count; i++){
    struct sysinfo_process *p = &si.processes[i];
    if(p->pid <= 0 || p->parent_pid < 0 || p->state < PS_USED || p->state > PS_ZOMBIE ||
       p->open_file_count < 0 || p->open_file_count > NOFILE) return 0;
    for(int j = 0; j < i; j++) if(p->pid == si.processes[j].pid) return 0;
    int last = -1;
    for(int j = 0; j < p->open_file_count; j++){
      struct sysinfo_fd *fd = &p->open_files[j];
      if(fd->fd <= last || fd->fd >= NOFILE || fd->file_index < 0 || fd->file_index >= NFILE) return 0;
      last = fd->fd;
    }
    if(!zeroed(p->open_files + p->open_file_count,
               (NOFILE-p->open_file_count)*sizeof(struct sysinfo_fd))) return 0;
  }
  return zeroed(si.files + si.open_file_count, (NFILE-si.open_file_count)*sizeof(si.files[0])) &&
         zeroed(si.processes + si.process_count, (NPROC-si.process_count)*sizeof(si.processes[0]));
}
static void snap(void) {
  require(sysinfo(&si) == 0 && valid_snapshot(), "sysinfo / bounds / unique IDs / zero tails");
}
static struct sysinfo_process *find_proc(int pid) {
  for(int i = 0; i < si.process_count; i++) if(si.processes[i].pid == pid) return &si.processes[i];
  return 0;
}
static struct sysinfo_file *find_file(int index) {
  for(int i = 0; i < si.open_file_count; i++) if(si.files[i].file_index == index) return &si.files[i];
  return 0;
}
static int idx_of(struct sysinfo_process *p, int fd) {
  if(p) for(int i = 0; i < p->open_file_count; i++)
    if(p->open_files[i].fd == fd) return p->open_files[i].file_index;
  return -1;
}
static struct sysinfo_file *file_of(int fd) { return find_file(idx_of(find_proc(getpid()), fd)); }
static int has_pid(struct sysinfo_file *f, int pid) {
  for(int i = 0; i < f->process_count; i++) if(f->pids[i] == pid) return 1;
  return 0;
}
static int wait_state(int pid, int state) {
  for(int i = 0; i < 100; i++){
    snap();
    struct sysinfo_process *p = find_proc(pid);
    if(p && p->state == state) return 1;
    sleep(1);
  }
  return 0;
}
static int consistent(void) {
  snap();
  for(int i = 0; i < si.process_count; i++)
    for(int j = 0; j < si.processes[i].open_file_count; j++)
      if(!find_file(si.processes[i].open_files[j].file_index)) return 0;
  for(int i = 0; i < si.open_file_count; i++){
    struct sysinfo_file *f = &si.files[i];
    int refs = 0, owners = 0;
    for(int j = 0; j < si.process_count; j++){
      struct sysinfo_process *p = &si.processes[j];
      int count = 0;
      for(int k = 0; k < p->open_file_count; k++) count += p->open_files[k].file_index == f->file_index;
      if(count){ owners++; refs += count; if(!has_pid(f, p->pid)) return 0; }
    }
    if(refs != f->ref || owners != f->process_count) return 0;
  }
  return 1;
}
static void putu(uint64 value) {
  char digits[21];
  int count = 0;
  do { digits[count++] = '0' + value % 10; value /= 10; } while(value);
  while(count) write(1, &digits[--count], 1);
}
static void dump(void) {
  snap();
  printf("Files=%d processes=%d\n", si.open_file_count, si.process_count);
  for(int i = 0; i < si.open_file_count; i++){
    struct sysinfo_file *f = &si.files[i];
    printf("file#%d type=%d mode=%d%d ref=%d pids:", f->file_index, f->type, f->readable, f->writable, f->ref);
    for(int j = 0; j < f->process_count; j++) printf(" %d", f->pids[j]);
    printf("\n");
  }
  for(int i = 0; i < si.process_count; i++){
    struct sysinfo_process *p = &si.processes[i];
    printf("pid=%d state=%d parent=%d sched=", p->pid, p->state, p->parent_pid);
    putu(p->scheduling_count);
    printf(" fds:");
    for(int j = 0; j < p->open_file_count; j++) printf(" %d->#%d", p->open_files[j].fd, p->open_files[j].file_index);
    printf("\n");
  }
}

static void test_files(void) {
  printf("[1] basic, open, mode, dup, close\n");
  dump();
  int files = si.open_file_count, processes = si.process_count, same = 1;
  for(int i = 0; i < 50; i++){
    snap(); same &= si.open_file_count == files && si.process_count == processes;
  }
  CHECK(same && files >= 1 && processes >= 2, "50 stable snapshots");
  struct sysinfo_process *me = find_proc(getpid());
  CHECK(me && me->state == PS_RUNNING && find_proc(me->parent_pid) &&
        find_proc(1) && find_proc(1)->parent_pid == 0, "self, state, parent, init");
  struct sysinfo_file *f = file_of(0);
  CHECK(f && f->type == FT_DEVICE && f->readable && f->writable && has_pid(f, getpid()) &&
        idx_of(me, 0) == idx_of(me, 1) && idx_of(me, 1) == idx_of(me, 2), "console aliases and PID");
  int fd = open("si_a", O_CREATE | O_RDWR), rd = open("README", O_RDONLY), wr = open("si_a", O_WRONLY);
  require(fd >= 0 && rd >= 0 && wr >= 0, "open files");
  snap(); f = file_of(fd);
  CHECK(si.open_file_count == files + 3 && f && f->type == FT_INODE && f->readable && f->writable &&
        f->ref == 1 && f->process_count == 1 && f->pids[0] == getpid(), "open count, INODE, RDWR, ref, PID");
  struct sysinfo_file *r = file_of(rd), *w = file_of(wr);
  CHECK(f && r && r->readable && !r->writable && w && !w->readable && w->writable &&
        w->file_index != f->file_index, "RDONLY, WRONLY, separate objects for same path");
  int alias1 = dup(fd), alias2 = dup(fd);
  require(alias1 >= 0 && alias2 >= 0, "dup twice");
  snap(); f = file_of(fd);
  CHECK(f && f->ref == 3 && f->process_count == 1 && has_pid(f, getpid()) &&
        file_of(alias1) == f && file_of(alias2) == f && si.open_file_count == files + 3, "dup shares object, ref=3, one PID");
  close(fd); snap(); f = file_of(alias1);
  CHECK(f && f->ref == 2 && f->process_count == 1 && !file_of(fd), "close one alias retains object");
  close(alias1); close(alias2); close(rd); close(wr); unlink("si_a");
  snap(); CHECK(si.open_file_count == files && consistent(), "last close restores files and references");
}

static void test_processes(void) {
  printf("[2] pipe, fork, shared aliases, sleep, exit, zombie, wait, reuse\n");
  snap();
  int files = si.open_file_count, processes = si.process_count, parent = getpid(), ends[2];
  require(pipe(ends) == 0, "pipe");
  snap();
  struct sysinfo_file *r = file_of(ends[0]), *w = file_of(ends[1]);
  require(r && w, "pipe rows");
  int ri = r->file_index, wi = w->file_index;
  CHECK(si.open_file_count == files + 2 && ri != wi && r->type == FT_PIPE && w->type == FT_PIPE &&
        r->readable && !r->writable && !w->readable && w->writable && r->ref == 1 && w->ref == 1, "pipe type/mode/ref");
  int alias = dup(ends[0]), pid = fork();
  require(alias >= 0 && pid >= 0, "dup and fork");
  if(pid == 0){
    char token;
    require(read(ends[0], &token, 1) == 1, "child gate");
    snap();
    struct sysinfo_process *me = find_proc(getpid());
    exit(me && me->parent_pid == parent && idx_of(me, alias) == ri && idx_of(me, ends[1]) == wi ? 0 : 1);
  }
  require(wait_state(pid, PS_SLEEPING), "child sleeping");
  struct sysinfo_process *child = find_proc(pid), *me = find_proc(parent);
  CHECK(me && si.process_count == processes + 1 && child->parent_pid == parent && child->scheduling_count >= 1 &&
        child->open_file_count == me->open_file_count && idx_of(child, ends[0]) == ri &&
        idx_of(child, alias) == ri && idx_of(child, ends[1]) == wi, "fork inherits FDs, parent, sleeping state, counter");
  r = find_file(ri); w = find_file(wi);
  CHECK(r && w && r->ref == 4 && w->ref == 2 && r->process_count == 2 &&
        has_pid(r, parent) && has_pid(r, pid) && consistent(), "fork+dup: four references, two unique PIDs");
  require(write(ends[1], "x", 1) == 1 && wait_state(pid, PS_ZOMBIE), "release and await zombie");
  child = find_proc(pid);
  CHECK(child->open_file_count == 0 && si.process_count == processes + 1, "zombie counted, descriptors closed");
  int status = -1;
  CHECK(wait(&status) == pid && status == 0, "child syscall and wait status");
  snap(); r = find_file(ri);
  CHECK(!find_proc(pid) && si.process_count == processes && r && r->ref == 2 && r->process_count == 1, "wait removes child and its references");
  close(alias); close(ends[0]); close(ends[1]);
  int next = fork();
  require(next >= 0, "fork reused slot");
  if(next == 0) exit(0);
  CHECK(wait(&status) == next && status == 0 && next != pid, "new process has a new PID");
  snap(); CHECK(si.open_file_count == files && si.process_count == processes && consistent(), "lifecycle cleanup");
}

static void test_scheduling(void) {
  printf("[3] scheduling\n");
  snap(); uint64 before = find_proc(getpid())->scheduling_count;
  sleep(1); sleep(1);
  snap(); uint64 after = find_proc(getpid())->scheduling_count;
  CHECK(after > before, "dispatch count increases after sleep");
  for(volatile int i = 0; i < 30000000; i++);
  snap(); CHECK(find_proc(getpid())->scheduling_count >= after, "dispatch count never decreases");
  printf("sched: "); putu(before); printf(" -> "); putu(after); printf("\n");
}

static int spawn(int limit, int opens, int *gate) {
  int ready[2], pids[NPROC], count = 0;
  require(pipe(gate) == 0 && pipe(ready) == 0, "limit gates");
  while(count < limit){
    int pid = fork();
    if(pid < 0) break;
    if(pid == 0){
      close(gate[1]); close(ready[0]);
      for(int i = 0; i < opens; i++) open("README", O_RDONLY);
      require(write(ready[1], "r", 1) == 1, "limit ready");
      close(ready[1]);
      char token; read(gate[0], &token, 1); exit(0);
    }
    pids[count++] = pid;
  }
  char token;
  for(int i = 0; i < count; i++) require(read(ready[0], &token, 1) == 1, "wait ready");
  for(int i = 0; i < count; i++) require(wait_state(pids[i], PS_SLEEPING), "idle limit worker");
  gate[2] = ready[0]; gate[3] = ready[1]; close(gate[0]);
  return count;
}
static void test_limits(void) {
  printf("[4] NOFILE, NPROC, NFILE\n");
  snap(); int files = si.open_file_count, processes = si.process_count, fds[NOFILE], count = 0;
  while(count < NOFILE && (fds[count] = open("README", O_RDONLY)) >= 0) count++;
  snap(); CHECK(find_proc(getpid())->open_file_count == NOFILE && open("README", O_RDONLY) == -1 &&
        si.open_file_count == files + count && consistent(), "NOFILE and failed open cleanup");
  for(int i = 0; i < count; i++) close(fds[i]);
  for(int full_files = 0; full_files < 2; full_files++){
    int gate[4];
    count = spawn(full_files ? 10 : NPROC, full_files ? 10 : 0, gate);
    snap(); CHECK(count > 0 && (full_files ? si.open_file_count == NFILE : si.process_count == NPROC) &&
          si.process_count == processes + count && consistent(), full_files ? "NFILE full" : "NPROC full");
    close(gate[1]); close(gate[2]); close(gate[3]);
    for(int i = 0; i < count; i++) require(wait(0) > 0, "reap limit workers");
    snap(); CHECK(si.open_file_count == files && si.process_count == processes && consistent(), "limit cleanup");
  }
}

static void test_pointers(void) {
  printf("[5] invalid pointers, permissions, boundaries, canaries\n");
  uint64 marker, bad[] = {~0ULL, ~15ULL, MAXVA, 0x80000000ULL, 0x7fffffff00ULL,
    TRAPFRAME, TRAMPOLINE, PGROUNDDOWN((uint64)&marker) - PGSIZE};
  for(int i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) CHECK(sysinfo((struct sysinfo *)bad[i]) == -1, "invalid address rejected");
  int size = sizeof(si) + 3 * PGSIZE;
  char *heap = sbrk(size);
  require(heap != (char *)-1, "boundary buffer");
  char *base = (char *)PGROUNDUP((uint64)heap), *top = (char *)PGROUNDUP((uint64)sbrk(0));
  CHECK(sysinfo((struct sysinfo *)top) == -1 && sysinfo((struct sysinfo *)(top-8)) == -1, "unmapped tail / partial copyout");
  CHECK(sysinfo((struct sysinfo *)base) == 0 && sysinfo((struct sysinfo *)(base+13)) == 0 &&
        sysinfo((struct sysinfo *)(top-sizeof(si))) == 0 &&
        sysinfo((struct sysinfo *)(top-sizeof(si)+1)) == -1, "aligned, unaligned, exact end, one byte overflow");
  memset(base, 0xa5, sizeof(si) + 200);
  CHECK(sysinfo((struct sysinfo *)(base+100)) == 0, "canary destination");
  int intact = 1;
  for(int i = 0; i < 100; i++) intact &= (uchar)base[i] == 0xa5 && (uchar)base[100+sizeof(si)+i] == 0xa5;
  CHECK(intact, "no writes outside struct");
  require(sbrk(-size) != (char *)-1, "release boundary buffer");
  for(int i = 0; i < 2; i++){
    int pid = fork(), status = -1;
    require(pid >= 0, "permission probe");
    if(pid == 0) exit(sysinfo(i ? (struct sysinfo *)test_pointers : 0) == -1 ? 0 : 1);
    CHECK(wait(&status) == pid && status == 0, "NULL / read-only text rejected, child survives");
  }
  memset(&si, 0xa5, sizeof(si));
  snap(); CHECK(consistent(), "recovery, zero tails and stable references");
}

static void test_concurrent(int churn) {
  printf("[%d] %s\n", churn ? 7 : 6, churn ? "2000 snapshots during dup/close" : "4 concurrent callers");
  snap(); int processes = si.process_count, ready[2], pids[4], started = 0, bad = 0;
  require(pipe(ready) == 0, "worker gate");
  for(int i = 0; i < 4; i++){
    int pid = fork();
    if(pid < 0) break;
    if(pid == 0){
      close(ready[0]);
      int fd = dup(0);
      require(fd >= 0 && close(fd) == 0 && write(ready[1], "r", 1) == 1, "worker ready");
      close(ready[1]);
      for(int j = 0; churn || j < 40; j++){
        if(churn){ fd = dup(0); if(fd < 0 || close(fd) < 0) exit(1); }
        else if(sysinfo(&si) < 0 || !valid_snapshot() || !find_proc(getpid())) exit(1);
      }
      exit(0);
    }
    pids[started++] = pid;
  }
  close(ready[1]);
  char token;
  for(int i = 0; i < started; i++) if(read(ready[0], &token, 1) != 1) bad++;
  for(int i = 0; churn && i < 2000; i++) if(sysinfo(&si) < 0 || !valid_snapshot()) bad++;
  for(int i = 0; churn && i < started; i++) if(kill(pids[i]) < 0) bad++;
  for(int i = 0; i < started; i++){
    int status = 0;
    if(wait(&status) < 0 || status != (churn ? -1 : 0)) bad++;
  }
  close(ready[0]);
  CHECK(started == 4 && bad == 0, "all workers and snapshots valid");
  snap(); CHECK(si.process_count == processes && consistent(), "worker cleanup");
}

int main(void) {
  test_files(); test_processes(); test_scheduling(); test_limits();
  test_pointers(); test_concurrent(0); test_concurrent(1);
  printf("===== %d/%d PASS =====\n", total-failed, total);
  exit(failed != 0);
}
