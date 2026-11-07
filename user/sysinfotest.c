#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"
#include "kernel/riscv.h"
#include "kernel/memlayout.h"
#include "kernel/sysinfo.h"
#include "user/user.h"

// struct sysinfo ~38KB: PHAI la bien toan cuc (stack user chi 1 trang)
static struct sysinfo si;
static int failed, total;

// Gia tri enum cua kernel (file.h / proc.h) - sua neu ban xv6 khac
enum { FT_NONE, FT_PIPE, FT_INODE, FT_DEVICE };
enum { PS_UNUSED, PS_USED, PS_SLEEPING, PS_RUNNABLE, PS_RUNNING, PS_ZOMBIE };
static char *tname[] = {"NONE", "PIPE", "INODE", "DEVICE"};
static char *sname[] = {"UNUSED", "USED", "SLEEPING", "RUNNABLE", "RUNNING", "ZOMBIE"};

#define CHECK(c, m) do { total++; \
  if(c) printf("  [PASS] %s\n", m); \
  else { printf("  [FAIL] %s (dong %d)\n", m, __LINE__); failed++; } } while(0)

static void require(int ok, char *what) {
  if(!ok){ printf("  [FAIL] %s\n", what); exit(1); }
}

// Validate every count before indexing arrays, also during concurrent calls.
// Cross-section equality is only meaningful when descriptor writers are idle.
static int valid_snapshot(void) {
  if(si.open_file_count < 0 || si.open_file_count > NFILE ||
     si.process_count < 0 || si.process_count > NPROC) return 0;
  for(int i = 0; i < si.open_file_count; i++){
    struct sysinfo_file *f = &si.files[i];
    if(f->file_index < 0 || f->file_index >= NFILE ||
       f->type < FT_NONE || f->type > FT_DEVICE || f->ref <= 0 ||
       f->process_count < 0 || f->process_count > NPROC) return 0;
    for(int j = 0; j < i; j++)
      if(si.files[j].file_index == f->file_index) return 0;
    for(int a = 0; a < f->process_count; a++){
      if(f->pids[a] <= 0) return 0;
      for(int b = 0; b < a; b++)
        if(f->pids[a] == f->pids[b]) return 0;
    }
  }
  for(int i = 0; i < si.process_count; i++){
    struct sysinfo_process *p = &si.processes[i];
    if(p->pid <= 0 || p->state < PS_USED || p->state > PS_ZOMBIE ||
       p->parent_pid < 0 || p->open_file_count < 0 ||
       p->open_file_count > NOFILE) return 0;
    for(int j = 0; j < i; j++)
      if(si.processes[j].pid == p->pid) return 0;
    for(int k = 0; k < p->open_file_count; k++){
      struct sysinfo_fd *d = &p->open_files[k];
      if(d->fd < 0 || d->fd >= NOFILE ||
         d->file_index < 0 || d->file_index >= NFILE) return 0;
      for(int j = 0; j < k; j++)
        if(p->open_files[j].fd == d->fd) return 0;
    }
  }
  return 1;
}

static void snap(void) {
  require(sysinfo(&si) == 0, "sysinfo that bai");
  require(valid_snapshot(), "snapshot vuot gioi han / du lieu khong hop le");
}
static int nfiles(void) { snap(); return si.open_file_count; }
static int nproc(void)  { snap(); return si.process_count; }

static struct sysinfo_process *find_proc(int pid) {
  for(int i = 0; i < si.process_count; i++)
    if(si.processes[i].pid == pid) return &si.processes[i];
  return 0;
}
static struct sysinfo_file *find_file(int idx) {
  for(int i = 0; i < si.open_file_count; i++)
    if(si.files[i].file_index == idx) return &si.files[i];
  return 0;
}
static int idx_of(struct sysinfo_process *p, int fd) {   // fd -> file_index, -1 neu khong mo
  if(!p) return -1;
  for(int i = 0; i < p->open_file_count; i++)
    if(p->open_files[i].fd == fd) return p->open_files[i].file_index;
  return -1;
}
static struct sysinfo_file *file_of(struct sysinfo_process *p, int fd) {
  int k = idx_of(p, fd);
  return k < 0 ? 0 : find_file(k);
}
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
static int is_zero(void *p, int n) {
  uchar *c = p;
  for(int i = 0; i < n; i++) if(c[i]) return 0;
  return 1;
}

