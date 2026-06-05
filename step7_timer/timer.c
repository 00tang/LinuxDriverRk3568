#include<linux/types.h>
#include<linux/kernel.h>
#include<linux/delay.h>
#include<linux/module.h>
#include<linux/ide.h>
#include<linux/init.h>
#include<linux/module.h>
#include<linux/errno.h>
#include<linux/gpio.h>
#include<linux/cdev.h>
#include<linux/device.h>
#include<linux/of.h>
#include<linux/of_address.h>
#include<linux/of_gpio.h>
#include<linux/semaphore.h>
#include<linux/timer.h>
// #include<asm/mach/map.h>
#include<asm/uaccess.h>
#include<asm/io.h>

#define TIMER_CNT              1
#define TIMER_NAME          "timer"
#define CLASS_NAME         "timer_class"
#define CLOSE_CMD           (_IO(0XEF,0X1))
#define OPEN_CMD            (_IO(0XEF,0X2))
#define SETPERIOD_CMD       (_IO(0XEF,0X3))
#define LEDON                  1
#define LEDOFF                 0


struct timer_dev
{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    int major;
    int minor;
    struct device_node *nd;
    int led_gpio;
    int  timeperiod;
    struct timer_list timer; 
    spinlock_t lock;
};

struct timer_dev timerdev;

static int led_init(void)
{
    int ret;
    const char *str;

    timerdev.nd = of_find_node_by_path("/gpioled");
    if(timerdev.nd == NULL)
    {
        printk(KERN_ERR "timerdev node not find\n");
        return -EINVAL;
    }

    ret = of_property_read_string(timerdev.nd,"status",&str);
    if(ret < 0)
    {
        printk(KERN_ERR "timerdev status not find\n");
        return -EINVAL;
    }

    if(strcmp(str,"okay"))
    {
        printk(KERN_ERR "timerdev status not okay\n");
        return -EINVAL;
    }

    ret = of_property_read_string(timerdev.nd,"compatible",&str);
    if(ret < 0)
    {
        printk(KERN_ERR "timerdev compatible match failed\n");
        return -EINVAL;
    }

    if(strcmp(str,"alientek,led"))
    {
        printk(KERN_ERR "tcompatible match failed\n");
        return -EINVAL;
    }

    timerdev.led_gpio = of_get_named_gpio(timerdev.nd,"led-gpio",0);
    if(timerdev.led_gpio < 0)
    {
        printk(KERN_ERR "timerdev.led_gpio can't get gpio\n");
        return -EINVAL;
    }
    printk(KERN_INFO "led-gpio num = %d\n",timerdev.led_gpio);

    ret = gpio_request(timerdev.led_gpio,"led");
    if(ret)
    {
        printk(KERN_ERR "gpio_request Failed\n");
        return ret;
    }

    ret = gpio_direction_output(timerdev.led_gpio,0);
    if(ret < 0)
    {
        printk(KERN_ERR "can't set gpio\n");
        return ret;
    }

    return 0;
}

static int timer_open(struct inode *inode,struct file *filp)
{
    filp->private_data = &timerdev;

    timerdev.timeperiod = 1000;
    printk(KERN_INFO "timer device open success\n");

    return 0;
}

static int led_release(struct inode *inode, struct file *filp)
{
    printk(KERN_INFO "timer device closed success\n");

    return 0;
}

static long timer_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) 
{
    struct timer_dev *dev = (struct timer_dev *)filp->private_data;
    int timerperiod;
    unsigned long flags;

    switch (cmd)
    {
    case CLOSE_CMD:
        /* close timer */
        del_timer_sync(&dev->timer);
        break;
    
    case OPEN_CMD:
        /*open timer*/
        spin_lock_irqsave(&dev->lock,flags);
        timerperiod = dev->timeperiod;
        spin_unlock_irqrestore(&dev->lock,flags);
        mod_timer(&dev->timer,jiffies + msecs_to_jiffies(timerperiod));
        break;

    case SETPERIOD_CMD: /* 设置定时器周期 */ 
        spin_lock_irqsave(&dev->lock, flags); 
        dev->timeperiod = arg; 
        spin_unlock_irqrestore(&dev->lock, flags); 
        mod_timer(&dev->timer, jiffies + msecs_to_jiffies(arg));
        break; 

    default:
        break;
    }
    return 0;
}

