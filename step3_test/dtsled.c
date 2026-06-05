#include<linux/module.h>
#include<linux/kernel.h>
#include<linux/delay.h>
#include<linux/ide.h>
#include<linux/init.h>
#include<linux/types.h>
#include<linux/errno.h>
#include<linux/gpio.h>
#include<linux/cdev.h>
#include<linux/device.h>
#include<linux/of.h>
#include<asm/uaccess.h>
#include<linux/of_address.h>
#include<asm/io.h>

#define DEVICE_CNT      1
#define DEVICE_NAME    "desled"
#define LED_OFF         0
#define LED_ON          1
#define CLASS_NAME      "class_desled"

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
    struct device_node *nd;
};
struct newchrled_dev dtsled;

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

void led_unmap(void)
{
    iounmap(PMU_GRF_GPIO0C_IOMUX_L_PT);
    iounmap(PMU_GRF_GPIO0C_DS_0_PT);
    iounmap(GPIO0_SWPORT_DR_H_PT);
    iounmap(GPIO0_SWPORT_DDR_H_PT);
}

static int led_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &dtsled;
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
    int retvalue;
    unsigned char databuf[1];
    unsigned char ledstat;

    retvalue = copy_from_user(databuf,buf,count);
    if(retvalue)
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
    u32 regdata[16];
    const char *str;
    struct property *proper;

    //获取设备树节点
    dtsled.nd = of_find_node_by_path("/rk3568_led");
    if(dtsled.nd == NULL)
    {
        printk(KERN_ERR "rk3568_led node not find\n");
        return 0;
    }
    else
    {
        printk(KERN_INFO "rk3568_led node find\n");
    }

    //获取compatible属性
    proper = of_find_property(dtsled.nd,"compatible",NULL);
    if(proper == NULL)
    {
        printk(KERN_ERR "compatible property find failed\n");
    }
    else
    {
        printk(KERN_INFO "compatible = %s \n",(char *)proper->value);
    }
    
    ret = of_property_read_string(dtsled.nd,"status",&str);
    if(ret < 0)
    {
        printk(KERN_ERR "status read failed\n");
    }
    else
    {
        printk(KERN_INFO "status = %s \n",str);
    }

    ret = of_property_read_u32_array(dtsled.nd,"reg",regdata,16);
    if(ret < 0)
    {
        printk(KERN_ERR "reg property read failed\n");
    }
    else
    {
        u8 i = 0;
        printk(KERN_INFO "reg data: \n");
        for(i = 0; i < 16; i++)
        {
            printk(KERN_INFO "%#x\n",regdata[i]);
        }
    }

    PMU_GRF_GPIO0C_IOMUX_L_PT = of_iomap(dtsled.nd,0);
    PMU_GRF_GPIO0C_DS_0_PT = of_iomap(dtsled.nd,1);
    GPIO0_SWPORT_DR_H_PT = of_iomap(dtsled.nd,2);
    GPIO0_SWPORT_DDR_H_PT = of_iomap(dtsled.nd,3);

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
    ret = alloc_chrdev_region(&dtsled.devid, 0, DEVICE_CNT, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ERR "alloc_chrdev_region failed\n");
        led_unmap();
        return ret;
    }
    dtsled.major = MAJOR(dtsled.devid);
    printk(KERN_INFO "major = %d\n", dtsled.major );

    // 2. 初始化 cdev
    cdev_init(&dtsled.led_cdev, &newchrled_fops);
    dtsled.led_cdev.owner = THIS_MODULE;

    // 3. 注册 cdev
    ret = cdev_add(&dtsled.led_cdev, dtsled.devid, 1);
    if (ret < 0) {
        printk(KERN_ERR "cdev_add failed\n");
        unregister_chrdev_region(dtsled.devid, 1);
        led_unmap();
        return ret;
    }

    // 4. 自动创建设备节点（核心新增内容）
    dtsled.led_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(dtsled.led_class)) {
        printk(KERN_ERR "class_create failed\n");
        cdev_del(&dtsled.led_cdev);
        unregister_chrdev_region(dtsled.devid, 1);
        led_unmap();
        return PTR_ERR(dtsled.led_class);
    }

    dtsled.led_device = device_create(dtsled.led_class, NULL, dtsled.devid, NULL, DEVICE_NAME);
    if (IS_ERR(dtsled.led_device)) {
        printk(KERN_ERR "device_create failed\n");
        class_destroy(dtsled.led_class);
        cdev_del(&dtsled.led_cdev);
        unregister_chrdev_region(dtsled.devid, 1);
        led_unmap();
        return PTR_ERR(dtsled.led_device);
    }

    printk(KERN_INFO "led driver loaded, /dev/%s created\n", DEVICE_NAME);
    return 0;
    
}

static void __exit led_exit(void)
{
    device_destroy(dtsled.led_class, dtsled.devid);
    class_destroy(dtsled.led_class);
    cdev_del(&dtsled.led_cdev);
    unregister_chrdev_region(dtsled.devid, 1);
    led_unmap();

    printk(KERN_INFO "led driver unloaded\n");
}

module_init(led_init);
module_exit(led_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("RK3568 Learner");
MODULE_DESCRIPTION("LED driver with register operations");