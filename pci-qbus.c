/*
  pci-qbus.c

  PCI-QBUS driver

  v1.0 Dec-2006 by Solo
  v1.1 Nov-2007 by Solo - new firmware of pci-qbus board
  v1.2 Nov-2007 by Solo - kernel 2.6.9 port
  v1.3 Apr-2008 by Solo - new kernel 2.6.19 support, new PCI driver registration scheme
  v1.4 Sep-2015 by frostoov - new kernels support, new read/write/init/exit
*/

#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>

#define PCI_QBUS_VERSION "1.4"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
#error "Kernels older that 2,6,19 are not supported by this file"
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Oleg.Solovyanov@ihep.ru");
MODULE_DESCRIPTION("Character driver for IHEP PCI-QBUS interface cards");
MODULE_VERSION(PCI_QBUS_VERSION);

// global symbols
static const int PCI_QBUS_VENDOR_ID = 0x1172;
static const int PCI_QBUS_DEVICE_ID = 0x3;


const int  PCI_QBUS_IO_RANGE    = 32;

const int  PCI_QBUS_STATUS_PORT = 0;
const int  PCI_QBUS_VECTOR_PORT = 4;
const int  PCI_QBUS_ADDR_PORT   = 8;
const int  PCI_QBUS_DATA_PORT   = 12;
const int  PCI_QBUS_ADDW_PORT   = 16;

const int  PCI_QBUS_REG5_PORT   = 20;
const int  PCI_QBUS_REG6_PORT   = 24;
const int  PCI_QBUS_REG7_PORT   = 28;

const int PCI_QBUS_READY     = 1;
const int PCI_QBUS_TIMEOUT   = 2;
const int PCI_QBUS_INTERRUPT = 4;

static int pci_qbus_major;
static int pci_qbus_minor;
static int io_port;

static struct cdev pci_qbus_cdev;
static struct class* pci_qbus_device_class;
static struct device* pci_qbus_device;

static const char* const pci_qbus_name = "pq";

// interrupt service routine
irqreturn_t pci_qbus_interrupt(int irq, void* dev_id) {
    return IRQ_NONE; // nothing to be done, allow other handlers to run
}

// device open routine
int pci_qbus_open(struct inode* inode, struct file* filp) {
    if(io_port == 0)
        return -1;
    else
        return 0;
}

// device close routine
int pci_qbus_release(struct inode* inode, struct file* filp) {
    return 0;
}

// address setup and cleanup
loff_t pci_qbus_llseek(struct file* filp, loff_t offset, int whence) {
    switch(whence) {
    // SEEK_SET - set address
    case SEEK_SET: {
        filp->f_pos = offset;
        break;
    }
    // SEEK_CUR - clear error status
    case SEEK_CUR: {
        outw(0, io_port + PCI_QBUS_VECTOR_PORT);
        break;
    }
    // SEEK_END - device+branch reset
    case SEEK_END: {
        outw(0, io_port + PCI_QBUS_STATUS_PORT);
        break;
    }
    default:
        break;
    }

    return filp->f_pos;
}

ssize_t pci_qbus_read_word(struct file* filp, uint16_t* word, loff_t* offp) {
    static uint16_t data;

    // write addr to perform cycle
    outw(filp->f_pos, io_port + PCI_QBUS_ADDR_PORT);
    // wait for resonse
    do {
        data = inw(io_port + PCI_QBUS_STATUS_PORT);
    } while(data == 0);
    if(data == PCI_QBUS_READY) {
         // read data
        *word = inw(io_port + PCI_QBUS_DATA_PORT);
        return sizeof(uint16_t);
    } else {
        outw(0, io_port + PCI_QBUS_VECTOR_PORT); // clear timeout status
        return 0;
    }
}

// read from device
ssize_t pci_qbus_read(struct file* filp, char* buf, size_t count, loff_t* offp) {
    uint16_t word;
    ssize_t ret;
    size_t c = 0;
    while(c < count) {
        ret = pci_qbus_read_word(filp, &word, offp);
        if(ret == 0) {
            break;
        }
        copy_to_user(buf + c, &word, sizeof(word));
        c += ret;
    }

    return c;
}

