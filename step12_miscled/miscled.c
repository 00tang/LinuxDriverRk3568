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
#include<linux/uaccess.h>
#include<asm/io.h>
#include<linux/wait.h>
#include<linux/poll.h>
#include<linux/fs.h>
#include<linux/fcntl.h>
#include<linux/platform_device.h>

#define MISCLED_NAME    "miscled"
#define MISCLED_MINOR    236
#define LEDOFF            0 
#define LEDON             1

struct miscled_dev{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    int led_gpio;
    struct mutex lock;
};

struct miscled_dev miscled;

static int led_gpio_init(struct device_node *nd)
{
    int ret;

    miscled.led_gpio = of_get_named_gpio(nd,"miscled-gpio",0);
    if(!gpio_is_valid(miscled.led_gpio))
    {
        printk(KERN_ERR "miscled: failed to get led-gpio\n");
        return -EINVAL;
    }

    ret = gpio_request(miscled.led_gpio,"led");
    if(ret)
    {
        printk(KERN_ERR "miscled: failed request led-gpio\n");
        return ret;
    }

    gpio_direction_output(miscled.led_gpio,0);
    return 0;
}

static int miscled_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &miscled;
    printk(KERN_INFO "device opened\n");
    return 0;
}

static int miscled_release(struct inode *inode, struct file *filp)
{
    printk(KERN_INFO "device closed\n");
    return 0;
}

static ssize_t miscled_read(struct file *filp, char __user *buf, size_t count, loff_t *fpos)
{
    return 0;
}
static ssize_t miscled_write(struct file *filp,const char __user *buf,size_t count,loff_t *fop)
{
    int retvalue;
    unsigned char databuf[1];
   unsigned char ledstat;
    struct miscled_dev *dev = filp->private_data;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    retvalue = copy_from_user(databuf,buf,count);
    if(retvalue != 0)
    {
        printk(KERN_ERR "kernel write failed\n");
        mutex_unlock(&dev->lock);
        return -EFAULT;
    }

    ledstat = databuf[0];

    if(ledstat == LED_ON)
    {
        gpio_set_value(dev->led_gpio,1);
    }
    else if(ledstat == LED_OFF)
    {
        gpio_set_value(dev->led_gpio,0);
    }

    mutex_unlock(&dev->lock);

    return count;
}

static struct file_operations miscleddev_fops = {
    .owner = THIS_MODULE,
    .open = miscled_open,
    .release = miscled_release,
    .read = miscled_read,
    .write = miscled_write,
};

static struct miscdevice led_miscdev = {
    .minor = MISCLED_MINOR,
    .name = MISCLED_NAME,
    .fops = &miscleddev_fops,
};

static int miscled_probe(struct platform_device *pdev)
{
    int ret = 0;
    printk(KERN_INFO "led driver and device was matched\n");

    mutex_init(&miscled.lock);
    ret = led_gpio_init(pdev->dev.of_node);
    if(ret < 0)
    {
        printk(KERN_ERR "led gpio init failed\n");
        return ret;
    }

    ret = misc_register(&led_miscdev);
    if(ret < 0)
    {   
        printk(KERN_ERR "led device register failed\n");
        goto free_gpio;
    }

    return 0;

free_gpio:
    gpio_free(miscled.led_gpio);
    mutex_destroy(&miscled.lock);
    return -EINVAL;
}

static int miscled_remover(struct platform_device *dev)
{
    gpio_set_value(miscled.led_gpio,0);
    gpio_free(miscled.led_gpio);
    misc_deregister(&led_miscdev);
    mutex_destroy(&miscled.lock);  // 可选
    return 0;
}

static const struct of_device_id led_of_match[] = {
    {.compatible = "alientek,miscled"},
    {/* Sentinel*/}
};

MODULE_DEVICE_TABLE(of,led_of_match);

static struct platform_driver led_driver = {
    .driver = {
        .name = "misc-led",
        .of_match_table = led_of_match,
    },

    .probe = miscled_probe,
    .remove = miscled_remover,
};

static int __init leddriver_init(void)
{
    return platform_driver_register(&led_driver);
}

static void  __exit leddriver_exit(void)
{
    platform_driver_unregister(&led_driver);
}

module_init(leddriver_init); 
module_exit(leddriver_exit); 
MODULE_LICENSE("GPL"); 
MODULE_AUTHOR("ALIENTEK"); 
