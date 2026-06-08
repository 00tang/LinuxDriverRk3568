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
#include<linux/irq.h>
#include<asm/uaccess.h>
#include<asm/io.h>
#include<linux/wait.h>
#include<linux/poll.h>
#include<linux/fs.h>
#include<linux/fcntl.h>
#include<linux/platform_device.h>
#include<asm/mach/map.h>

#define LEDDEV_CNT         1
#define LEDDEV_NAME    "dtsplatled"
#define CLASS_NAME     "dtsplatled-class"
#define LED_OFF             0
#define LED_ON              1

struct leddev_dev
{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    int major;
    int minor;
    struct device_node *node;
    int gpio_led;
    struct mutex lock;
};
struct leddev_dev leddev;

// static void led_switch(u8 sta)
// {
//     if(sta == LED_ON)
//     {
//         gpio_set_value(leddev.gpio_led,LED_ON);
//     }
//     else if(sta == LED_OFF)
//     {
//          gpio_set_value(leddev.gpio_led,LED_OFF);
//     }
// }

static int  gpio_led_init(struct device_node *nd)
{
    int ret;
    leddev.gpio_led = of_get_named_gpio(nd,"led-gpio",0);
    if(!gpio_is_valid(leddev.gpio_led))
    {
        printk(KERN_ERR "leddev: Failed to get led-gpio\n");
        return -EINVAL;
    }

    ret = gpio_request(leddev.gpio_led,"LED");
    if (ret)
    {
        /* code */
         printk(KERN_ERR "leddev: Failed to request led-gpio\n");
         return ret;
    }

    gpio_direction_output(leddev.gpio_led,LED_OFF);

    return 0;
    
}

static int led_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &leddev;
    printk(KERN_INFO "device opened\n");
    return 0;
}

static int led_release(struct inode *inode, struct file *filp)
{
    printk(KERN_INFO "device closed\n");
    return 0;
}

static ssize_t led_read(struct file *filp, char __user *buf, size_t count, loff_t *fpos)
{
    return 0;
}

static ssize_t led_write(struct file *filp, const char __user *buf,size_t count, loff_t *fpos)
{
    int retvalue;
    unsigned char databuf[1];
    unsigned char ledstat;
    struct leddev_dev *dev = filp->private_data;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    retvalue = copy_from_user(databuf,buf,count);
    if(retvalue != 0)
    {
        printk(KERN_ERR "kernel write failed\n");
        return -EFAULT;
    }

    ledstat = databuf[0];

    if(ledstat == LED_ON)
    {
        gpio_set_value(dev->gpio_led,1);
    }
    else if(ledstat == LED_OFF)
    {
        gpio_set_value(dev->gpio_led,0);
    }

    mutex_unlock(&dev->lock);

    return count;
}

static struct file_operations leddev_fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .release = led_release,
    .read = led_read,
    .write = led_write,
};

static int led_probe(struct platform_device *pdev)
{
    int ret = 0;
    // leddev.node = pdev->dev.of_node;
    printk(KERN_INFO "LED driver and device was matched \n");

   ret = gpio_led_init(pdev->dev.of_node);
   if(ret < 0)
   {
        printk(KERN_INFO "LED driver init Failed\n");
        return ret; 
   }
    mutex_init(&leddev.lock);
    // 1. 分配设备号
    ret = alloc_chrdev_region(&leddev.devid, 0, LEDDEV_CNT, LEDDEV_NAME);
    if (ret < 0) 
    {
        printk(KERN_ERR "alloc_chrdev_region failed\n");
        goto err_gpio_free;
    }
    leddev.major = MAJOR(leddev.devid);
    printk(KERN_INFO "led-gpio major = %d\n", leddev.major );

    // 2. 初始化 cdev
    cdev_init(&leddev.cdev, &leddev_fops);
    leddev.cdev.owner = THIS_MODULE;

    // 3. 注册 cdev
    ret = cdev_add(&leddev.cdev, leddev.devid, 1);
    if (ret < 0) {
        printk(KERN_ERR "cdev_add failed\n");
        goto err_unregister_region;
    }

    // 4. 自动创建设备节点（核心新增内容）
    leddev.class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(leddev.class)) {
        printk(KERN_ERR "class_create failed\n");
        ret = PTR_ERR(leddev.class);
        goto err_cdev_del;
    }

    leddev.device = device_create(leddev.class, NULL, leddev.devid, NULL, LEDDEV_NAME);
    if (IS_ERR(leddev.device)) {
        printk(KERN_ERR "device_create failed\n");
        ret = PTR_ERR(leddev.device);
        goto err_class_destroy;
    }

    printk(KERN_INFO "led driver loaded, /dev/%s created\n", LEDDEV_NAME);
    return 0;

    err_class_destroy:
    class_destroy(leddev.class);
    err_cdev_del:
    cdev_del(&leddev.cdev);
    err_unregister_region:
    unregister_chrdev_region(leddev.devid, LEDDEV_CNT);
    err_gpio_free:
    gpio_free(leddev.gpio_led);
    // err_node_put:
    // of_node_put(leddev.node);  // 释放设备树节点引用
    return ret;

    
}

static int  led_remove(struct platform_device *dev) 
{ 
    /* 注销字符设备驱动 */ 
    gpio_set_value(leddev.gpio_led,0);
    device_destroy(leddev.class, leddev.devid);  // 1. 先销毁设备
    class_destroy(leddev.class);                  // 2. 再销毁类
    cdev_del(&leddev.cdev);                       // 3. 删除 cdev
    unregister_chrdev_region(leddev.devid, LEDDEV_CNT); // 4. 释放设备号
    gpio_free(leddev.gpio_led);                       // 5. 释放 GPIO
    // of_node_put(leddev.node);                           // 6. 释放节点引用

    return 0;
} 


static const struct of_device_id led_of_match[] = {
    {.compatible = "alientek,led"},
    {/* Sentinel*/}
};

MODULE_DEVICE_TABLE(of,led_of_match);

static struct platform_driver led_driver = {
    .driver = {
        .name = "rk3568-led",
        .of_match_table = led_of_match,
    },

    .probe = led_probe,
    .remove = led_remove,
};

static int __init leddriver_init(void)
{
    return platform_driver_register(&led_driver);
}

static void __exit leddriver_exit(void)
{
    return platform_driver_unregister(&led_driver);
}

module_init(leddriver_init); 
module_exit(leddriver_exit); 
MODULE_LICENSE("GPL"); 
MODULE_AUTHOR("ALIENTEK"); 




