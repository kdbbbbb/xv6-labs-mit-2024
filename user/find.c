#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

//argv[1]要搜索的起始目录，argv[2]目标文件名，
// 核心函数，用于递归查找路径下所有文件或目录，打印出名字和 target 相同的完整路径
void find(char *path, char *target) { 
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;
  //open(path, 0)
  //如果打开失败，说明路径无效或无权限，直接返回。
  if ((fd = open(path, 0)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }
  //fstat(fd, &st)：将打开的文件描述符 fd 的状态信息填充到 st 中
  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  if (st.type == T_FILE) {//处理普通文件
    // 文件：直接比较末尾文件名
    char *last = path + strlen(path);
    while (last >= path && *last != '/') last--;
    last++; // now points to file name
    if (strcmp(last, target) == 0) {
      printf("%s\n", path);
    }
  } else if (st.type == T_DIR) {
    // 目录：递归处理子项
    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {//拼接子文件名时，必须保证不会超过 buf[512] 的容量
      fprintf(2, "find: path too long\n");
      close(fd);
      return;
    }

    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';
   //不断读取目录项 de（struct dirent）直到读完
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        //忽略空或特殊目录项
      if (de.inum == 0) continue;//表示这个目录项没有被使用，跳过。
      if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) continue;//"." 和 ".." 是当前目录和父目录，跳过避免无限递归。

      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;//字符串尾0

      // 递归调用子路径
      find(buf, target);
    }
  }

  close(fd);
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(2, "Usage: find <path> <target>\n");
    exit(1);
  }
  find(argv[1], argv[2]);
  exit(0);
}
