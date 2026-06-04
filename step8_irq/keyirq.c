#include <linux/types.h> 
#include <linux/kernel.h> 
#include <linux/delay.h> 
#include <linux/ide.h> 
#include <linux/init.h> 
#include <linux/module.h> 
#include <linux/errno.h> 
#include <linux/gpio.h> 
#include <linux/cdev.h> 
#include <linux/device.h> 
#include <linux/of.h> 
#include <linux/of_address.h> 
#include <linux/of_gpio.h> 
#include <linux/semaphore.h> 
#include <linux/of_irq.h> 
#include <linux/irq.h> 
//#include <asm/mach/map.h> 
#include <asm/uaccess.h> 
#include <asm/io.h> 

#define KEY_CNT         1
#define CLASS_NAME      "class_key"
#define KEY_NAME        "key"

enum{
    KEY_PRESS = 0,
    KEY_RELEASE,
    KEY_KEEP,
};

struct key_dev
{
    /* data */
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *nd;
    int key_gpio;
    int major;
    struct timer_list timer;
    int irq_num;
    spinlock_t spinlock;
};

static struct key_dev key;
static int status = KEY_KEEP;

static irqreturn_t key_interrupt(int irq, void *dev_id)
{
    /*按键消抖*/
    mod_timer(&key.timer,jiffies + msecs_to_jiffies(15));
    return IRQ_HANDLED;
}

static int key_parse_dt(void)
{
    int ret;
    const char *str;

    key.nd = of_find_node_by_path("/key_irqtest");
    if(key.nd == NULL)
    {
        printk(KERN_ERR "key node not find\n");
        return -EINVAL;
    }

    ret = of_property_read_string(key.nd,"status",&str);
    if(ret)
    {
        printk(KERN_ERR "key status node not find 1\n");
        return -EINVAL;
    }

    if(strcmp(str,"okay"))
    {
        printk(KERN_ERR "key status node not find 2\n");
        return -EINVAL;
    }

    ret = of_property_read_string(key.nd,"compatible",&str);
    if(ret < 0)
    {
        printk(KERN_ERR "failed to get compatible peoperty\n");
        return -EINVAL;
    }

    if(strcmp(str,"alientek,key"))
    {
        printk(KERN_ERR "key: Compatible match failed\\n");
        return -EINVAL;
    }

    key.key_gpio = of_get_named_gpio(key.nd,"key-gpio",0);
    if(key.key_gpio < 0)
    {
        printk(KERN_ERR "can't get key-gpio\n");
        return -EINVAL;
    }

    key.irq_num = irq_of_parse_and_map(key.nd,0);
    if(!key.irq_num)
    {
        printk(KERN_ERR "parse_and_map error\n");
        return -EINVAL;
    }

    printk(KERN_INFO "key-gpio num = %d\n",key.key_gpio);
    return 0;
}

static int key_gpio_init(void)
{
    int ret;
    unsigned long irq_flags;

    ret = gpio_request(key.key_gpio,"KEY0");
    if (ret)
    {
        printk(KERN_ERR "key:failed to request key-gpio\n");
        return ret;
    }
    
    gpio_direction_input(key.key_gpio);

    irq_flags = irq_get_trigger_type(key.irq_num);
    if(irq_flags == IRQ_TYPE_NONE)
    {
        irq_flags = IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING;
    }

    ret = request_irq(key.irq_num,key_interrupt,irq_flags,"key0_IRQ",NULL);
    if (ret)
    {
        gpio_free(key.key_gpio);
        printk(KERN_ERR "request_irq error\n");
        return ret;
    }

    return 0;
}

static void key_timer_function(struct timer_list *arg)
{
    static int last_val = 0;
    unsigned long flags;
    int current_val;

    spin_lock_irqsave(&key.spinlock,flags);

    current_val = gpio_get_value(key.key_gpio);
    if(1 == current_val && !last_val)
    {
        status = KEY_PRESS;
    }
    else if(0 == current_val && last_val )
    {
        status = KEY_RELEASE;
    }
    else
    {
        status = KEY_KEEP;
    }
    last_val = current_val;

    spin_unlock_irqrestore(&key.spinlock, flags);
}

