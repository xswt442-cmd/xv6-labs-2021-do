#include "kernel/types.h"
#include "user/user.h"

static int
is_number(char *s)
{
  if(*s == '\0')
    return 0;
  while(*s){
    if(*s < '0' || *s > '9')
      return 0;
    s++;
  }
  return 1;
}

int
main(int argc, char *argv[])
{
  if(argc != 2 || !is_number(argv[1])){
    fprintf(2, "usage: sleep ticks\n");
    exit(1);
  }

  sleep(atoi(argv[1]));
  exit(0);
}
