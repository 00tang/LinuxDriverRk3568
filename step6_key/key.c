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
// #include<asm/mach/map.h>
#include<asm/uaccess.h>
#include<asm/io.h>

#define KEYDEV_CNT         1
#define KEYDEV_NAME    "keydev"
#define CLASS_NAME      "keydev-class"

#define KEY0VALUE   0XF0
#define INVAKEY     0X00


struct key_dev
{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    int major;
    int minor;
    struct device_node *nd;
    int key_gpio;
    atomic_t keyvalue;
};
struct key_dev keydev;


static int keyio_init(void)
{
    int ret;
    const char *str;

    keydev.nd = of_find_node_by_path("/key");
    if(keydev.nd == NULL)
    {
        printk(KERN_ERR "keydev node noy find\n");
        return -EINVAL;
    }

    ret = of_property_read_string(keydev.nd,"status",&str);
    if(ret < 0 )
    {
        return -EINVAL;
    }
    
    if (strcmp(str, "okay"))
    return -EINVAL; 

    ret = of_property_read_string(keydev.nd,"compatible",&str);
    if(ret < 0)
    {
        printk(KERN_ERR "keydev not find compatible property\n");
        return -EINVAL;
    }

    if(strcmp(str, "alientek,key"))
    {
        printk(KERN_ERR "can't get compatible\n");
        return -EINVAL;
    }

    keydev.key_gpio = of_get_named_gpio(keydev.nd,"key-gpio",0);
    if(keydev.key_gpio < 0)
    {
        printk(KERN_ERR "keydev.key_gpio can't get value\n");
        return -EINVAL;
    }
    printk(KERN_INFO "key-gpio num = %d\r\n", keydev.key_gpio);

    ret = gpio_request(keydev.key_gpio,"KEY0");
    if(ret)
    {
        printk(KERN_ERR "keydev faikey to request\n");
        return ret;
    }

    ret = gpio_direction_input(keydev.key_gpio);
    if(ret)
    {
        printk(KERN_ERR "keydev faikey set to gpio\n");
        return ret;
    }

    return 0;
}

static int key_open(struct inode *inode, struct file *filp)
{
//     int ret = 0;
    filp->private_data = &keydev;

    // ret = keyio_init();
    // if(ret < 0 ) return ret;

    printk(KERN_INFO "device opened\n");
    return 0;
}

static int key_release(struct inode *inode, struct file *filp)
{
    // struct key_dev *dev = filp->private_data;
    // gpio_free(dev->key_gpio);


    printk(KERN_INFO "device closed\n");
    return 0;
}

static ssize_t key_read(struct file *filp, char __user *buf, size_t count, loff_t *fpos)
{
    int ret;
    int value;
    struct key_dev *dev = filp->private_data;

    int gpio_val = gpio_get_value(dev->key_gpio);
    printk(KERN_INFO "GPIO value = %d\n", gpio_val);

    if(gpio_val == 1)
    {
        while(gpio_get_value(dev->key_gpio)); 
        atomic_set(&dev->keyvalue,KEY0VALUE);
    }
    else
    {
        atomic_set(&dev->keyvalue,INVAKEY);
    }

    value = atomic_read(&dev->keyvalue);
    ret = copy_to_user(buf,&value,sizeof(value));

    return 0;
}

static ssize_t key_write(struct file *filp, const char __user *buf,size_t count, loff_t *fpos)
{
    return 0;
}

static struct file_operations keydev_fops = {
    .owner = THIS_MODULE,
    .open = key_open,
    .release = key_release,
    .read = key_read,
    .write = key_write,
};

static int __init key_driver_init(void)
{
    int ret = 0;

    ret = keyio_init();
    if (ret < 0) 
    {
        printk(KERN_ERR "keyio_init failed\n");
        return ret;
    }
    // keydev.keyvalue = (atomic_t)ATOMIC_INIT(0);
    atomic_set(&keydev.keyvalue, INVAKEY);

    // 1. 分配设备号
    ret = alloc_chrdev_region(&keydev.devid, 0, KEYDEV_CNT, KEYDEV_NAME);
    if (ret < 0) 
    {
        printk(KERN_ERR "alloc_chrdev_region faikey\n");
        goto err_gpio_free;
    }
    keydev.major = MAJOR(keydev.devid);
    printk(KERN_INFO "keydev-gpio major = %d\n", keydev.major );

    // 2. 初始化 cdev
    cdev_init(&keydev.cdev, &keydev_fops);
    keydev.cdev.owner = THIS_MODULE;

    // 3. 注册 cdev
    ret = cdev_add(&keydev.cdev, keydev.devid, 1);
    if (ret < 0) {
        printk(KERN_ERR "cdev_add faikey\n");
        goto err_unregister_region;
    }

    // 4. 自动创建设备节点（核心新增内容）
    keydev.class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(keydev.class)) {
        printk(KERN_ERR "class_create faikey\n");
        ret = PTR_ERR(keydev.class);
        goto err_cdev_del;
    }

    keydev.device = device_create(keydev.class, NULL, keydev.devid, NULL,KEYDEV_NAME);
    if (IS_ERR(keydev.device)) {
        printk(KERN_ERR "device_create faikey\n");
        ret = PTR_ERR(keydev.device);
        goto err_class_destroy;
    }

    printk(KERN_INFO "key driver loaded, /dev/%s created\n", KEYDEV_NAME);
    return 0;

    err_class_destroy:
    class_destroy(keydev.class);
    err_cdev_del:
    cdev_del(&keydev.cdev);
    err_unregister_region:
    unregister_chrdev_region(keydev.devid, KEYDEV_CNT);
    err_gpio_free:
    gpio_free(keydev.key_gpio);
    return ret;

    
}

static void __exit key_driver_exit(void) 
{ 
    /* 注销字符设备驱动 */ 
    device_destroy(keydev.class, keydev.devid);  // 1. 先销毁设备
    class_destroy(keydev.class);                  // 2. 再销毁类
    cdev_del(&keydev.cdev);                       // 3. 删除 cdev
    unregister_chrdev_region(keydev.devid, KEYDEV_CNT); // 4. 释放设备号
    gpio_free(keydev.key_gpio);                       // 5. 释放 GPIO
    of_node_put(keydev.nd);                           // 6. 释放节点引用
} 

module_init(key_driver_init); 
module_exit(key_driver_exit); 
MODULE_LICENSE("GPL"); 
MODULE_AUTHOR("ALIENTEK"); 