// xv6 printf cannot print the full 64-bit scheduling counter.
static void putu(uint64 v) {
  char buf[21];
  int i = 0;
  if(v == 0) buf[i++] = '0';
  while(v > 0){ buf[i++] = '0' + (v % 10); v /= 10; }
  while(i > 0) write(1, &buf[--i], 1);
}

static void dump(void) {
  snap();
  printf("-- FILE DANG MO: %d --\n", si.open_file_count);
  for(int i = 0; i < si.open_file_count; i++){
    struct sysinfo_file *f = &si.files[i];
    printf("  file#%d %s %s%s ref=%d procs=%d pids:", f->file_index, tname[f->type],
           f->readable ? "r" : "-", f->writable ? "w" : "-", f->ref, f->process_count);
    for(int j = 0; j < f->process_count; j++) printf(" %d", f->pids[j]);
    printf("\n");
  }
  printf("-- TIEN TRINH (state != UNUSED): %d --\n", si.process_count);
  for(int i = 0; i < si.process_count; i++){
    struct sysinfo_process *p = &si.processes[i];
    printf("  pid=%d ppid=%d %s sched=", p->pid, p->parent_pid, sname[p->state]);
    putu(p->scheduling_count);
    printf(" fds:");
    for(int k = 0; k < p->open_file_count; k++)
      printf(" %d->#%d", p->open_files[k].fd, p->open_files[k].file_index);
    printf("\n");
  }
}

// Bat bien tong the cua snapshot (chi dung khi cac tien trinh khac dang "yen")
//  - moi chi so nam trong gioi han mang, file_index duy nhat
//  - ref == tong so fd tro toi file; process_count == so PID khac nhau; pids khong trung
//  - moi fd cua moi tien trinh deu tro toi file co trong danh sach tong
static int consistent(void) {
  snap();
  if(si.open_file_count < 0 || si.open_file_count > NFILE) return 0;
  if(si.process_count < 0 || si.process_count > NPROC) return 0;
  for(int i = 0; i < si.process_count; i++){
    struct sysinfo_process *p = &si.processes[i];
    if(p->open_file_count < 0 || p->open_file_count > NOFILE) return 0;
    for(int k = 0; k < p->open_file_count; k++){
      struct sysinfo_fd *d = &p->open_files[k];
      if(d->fd < 0 || d->fd >= NOFILE || !find_file(d->file_index)) return 0;
    }
  }
  for(int i = 0; i < si.open_file_count; i++){
    struct sysinfo_file *f = &si.files[i];
    if(f->file_index < 0 || f->file_index >= NFILE) return 0;
    if(f->process_count < 0 || f->process_count > NPROC) return 0;
    for(int j = i + 1; j < si.open_file_count; j++)
      if(si.files[j].file_index == f->file_index) return 0;
    int cnt = 0, procs = 0;
    for(int j = 0; j < si.process_count; j++){
      struct sysinfo_process *p = &si.processes[j];
      int c = 0;
      for(int k = 0; k < p->open_file_count; k++)
        if(p->open_files[k].file_index == f->file_index) c++;
      if(c){ procs++; cnt += c; if(!has_pid(f, p->pid)) return 0; }
    }
    if(cnt != f->ref || procs != f->process_count) return 0;
    for(int a = 0; a < f->process_count; a++)
      for(int b = a + 1; b < f->process_count; b++)
        if(f->pids[a] == f->pids[b]) return 0;
  }
  return 1;
}

