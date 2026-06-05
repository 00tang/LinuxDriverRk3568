#include "stdio.h" 
#include "unistd.h" 
#include "sys/types.h" 
#include "sys/stat.h" 
#include "fcntl.h" 
#include "stdlib.h" 
#include "string.h" 
 
 #define KEY0VALUE      0XFF
 #define INVAKEY        0X00
 
 /* 
 * @description : main 主程序 
 * @param - argc : argv 数组元素个数 
 * @param - argv : 具体参数 
 * @return : 0 成功;其他 失败 
 */ 
 int main(int argc, char *argv[]) 
 { 
    int fd, ret; 
    char *filename; 
    int keyvalue; 
 
    if(argc != 2)
    { 
        printf("Error Usage!\r\n"); 
        return -1; 
    } 
 
    filename = argv[1]; 
 
    /* 打开 key 驱动 */ 
    fd = open(filename, O_RDWR); 
    if(fd < 0)
    { 
        printf("file %s open failed!\r\n", argv[1]); 
        return -1; 
    } 
 
 /* 循环读取按键值数据！ */ 
    while(1) 
    { 
        read(fd, &keyvalue, sizeof(keyvalue)); 
        printf("keyvalue = %#X\n", keyvalue);  // 不管什么值都打印
        sleep(500);  // 加个延时，避免刷屏
    }
 
    ret= close(fd); /* 关闭文件 */ 
    if(ret < 0)
    { 
        printf("file %s close failed!\r\n", argv[1]);  
        return -1; 
    } 
    return 0; 
 } 
