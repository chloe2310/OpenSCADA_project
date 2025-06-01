#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/cdev.h>
#include <linux/device.h>

#define GPIO0_BASE      0x44E07000
#define GPIO_OE         0x134
#define GPIO_DATAOUT    0x13C
#define LED_GPIO_PIN    31  // GPIO0_31 → P9_13

#define DEVICE_NAME     "led0"
#define CLASS_NAME      "led"

static int major;
static struct class* led_class = NULL;
static struct device* led_device = NULL;

static void __iomem *gpio_base = NULL;
static void __iomem *gpio_oe = NULL;
static void __iomem *gpio_dataout = NULL;

static int led_open(struct inode *inode, struct file *file) {
    printk(KERN_DEBUG "[LED DRIVER] Open Device\n");
    return 0;
}

static int led_release(struct inode *inode, struct file *file) {
    printk(KERN_DEBUG "[LED DRIVER] Close Device\n");
    return 0;
}

static ssize_t led_write(struct file *file, const char __user *buf, size_t len, loff_t *offset) {
    char kbuf[2] = {0};
    u32 current_dataout, new_dataout;

    printk(KERN_DEBUG "[LED DRIVER] call write function len=%zu\n", len);

    if (len < 1) {
        printk(KERN_ERR "[LED DRIVER] invalid lenght: %zu\n", len);
        return -EINVAL;
    }

    if (copy_from_user(kbuf, buf, 1)) {
        printk(KERN_ERR "[LED DRIVER] copy_from_user false\n");
        return -EFAULT;
    }

    printk(KERN_DEBUG "[LED DRIVER] Dữ liệu người dùng: %c\n", kbuf[0]);

    current_dataout = readl(gpio_dataout);
    printk(KERN_DEBUG "[LED DRIVER] Giá trị GPIO_DATAOUT hiện tại: 0x%08x\n", current_dataout);

    if (kbuf[0] == '1') {
        new_dataout = current_dataout | (1 << LED_GPIO_PIN);
        writel(new_dataout, gpio_dataout);
        printk(KERN_DEBUG "[LED DRIVER] Bật LED, giá trị GPIO_DATAOUT mới: 0x%08x\n", new_dataout);
    } else if (kbuf[0] == '0') {
        new_dataout = current_dataout & ~(1 << LED_GPIO_PIN);
        writel(new_dataout, gpio_dataout);
        printk(KERN_DEBUG "[LED DRIVER] Tắt LED, giá trị GPIO_DATAOUT mới: 0x%08x\n", new_dataout);
    } else {
        printk(KERN_ERR "[LED DRIVER] Dữ liệu đầu vào không hợp lệ: %c\n", kbuf[0]);
        return -EINVAL;
    }

    // Kiểm tra trạng thái sau khi ghi
    current_dataout = readl(gpio_dataout);
    printk(KERN_DEBUG "[LED DRIVER] GPIO_DATAOUT sau khi ghi: 0x%08x\n", current_dataout);
    printk(KERN_DEBUG "[LED DRIVER] Trạng thái LED (bit %d): %d\n", LED_GPIO_PIN, 
           (current_dataout & (1 << LED_GPIO_PIN)) ? 1 : 0);

    return len;
}

static ssize_t led_read(struct file *file, char __user *buf, size_t len, loff_t *offset) {
    char state;
    u32 current_dataout;

    printk(KERN_DEBUG "[LED DRIVER] Gọi hàm read với len=%zu, offset=%lld\n", len, *offset);

    if (*offset > 0) {
        printk(KERN_DEBUG "[LED DRIVER] Đã đến EOF\n");
        return 0;
    }

    current_dataout = readl(gpio_dataout);
    state = (current_dataout & (1 << LED_GPIO_PIN)) ? '1' : '0';
    printk(KERN_DEBUG "[LED DRIVER] Trạng thái LED hiện tại: %c (GPIO_DATAOUT: 0x%08x)\n", state, current_dataout);

    if (copy_to_user(buf, &state, 1)) {
        printk(KERN_ERR "[LED DRIVER] copy_to_user false\n");
        return -EFAULT;
    }

    *offset += 1;
    return 1;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .release = led_release,
    .write = led_write,
    .read = led_read,
};

static int __init led_init(void) {
    int ret;
    u32 oe_val;

    printk(KERN_DEBUG "[LED DRIVER] Init module\n");

    major = register_chrdev(0, DEVICE_NAME, &fops);
    if (major < 0) {
        printk(KERN_ERR "[LED DRIVER] register false: %d\n", major);
        return major;
    }
    printk(KERN_DEBUG "[LED DRIVER]  major: %d\n", major);

    led_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(led_class)) {
        ret = PTR_ERR(led_class);
        printk(KERN_ERR "[LED DRIVER] create class false : %d\n", ret);
        unregister_chrdev(major, DEVICE_NAME);
        return ret;
    }
    printk(KERN_DEBUG "[LED DRIVER] Đã tạo class: %s\n", CLASS_NAME);

    led_device = device_create(led_class, NULL, MKDEV(major, 0), NULL, DEVICE_NAME);
    if (IS_ERR(led_device)) {
        ret = PTR_ERR(led_device);
        printk(KERN_ERR "[LED DRIVER] Init Device false: %d\n", ret);
        class_destroy(led_class);
        unregister_chrdev(major, DEVICE_NAME);
        return ret;
    }
    printk(KERN_DEBUG "[LED DRIVER] Init Device : %s\n", DEVICE_NAME);

    gpio_base = ioremap(GPIO0_BASE, 0x1000);
    if (!gpio_base) {
        printk(KERN_ERR "[LED DRIVER] ioremap GPIO0_BASE false\n");
        device_destroy(led_class, MKDEV(major, 0));
        class_destroy(led_class);
        unregister_chrdev(major, DEVICE_NAME);
        return -ENOMEM;
    }
    printk(KERN_DEBUG "[LED DRIVER] had map GPIO base: 0x%p\n", gpio_base);

    gpio_oe = gpio_base + GPIO_OE;
    gpio_dataout = gpio_base + GPIO_DATAOUT;

    // Cấu hình chân GPIO làm đầu ra
    oe_val = readl(gpio_oe);
    printk(KERN_DEBUG "[LED DRIVER] GPIO_OE : 0x%08x\n", oe_val);
    writel(oe_val & ~(1 << LED_GPIO_PIN), gpio_oe);
    oe_val = readl(gpio_oe);
    printk(KERN_DEBUG "[LED DRIVER] GPIO_OE set pin %d output : 0x%08x\n", LED_GPIO_PIN, oe_val);

    printk(KERN_INFO "[LED DRIVER] Init GPIO0_31 (P9_13) output\n");
    return 0;
}

static void __exit led_exit(void) {
    printk(KERN_DEBUG "[LED DRIVER] disable module\n");
    device_destroy(led_class, MKDEV(major, 0));
    printk(KERN_DEBUG "[LED DRIVER] Cancel device\n");
    class_unregister(led_class);
    class_destroy(led_class);
    printk(KERN_DEBUG "[LED DRIVER] Cancel class\n");
    unregister_chrdev(major, DEVICE_NAME);
    printk(KERN_DEBUG "[LED DRIVER] uninstall character device driver \n");
    iounmap(gpio_base);
    printk(KERN_DEBUG "[LED DRIVER] uninstall ioremap GPIO base\n");

    printk(KERN_INFO "[LED DRIVER] uninstall module\n");
}

module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ADMIN");
MODULE_DESCRIPTION("Điều khiển LED qua /dev/led0 sử dụng ioremap");