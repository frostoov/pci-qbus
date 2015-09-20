/*
  pci-qbus.c

  PCI-QBUS driver

  v1.0 Dec-2006 by Solo
  v1.1 Nov-2007 by Solo - new firmware of pci-qbus board
  v1.2 Nov-2007 by Solo - kernel 2.6.9 port
  v1.3 Apr-2008 by Solo - new kernel 2.6.19 support, new PCI driver registration scheme
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/ioctl.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/version.h>
#include <linux/io.h>
#include <linux/uaccess.h>

#define PCI_QBUS_VERSION "1.3"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
#error "Kernels older that 2,6,19 are not supported by this file"
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Oleg.Solovyanov@ihep.ru");
MODULE_DESCRIPTION("Character driver for IHEP PCI-QBUS interface cards");
MODULE_VERSION(PCI_QBUS_VERSION);

// global symbols
static const int PCI_QBUS_VENDOR_ID = 0x1172;
static const int PCI_QBUS_DEVICE_ID = 3;

#define PCI_QBUS_MAXDEV      4
#define PCI_QBUS_IO_RANGE   32

#define PCI_QBUS_STATUS_PORT 0
#define PCI_QBUS_VECTOR_PORT 4
#define PCI_QBUS_ADDR_PORT   8
#define PCI_QBUS_DATA_PORT  12
#define PCI_QBUS_ADDW_PORT  16

#define PCI_QBUS_REG5_PORT  20
#define PCI_QBUS_REG6_PORT  24
#define PCI_QBUS_REG7_PORT  28

#define PCI_QBUS_READY       1
#define PCI_QBUS_TIMEOUT     2
#define PCI_QBUS_INTERRUPT   4

inline dev_t pciQbusDeviceNum(const struct file* filp) {
    return filp->f_path.dentry->d_inode->i_rdev;
}

// global variables
static unsigned int pci_qbus_major = 0;
static char name[PCI_QBUS_MAXDEV][4] = {"pq0", "pq1", "pq2", "pq3"};
static int io[PCI_QBUS_MAXDEV];
static int irq[PCI_QBUS_MAXDEV];
static int debug = 0;
static int interrupts = 0;

module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "debug level (default 0)");
module_param(interrupts, int, 0);
MODULE_PARM_DESC(interrupts, "use interrupts (default 0)");

// delay in usec using ISA port I/O
void pci_qbus_delay(int us) {
    int t;
    for (t = 0; t < us; t++)
        outb(0, 0x80);
}

// interrupt service routine
irqreturn_t pci_qbus_interrupt(int irq, void* dev_id)
{
    if (debug > 1) printk(KERN_INFO "pci-qbus: irq=%d interrupt from io=0x%x\n", irq, (uintptr_t)dev_id);
    return IRQ_NONE; // nothing to be done, allow other handlers to run
}

// device open routine
int pci_qbus_open(struct inode* inode, struct file* filp) {
    int port = io[pciQbusDeviceNum(filp)]; // base port
    short w;

    int minor = MINOR(inode->i_rdev);
    if(debug > 0) printk(KERN_INFO "pci-qbus: %s open(0x%x,%d)\n", name[minor], io[minor], irq[minor]);

    // check if card is accessible
    if(debug > 0) {
        outw(0x5a5a, port + PCI_QBUS_REG5_PORT);
        w = inw(port + PCI_QBUS_REG5_PORT);
        if(w != 0x5a5a) printk(KERN_WARNING "pci-qbus: %s error reg5_port - write %x read %x base=%x\n", name[minor], 0x5a5a, w, port);
    }

    return 0;
}

// device close routine
int pci_qbus_release(struct inode* inode, struct file* filp) {
    int minor = MINOR(inode->i_rdev);
    if(debug > 0) printk(KERN_INFO "pci-qbus: %s close\n", name[minor]);
    return 0;
}

// address setup and cleanup
loff_t pci_qbus_llseek(struct file* filp, loff_t offset, int whence) {
    int port = io[pciQbusDeviceNum(filp)]; // base port

    if(debug > 0)
        printk(KERN_INFO "pci-qbus: %s lseek(0x%x,%d)\n", name[pciQbusDeviceNum(filp)], ((short)offset) & 0xffff, whence);

    switch(whence) {
    // SEEK_SET - set address
    case SEEK_SET: {
        filp->f_pos = offset;
        break;
    }
    // SEEK_CUR - clear error status
    case SEEK_CUR: {
        outw(0, port + PCI_QBUS_VECTOR_PORT);
        break;
    }
    // SEEK_END - device+branch reset
    case SEEK_END: {
        outw(0, port + PCI_QBUS_STATUS_PORT);
        break;
    }
    default:
        break;
    }

    return filp->f_pos;
}

// read from device
ssize_t pci_qbus_read(struct file* filp, char* buf, size_t count, loff_t* offp) {
    unsigned int port = io[pciQbusDeviceNum(filp)]; // base port
    unsigned short w;

    outw(filp->f_pos, port + PCI_QBUS_ADDR_PORT); // write addr to perform cycle
    while((w = inw(port + PCI_QBUS_STATUS_PORT)) == 0) ; // wait for resonse
    if(w == 1) {
        w = inw(port + PCI_QBUS_DATA_PORT);       // read data
        copy_to_user(buf, &w, 2);

        if(debug > 0)
            printk(KERN_INFO "pci-qbus: %s read addr=0x%x data=0x%x\n", name[pciQbusDeviceNum(filp)], (unsigned short)filp->f_pos, (unsigned short)w);

        return 2;
    }

    outw(0, PCI_QBUS_VECTOR_PORT); // clear timeout status
    if(debug > 0)
        printk(KERN_WARNING "pci-qbus: %s read timeout!\n", name[pciQbusDeviceNum(filp)]);
    return 0;
}

// write to device
ssize_t pci_qbus_write(struct file* filp, const char* buf, size_t count, loff_t* offp) {
    unsigned int port = io[pciQbusDeviceNum(filp)]; // base port
    unsigned short w;

    copy_from_user(&w, buf, 2);

    if(debug > 0)
        printk(KERN_INFO "pci-qbus: %s write addr=0x%x data=0x%x\n", name[pciQbusDeviceNum(filp)], (unsigned short)filp->f_pos, (unsigned short)w);

    outw(w, port + PCI_QBUS_DATA_PORT);         // set data word to write
    outw(filp->f_pos, port + PCI_QBUS_ADDW_PORT); // write addr to perform cycle
    while((w = inw(port + PCI_QBUS_STATUS_PORT)) == 0) ; // wait for resonse
    if(w == 1) {
        return 2;
    }

    outw(0, port + PCI_QBUS_VECTOR_PORT); // clear timeout status
    if(debug > 0)
        printk(KERN_WARNING "pci-qbus: %s write timeout 0x%x!\n", name[pciQbusDeviceNum(filp)], w);
    return 0;
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
// old pci driver model init
static int __init pci_qbus_init(void) {
    int i, err;
    struct pci_dev* dev = NULL;

    printk(KERN_INFO "pci-qbus: v" PCI_QBUS_VERSION " by Solo\n");
    i = register_chrdev(0, "pq", &pci_qbus_fops);
    if(i < 0) {
        printk("pci-qbus: can't get major number\n");
        return i;
    }
    pci_qbus_major = i;

    // clear tables
    for(i = 0; i < PCI_QBUS_MAXDEV; i++) {
        io[i] = 0;
        irq[i] = 0;
    }

    // find available cards
    for(i = 0; i < PCI_QBUS_MAXDEV; i++) {
        dev = pci_get_device(PCI_QBUS_VENDOR_ID, PCI_QBUS_DEVICE_ID, dev);
        if(!dev) break; // no more devices

        // enable device
        if ( (err = pci_enable_device(dev)) < 0 ) return err;

        io[i] = pci_resource_start(dev, 0); // get the base I/O address
        irq[i] = dev->irq;
        printk("pci-qbus: found card io=0x%x irq=0x%x\n", io[i], irq[i]);
    }

    // allocate hw resources
    for(i = 0; i < PCI_QBUS_MAXDEV; i++) {
        if(io[i] > 0) {
            if(request_region(io[i], PCI_QBUS_IO_RANGE, name[i]) == NULL) {
                printk(KERN_ERR "%s:cannot allocate PCI I/O port %d\n", name[i], io[i]);
                io[i] = 0;
                irq[i] = 0;
            }
            if(interrupts > 0 && irq[i] > 0) {
                if(request_irq(irq[i], pci_qbus_interrupt, IRQF_SHARED, name[i], &io[i]) < 0) {
                    printk(KERN_ERR "%s:cannot allocate interrupt line %d\n", name[i], irq[i]);
                    irq[i] = 0;
                }
            }
        }
    }

    return 0;
}

// old pci driver model exit
static void __exit pci_qbus_exit(void) {
    int i;

    printk(KERN_INFO "pci-qbus: module unload\n");

    // release hw resources
    for(i = 0; i < PCI_QBUS_MAXDEV; i++) {
        if(io[i] > 0) {
            release_region(io[i], PCI_QBUS_IO_RANGE);
        }
        if(interrupts > 0 && irq[i] > 0) {
            free_irq(irq[i], &io[i]);
        }
    }

    unregister_chrdev(pci_qbus_major, "pq");
}

// registration
module_init(pci_qbus_init);
module_exit(pci_qbus_exit);
