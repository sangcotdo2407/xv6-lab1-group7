#include "fs.h"
#include "sleeplock.h"
#include "file.h"

extern uint64 sys_dup(void);
extern uint64 sys_close(void);
extern uint64 sys_pipe(void);

static struct {
  uint64 before;
  struct sysinfo info;
  uint64 after;
} pit_buffer;
static struct proc *pit_slots[NPROC];
static struct file *pit_files[NFILE];
static struct file *pit_exit_private;
static struct spinlock pit_gate;
static int pit_ready, pit_go, pit_done, pit_exit_go;

static void
pit_check(int ok, char *message)
{
  if(!ok){
    printf("PROCESS_INFO FAIL: %s\n", message);
    panic("process info test");
  }
}

static int
pit_zero(void *data, int size)
{
  uchar *p = data;
  for(int i = 0; i < size; i++)
    if(p[i] != 0)
      return 0;
  return 1;
}

static void
pit_collect(void)
{
  struct sysinfo *info = &pit_buffer.info;
  pit_check(collect_process_info(info) == 0, "collect succeeds");
  pit_check(pit_buffer.before == 0x123456789abcdef0UL &&
            pit_buffer.after == 0xfedcba9876543210UL, "buffer guards");
  pit_check(info->open_file_count == 12345 &&
            info->files[NFILE-1].ref == 67890, "file portion preserved");
  pit_check(info->process_count > 0 && info->process_count <= NPROC,
            "process bounds");
  for(int i = 0; i < info->process_count; i++){
    struct sysinfo_process *sp = &info->processes[i];
    pit_check(sp->pid > 0 && sp->state > UNUSED && sp->state <= ZOMBIE,
              "pid and state");
    pit_check(sp->open_file_count >= 0 && sp->open_file_count <= NOFILE,
              "descriptor bounds");
    int last = -1;
    for(int j = 0; j < sp->open_file_count; j++){
      struct sysinfo_fd *sf = &sp->open_files[j];
      pit_check(sf->fd > last && sf->fd < NOFILE, "packed descriptor order");
      pit_check(sf->file_index >= 0 && sf->file_index < NFILE,
                "file index bounds");
      last = sf->fd;
    }
    pit_check(pit_zero(&sp->open_files[sp->open_file_count],
                      (NOFILE-sp->open_file_count)*sizeof(struct sysinfo_fd)),
              "unused descriptor entries zero");
  }
  pit_check(pit_zero(&info->processes[info->process_count],
                    (NPROC-info->process_count)*sizeof(struct sysinfo_process)),
            "unused process entries zero");
}

static struct sysinfo_process *
pit_find(int pid)
{
  for(int i = 0; i < pit_buffer.info.process_count; i++)
    if(pit_buffer.info.processes[i].pid == pid)
      return &pit_buffer.info.processes[i];
  return 0;
}

static void
pit_failed_collect(void)
{
  pit_check(collect_process_info(&pit_buffer.info) == -1,
            "invalid table pointer rejected");
  pit_check(pit_buffer.info.process_count == 0 &&
            pit_zero(pit_buffer.info.processes,
                     sizeof(pit_buffer.info.processes)), "failure clears rows");
  pit_check(pit_buffer.info.open_file_count == 12345 &&
            pit_buffer.info.files[NFILE-1].ref == 67890, "failure preserves files");
}

