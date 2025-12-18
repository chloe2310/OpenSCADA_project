#include <linux/module.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/delay.h>

#define DRIVER_AUTHOR "Chloe"
#define DRIVER_DESC   "DHT11 driver"
#define DRIVER_VERS   "1.0"

// GPIO definitions
#define GPIO0_ADDR_BASE     0x44E07000
#define GPIO0_ADDR_END      0x44E07FFF
#define GPIO0_ADDR_SIZE     (GPIO0_ADDR_END - GPIO0_ADDR_BASE)

#define GPIO_OE_OFFSET          0x134
#define GPIO_DATAIN_OFFSET      0x138
#define GPIO_CLEARDATAOUT_OFFSET 0x190
#define GPIO_SETDATAOUT_OFFSET   0x194

#define DHT11_PIN               30      /* P9_11 = GPIO0_30 */
#define DHT11_MASK             (1 << DHT11_PIN)

struct dht11_dev {
    dev_t dev_num;
    struct class *dht11_class;
    struct cdev dht11_cdev;
    uint32_t __iomem *gpio0_base;
    uint8_t data[5];
} dht_dev;

/* Function Prototypes */
static int      __init dht11_init(void);
static void     __exit dht11_exit(void);
static int      dht11_open(struct inode *inode, struct file *file);
static int      dht11_release(struct inode *inode, struct file *file);
static ssize_t  dht11_read(struct file *filp, char __user *user_buf, size_t size, loff_t *offset);

static struct file_operations fops = {
    .owner      = THIS_MODULE,
    .read       = dht11_read,
    .open       = dht11_open,
    .release    = dht11_release,
};

