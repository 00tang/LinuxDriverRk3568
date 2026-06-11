#include "stdio.h" 
#include "unistd.h" 
#include "sys/types.h" 
#include "sys/stat.h" 
#include "fcntl.h" 
#include "stdlib.h" 
#include "string.h" 
#include <linux/input.h>

int main(int argc,char *argv[])
{
    int fd,ret;
    struct input_event ev;

    if(argc != 2){ 
        printf("Error Usage!\r\n"); 
        return -1; 
    } 

    fd = open(argv[1],O_RDWR);
    if(0 > fd)
    {
        printf("file %s open failed!\r\n", argv[1]); 
        return -1; 
    }

    while(1)
    {
        ret = read(fd, &ev, sizeof(struct input_event));
        if (ret == sizeof(struct input_event)) 
        {
            printf("type=%d code=%d value=%d\n", ev.type, ev.code, ev.value);
            if (ev.type == EV_KEY && ev.code == KEY_0) 
            {
                if (ev.value)
                    printf("Key0 Press\n");
                else
                    printf("Key0 Release\n");
            }
        }
    }
    close(fd); 
    return 0;
}