static void
process_info_test_worker(void)
{
  struct proc *p = myproc();
  pit_check(p->scheduling_count == 1, "child first dispatch is one");
  release(&p->lock);

  acquire(&pit_gate);
  pit_ready = 1;
  wakeup(&pit_ready);
  while(!pit_go)
    sleep(&pit_go, &pit_gate);
  release(&pit_gate);

  int fds[NOFILE];
  for(int i = 1; i < NOFILE; i++){
    p->trapframe->a0 = 0;
    fds[i] = sys_dup();
    pit_check(fds[i] == i, "dup fills NOFILE");
  }
  p->trapframe->a0 = 0;
  pit_check(sys_dup() == (uint64)-1, "dup full table");
  pit_check(sys_pipe() == (uint64)-1, "pipe without free descriptor");
  p->trapframe->a0 = fds[NOFILE-1];
  pit_check(sys_close() == 0, "close last descriptor");
  p->trapframe->a0 = 0;
  pit_check(sys_pipe() == (uint64)-1, "pipe with only one free descriptor");
  for(int i = 1; i < NOFILE-1; i++){
    p->trapframe->a0 = fds[i];
    pit_check(sys_close() == 0, "close duplicate");
  }

  for(int i = 0; i < 200; i++){
    p->trapframe->a0 = 0;
    int fd = sys_dup();
    pit_check(fd == 1, "dup shared file");
    p->trapframe->a0 = fd;
    pit_check(sys_close() == 0, "close shared duplicate");
    p->trapframe->a0 = MAXVA;
    pit_check(sys_pipe() == (uint64)-1, "pipe invalid copyout rollback");
    p->trapframe->a0 = 0;
    pit_check(sys_pipe() == 0, "pipe success");
    int pipefds[2];
    pit_check(copyin(p->pagetable, (char *)pipefds, 0, sizeof(pipefds)) == 0,
              "read pipe descriptors");
    for(int j = 0; j < 2; j++){
      p->trapframe->a0 = pipefds[j];
      pit_check(sys_close() == 0, "close pipe");
    }
    yield();
  }

  for(int fd = 1; fd < NOFILE; fd++){
    p->trapframe->a0 = 0;
    pit_check(sys_dup() == fd, "prepare duplicated exit descriptors");
  }
  for(int fd = 0; fd < NOFILE; fd++){
    if(fd == 2 || fd == NOFILE-1)
      continue;
    p->trapframe->a0 = fd;
    pit_check(sys_close() == 0, "prepare sparse exit descriptors");
  }
  pit_exit_private = filealloc();
  pit_check(pit_exit_private != 0, "private exit file allocation");
  pit_exit_private->type = FD_NONE;
  acquire(&p->lock);
  p->ofile[NOFILE/2] = pit_exit_private;
  release(&p->lock);

  acquire(&pit_gate);
  pit_done = 1;
  while(!pit_exit_go)
    sleep(&pit_exit_go, &pit_gate);
  release(&pit_gate);
  exit(37);
}

