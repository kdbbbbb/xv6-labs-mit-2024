#include "kernel/types.h"
#include "user/user.h"
__attribute__((noreturn))
void primes(int pfd_read) {
  int n, prime;
  int pfd_next[2];

  if (read(pfd_read, &prime, sizeof(int)) != sizeof(int)) {
    close(pfd_read);
    exit(0);
  }

  printf("prime %d\n", prime);

  if (pipe(pfd_next) < 0) {
    fprintf(2, "pipe failed\n");
    exit(1);
  }

  int pid = fork();
  if (pid < 0) {
    fprintf(2, "fork failed\n");
    exit(1);
  }

  if (pid == 0) {
    close(pfd_next[1]);
    close(pfd_read);
    primes(pfd_next[0]);
  } else {
    close(pfd_next[0]);
    while (read(pfd_read, &n, sizeof(int)) == sizeof(int)) {
      if (n % prime != 0)
        write(pfd_next[1], &n, sizeof(int));
    }
    close(pfd_read);
    close(pfd_next[1]);
    wait(0);
    exit(0);
  }
}

int main() {
  int pfd[2];
  pipe(pfd);

  int pid = fork();
  if (pid < 0) {
    fprintf(2, "fork failed\n");
    exit(1);
  }

  if (pid == 0) {
    close(pfd[1]);
    primes(pfd[0]);
  } else {
    close(pfd[0]);
    for (int i = 2; i <= 280; i++)
      write(pfd[1], &i, sizeof(int));
    close(pfd[1]);
    wait(0);
  }

  exit(0);
}