static int dht11_read_data(void)
{
    int i, j;
    uint8_t byte;
    int count;
    unsigned long flags;
    uint32_t gpio_value;
    
    printk(KERN_DEBUG "DHT11: Bắt đầu đọc dữ liệu từ cảm biến\n");
    
    // Lưu trạng thái ngắt hiện tại và tắt ngắt để đảm bảo timing chính xác
    local_irq_save(flags);
    
    // Kiểm tra trạng thái ban đầu của chân GPIO
    gpio_value = *(dht_dev.gpio0_base + GPIO_DATAIN_OFFSET/4) & DHT11_MASK;
    printk(KERN_DEBUG "DHT11: Trạng thái ban đầu của chân GPIO: %s (0x%08X)\n", 
           gpio_value ? "HIGH" : "LOW", gpio_value);
    
    // Set pin as output
    *(dht_dev.gpio0_base + GPIO_OE_OFFSET/4) &= ~DHT11_MASK;
    printk(KERN_DEBUG "DHT11: Đã thiết lập chân GPIO làm OUTPUT\n");
    
    // Send start signal
    *(dht_dev.gpio0_base + GPIO_CLEARDATAOUT_OFFSET/4) |= DHT11_MASK;
    printk(KERN_DEBUG "DHT11: Đã kéo chân GPIO xuống LOW để bắt đầu tín hiệu\n");
    mdelay(18);  // Pull low for 18ms
    
    *(dht_dev.gpio0_base + GPIO_SETDATAOUT_OFFSET/4) |= DHT11_MASK;
    printk(KERN_DEBUG "DHT11: Đã kéo chân GPIO lên HIGH sau 18ms\n");
    udelay(40);  // Pull high for 40us
    
    // Set pin as input
    *(dht_dev.gpio0_base + GPIO_OE_OFFSET/4) |= DHT11_MASK;
    printk(KERN_DEBUG "DHT11: Đã thiết lập chân GPIO làm INPUT để đọc phản hồi\n");
    
    // Đợi DHT11 phản hồi (kéo xuống LOW trong 80us, sau đó kéo lên HIGH trong 80us)
    count = 0;
    while((*(dht_dev.gpio0_base + GPIO_DATAIN_OFFSET/4) & DHT11_MASK) && count++ < 100) 
        udelay(1);
    
    if(count >= 100) {
        local_irq_restore(flags);
        printk(KERN_ERR "DHT11: Không nhận được phản hồi từ cảm biến (chân không chuyển sang LOW)\n");
        return -1;
    }
    
    printk(KERN_DEBUG "DHT11: Đã nhận phản hồi LOW từ cảm biến sau %d us\n", count);
    
    count = 0;
    while(!(*(dht_dev.gpio0_base + GPIO_DATAIN_OFFSET/4) & DHT11_MASK) && count++ < 100) 
        udelay(1);
    
    if(count >= 100) {
        local_irq_restore(flags);
        printk(KERN_ERR "DHT11: Cảm biến bị kẹt ở trạng thái LOW\n");
        return -1;
    }
    
    printk(KERN_DEBUG "DHT11: Cảm biến đã chuyển từ LOW sang HIGH sau %d us\n", count);
    
    count = 0;
    while((*(dht_dev.gpio0_base + GPIO_DATAIN_OFFSET/4) & DHT11_MASK) && count++ < 100) 
        udelay(1);
    
    if(count >= 100) {
        local_irq_restore(flags);
        printk(KERN_ERR "DHT11: Cảm biến bị kẹt ở trạng thái HIGH\n");
        return -1;
    }
    
    printk(KERN_DEBUG "DHT11: Cảm biến đã chuyển từ HIGH sang LOW sau %d us\n", count);
    printk(KERN_DEBUG "DHT11: Bắt đầu đọc 40 bit dữ liệu\n");
    
    // Read 40 bits (5 bytes) of data
    for(i = 0; i < 5; i++) {
        byte = 0;
        for(j = 0; j < 8; j++) {
            // Wait for low-to-high transition
            count = 0;
            while(!(*(dht_dev.gpio0_base + GPIO_DATAIN_OFFSET/4) & DHT11_MASK) && count++ < 100) 
                udelay(1);
            
            if(count >= 100) {
                local_irq_restore(flags);
                printk(KERN_ERR "DHT11: Timeout khi đợi bit %d của byte %d chuyển từ LOW sang HIGH\n", j, i);
                return -1;
            }
            
            // Measure high pulse width (0 = ~28us, 1 = ~70us)
            udelay(30);  // Wait for 30us
            
            // If pin is still high after 30us, it's a '1' bit
            if(*(dht_dev.gpio0_base + GPIO_DATAIN_OFFSET/4) & DHT11_MASK)
                byte |= (1 << (7-j));
                
            // Wait for high-to-low transition
            count = 0;
            while((*(dht_dev.gpio0_base + GPIO_DATAIN_OFFSET/4) & DHT11_MASK) && count++ < 100) 
                udelay(1);
            
            if(count >= 100) {
                local_irq_restore(flags);
                printk(KERN_ERR "DHT11: Bit %d của byte %d bị kẹt ở trạng thái HIGH\n", j, i);
                return -1;
            }
        }
        dht_dev.data[i] = byte;
        printk(KERN_DEBUG "DHT11: Đã đọc byte %d = 0x%02X\n", i, byte);
    }
    
    // Re-enable interrupts
    local_irq_restore(flags);
    
    // Verify checksum
    if(dht_dev.data[4] == (dht_dev.data[0] + dht_dev.data[1] + 
                          dht_dev.data[2] + dht_dev.data[3])) {
        printk(KERN_INFO "DHT11: Đọc thành công - Độ ẩm: %d.%d%% Nhiệt độ: %d.%d°C Checksum: 0x%02X\n",
               dht_dev.data[0], dht_dev.data[1], dht_dev.data[2], dht_dev.data[3], dht_dev.data[4]);
        return 0;
    }
    
    printk(KERN_ERR "DHT11: Kiểm tra checksum thất bại. Nhận: 0x%02X, Tính toán: 0x%02X\n", 
           dht_dev.data[4], (dht_dev.data[0] + dht_dev.data[1] + dht_dev.data[2] + dht_dev.data[3]));
    return -1;
}

