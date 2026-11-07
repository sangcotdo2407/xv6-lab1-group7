#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "kernel/riscv.h"
#include "kernel/sysinfo.h"
#include "user/user.h"

static struct sysinfo si;
static int failures;
#define CHECK(c, m) do { if(c) printf("  ok %s\n", m); \
  else { failures++; printf("  FAIL %s\n", m); } } while(0)

static void require(int ok, char *message) {
  if(!ok){ printf("  FAIL %s\n", message); exit(1); }
}
static int zeroed(void *data, int size) {
  uchar *bytes = data;
  for(int i = 0; i < size; i++) if(bytes[i]) return 0;
  return 1;
}
static int snapshot_ok(void) {
  if(si.open_file_count < 0 || si.open_file_count > NFILE ||
     si.process_count < 0 || si.process_count > NPROC) return 0;
  for(int i = 0; i < si.open_file_count; i++){
    struct sysinfo_file *f = &si.files[i];
    if(f->file_index < 0 || f->file_index >= NFILE || f->ref < 1 || f->type < 0 || f->type > 3 ||
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
    if(p->pid <= 0 || p->parent_pid < 0 || p->state < 1 || p->state > 5 ||
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
static int count_now(void) {
  require(sysinfo(&si) == 0 && snapshot_ok(), "snapshot bounds / IDs / modes / zero tails");
  return si.open_file_count;
}
static int own_fds(void) {
  for(int i = 0; i < si.process_count; i++)
    if(si.processes[i].pid == getpid()) return si.processes[i].open_file_count;
  return -1;
}

static void test_errors(void) {
  printf("filetest: invalid descriptors and pipe rollback\n");
  int files = count_now(), before = own_fds(), fds[NOFILE], count = 0, ends[2];
  CHECK(dup(-1) == -1 && close(-1) == -1 && close(NOFILE) == -1 &&
        open("no-such-file", O_RDONLY) == -1, "invalid FD and missing path");
  CHECK(count_now() == files && own_fds() == before, "failed calls preserve files and FDs");
  for(int i = 0; i < 2; i++){
    while(count < NOFILE && (fds[count] = dup(0)) >= 0) count++;
    if(i) close(fds[--count]);
    CHECK(pipe(ends) == -1, i ? "pipe: only one free FD" : "pipe: no free FD");
    CHECK(count_now() == files && own_fds() == before + count, "fdalloc rollback releases both objects and FD");
    while(count) close(fds[--count]);
  }
  CHECK(pipe((int *)MAXVA) == -1, "pipe rejects first copyout");
  CHECK(count_now() == files && own_fds() == before, "first copyout cleanup");
  char *heap = sbrk(PGSIZE);
  require(heap != (char *)-1, "pipe boundary buffer");
  int *edge = (int *)(PGROUNDUP((uint64)sbrk(0)) - sizeof(int));
  CHECK(pipe(edge) == -1, "pipe rejects second copyout");
  require(sbrk(-PGSIZE) != (char *)-1, "release boundary buffer");
  CHECK(count_now() == files && own_fds() == before, "second copyout cleanup");
}

static void test_churn(void) {
  printf("filetest: 10000 snapshots with four workers\n");
  int files = count_now(), processes = si.process_count, ready[2], pids[4], started = 0, bad = 0;
  require(pipe(ready) == 0, "worker ready pipe");
  for(int worker = 0; worker < 4; worker++){
    int pid = fork();
    if(pid < 0) break;
    if(pid == 0){
      close(ready[0]);
      int fd = dup(0);
      require(fd >= 0 && close(fd) == 0 && write(ready[1], "r", 1) == 1, "worker starts");
      close(ready[1]);
      for(;;){
        fd = dup(0);
        if(fd < 0 || close(fd) < 0) exit(1);
        if(worker == 1){
          fd = open("README", O_RDONLY);
          if(fd < 0 || close(fd) < 0) exit(1);
        } else if(worker == 2){
          int ends[2];
          if(pipe(ends) < 0 || close(ends[0]) < 0 || close(ends[1]) < 0) exit(1);
        } else if(worker == 3 && pipe((int *)MAXVA) != -1) exit(1);
      }
    }
    pids[started++] = pid;
  }
  close(ready[1]);
  char token;
  for(int i = 0; i < started; i++) if(read(ready[0], &token, 1) != 1) bad++;
  for(int i = 0; i < 10000; i++) if(sysinfo(&si) < 0 || !snapshot_ok()){ bad++; break; }
  for(int i = 0; i < started; i++) if(kill(pids[i]) < 0) bad++;
  for(int i = 0; i < started; i++){
    int status = 0;
    if(wait(&status) < 0 || status != -1) bad++;
  }
  close(ready[0]);
  CHECK(started == 4 && bad == 0, "dup/close, open/close, pipe, rollback: all snapshots valid");
  CHECK(count_now() == files && si.process_count == processes, "workers release all files and processes");
}

int main(void) {
  test_errors(); test_churn();
  if(failures) printf("filetest: %d FAILED\n", failures);
  else printf("filetest: ALL PASSED\n");
  exit(failures != 0);
}
