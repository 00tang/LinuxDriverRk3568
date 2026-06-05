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
// #include<asm/mach/map.h>
#include<asm/uaccess.h>
#include<asm/io.h>

#define GPIOLED_CNT         1
#define GPIOLED_NAME    "gpioled"
#define CLASS_NAME      "gpioled-class"
#define LED_OFF             0
#define LED_ON              1

struct gpioled_dev
{
    dev_t devid;
    struct cdev led_cdev;
    struct class *led_class;
    struct device *led_device;
    int major;
    int minor;
    struct device_node *nd;
    int led_gpio;
    atomic_t lock;
};
struct gpioled_dev gpioled;

static int led_open(struct inode *inode, struct file *filp)
{
    if(!atomic_dec_and_test(&gpioled.lock))
    {
        atomic_inc(&gpioled.lock);

        return -EBUSY;
    }

    filp->private_data = &gpioled;
    printk(KERN_INFO "led_device opened\n");
    return 0;
}

static int led_release(struct inode *inode, struct file *filp)
{
    struct gpioled_dev *dev = filp->private_data;

    atomic_inc(&dev->lock);


    printk(KERN_INFO "led_device closed\n");
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
    struct gpioled_dev *dev = filp->private_data;

    retvalue = copy_from_user(databuf,buf,count);
    if(retvalue < 0)
    {
        printk(KERN_ERR "kernel write failed\n");
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
    return 0;
}

static struct file_operations gpioled_fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .release = led_release,
    .read = led_read,
    .write = led_write,
};

static int __init led_init(void)
{
    int ret = 0;
    const char *str;

    gpioled.lock = (atomic_t)ATOMIC_INIT(0);

    atomic_set(&gpioled.lock,1);

    gpioled.nd = of_find_node_by_path("/gpioled");
    if(gpioled.nd == NULL)
    {
        printk(KERN_ERR "gpioled node not finded \n");
        return -EINVAL;
    }

    ret = of_property_read_string(gpioled.nd,"status",&str);
    if(ret < 0)
    {
        printk(KERN_ERR "Read status failed\n");
        goto err_node_put;
    }

    if(strcmp(str,"okay"))
    {
        printk(KERN_ERR "status is not okay\n");
        ret = -EINVAL;
        goto err_node_put;
    }

    ret = of_property_read_string(gpioled.nd,"compatible",&str);
    if(ret < 0)
    {
        printk(KERN_ERR "gpioled: Failed to get compatible property\n");
        goto err_node_put;
    }

    if(strcmp(str,"alientek,led"))
    {
        printk("gpioled: Compatible match failed\n");
        ret = -EINVAL;
        goto err_node_put;
    }

    //get gpio compatible from devicetree
    gpioled.led_gpio = of_get_named_gpio(gpioled.nd,"led-gpio",0);
    if(gpioled.led_gpio < 0)
    {
        printk(KERN_ERR "can't get led-gpio");
        ret = -EINVAL;
        goto err_node_put;
    }
    printk(KERN_INFO " led-gpio num = %d \n",gpioled.led_gpio);

    ret = gpio_request(gpioled.led_gpio,"LED-GPIO");
    if (ret) 
    {  
        printk(KERN_ERR "gpioled: Failed to request led-gpio\n"); 
        goto err_node_put;
    } 

    ret = gpio_direction_output(gpioled.led_gpio,0);
    if(ret < 0)
    {
        printk(KERN_ERR " can't set led-gpio\n");
         goto err_gpio_free;
    }

    // 1. 分配设备号
    ret = alloc_chrdev_region(&gpioled.devid, 0, GPIOLED_CNT, GPIOLED_NAME);
    if (ret < 0) 
    {
        printk(KERN_ERR "alloc_chrdev_region failed\n");
        goto err_gpio_free;
    }
    gpioled.major = MAJOR(gpioled.devid);
    printk(KERN_INFO "led-gpio major = %d\n", gpioled.major );

    // 2. 初始化 cdev
    cdev_init(&gpioled.led_cdev, &gpioled_fops);
    gpioled.led_cdev.owner = THIS_MODULE;

    // 3. 注册 cdev
    ret = cdev_add(&gpioled.led_cdev, gpioled.devid, 1);
    if (ret < 0) {
        printk(KERN_ERR "cdev_add failed\n");
        goto err_unregister_region;
    }

    // 4. 自动创建设备节点（核心新增内容）
    gpioled.led_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(gpioled.led_class)) {
        printk(KERN_ERR "class_create failed\n");
        ret = PTR_ERR(gpioled.led_class);
        goto err_cdev_del;
    }

    gpioled.led_device = device_create(gpioled.led_class, NULL, gpioled.devid, NULL, GPIOLED_NAME);
    if (IS_ERR(gpioled.led_device)) {
        printk(KERN_ERR "device_create failed\n");
        ret = PTR_ERR(gpioled.led_device);
        goto err_class_destroy;
    }

    printk(KERN_INFO "led driver loaded, /dev/%s created\n", GPIOLED_NAME);
    return 0;

    err_class_destroy:
    class_destroy(gpioled.led_class);
    err_cdev_del:
    cdev_del(&gpioled.led_cdev);
    err_unregister_region:
    unregister_chrdev_region(gpioled.devid, GPIOLED_CNT);
    err_gpio_free:
    gpio_free(gpioled.led_gpio);
    err_node_put:
    of_node_put(gpioled.nd);  // 释放设备树节点引用
    return ret;

    
}

static void __exit led_exit(void) 
{ 
    /* 注销字符设备驱动 */ 
    device_destroy(gpioled.led_class, gpioled.devid);  // 1. 先销毁设备
    class_destroy(gpioled.led_class);                  // 2. 再销毁类
    cdev_del(&gpioled.led_cdev);                       // 3. 删除 cdev
    unregister_chrdev_region(gpioled.devid, GPIOLED_CNT); // 4. 释放设备号
    gpio_free(gpioled.led_gpio);                       // 5. 释放 GPIO
    of_node_put(gpioled.nd);                           // 6. 释放节点引用
} 

module_init(led_init); 
module_exit(led_exit); 
MODULE_LICENSE("GPL"); 
MODULE_AUTHOR("ALIENTEK"); 