static ssize_t dht11_read(struct file *filp, char __user *buf, size_t len, loff_t *offset)
{
    char data[64];  // Increased buffer size
    int ret;
    size_t data_len;
    
    if(*offset > 0)
        return 0;

    printk(KERN_DEBUG "DHT11: Đang đọc dữ liệu cảm biến\n");
    ret = dht11_read_data();
    if(ret < 0) {
        data_len = sprintf(data, "Lỗi khi đọc DHT11\nVui lòng kiểm tra dmesg để biết chi tiết\n");
    } else {
        data_len = sprintf(data, "Độ ẩm: %d.%d%%\nNhiệt độ: %d.%d°C\nChecksum: 0x%02X\n",
                dht_dev.data[0], dht_dev.data[1], 
                dht_dev.data[2], dht_dev.data[3],
                dht_dev.data[4]);
    }

    if(copy_to_user(buf, data, data_len)) {
        printk(KERN_ERR "DHT11: Không thể sao chép dữ liệu cho người dùng\n");
        return -EFAULT;
    }

    *offset = data_len;
    return data_len;
}

static int dht11_open(struct inode *inode, struct file *file)
{
    pr_info("DHT11: Thiết bị đã được mở\n");
    return 0;
}

static int dht11_release(struct inode *inode, struct file *file)
{
    pr_info("DHT11: Thiết bị đã được đóng\n");
    return 0;
}

static int __init dht11_init(void)
{
    /* 1. Map GPIO registers */
    dht_dev.gpio0_base = ioremap(GPIO0_ADDR_BASE, GPIO0_ADDR_SIZE);
    if(!dht_dev.gpio0_base) {
        pr_err("DHT11: Không thể ánh xạ thanh ghi GPIO\n");
        return -ENOMEM;
    }
    
    printk(KERN_INFO "DHT11: Đã ánh xạ thanh ghi GPIO thành công\n");
    printk(KERN_INFO "DHT11: Sử dụng chân P9_11 (GPIO0_%d)\n", DHT11_PIN);

    /* 2. Allocate device number */
    if(alloc_chrdev_region(&dht_dev.dev_num, 0, 1, "dht11") < 0) {
        pr_err("DHT11: Không thể cấp phát số thiết bị\n");
        goto unmap_gpio;
    }

    /* 3. Create device class */
    dht_dev.dht11_class = class_create(THIS_MODULE, "dht11_class");
    if(IS_ERR(dht_dev.dht11_class)) {
        pr_err("DHT11: Không thể tạo lớp thiết bị\n");
        goto unreg_chrdev;
    }

    /* 4. Create device file */
    if(device_create(dht_dev.dht11_class, NULL, dht_dev.dev_num, NULL, "dht11") == NULL) {
        pr_err("DHT11: Không thể tạo tệp thiết bị\n");
        goto destroy_class;
    }

    /* 5. Initialize cdev structure and add it */
    cdev_init(&dht_dev.dht11_cdev, &fops);
    if(cdev_add(&dht_dev.dht11_cdev, dht_dev.dev_num, 1) < 0) {
        pr_err("DHT11: Không thể thêm cdev\n");
        goto destroy_device;
    }

    printk(KERN_INFO "DHT11: Driver đã được khởi tạo thành công\n");
    printk(KERN_INFO "DHT11: Sử dụng lệnh 'cat /dev/dht11' để đọc dữ liệu\n");
    return 0;

destroy_device:
    device_destroy(dht_dev.dht11_class, dht_dev.dev_num);
destroy_class:
    class_destroy(dht_dev.dht11_class);
unreg_chrdev:
    unregister_chrdev_region(dht_dev.dev_num, 1);
unmap_gpio:
    iounmap(dht_dev.gpio0_base);
    return -1;
}

static void __exit dht11_exit(void)
{
    cdev_del(&dht_dev.dht11_cdev);
    device_destroy(dht_dev.dht11_class, dht_dev.dev_num);
    class_destroy(dht_dev.dht11_class);
    unregister_chrdev_region(dht_dev.dev_num, 1);
    iounmap(dht_dev.gpio0_base);
    pr_info("DHT11: Driver đã được gỡ bỏ\n");
}

module_init(dht11_init);
module_exit(dht11_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(ADMIN);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERS);
