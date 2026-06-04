#include "stdio.h" 
#include "unistd.h" 
#include "sys/types.h" 
#include "sys/stat.h" 
#include "fcntl.h" 
#include "stdlib.h" 
#include "string.h" 
 
int main(int argc, char *argv[]) 
{ 
    int fd, ret; 
    int key_val;

    if (argc != 2)
    { 
        printf("Error Usage!\r\n"); 
        return -1; 
    } 

    fd = open(argv[1], O_RDWR); 
    if (fd < 0) 
    { 
        printf("Can't open file %s\r\n", argv[1]); 
        return -1;  
    } 

    while(1)
    {
        read(fd,&key_val,sizeof(int));

        if(0 == key_val)
            printf("key press\n");
        else if(key_val == 1)
            printf("key release\n");
    }

    close(fd);
    return 0;
 } 