static int timer_open(struct inode *inode,struct file *filp)
{

    printk(KERN_INFO "timer device open success\n");

    return 0;
}

static int timer_release(struct inode *inode, struct file *filp)
{
    printk(KERN_INFO "timer device closed success\n");

    return 0;
}

static ssize_t timer_read(struct file *filp, char __user *buf, size_t count, loff_t *fpos)
{
    unsigned long flags;
    int ret;
    int val;

    if (count < sizeof(val)) {
        printk(KERN_ERR "Count num have problem\n");
        return -EINVAL;
    }
    spin_lock_irqsave(&key.spinlock,flags);

    val = status;

    status = KEY_KEEP;

    spin_unlock_irqrestore(&key.spinlock,flags);

    ret = copy_to_user(buf,&val,sizeof(val));
    if(ret)
    {
        return -EFAULT;
    }

    return sizeof(val);
}

static ssize_t timer_write(struct file *filp, const char __user *buf,size_t count, loff_t *fpos)
{
    return 0;
}

static struct file_operations key_fops = { 
    .owner = THIS_MODULE, 
    .open = timer_open, 
    .read = timer_read, 
    .write = timer_write, 
    .release = timer_release, 
}; 

static int __init timer_driver_init(void)
{
    int ret = 0;

    spin_lock_init(&key.spinlock);
    
    timer_setup(&key.timer,key_timer_function,0);

    ret = key_parse_dt();
    if (ret < 0) 
        return ret;

    ret = key_gpio_init();
    if(ret)
        return ret;

    // 1. 分配设备号
    ret = alloc_chrdev_region(&key.devid, 0, KEY_CNT, KEY_NAME);
    if (ret < 0) 
    {
        printk(KERN_ERR "alloc_chrdev_region faikey\n");
        goto err_gpio_free;
    }
    key.major = MAJOR(key.devid);
    printk(KERN_INFO "key-gpio major = %d\n", key.major );

    // 2. 初始化 cdev
    cdev_init(&key.cdev, &key_fops);
    key.cdev.owner = THIS_MODULE;

    // 3. 注册 cdev
    ret = cdev_add(&key.cdev, key.devid, 1);
    if (ret < 0) {
        printk(KERN_ERR "cdev_add faikey\n");
        goto err_unregister_region;
    }

    // 4. 自动创建设备节点（核心新增内容）
    key.class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(key.class)) {
        printk(KERN_ERR "class_create faikey\n");
        ret = PTR_ERR(key.class);
        goto err_cdev_del;
    }

    key.device = device_create(key.class, NULL, key.devid, NULL,KEY_NAME);
    if (IS_ERR(key.device)) {
        printk(KERN_ERR "device_create faikey\n");
        ret = PTR_ERR(key.device);
        goto err_class_destroy;
    }

    printk(KERN_INFO "key driver loaded, /dev/%s created\n", KEY_NAME);
    return 0;

    err_class_destroy:
    class_destroy(key.class);
    err_cdev_del:
    cdev_del(&key.cdev);
    err_unregister_region:
    unregister_chrdev_region(key.devid, KEY_CNT);
    err_gpio_free:
    free_irq(key.irq_num, NULL); 
    gpio_free(key.key_gpio);
    return -EIO;

    
}

static void __exit timer_driver_exit(void) 
{ 
    /* 注销字符设备驱动 */ 
    device_destroy(key.class, key.devid);  // 1. 先销毁设备
    class_destroy(key.class);                  // 2. 再销毁类
    cdev_del(&key.cdev);                       // 3. 删除 cdev
    unregister_chrdev_region(key.devid, KEY_CNT); // 4. 释放设备号
    free_irq(key.irq_num, NULL); 
    gpio_free(key.key_gpio);                       // 5. 释放 GPIO
    of_node_put(key.nd);                           // 6. 释放节点引用
} 

module_init(timer_driver_init); 
module_exit(timer_driver_exit); 
MODULE_LICENSE("GPL"); 
MODULE_AUTHOR("ALIENTEK"); 