//  1. co ban: goi nhieu lan
static void t_basic(void) {
  printf("[1] goi nhieu lan, ket qua on dinh\n");
  dump();
  snap();
  int nf = si.open_file_count, np = si.process_count, same = 1;
  for(int i = 0; i < 5; i++){
    snap();
    if(si.open_file_count != nf || si.process_count != np) same = 0;
  }
  CHECK(same, "5 lan goi lien tiep cho cung so file / so tien trinh");
  CHECK(nf >= 1 && np >= 2, "co it nhat console va 2 tien trinh (sh + test)");
  struct sysinfo_process *me = find_proc(getpid());
  CHECK(me && me->state == PS_RUNNING, "tien trinh hien tai co state RUNNING");
  CHECK(me && find_proc(me->parent_pid) != 0, "parent_pid tro toi tien trinh dang ton tai (sh)");
  CHECK(find_proc(1) && find_proc(1)->parent_pid == 0, "init (pid 1) ton tai, khong co cha");
  CHECK(me && idx_of(me, 0) >= 0 && idx_of(me, 0) == idx_of(me, 1) && idx_of(me, 1) == idx_of(me, 2),
        "fd 0,1,2 cung tro toi 1 file (console)");
  struct sysinfo_file *c = file_of(me, 0);
  CHECK(c && c->type == FT_DEVICE && c->readable && c->writable, "console: DEVICE, doc+ghi");
  CHECK(c && has_pid(c, getpid()), "console liet ke PID cua tien trinh hien tai");
  CHECK(consistent(), "bat bien ref / process_count / pids / file_index");
}

//  2. open / close / mode
static void t_open(void) {
  printf("[2] open / close / quyen doc-ghi\n");
  int base = nfiles(), self = getpid();
  int fd = open("si_a", O_CREATE | O_RDWR);
  require(fd >= 0, "open si_a");
  snap();
  struct sysinfo_process *me = find_proc(self);
  struct sysinfo_file *f = file_of(me, fd);
  CHECK(si.open_file_count == base + 1, "open them dung 1 file");
  CHECK(f && f->type == FT_INODE, "type INODE");
  CHECK(f && f->readable && f->writable, "O_RDWR -> readable && writable");
  CHECK(f && f->ref == 1 && f->process_count == 1, "ref = 1, process_count = 1");
  CHECK(f && f->pids[0] == self, "pids[0] = pid hien tai");

  int fr = open("README", O_RDONLY), fw = open("si_a", O_WRONLY);   // mo trung duong dan -> struct file khac
  require(fr >= 0 && fw >= 0, "open README / si_a");
  snap(); me = find_proc(self);
  struct sysinfo_file *r = file_of(me, fr), *w = file_of(me, fw);
  CHECK(si.open_file_count == base + 3, "mo them 2 file (ke ca mo trung duong dan) -> +2");
  CHECK(r && r->readable && !r->writable, "O_RDONLY -> chi doc");
  CHECK(w && !w->readable && w->writable, "O_WRONLY -> chi ghi");
  CHECK(f && w && idx_of(me, fd) != idx_of(me, fw), "open 2 lan cung duong dan -> 2 file_index khac nhau");
  close(fr); close(fw); close(fd); unlink("si_a");
  CHECK(nfiles() == base, "close het -> so file ve ban dau");
  CHECK(consistent(), "bat bien");
}

//  3. dup
static void t_dup(void) {
  printf("[3] dup: nhieu fd, cung 1 struct file\n");
  int base = nfiles(), self = getpid();
  int fd = open("README", O_RDONLY), fd2 = dup(fd), fd3 = dup(fd);
  require(fd >= 0 && fd2 >= 0 && fd3 >= 0, "open / dup");
  snap();
  struct sysinfo_process *me = find_proc(self);
  CHECK(si.open_file_count == base + 1, "dup khong tao them file moi");
  CHECK(idx_of(me, fd) == idx_of(me, fd2) && idx_of(me, fd2) == idx_of(me, fd3), "3 fd cung file_index");
  struct sysinfo_file *f = file_of(me, fd);
  CHECK(f && f->ref == 3, "ref = 3");
  CHECK(f && f->process_count == 1 && f->pids[0] == self, "process_count = 1 du ref = 3 (PID khong lap)");
  close(fd);
  snap(); me = find_proc(self); f = file_of(me, fd2);
  CHECK(f && f->ref == 2 && f->process_count == 1, "dong 1 fd -> ref = 2");
  CHECK(idx_of(me, fd) == -1, "fd da dong khong con trong open_files[]");
  close(fd2); close(fd3);
  CHECK(nfiles() == base, "dong het -> so file ve ban dau");
  CHECK(consistent(), "bat bien");
}

