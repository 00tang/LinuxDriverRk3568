#include<linux/module.h>
#include<linux/kernel.h>
#include<linux/delay.h>
#include<linux/ide.h>
#include<linux/errno.h>
#include<linux/gpio.h>
#include<asm/uaccess.h>
#include<asm/io.h>
#include<linux/types.h>
#include<linux/init.h>
#include<linux/cdev.h>
#include<linux/device.h>

#define DEVICE_NAME     "LED"
#define CLASS_NAME      "AUTO_CLASS"
#define DEVICE_CNT        1

#define  LED_ON     1
#define  LED_OFF    0

#define PMU_GRF_BASE            (0XFDC20000)
#define PMU_GRF_GPIO0C_IOMUX_L  (PMU_GRF_BASE + 0x0010) //复用GPIO0C REG
#define PMU_GRF_GPIO0C_DS_0     (PMU_GRF_BASE + 0X0090) //推力

#define GPIO0_BASE              (0XFDD60000)
#define GPIO0_SWPORT_DR_H       (GPIO0_BASE + 0X0004)//高低电平
#define GPIO0_SWPORT_DDR_H      (GPIO0_BASE + 0X000C)//输入输出

//印射后的地址--虚拟地址
static void __iomem *PMU_GRF_GPIO0C_IOMUX_L_PT;
static void __iomem *PMU_GRF_GPIO0C_DS_0_PT;
static void __iomem *GPIO0_SWPORT_DR_H_PT;
static void __iomem *GPIO0_SWPORT_DDR_H_PT;

struct newchrled_dev
{
    dev_t devid;
    struct cdev led_cdev;
    struct class *led_class;
    struct device *led_device;
    int major;
    int minor;
};

struct newchrled_dev newchrled;

void led_switch(u8 sta)
{
    u32 val = 0;
    if( sta == LED_ON )
    {
        val = readl(GPIO0_SWPORT_DR_H_PT);
        val &= ~(1 << 0);
        val |= ((1 << 16) | (1 << 0));

        writel(val,GPIO0_SWPORT_DR_H_PT);
    }
    else if(sta == LED_OFF)
    {
        val = readl(GPIO0_SWPORT_DR_H_PT);
        val &= ~(1 << 0);
        val |= ((1 << 16) | (0 << 0));

        writel(val,GPIO0_SWPORT_DR_H_PT);
    }
}

void led_remap(void)
{
    PMU_GRF_GPIO0C_IOMUX_L_PT = ioremap(PMU_GRF_GPIO0C_IOMUX_L, 4);
    PMU_GRF_GPIO0C_DS_0_PT = ioremap(PMU_GRF_GPIO0C_DS_0, 4);
    GPIO0_SWPORT_DR_H_PT = ioremap(GPIO0_SWPORT_DR_H, 4);
    GPIO0_SWPORT_DDR_H_PT = ioremap(GPIO0_SWPORT_DDR_H, 4);
}

void led_unmap(void)
{
    iounmap(PMU_GRF_GPIO0C_IOMUX_L_PT);
    iounmap(PMU_GRF_GPIO0C_DS_0_PT);
    iounmap(GPIO0_SWPORT_DR_H_PT);
    iounmap(GPIO0_SWPORT_DDR_H_PT);
}

static int led_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &newchrled;
    printk(KERN_INFO "led_device opened\n");
    return 0;
}

static int led_release(struct inode *inode, struct file *filp)
{
    printk(KERN_INFO "led_device closed\n");
    return 0;
}

static ssize_t led_read(struct file *filp, char __user *buf, size_t count, loff_t *fpos)
{
    return 0;
}

static ssize_t led_write(struct file *filp, const char __user *buf,size_t count, loff_t *fpos)
{
    int ret;
    unsigned char databuf[1];
    unsigned char ledstat;

    ret = copy_from_user(databuf,buf,count);
    if(ret)
    {
        printk(KERN_ERR "kernel write failed\n");
        return -EFAULT;
    }

    ledstat = databuf[0];

    if(ledstat == LED_ON)
    {
        led_switch(LED_ON);
    }
    else if(ledstat == LED_OFF)
    {
        led_switch(LED_OFF);
    }
    return 0;
}

static struct file_operations newchrled_fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .release = led_release,
    .read = led_read,
    .write = led_write,
};

static int __init led_init(void)
{
    u32 val = 0;
    int ret;

    led_remap();

    val = readl(PMU_GRF_GPIO0C_IOMUX_L_PT);
    val &= ~(7 << 0);
    val |= ((7 << 16) | (0x0 << 0));

    writel(val,PMU_GRF_GPIO0C_IOMUX_L_PT);

    val = readl(PMU_GRF_GPIO0C_DS_0_PT);
    val &= ~(0x3F << 0);
    val |= ((0x3F << 16) | (0x3F<< 0));

    writel(val,PMU_GRF_GPIO0C_DS_0_PT);


    val = readl(GPIO0_SWPORT_DDR_H_PT);
    val &= ~(1 << 0);
    val |= ((1 << 16) | (1 << 0));
    
    writel(val,GPIO0_SWPORT_DDR_H_PT);

    val = readl(GPIO0_SWPORT_DR_H_PT);
    val &= ~(1 << 0);
    val |= ((1 << 16) | (0 << 0));

    writel(val,GPIO0_SWPORT_DR_H_PT);

     // 1. 分配设备号
    ret = alloc_chrdev_region(&newchrled.devid, 0, DEVICE_CNT, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ERR "alloc_chrdev_region failed\n");
        led_unmap();
        return ret;
    }
    newchrled.major = MAJOR(newchrled.devid);
    printk(KERN_INFO "major = %d\n", newchrled.major );

    // 2. 初始化 cdev
    cdev_init(&newchrled.led_cdev, &newchrled_fops);
    newchrled.led_cdev.owner = THIS_MODULE;

    // 3. 注册 cdev
    ret = cdev_add(&newchrled.led_cdev, newchrled.devid, 1);
    if (ret < 0) {
        printk(KERN_ERR "cdev_add failed\n");
        unregister_chrdev_region(newchrled.devid, 1);
        led_unmap();
        return ret;
    }

    // 4. 自动创建设备节点（核心新增内容）
    newchrled.led_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(newchrled.led_class)) {
        printk(KERN_ERR "class_create failed\n");
        cdev_del(&newchrled.led_cdev);
        unregister_chrdev_region(newchrled.devid, 1);
        led_unmap();
        return PTR_ERR(newchrled.led_class);
    }

    newchrled.led_device = device_create(newchrled.led_class, NULL, newchrled.devid, NULL, DEVICE_NAME);
    if (IS_ERR(newchrled.led_device)) {
        printk(KERN_ERR "device_create failed\n");
        class_destroy(newchrled.led_class);
        cdev_del(&newchrled.led_cdev);
        unregister_chrdev_region(newchrled.devid, 1);
        led_unmap();
        return PTR_ERR(newchrled.led_device);
    }

    printk(KERN_INFO "led driver loaded, /dev/%s created\n", DEVICE_NAME);
    return 0;
}

static void __exit led_exit(void)
{
    device_destroy(newchrled.led_class, newchrled.devid);
    class_destroy(newchrled.led_class);
    cdev_del(&newchrled.led_cdev);
    unregister_chrdev_region(newchrled.devid, 1);
    led_unmap();

    printk(KERN_INFO "led driver unloaded\n");
}

module_init(led_init);
module_exit(led_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("RK3568 Learner");
MODULE_DESCRIPTION("LED driver with register operations");