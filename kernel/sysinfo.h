#ifndef XV6_SYSINFO_H
#define XV6_SYSINFO_H

#include "types.h"
#include "param.h"

struct sysinfo_file {
  int file_index;
  int type;
  int readable;
  int writable;
  int ref;
  int process_count;
  int pids[NPROC];
};

struct sysinfo_fd {
  int fd;
  int file_index;
};

struct sysinfo_process {
  int pid;
  int state;
  int parent_pid;
  uint64 scheduling_count;
  int open_file_count;
  struct sysinfo_fd open_files[NOFILE];
};

struct sysinfo {
  int open_file_count;
  struct sysinfo_file files[NFILE];
  int process_count;
  struct sysinfo_process processes[NPROC];
};

#endif
