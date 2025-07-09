#include "kernel/types.h"
#include "user/user.h"
//表示这个函数不会返回,用它的进程要么递归下去，要么 exit
__attribute__((noreturn))
void primes(int pfd_read) {
  int n, prime;
  int pfd_next[2];
  // 读取第一个素数，如果读不到（上一个进程没数据了，就退出）
  if (read(pfd_read, &prime, sizeof(int)) != sizeof(int)) {
    close(pfd_read);
    exit(0);
  }

  printf("prime %d\n", prime);
// 创建一个新的管道，用于传递下一个素数
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
    close(pfd_next[1]);// 子进程只需要读端
    close(pfd_read);//不再需要当前读端
    primes(pfd_next[0]);//递归调用
  } else {// 父进程筛选当前素数的倍数，转发到其他数字到下一层管道
    close(pfd_next[0]);
    while (read(pfd_read, &n, sizeof(int)) == sizeof(int)) {
      if (n % prime != 0)
        write(pfd_next[1], &n, sizeof(int));
    }
    close(pfd_read);
    close(pfd_next[1]);
    wait(0);//等待子进程结束
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
