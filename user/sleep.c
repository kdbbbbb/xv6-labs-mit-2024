#include "user/user.h"

int
main(int argc, char *argv[])
{
    // 参数数量检查
    if(argc != 2){
        printf("Usage: sleep ticks\n");
        exit(1);
    }

    // 将字符串参数转换为整数ticks
    int ticks = atoi(argv[1]);

    // 参数合法性检查：ticks 必须是正整数
    if(ticks <= 0){
        printf("Usage: sleep ticks\n");
        exit(1);
    }

    // 调用内核sleep系统调用，暂停ticks个时钟节拍
    sleep(ticks);

    // 结束进程，返回成功状态
    exit(0);
}