//  4. fork + pipe: file dung chung cha/con
static void t_fork_pipe(void) {
  printf("[4] fork + pipe: file dung chung giua cha va con\n");
  int base = nfiles(), pbase = nproc(), mypid = getpid(), p[2];
  require(pipe(p) == 0, "pipe");
  snap();
  struct sysinfo_process *me = find_proc(mypid);
  int rdidx = idx_of(me, p[0]), wridx = idx_of(me, p[1]);
  struct sysinfo_file *rd = find_file(rdidx), *wr = find_file(wridx);
  CHECK(si.open_file_count == base + 2, "pipe tao 2 file");
  CHECK(rd && wr && rd->type == FT_PIPE && wr->type == FT_PIPE, "ca hai type PIPE");
  CHECK(rd && wr && rd->readable && !rd->writable && !wr->readable && wr->writable,
        "dau doc chi doc, dau ghi chi ghi");
  CHECK(rd && wr && rd->ref == 1 && wr->ref == 1, "ref = 1 truoc fork");

  int pid = fork();
  require(pid >= 0, "fork pipe child");
  if(pid == 0){
    char c;
    read(p[0], &c, 1);                          // ngu o day cho cha chup snapshot
    snap();
    struct sysinfo_process *m = find_proc(getpid());
    exit(m && m->parent_pid == mypid && idx_of(m, p[0]) == rdidx && idx_of(m, p[1]) == wridx ? 0 : 1);
  }
  CHECK(wait_state(pid, PS_SLEEPING), "doi con that su ngu tren pipe");
  struct sysinfo_process *ch = find_proc(pid);
  me = find_proc(mypid);
  CHECK(si.process_count == pbase + 1, "process_count tang 1 sau fork");
  CHECK(ch && ch->parent_pid == mypid, "parent_pid cua con = pid cua cha");
  CHECK(ch && ch->state == PS_SLEEPING, "con dang SLEEPING (doi pipe)");
  CHECK(ch && ch->scheduling_count >= 1, "con da duoc lap lich >= 1 lan");
  CHECK(ch && me && ch->open_file_count == me->open_file_count, "con ke thua dung so fd cua cha");
  CHECK(ch && idx_of(ch, p[0]) == rdidx && idx_of(ch, p[1]) == wridx, "fd cua con tro cung file_index voi cha");
  rd = find_file(rdidx); wr = find_file(wridx);
  CHECK(rd && rd->ref == 2 && wr && wr->ref == 2, "ref cua pipe tang len 2");
  CHECK(rd && rd->process_count == 2 && has_pid(rd, mypid) && has_pid(rd, pid), "pids chua ca cha va con");
  CHECK(consistent(), "bat bien");

  require(write(p[1], "x", 1) == 1, "release pipe child");
  int st = -1;
  CHECK(wait(&st) == pid && st == 0, "con tu goi sysinfo thay dung parent_pid va fd");
  close(p[0]); close(p[1]);
  CHECK(nfiles() == base && nproc() == pbase, "don dep: so file / so tien trinh ve ban dau");
}

//  5. zombie
static void t_zombie(void) {
  printf("[5] zombie va tai su dung o proc\n");
  int pbase = nproc(), base = nfiles();
  int pid = fork();
  require(pid >= 0, "fork zombie child");
  if(pid == 0) exit(0);
  CHECK(wait_state(pid, PS_ZOMBIE), "doi con that su thanh zombie");
  struct sysinfo_process *ch = find_proc(pid);
  CHECK(ch && ch->state == PS_ZOMBIE, "con da exit, cha chua wait -> ZOMBIE");
  CHECK(ch && ch->open_file_count == 0, "zombie khong giu file");
  CHECK(si.process_count == pbase + 1, "zombie van duoc dem (state != UNUSED)");
  wait(0);
  snap();
  CHECK(find_proc(pid) == 0 && si.process_count == pbase, "sau wait: tien trinh bien mat khoi danh sach");
  CHECK(nfiles() == base, "so file khong doi");
  int pid2 = fork();
  require(pid2 >= 0, "fork reused proc slot");
  if(pid2 == 0) exit(0);
  wait(0);
  CHECK(pid2 != pid, "tien trinh moi co pid moi (o proc tai su dung nhung pid khong lap)");
}

