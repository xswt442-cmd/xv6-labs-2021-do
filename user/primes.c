#include "kernel/types.h"
#include "user/user.h"

static void sieve(int input);
static void (*next_stage)(int) = sieve;

static void
sieve(int input)
{
  int prime;
  int number;
  int next[2];
  int pid;

  if(read(input, &prime, sizeof(prime)) != sizeof(prime)){
    close(input);
    exit(0);
  }
  printf("prime %d\n", prime);

  if(pipe(next) < 0){
    fprintf(2, "primes: pipe failed\n");
    close(input);
    exit(1);
  }
  pid = fork();
  if(pid < 0){
    fprintf(2, "primes: fork failed\n");
    close(input);
    close(next[0]);
    close(next[1]);
    exit(1);
  }
  if(pid == 0){
    close(input);
    close(next[1]);
    next_stage(next[0]);
  }

  close(next[0]);
  while(read(input, &number, sizeof(number)) == sizeof(number)){
    if(number % prime != 0)
      write(next[1], &number, sizeof(number));
  }
  close(input);
  close(next[1]);
  wait(0);
  exit(0);
}

int
main(int argc, char *argv[])
{
  int numbers[2];
  int pid;
  int number;

  if(pipe(numbers) < 0){
    fprintf(2, "primes: pipe failed\n");
    exit(1);
  }
  pid = fork();
  if(pid < 0){
    fprintf(2, "primes: fork failed\n");
    close(numbers[0]);
    close(numbers[1]);
    exit(1);
  }
  if(pid == 0){
    close(numbers[1]);
    sieve(numbers[0]);
  }

  close(numbers[0]);
  for(number = 2; number <= 35; number++)
    write(numbers[1], &number, sizeof(number));
  close(numbers[1]);
  wait(0);
  exit(0);
}
