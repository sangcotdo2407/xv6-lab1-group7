#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  char name[] = "stressfs0";
  char data[512];

  for(int i = 0; i < 5; i++){
    name[8] = '0' + i;
    int fd = open(name, 0);
    if(fd < 0)
      goto bad;
    for(int block = 0; block < 20; block++){
      if(read(fd, data, sizeof(data)) != sizeof(data))
        goto bad;
      for(int j = 0; j < sizeof(data); j++)
        if(data[j] != 'a')
          goto bad;
    }
    if(read(fd, data, 1) != 0 || close(fd) != 0 || unlink(name) != 0)
      goto bad;
  }
  printf("STRESSFS_CHECK ALL PASSED: five files, 10240 bytes each\n");
  exit(0);

bad:
  printf("STRESSFS_CHECK FAIL: contents, size or cleanup\n");
  exit(1);
}
