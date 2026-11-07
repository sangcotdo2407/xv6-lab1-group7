#include "kernel/types.h"
#include "kernel/sysinfo.h"
#include "user/user.h"

static struct sysinfo info;

int
main(void)
{
  // The temporary kernel fails each collector once, in this order.
  for(int i = 0; i < 2; i++){
    memset(&info, 0xa5, sizeof(info));
    if(sysinfo(&info) != -1){
      printf("SYSINFO_FAULT FAIL: collector error lost\n");
      exit(1);
    }
    uchar *bytes = (uchar *)&info;
    for(int j = 0; j < sizeof(info); j++){
      if(bytes[j] != 0xa5){
        printf("SYSINFO_FAULT FAIL: copyout after collector error\n");
        exit(1);
      }
    }
  }
  for(int i = 0; i < 100; i++){
    if(sysinfo(&info) != 0 || info.process_count < 2 || info.open_file_count < 1){
      printf("SYSINFO_FAULT FAIL: recovery or sleeplock leak\n");
      exit(1);
    }
  }
  printf("SYSINFO_FAULT ALL PASSED: two errors, no copyout, 100 recovery calls\n");
  exit(0);
}
