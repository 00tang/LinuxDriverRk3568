#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/init.h>

static int __init i2c_test_init(void)
{
    struct device_node *np;
    const char *compatible;
    int ret;

    printk(KERN_INFO "=== i2c_test driver loaded ===\n");

    // 查找 I2C 节点
    np = of_find_node_by_path("/i2c@fe5a0000");
    if (!np) {
        printk(KERN_ERR "Failed to find i2c node\n");
        return -ENODEV;
    }
    printk(KERN_INFO "Found node: %s\n", np->full_name);

    // 读取 compatible 属性
    ret = of_property_read_string(np, "compatible", &compatible);
    if (ret == 0) {
        printk(KERN_INFO "compatible = %s\n", compatible);
    } else {
        printk(KERN_WARNING "compatible property not found\n");
    }

    return 0;
}

static void __exit i2c_test_exit(void)
{
    printk(KERN_INFO "=== i2c_test driver unloaded ===\n");
}

module_init(i2c_test_init);
module_exit(i2c_test_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("RK3568 Learner");
MODULE_DESCRIPTION("I2C device tree test");