//  6. dem lap lich
static void t_sched(void) {
  printf("[6] scheduling_count\n");
  snap(); uint64 s0 = find_proc(getpid())->scheduling_count;
  sleep(1); sleep(1);
  snap(); uint64 s1 = find_proc(getpid())->scheduling_count;
  CHECK(s1 > s0, "tang sau khi ngu / duoc danh thuc");
  for(volatile int i = 0; i < 30000000; i++) ;     // chay lau de bi ngat timer
  snap(); uint64 s2 = find_proc(getpid())->scheduling_count;
  CHECK(s2 >= s1, "khong bao gio giam");
  printf("  scheduling_count: "); putu(s0); printf(" -> "); putu(s1); printf(" -> "); putu(s2); printf("\n");
}

//  7. gioi han he thong
static int spawn(int maxn, int nopen, int *pf) {    // fork cac con, moi con mo nopen file roi cho
  int ready[2], pids[NPROC];
  require(pipe(pf) == 0 && pipe(ready) == 0, "spawn pipes");
  int n = 0;
  while(n < maxn && n < NPROC){
    int pid = fork();
    if(pid < 0) break;
    if(pid == 0){
      close(pf[1]);
      close(ready[0]);
      for(int i = 0; i < nopen; i++) open("README", O_RDONLY);
      require(write(ready[1], "r", 1) == 1, "spawn ready");
      close(ready[1]);
      char c; read(pf[0], &c, 1);
      exit(0);
    }
    pids[n++] = pid;
  }
  char c;
  for(int i = 0; i < n; i++)
    require(read(ready[0], &c, 1) == 1, "wait for initialized child");
  // The ready write precedes close(ready[1]); wait for the gate read so all
  // children have finished changing descriptors before consistent().
  for(int i = 0; i < n; i++)
    require(wait_state(pids[i], PS_SLEEPING), "wait for idle limit worker");
  // Keep both ready endpoints allocated through the NFILE snapshot.
  pf[2] = ready[0];
  pf[3] = ready[1];
  close(pf[0]);
  return n;
}
static void reap(int *pf, int n) {
  close(pf[1]); close(pf[2]); close(pf[3]);
  for(int i = 0; i < n; i++) require(wait(0) > 0, "reap limit worker");
}

static void t_limits(void) {
  printf("[7] gioi han: NOFILE / NPROC / NFILE\n");
  int base = nfiles(), fds[NOFILE + 4], n = 0;
  while(n < NOFILE + 4){ int fd = open("README", O_RDONLY); if(fd < 0) break; fds[n++] = fd; }
  snap();
  CHECK(n < NOFILE + 4, "open that bai khi het fd");
  CHECK(find_proc(getpid())->open_file_count == NOFILE, "tien trinh dung het NOFILE fd");
  CHECK(si.open_file_count == base + n, "so file khop so lan open thanh cong");
  CHECK(consistent(), "bat bien khi 1 tien trinh dung het fd");
  for(int i = 0; i < n; i++) close(fds[i]);
  CHECK(nfiles() == base, "dong het -> ve ban dau");

  int pbase = nproc(), pf[4];
  n = spawn(1000, 0, pf);
  snap();
  CHECK(n > 0 && si.process_count == pbase + n, "process_count = ban dau + so con fork duoc");
  CHECK(si.process_count == NPROC, "bang tien trinh day (fork that bai o NPROC)");
  CHECK(consistent(), "bat bien khi bang tien trinh day");
  reap(pf, n);
  CHECK(nproc() == pbase, "don dep xong: ve ban dau");

  base = nfiles();
  n = spawn(10, 10, pf);
  snap();
  CHECK(si.open_file_count == NFILE, "bang file day: open_file_count = NFILE");
  CHECK(consistent(), "bat bien khi bang file day");
  reap(pf, n);
  CHECK(nfiles() == base, "don dep xong: ve ban dau");
}

