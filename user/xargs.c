#include "kernel/types.h"
#include "user/user.h"
#include "kernel/param.h"

int
main(int argc, char *argv[])
{
  char buf[512];            // 临时读取缓冲区
  char *child_argv[MAXARG]; // exec 的参数列表
  int i;

  // 拷贝原始命令参数到 child_argv
  for (i = 1; i < argc; i++) {
    child_argv[i - 1] = argv[i];
  }

  int n = 0; // 当前读取的字符数
  char ch;
  while (read(0, &ch, 1) == 1) {
    if (ch == '\n') {
      buf[n] = 0; // 构成一行完整的参数

      // 设置 exec 参数列表
      child_argv[argc - 1] = buf;
      child_argv[argc] = 0;

      if (fork() == 0) {
        exec(child_argv[0], child_argv);
        fprintf(2, "exec failed\n");
        exit(1);
      } else {
        wait(0); // 等待子进程完成
      }

      n = 0; // 重新开始读取下一行
    } else {
      buf[n++] = ch;
    }
  }

  exit(0);
}
