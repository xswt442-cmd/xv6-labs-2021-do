#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define MAXLINE 512

static int
is_space(char c)
{
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void
run_command(char *command, char **fixed, int fixed_count, char *line)
{
  char *args[MAXARG];
  char *p;
  int count;
  int pid;

  for(count = 0; count < fixed_count; count++)
    args[count] = fixed[count];

  p = line;
  while(*p){
    while(is_space(*p))
      p++;
    if(*p == '\0')
      break;
    if(count >= MAXARG - 1){
      fprintf(2, "xargs: too many arguments\n");
      return;
    }
    args[count++] = p;
    while(*p && !is_space(*p))
      p++;
    if(*p)
      *p++ = '\0';
  }
  args[count] = 0;

  pid = fork();
  if(pid < 0){
    fprintf(2, "xargs: fork failed\n");
    return;
  }
  if(pid == 0){
    exec(command, args);
    fprintf(2, "xargs: exec %s failed\n", command);
    exit(1);
  }
  wait(0);
}

int
main(int argc, char *argv[])
{
  char line[MAXLINE];
  char byte;
  int length;
  int overflow;
  int fixed_count;

  if(argc < 2){
    fprintf(2, "usage: xargs command [args ...]\n");
    exit(1);
  }
  fixed_count = argc - 1;
  if(fixed_count >= MAXARG){
    fprintf(2, "xargs: too many fixed arguments\n");
    exit(1);
  }

  length = 0;
  overflow = 0;
  while(read(0, &byte, 1) == 1){
    if(byte == '\n'){
      if(overflow)
        fprintf(2, "xargs: input line too long\n");
      else {
        line[length] = '\0';
        run_command(argv[1], argv + 1, fixed_count, line);
      }
      length = 0;
      overflow = 0;
    } else if(length < MAXLINE - 1) {
      line[length++] = byte;
    } else {
      overflow = 1;
    }
  }
  if(overflow)
    fprintf(2, "xargs: input line too long\n");
  else if(length > 0){
    line[length] = '\0';
    run_command(argv[1], argv + 1, fixed_count, line);
  }
  exit(0);
}
