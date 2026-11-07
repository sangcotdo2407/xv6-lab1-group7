#define main sysinfotest_main
#include "user/sysinfotest.c"
#undef main

static char captured[32];
static int used;

int
write(int fd, const void *data, int size)
{
  if(used+size >= sizeof(captured))
    __builtin_abort();
  const char *p = data;
  for(int i = 0; i < size; i++)
    captured[used++] = p[i];
  captured[used] = 0;
  return size;
}

int
main(void)
{
  uint64 values[] = {0, 2147483648UL, 4294967296UL, ~0UL};
  char *expected[] = {"0", "2147483648", "4294967296", "18446744073709551615"};
  for(int i = 0; i < 4; i++){
    used = 0;
    putu(values[i]);
    int j = 0;
    while(captured[j] && captured[j] == expected[i][j])
      j++;
    if(captured[j] != expected[i][j]){
      __builtin_printf("SYSINFO_PRINT_NATIVE FAILED\n");
      return 1;
    }
  }
  __builtin_printf("SYSINFO_PRINT_NATIVE ALL PASSED\n");
  return 0;
}