//  8. con tro khong hop le
static void t_badptr(void) {
  printf("[8] con tro khong hop le\n");
  CHECK(sysinfo((struct sysinfo *)0xffffffffffffffffULL) < 0, "0xfff...f -> -1");
  CHECK(sysinfo((struct sysinfo *)0xfffffffffffffff0ULL) < 0, "dia chi gan tran so (cong len bi quan) -> -1");
  CHECK(sysinfo((struct sysinfo *)(1ULL << 38)) < 0, "dia chi >= MAXVA -> -1");
  CHECK(sysinfo((struct sysinfo *)0x80000000ULL) < 0, "dia chi vung kernel -> -1");
  // Use a disposable heap tail: a failed multi-page copyout may have copied
  // its first bytes already. Never aim such a test at the live user stack.
  char *h = sbrk(PGSIZE);
  require(h != (char *)-1, "allocate invalid-pointer test page");
  char *top = (char *)PGROUNDUP((uint64)sbrk(0));
  CHECK(sysinfo((struct sysinfo *)top) < 0, "dia chi chua map (sau heap) -> -1");
  CHECK(sysinfo((struct sysinfo *)(top - 8)) < 0, "struct vat qua cuoi vung map -> -1");
  require(sbrk(-PGSIZE) != (char *)-1, "free invalid-pointer test page");
  uint64 stack_marker;
  CHECK(sysinfo((struct sysinfo *)(PGROUNDDOWN((uint64)&stack_marker) - PGSIZE)) < 0,
        "stack guard (khong co PTE_U) -> -1");
  CHECK(sysinfo((struct sysinfo *)TRAPFRAME) < 0, "trapframe chi kernel truy cap -> -1");
  CHECK(sysinfo((struct sysinfo *)TRAMPOLINE) < 0, "trampoline chi kernel truy cap -> -1");

  // ELF text is read-only with the normal xv6 toolchain. Isolate these
  // probes so a broken permission check cannot overwrite the parent's code.
  for(int i = 0; i < 2; i++){
    int pid = fork();
    require(pid >= 0, "fork read-only pointer probe");
    if(pid == 0){
      struct sysinfo *dst = i ? (struct sysinfo *)t_badptr : 0;
      exit(sysinfo(dst) == -1 ? 0 : 1);
    }
    int st = -1;
    CHECK(wait(&st) == pid && st == 0, "NULL / read-only text -> -1, con van song");
  }
  int pid = fork();
  require(pid >= 0, "fork invalid-pointer probe");
  if(pid == 0) exit(sysinfo((struct sysinfo *)0x7fffffff00ULL) < 0 ? 0 : 1);
  int st = -1;
  CHECK(wait(&st) == pid && st == 0, "tien trinh con voi con tro xau nhan -1, khong bi giet");
  CHECK(sysinfo(&si) == 0, "kernel van song, goi hop le van thanh cong");
  CHECK(consistent(), "bat bien van dung (khong ro khoa / ro bo nho)");
}

