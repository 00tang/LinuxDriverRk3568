#include<linux/types.h>
#include<linux/kernel.h>
#include<linux/delay.h>
#include<linux/module.h>
#include<linux/ide.h>
#include<linux/init.h>
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
#include<linux/input.h>
#include<linux/interrupt.h>

#define KEYINOUT_NAME  "keyinput"

struct key_dev
{
    struct input_dev *idev;
    struct timer_list timer;
    int gpio_key;
    int irq_key;
};

static struct key_dev key;

static irqreturn_t key_interrupt(int irq, void *dev_id)
{
    if(key.irq_key != irq)
        return IRQ_NONE;
   
    disable_irq_nosync(irq);
    mod_timer(&key.timer,jiffies + msecs_to_jiffies(15));

    return IRQ_HANDLED;
}

static int key_gpio_init(struct device_node *nd)
{
    int ret;
    unsigned long irq_flags;

    key.gpio_key = of_get_named_gpio(nd,"key-gpio",0);
    if(!gpio_is_valid(key.gpio_key))
    {
        printk(KERN_ERR "key:failed to get key-gpio\n");
        return -EINVAL;
    }

    ret = gpio_request(key.gpio_key,"KEY0");
    if (ret)
    {
        printk(KERN_ERR "key:failed to request key-gpio\n");
        return ret;
    }
    
    gpio_direction_input(key.gpio_key);

    key.irq_key = irq_of_parse_and_map(nd, 0); 
    if(!key.irq_key)
    {
        printk(KERN_ERR "key:failed to irq_key\n");
        return -EINVAL;
    }

    irq_flags = irq_get_trigger_type(key.irq_key);
    if(irq_flags == IRQ_TYPE_NONE)
    {
        irq_flags = IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING;
    }

    ret = request_irq(key.irq_key,key_interrupt,irq_flags,"key0_IRQ",NULL);
    if (ret)
    {
        gpio_free(key.gpio_key);
        printk(KERN_ERR "request_irq error\n");
        return ret;
    }

    return 0;
}

static void key_timer_function(struct timer_list *arg)
{
    int val;

    val = gpio_get_value(key.gpio_key);
    input_report_key(key.idev,KEY_0,!val);
    input_sync(key.idev);

    enable_irq(key.irq_key);
}

static int atk_key_probe(struct platform_device *pdev)
{
    int ret;

    ret = key_gpio_init(pdev->dev.of_node);
    if(ret < 0)
    {
        printk(KERN_ERR "gpio init error\n");
        return ret;
    }

    timer_setup(&key.timer,key_timer_function,0);

    key.idev = input_alloc_device();
    key.idev->name = KEYINOUT_NAME;

#if 0
    __set_bit(EV_KEY, key.idev->evbit); /* 设置产生按键事件 */
    __set_bit(EV_REP, key.idev->evbit);
    __set_bit(KEY_0, key.idev->keybit);  
#endif

#if 0
    key.idev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP); 
    key.idev->keybit[BIT_WORD(KEY_0)] |= BIT_MASK(KEY_0); 
#endif

    key.idev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP);
    input_set_capability(key.idev, EV_KEY, KEY_0); 

    ret = input_register_device(key.idev);
    if(ret)
    {
        printk(KERN_ERR  "register input device failed\n");
        goto free_gpio;
    }

    return 0;

free_gpio:
    free_irq(key.irq_key,NULL);
    gpio_free(key.gpio_key);
    del_timer_sync(&key.timer);
    return ret;
}

static int atk_key_remove(struct platform_device *pdev)
{
    printk(KERN_INFO "key device removed\n");
    free_irq(key.irq_key,NULL);
    gpio_free(key.gpio_key);
    del_timer_sync(&key.timer);
    input_unregister_device(key.idev);

    return 0;
}

static const struct of_device_id key_of_match[] = {
    {.compatible = "alientek,key"},
    {/* Sentinel*/}
};

MODULE_DEVICE_TABLE(of,key_of_match);

static struct platform_driver key_input_driver = {
    .driver = {
        .name = "rk3568-key",
        .of_match_table = key_of_match,
    },

    .probe = atk_key_probe,
    .remove = atk_key_remove,
};

static int __init keyinput_init(void)
{
    return platform_driver_register(&key_input_driver);
}

static void  __exit keyinput_exit(void)
{
    platform_driver_unregister(&key_input_driver);
}

module_init(keyinput_init); 
module_exit(keyinput_exit); 
MODULE_LICENSE("GPL"); 
MODULE_AUTHOR("ALIENTEK"); 