ssize_t pci_qbus_write_word(struct file* filp, uint32_t word, loff_t* offp) {
    static uint16_t data;

    outw(word, io_port + PCI_QBUS_DATA_PORT);         // set data word to write
    outw(filp->f_pos, io_port + PCI_QBUS_ADDW_PORT); // write addr to perform cycle
    do {
        data = inw(io_port + PCI_QBUS_STATUS_PORT);
    } while(data == 0);
    if(data == PCI_QBUS_READY) {
        return sizeof(uint16_t);
    } else {
        outw(0, io_port + PCI_QBUS_VECTOR_PORT); // clear timeout status
        return 0;
    }
}

// write to device
ssize_t pci_qbus_write(struct file* filp, const char* buf, size_t count, loff_t* offp) {
    uint16_t word;
    ssize_t ret;
    size_t c = 0;

    while(c < count) {
        copy_from_user(&word, buf + c, sizeof(word));
        ret = pci_qbus_write_word(filp, word, offp);
        if(ret == 0) {
            break;
        }
        c += ret;
    }

    return c;
}

// available operations
struct file_operations pci_qbus_fops = {
    .owner = THIS_MODULE,
    .llseek = pci_qbus_llseek,
    .read = pci_qbus_read,
    .write = pci_qbus_write,
    .open = pci_qbus_open,
    .release = pci_qbus_release,
};

static int pci_qbus_setup_cdev(struct cdev* dev) {
    int err, devno = MKDEV(pci_qbus_major, pci_qbus_minor);

    cdev_init(dev, &pci_qbus_fops);
    dev->owner = THIS_MODULE;
    dev->ops   = &pci_qbus_fops;
    err = cdev_add(dev, devno, 1);
    if(err != 0) {
        printk(KERN_NOTICE "Error %d adding pci_qbus", err);
        return err;
    }
    return 0;
}

static int __init pci_qbus_init(void) {
    int err;
    struct pci_dev* pci_dev = NULL;
    dev_t devno;

    printk(KERN_INFO "pci-qbus: v" PCI_QBUS_VERSION " by Solo\n");
    io_port = 0;

    err = alloc_chrdev_region(&devno, 0, 1, "pq");
    if(err != 0) {
        printk(KERN_WARNING "pci-qbus: failed alloc chrdev region\n");
        return err;
    }
    pci_qbus_major = MAJOR(devno);
    pci_qbus_minor = MINOR(devno);

    pci_qbus_device_class = class_create(THIS_MODULE, "pci-qbus");
    if(pci_qbus_device_class == NULL) {
        printk(KERN_WARNING "pci-qbus: failed create class\n");
        unregister_chrdev_region(devno, 1);
        return err;
    }

    err = pci_qbus_setup_cdev(&pci_qbus_cdev);
    if(err != 0) {
        printk(KERN_WARNING "pci-qbus: failed setup cdev\n");
        class_destroy(pci_qbus_device_class);
        unregister_chrdev_region(devno, 1);
        return err;
    }

    pci_qbus_device = device_create(pci_qbus_device_class, NULL, devno, NULL, "pq");
    if(pci_qbus_device == NULL) {
        printk(KERN_WARNING "pci-qbus: failed create device\n");
        class_destroy(pci_qbus_device_class);
        unregister_chrdev_region(devno, 1);
        return err;
    }

    pci_dev = pci_get_device(PCI_QBUS_VENDOR_ID, PCI_QBUS_DEVICE_ID, pci_dev);
    if(pci_dev != 0) {
        err = pci_enable_device(pci_dev);
        if(err != 0)
            return err;
        io_port = pci_resource_start(pci_dev, 0);
        printk("pci-qbus: found card io=0x%x\n", io_port);
        if(request_region(io_port, PCI_QBUS_IO_RANGE, pci_qbus_name) == NULL) {
            printk(KERN_WARNING "pci-qbus: failed allocate PCI I/O port %d\n", io_port);
            io_port = 0;
        }
    } else {
        printk(KERN_WARNING "pci_qbus: failed get pci device\n");
    }

    return 0;
}

static void __exit pci_qbus_exit(void) {
    int devno = MKDEV(pci_qbus_major, pci_qbus_minor);
    printk(KERN_INFO "pci-qbus: module unload\n");
    if(io_port > 0)
        release_region(io_port, PCI_QBUS_IO_RANGE);
    device_destroy(pci_qbus_device_class, devno);
    class_destroy(pci_qbus_device_class);
    unregister_chrdev_region(devno, 1);
}

// registration
module_init(pci_qbus_init);
module_exit(pci_qbus_exit);