//  9. gioi han du lieu / bien trang
static void t_boundary(void) {
  printf("[9] bien trang, kich thuoc, khong ghi tran, khong ro ri\n");
  printf("  sizeof(struct sysinfo) = %d byte (%d trang)\n",
         (int)sizeof(si), (int)((sizeof(si) + 4095) / 4096));
  uint64 N = sizeof(si) + 3 * 4096;
  char *h = sbrk(N);
  require(h != (char *)-1, "allocate boundary test buffer");
  char *base = (char *)(((uint64)h + 4095) & ~4095ULL);
  char *top = (char *)(((uint64)sbrk(0) + 4095) & ~4095ULL);
  CHECK(sysinfo((struct sysinfo *)base) == 0, "struct bat dau dau trang, trai nhieu trang");
  CHECK(sysinfo((struct sysinfo *)(base + 13)) == 0, "dia chi le (khong can can le)");
  CHECK(sysinfo((struct sysinfo *)(top - sizeof(si))) == 0, "struct vua khit cuoi vung map");
  CHECK(sysinfo((struct sysinfo *)(top - sizeof(si) + 1)) < 0, "lech 1 byte ra ngoai -> -1");

  // khong ghi de ra ngoai [addr, addr + sizeof)
  memset(base, 0xAA, sizeof(si) + 200);
  CHECK(sysinfo((struct sysinfo *)(base + 100)) == 0, "copyout vao giua buffer canary");
  uchar *a = (uchar *)base; int ok = 1;
  for(int i = 0; i < 100; i++) if(a[i] != 0xAA) ok = 0;
  for(uint64 i = 100 + sizeof(si); i < sizeof(si) + 200; i++) if(a[i] != 0xAA) ok = 0;
  CHECK(ok, "khong ghi len du lieu truoc va sau struct");
  sbrk(-(int)N);

  // phan chua dung phai bang 0, khong lo du lieu kernel cu
  memset(&si, 0xAA, sizeof(si));
  snap();
  int clean = 1;
  for(int i = si.open_file_count; i < NFILE; i++)
    if(!is_zero(&si.files[i], sizeof(si.files[i]))) clean = 0;
  for(int i = si.process_count; i < NPROC; i++)
    if(!is_zero(&si.processes[i], sizeof(si.processes[i]))) clean = 0;
  for(int i = 0; i < si.open_file_count; i++){
    struct sysinfo_file *f = &si.files[i];
    if(!is_zero(&f->pids[f->process_count], (NPROC-f->process_count)*sizeof(int))) clean = 0;
  }
  for(int i = 0; i < si.process_count; i++){
    struct sysinfo_process *p = &si.processes[i];
    if(!is_zero(&p->open_files[p->open_file_count],
                (NOFILE-p->open_file_count)*sizeof(struct sysinfo_fd))) clean = 0;
  }
  CHECK(clean, "phan tu ngoai open_file_count / process_count deu bang 0");
  CHECK(si.open_file_count <= NFILE && si.process_count <= NPROC, "so luong nam trong gioi han mang");
  CHECK(consistent(), "bat bien");
}

//  10. nhieu tien trinh goi dong thoi
static void t_concurrent(void) {
  printf("[10] nhieu tien trinh goi sysinfo dong thoi (bo dem kernel dung chung)\n");
  int n = 4, pbase = nproc(), started = 0;
  for(int c = 0; c < n; c++){
    int pid = fork();
    if(pid < 0) break;
    if(pid == 0){
      int ok = 1;
      for(int i = 0; i < 40; i++){
        if(sysinfo(&si) < 0) { ok = 0; break; }
        if(!valid_snapshot() || !find_proc(getpid())) { ok = 0; break; }
      }
      exit(ok ? 0 : 1);
    }
    started++;
  }
  int bad = 0;
  for(int c = 0; c < started; c++){
    int st = -1;
    if(wait(&st) < 0 || st != 0) bad++;
  }
  CHECK(started == n && bad == 0, "4 tien trinh x 40 lan goi: snapshot nao cung hop le, khong deadlock");
  CHECK(nproc() == pbase, "don dep: ve ban dau");
}