static void
process_info_selftest(void)
{
  struct proc *self = myproc();
  struct sysinfo_process *sp;
  int selfpid = self->pid;
  printf("PROCESS_INFO BEGIN\n");
  pit_buffer.before = 0x123456789abcdef0UL;
  pit_buffer.after = 0xfedcba9876543210UL;
  pit_buffer.info.open_file_count = 12345;
  pit_buffer.info.files[NFILE-1].ref = 67890;
  pit_check(collect_process_info(0) == -1, "null buffer");
  pit_collect();
  pit_check(pit_buffer.info.process_count == 1, "UNUSED excluded");
  sp = pit_find(selfpid);
  pit_check(sp && sp->parent_pid == 0 && sp->state == RUNNING,
            "init running without parent");
  printf("PROCESS_INFO PASS: null, UNUSED, init, zero padding, file isolation\n");

  for(int i = 0; i < NFILE; i++){
    pit_files[i] = filealloc();
    pit_check(pit_files[i] && file_index_of(pit_files[i]) == i,
              "all NFILE slots allocated in index order");
    pit_files[i]->type = FD_NONE;
  }
  pit_check(filealloc() == 0, "NFILE exhaustion");
  pit_check(file_index_of(0) == -1, "file_index_of rejects NULL");
  uint64 first_file = (uint64)pit_files[0];
  uint64 past_files = (uint64)pit_files[NFILE-1] + sizeof(struct file);
  uint64 invalid_files[] = { first_file-1, first_file+1, first_file+8,
                             past_files-1, past_files, (uint64)-1 };
  for(int i = 0; i < NELEM(invalid_files); i++)
    pit_check(file_index_of((struct file *)invalid_files[i]) == -1,
              "file_index_of rejects out-of-range and misaligned addresses");
  printf("PROCESS_INFO PASS: shared file_index_of validates all slots and invalid pointers\n");

  int n = 0;
  struct proc *p;
  while((p = allocproc()) != 0){
    pit_check(n < NPROC, "test process storage bound");
    pit_check(p->scheduling_count == 0, "allocproc resets counter");
    pit_slots[n++] = p;
    release(&p->lock);
  }
  pit_check(n == NPROC-1, "NPROC exhaustion");
  p = pit_slots[0];
  int oldpid = p->pid;
  acquire(&p->lock);
  p->scheduling_count = (1UL << 40) + 7;
  for(int fd = 0; fd < NOFILE; fd++)
    p->ofile[fd] = filedup(pit_files[fd == NOFILE-1 ? NFILE-1 : fd]);
  release(&p->lock);
  acquire(&wait_lock);
  p->parent = self;
  release(&wait_lock);
  pit_collect();
  pit_check(pit_buffer.info.process_count == NPROC, "all non-UNUSED rows");
  sp = pit_find(oldpid);
  pit_check(sp && sp->parent_pid == selfpid && sp->state == USED &&
            sp->scheduling_count == (1UL << 40)+7, "USED, parent, uint64 count");
  pit_check(sp->open_file_count == NOFILE && sp->open_files[0].file_index == 0 &&
            sp->open_files[NOFILE-1].file_index == NFILE-1,
            "NOFILE and both ends of ftable");

  for(int state = SLEEPING; state <= ZOMBIE; state++){

    if(state == RUNNABLE || state == RUNNING)
      continue;
    acquire(&p->lock);
    p->state = state;
    release(&p->lock);
    pit_collect();
    pit_check(pit_find(oldpid)->state == state, "non-UNUSED states included");
  }

  for(int i = 0; i < NELEM(invalid_files); i++){
    acquire(&p->lock);
    p->ofile[0] = (struct file *)invalid_files[i];
    release(&p->lock);
    pit_failed_collect();
  }
  acquire(&p->lock);
  p->ofile[0] = pit_files[0];
  release(&p->lock);
  acquire(&wait_lock);
  p->parent = (struct proc *)((uint64)proc + 1);
  release(&wait_lock);
  pit_failed_collect();
  acquire(&wait_lock);
  p->parent = &proc[NPROC];
  release(&wait_lock);
  pit_failed_collect();
  acquire(&wait_lock);
  p->parent = self;
  release(&wait_lock);

  acquire(&p->lock);
  memset(p->ofile, 0, sizeof(p->ofile));
  release(&p->lock);
  for(int fd = 0; fd < NOFILE; fd++)
    fileclose(pit_files[fd == NOFILE-1 ? NFILE-1 : fd]);
  for(int i = 0; i < n; i++){
    acquire(&wait_lock);
    acquire(&pit_slots[i]->lock);
    freeproc(pit_slots[i]);
    pit_check(pit_slots[i]->scheduling_count == 0, "freeproc resets counter");
    release(&pit_slots[i]->lock);
    release(&wait_lock);
  }
  p = allocproc();
  pit_check(p == pit_slots[0] && p->pid != oldpid && p->scheduling_count == 0,
            "slot reuse has new pid and fresh counter");
  freeproc(p);
  release(&p->lock);
  for(int i = 0; i < NFILE; i++)
    fileclose(pit_files[i]);
  pit_collect();
  pit_check(pit_buffer.info.process_count == 1, "all synthetic slots freed");
  printf("PROCESS_INFO PASS: NPROC, NOFILE, NFILE, states, uint64, invalid pointers, reuse\n");

  int enabled = intr_get();
  intr_off();
  for(int i = 0; i < 32; i++){
    uint64 count = self->scheduling_count;
    pit_collect();
    pit_check(pit_find(selfpid)->scheduling_count == count,
              "collect does not increment counter");
    yield();
    pit_check(self->scheduling_count == count+1, "one increment per dispatch");
  }
  if(enabled)
    intr_on();
  printf("PROCESS_INFO PASS: 32 exact yield/dispatch counts\n");

  struct file *shared = filealloc();
  pit_check(shared != 0, "shared file allocation");
  shared->type = FD_NONE;
  acquire(&self->lock);
  self->ofile[0] = shared;
  release(&self->lock);
  char saved_name[16];
  safestrcpy(saved_name, self->name, sizeof(saved_name));
  safestrcpy(self->name, "pinfo-worker", sizeof(self->name));
  initlock(&pit_gate, "process test gate");
  int pid = fork();
  safestrcpy(self->name, saved_name, sizeof(self->name));
  pit_check(pid > 0, "real fork");
  acquire(&pit_gate);
  while(!pit_ready)
    sleep(&pit_ready, &pit_gate);
  release(&pit_gate);
  pit_collect();
  sp = pit_find(pid);
  pit_check(sp && sp->state == SLEEPING && sp->parent_pid == selfpid &&
            sp->open_file_count == 1 && sp->open_files[0].fd == 0 &&
            sp->open_files[0].file_index == pit_find(selfpid)->open_files[0].file_index,
            "fork inherits file identity and correct parent");
  uint64 last = sp->scheduling_count;
  for(int i = 0; i < 16; i++){
    yield();
    pit_collect();
    pit_check(pit_find(pid)->scheduling_count == last, "sleep does not count");
  }
  printf("PROCESS_INFO PASS: real fork, shared descriptors, sleeping counter stable\n");
  acquire(&pit_gate);
  pit_go = 1;
  wakeup(&pit_go);
  release(&pit_gate);
  int done = 0;
  int snapshots = 0;
  while(!done || snapshots < 500){
    pit_collect();
    sp = pit_find(pid);
    pit_check(sp && sp->scheduling_count >= last, "concurrent monotonic count");
    last = sp->scheduling_count;
    snapshots++;
    acquire(&pit_gate);
    done = pit_done;
    release(&pit_gate);
    yield();
  }
  pit_collect();
  sp = pit_find(pid);
  int shared_index = file_index_of(shared);
  pit_check(sp && sp->state == SLEEPING && sp->open_file_count == 3 &&
            sp->open_files[0].fd == 2 &&
            sp->open_files[0].file_index == shared_index &&
            sp->open_files[1].fd == NOFILE/2 &&
            sp->open_files[1].file_index == file_index_of(pit_exit_private) &&
            sp->open_files[2].fd == NOFILE-1 &&
            sp->open_files[2].file_index == shared_index,
            "sparse exit snapshot preserves aliased file identity");

  pit_check(shared->ref == 3 && pit_exit_private->ref == 1,
            "references before exit: parent, two aliases, private file");
  acquire(&pit_gate);
  pit_exit_go = 1;
  wakeup(&pit_exit_go);
  release(&pit_gate);
  do {
    yield();
    pit_collect();
    sp = pit_find(pid);
  } while(sp && sp->state != ZOMBIE);
  pit_check(sp && sp->open_file_count == 0 && sp->parent_pid == selfpid,
            "exit closes files and leaves zombie");

  pit_check(shared->ref == 1 && pit_exit_private->ref == 0,
            "exit drops each FD reference once and frees the private file");
  printf("PROCESS_INFO PASS: sparse exit, aliased FDs, exact reference cleanup\n");
  last = sp->scheduling_count;
  yield();
  pit_collect();
  pit_check(pit_find(pid)->scheduling_count == last, "zombie count stable");
  pit_check(wait(0) == pid, "wait reaps worker");
  pit_collect();
  pit_check(pit_find(pid) == 0, "reaped process excluded");
  acquire(&self->lock);
  self->ofile[0] = 0;
  release(&self->lock);
  fileclose(shared);
  printf("PROCESS_INFO PASS: concurrent dup/close/pipe, exit, zombie, wait (%d snapshots)\n",
         snapshots);
  printf("PROCESS_INFO ALL PASSED\n");
}
