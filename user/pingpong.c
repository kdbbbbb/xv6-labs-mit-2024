#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    int p2c[2]; // pipe: parent to child
    int c2p[2]; // pipe: child to parent
    char buf;//传输的数据缓冲区
    int pid;//保存fork()的返回进程id
    // 创建管道，p2c用于父进程向子进程发送数据，c2p用于子进程向父进程发送数据，如果任意一个失败，输出错误并退出
    if (pipe(p2c) < 0 || pipe(c2p) < 0) {
        fprintf(2, "pipe failed\n");
        exit(1);
    }

    pid = fork();// 创建子进程，fork()返回子进程的pid,pid == 0 表示当前是子进程，pid > 0 是父进程，
    if (pid < 0) {
        fprintf(2, "fork failed\n");
        exit(1);
    }

    if (pid == 0) {
        // 子进程关闭自己不需要用到的管道端，节省资源，避免死锁。
        close(p2c[1]); // 关闭写端
        close(c2p[0]); // 关闭读端

        // 从父管道读一个字节到 buf，如果返回值不是 1，说明读失败（例如父进程没写或出错）。
        if (read(p2c[0], &buf, 1) != 1) {
            fprintf(2, "child read error\n");
            exit(1);
        }
        
        // 输出当前子进程收到的信息
        printf("%d: received ping\n", getpid());

        // 写回父管道
        if (write(c2p[1], &buf, 1) != 1) {
            fprintf(2, "child write error\n");
            exit(1);
        }
        // 关闭用完的管道端口，子进程退出
        close(p2c[0]);
        close(c2p[1]);
        exit(0);

    } else {
        // 父进程
        close(p2c[0]); // 关闭读端
        close(c2p[1]); // 关闭写端

        buf = 'x'; // 任意一个字节

        // 写给子进程
        if (write(p2c[1], &buf, 1) != 1) {
            fprintf(2, "parent write error\n");
            exit(1);
        }

        // 从子进程管道读回
        if (read(c2p[0], &buf, 1) != 1) {
            fprintf(2, "parent read error\n");
            exit(1);
        }

        printf("%d: received pong\n", getpid());

        close(p2c[1]);
        close(c2p[0]);

        wait(0); // 等待子进程退出

        exit(0);
    }
}