//  11. dup/close liên tục trong khi chụp snapshot
static void t_fdrace(void) {
  printf("[11] dup/close lien tuc dong thoi voi sysinfo\n");
  enum { NW = 4, ROUNDS = 2000 };
  int pbase = nproc(), ready[2], pids[NW], nstart = 0;
  require(pipe(ready) == 0, "race ready pipe");
  for(int w = 0; w < NW; w++){
    int pid = fork();
    if(pid < 0) break;
    if(pid == 0){
      close(ready[0]);
      int first = 1;
      for(;;){                        // chạy đến khi bị kill
        int fd = dup(0);
        if(fd < 0 || close(fd) < 0) exit(1);
        if(first){
          if(write(ready[1], "r", 1) != 1) exit(1);
          close(ready[1]);
          first = 0;
        }
      }
    }
    pids[nstart++] = pid;
  }
  close(ready[1]);
  char c;
  int ready_count = 0;
  for(int w = 0; w < nstart; w++)     // đợi mọi worker thật sự bắt đầu
    if(read(ready[0], &c, 1) == 1) ready_count++;

  int bad = 0, snaps = 0;
  for(int i = 0; i < ROUNDS; i++){
    if(sysinfo(&si) < 0){ bad++; break; }
    if(!valid_snapshot()){ bad++; break; }
    snaps++;
    if(si.open_file_count < 0 || si.open_file_count > NFILE) bad++;
    if(si.process_count < 0 || si.process_count > NPROC) { bad++; continue; }
    for(int j = 0; j < si.process_count; j++){
      struct sysinfo_process *p = &si.processes[j];
      if(p->open_file_count < 0 || p->open_file_count > NOFILE){ bad++; continue; }
      for(int k = 0; k < p->open_file_count; k++){
        struct sysinfo_fd *d = &p->open_files[k];
        if(d->fd < 0 || d->fd >= NOFILE || d->file_index < 0 || d->file_index >= NFILE){
          if(bad == 0)
            printf("  sai: snapshot=%d pid=%d fd=%d file_index=%d\n", i, p->pid, d->fd, d->file_index);
          bad++;
        }
      }
    }
  }
  for(int w = 0; w < nstart; w++)
    if(kill(pids[w]) < 0) bad++;
  for(int w = 0; w < nstart; w++){
    int st = 0;
    if(wait(&st) < 0 || st != -1) bad++;
  }
  close(ready[0]);

  CHECK(nstart == NW && ready_count == NW, "fork du 4 worker va nhan du tin hieu ready");
  CHECK(snaps == ROUNDS, "2000 snapshot deu thanh cong");
  CHECK(bad == 0, "moi fd trong [0,NOFILE), moi file_index trong [0,NFILE) khi dang dup/close");
  CHECK(nproc() == pbase, "don dep: ve ban dau");
}

// Exercise both sys_pipe rollback paths touched by descriptor locking.
static void t_pipe_rollback(void) {
  printf("[12] pipe rollback: con tro loi / chi con 1 fd\n");
  int base = nfiles(), self = getpid();
  CHECK(pipe((int *)~0ULL) == -1, "pipe voi con tro loi -> -1");
  CHECK(nfiles() == base && consistent(), "rollback copyout khong ro file / fd");

  char *h = sbrk(PGSIZE);
  require(h != (char *)-1, "allocate pipe boundary page");
  int *edge = (int *)(PGROUNDUP((uint64)sbrk(0)) - sizeof(int));
  CHECK(pipe(edge) == -1, "copyout fd0 thanh cong, fd1 qua cuoi trang -> -1");
  require(sbrk(-PGSIZE) != (char *)-1, "free pipe boundary page");
  CHECK(nfiles() == base && consistent(), "rollback ca hai fd khi copyout fd1 loi");

  snap();
  int before = find_proc(self)->open_file_count;
  int fds[NOFILE], n = 0;
  while(before + n < NOFILE - 1){
    int fd = dup(0);
    require(fd >= 0, "fill descriptor table with dup");
    fds[n++] = fd;
  }
  int pf[2];
  CHECK(pipe(pf) == -1, "chi con 1 fd: fdalloc(rf) duoc, fdalloc(wf) loi");
  snap();
  CHECK(find_proc(self)->open_file_count == NOFILE - 1 && si.open_file_count == base,
        "rollback fd0, khong ro hai file pipe");
  CHECK(consistent(), "bat bien sau rollback fdalloc");
  for(int i = 0; i < n; i++) close(fds[i]);
  CHECK(nfiles() == base && consistent(), "don dep: file va fd ve ban dau");
}

int main(void) {
  printf("===== sysinfotest =====\n");
  t_basic(); t_open(); t_dup(); t_fork_pipe(); t_zombie();
  t_sched(); t_limits(); t_badptr(); t_boundary(); t_concurrent();
  t_fdrace(); t_pipe_rollback();
  printf("===== %d/%d PASS =====\n", total - failed, total);
  exit(failed != 0);
}