static struct file_operations timer_fpos = {
    .owner = THIS_MODULE,
    .open = timer_open,
    .unlocked_ioctl = timer_unlocked_ioctl,
    .release = led_release,
};

void timer_function(struct timer_list *arg) 
{ 
/* from_timer 是个宏，可以根据结构体的成员地址，获取到这个结构体的首地址。 200 第一个参数表示结构体，第二个参数表示第一个参数里的一个成员，第三个参数表 示第二个参数的类型,得到第一个参数的首地址。 
*/ 
    struct timer_dev *dev = from_timer(dev, arg, timer); 
    static int sta = 1; 
    int timerperiod; 
    unsigned long flags; 

    sta = !sta; /* 每次都取反，实现 LED 灯反转 */ 
    gpio_set_value(dev->led_gpio, sta); 

    /* 重启定时器 */ 
    spin_lock_irqsave(&dev->lock, flags); 
    timerperiod = dev->timeperiod; 
    spin_unlock_irqrestore(&dev->lock, flags); 
    mod_timer(&dev->timer, jiffies + msecs_to_jiffies(timerperiod)); 
 } 

static int __init timer_driver_init(void)
{
    int ret = 0;

    ret = led_init();
    if (ret < 0) 
    {
        printk(KERN_ERR "led_init failed\n");
        return ret;
    }

    spin_lock_init(&timerdev.lock);


    // 1. 分配设备号
    ret = alloc_chrdev_region(&timerdev.devid, 0, TIMER_CNT, TIMER_NAME);
    if (ret < 0) 
    {
        printk(KERN_ERR "alloc_chrdev_region faikey\n");
        goto err_gpio_free;
    }
    timerdev.major = MAJOR(timerdev.devid);
    printk(KERN_INFO "timerdev-gpio major = %d\n", timerdev.major );

    // 2. 初始化 cdev
    cdev_init(&timerdev.cdev, &timer_fpos);
    timerdev.cdev.owner = THIS_MODULE;

    // 3. 注册 cdev
    ret = cdev_add(&timerdev.cdev, timerdev.devid, 1);
    if (ret < 0) {
        printk(KERN_ERR "cdev_add faikey\n");
        goto err_unregister_region;
    }

    // 4. 自动创建设备节点（核心新增内容）
    timerdev.class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(timerdev.class)) {
        printk(KERN_ERR "class_create failed\n");
        ret = PTR_ERR(timerdev.class);
        goto err_cdev_del;
    }

    timerdev.device = device_create(timerdev.class, NULL, timerdev.devid, NULL,TIMER_NAME);
    if (IS_ERR(timerdev.device)) {
        printk(KERN_ERR "device_create failed\n");
        ret = PTR_ERR(timerdev.device);
        goto err_class_destroy;
    }

    timer_setup(&timerdev.timer, timer_function, 0);
    printk(KERN_INFO "timer driver loaded, /dev/%s created\n", TIMER_NAME);
    return 0;

    err_class_destroy:
    class_destroy(timerdev.class);
    err_cdev_del:
    cdev_del(&timerdev.cdev);
    err_unregister_region:
    unregister_chrdev_region(timerdev.devid, TIMER_CNT);
    err_gpio_free:
    gpio_free(timerdev.led_gpio);
    return ret;

    
}

static void __exit timer_driver_exit(void) 
{ 
    /* 注销字符设备驱动 */ 
    device_destroy(timerdev.class, timerdev.devid);  // 1. 先销毁设备
    class_destroy(timerdev.class);                  // 2. 再销毁类
    cdev_del(&timerdev.cdev);                       // 3. 删除 cdev
    unregister_chrdev_region(timerdev.devid, TIMER_CNT); // 4. 释放设备号
    gpio_free(timerdev.led_gpio);                       // 5. 释放 GPIO
    of_node_put(timerdev.nd);                           // 6. 释放节点引用
} 


module_init(timer_driver_init); 
module_exit(timer_driver_exit); 
MODULE_LICENSE("GPL"); 
MODULE_AUTHOR("ALIENTEK"); 




