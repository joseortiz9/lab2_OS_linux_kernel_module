/*
 *  chardev.c - Create an input/output character device
 */

#include <linux/kernel.h>    /* We're doing kernel work */
#include <linux/module.h>    /* Specifically, a module */
#include <linux/version.h>
#include <linux/fs.h>

#include <linux/if_tun.h>
#include <linux/if_macvlan.h>

#include <asm/uaccess.h>    /* for get_user and put_user */
#include <linux/net.h>    /* first structure */
#include <linux/memblock.h>    /* second structure */

#include "chardev.h"

#define SUCCESS 0
#define DEVICE_NAME "char_dev"
#define BUF_LEN 100

/* 
 * Is the device open right now? Used to prevent
 * concurent access into the same device 
 */
static int Device_Open = 0;

/* 
 * The message the device will give when asked 
 */
static char Message[BUF_LEN];
static char output[BUF_LEN];

/* 
 * How far did the process reading the message get?
 * Useful if the message is larger than the size of the
 * buffer we get to fill in device_read. 
 */
static char *Message_Ptr;


static struct memblock *res_memblock;

/* 
 * This is called whenever a process attempts to open the device file 
 */
static int device_open(struct inode *inode, struct file *file) {
    printk(KERN_INFO "device_open(%p)\n", file);

    // We don't want to talk to two processes at the same time
    if (Device_Open) {
        return -EBUSY;
    }

    Device_Open++;
    // Initialize the message
    Message_Ptr = Message;
    try_module_get(THIS_MODULE);
    return SUCCESS;
}

static int device_release(struct inode *inode, struct file *file) {
    printk(KERN_INFO "device_release(%p,%p)\n", inode, file);

    /*
     * We're now ready for our next caller
     */
    Device_Open--;

    module_put(THIS_MODULE);
    return SUCCESS;
}

/* 
 * This function is called whenever a process which has already opened the
 * device file attempts to read from it.
 */
static ssize_t device_read(
        struct file *file,    /* see include/linux/fs.h   */
        char __user *buffer,    /* buffer to be filled with data */
        size_t length,    /* length of the buffer     */
        loff_t *offset
        ) {
    /*
     * Number of bytes actually written to the buffer
     */
    int bytes_read = 0;

    printk(KERN_INFO "device_read(%p,%p,%d)\n", file, buffer, length);

    /*
     * If we're at the end of the message, return 0
     * (which signifies end of file)
     */
    if (*Message_Ptr == 0) {
        return 0;
    }


    /*
     * Actually put the data into the buffer
     */
    while (length && *Message_Ptr) {
        /*
         * Because the buffer is in the user data segment,
         * not the kernel data segment, assignment wouldn't
         * work. Instead, we have to use put_user which
         * copies data from the kernel data segment to the
         * user data segment.
         */
        put_user(*(Message_Ptr++), buffer++);
        length--;
        bytes_read++;
    }

    printk(KERN_INFO "Read %d bytes, %d left\n", bytes_read, length);

    /*
     * Read functions are supposed to return the number
     * of bytes actually inserted into the buffer
     */
    return bytes_read;
}

/* 
 * This function is called when somebody tries to
 * write into our device file. 
 */
static ssize_t device_write(struct file *file, const char __user *buffer, size_t length, loff_t *offset) {
    int i;

    printk(KERN_INFO "device_write(%p,%s,%d)", file, buffer, length);

    for (i = 0;i < length && i < BUF_LEN; i++)
        get_user(Message[i], buffer + i);

    Message_Ptr = Message;

    /*
     * Again, return the number of input characters used
     */
    return i;
}

static struct socket *get_socket(struct file *file) {
    struct socket *sock;

    if (!file)
        return ERR_PTR(-EBADF);
    sock = tun_get_socket(file);
    if (!IS_ERR(sock))
        return sock;
//    sock = macvtap_get_socket(file);
//    if (IS_ERR(sock))
//        fput(file);
    return sock;
}


/* 
 * This function is called whenever a process tries to do an ioctl on our
 * device file. We get two extra parameters (additional to the inode and file
 * structures, which all device functions get): the number of the ioctl called
 * and the parameter given to the ioctl function.
 *
 * If the ioctl is write or read/write (meaning output is returned to the
 * calling process), the ioctl call returns the output of this function.
 *
 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35))
static int device_ioctl(struct inode *inode,    /* see include/linux/fs.h */
                 struct file *file,    /* ditto */
                 unsigned int ioctl_num,    /* number and param for ioctl */
                 unsigned long ioctl_param)
#else
static long device_ioctl(struct file *file, unsigned int ioctl_num, unsigned long ioctl_param)
#endif
{
    int i;
    char *temp;
    char ch;

    /*
     * Switch according to the ioctl called
     */
    switch (ioctl_num) {
        case IOCTL_SET_MSG:
            /*
             * Receive a pointer to a message (in user space) and set that
             * to be the device's message.  Get the parameter given to
             * ioctl by the process.
             */
            temp = (char *) ioctl_param;

            // Find the length of the message
            get_user(ch, temp);
            for (i = 0; ch && i < BUF_LEN; i++, temp++) {
                get_user(ch, temp);
            }

            device_write(file, (char *) ioctl_param, i, 0);
            break;

        case IOCTL_GET_MSG:

            printk(KERN_INFO "Received msg from user_space: %s\n", Message);

            // Initialize output with empty string.
            sprintf(output, "%s", "");

            //int fd = fileno(file);

            if (strcmp(Message, "sock") == 0) {
                struct socket *sock = get_socket(file);
                if (sock == NULL)
                    printk(KERN_ALERT "Socket undefined!");

                strcat(output, "flags: ");
                strcat(output, sock->flags);
                strcat(output, "type: ");
                strcat(output, sock->type);
            }
            else if (strcmp(Message, "memblock") == 0) {
                struct memblock res_memblock;
                int memblock_memory_init_regions = 128;
                res_memblock = (struct memblock) {
                        .bottom_up = false,
                        .current_limit = 0x1,
                        .memory.regions = memblock_memory_init_regions,
                        .memory.cnt = 1,
                        .memory.max = memblock_memory_init_regions * 2,
                        .reserved.regions = memblock_memory_init_regions,
                        .reserved.cnt = 1,
                        .reserved.max = memblock_memory_init_regions * 2,
                };
                strcat(output, "memblock current_limit: ");
                char buff_int[10];
                sprintf(buff_int, "%d", res_memblock.current_limit);
                strcat(output, buff_int);
                strcat(output, "\n");
                strcat(output, "memblock memory max: ");
                sprintf(buff_int, "%d", res_memblock.memory.max);
                strcat(output, buff_int);
            }

            if (strcmp(output, "") == 0) {
                sprintf(output, "%s", "Wrong input");
            }
            strcat(output, "\n");

            printk(KERN_ALERT "output: %s", output);

            // Restart read message from user space with empty string.
            sprintf(Message, "%s", "");

            for (i = 0; i < strlen(output) && i < BUF_LEN; ++i) {
                Message[i] = output[i];
            }
            Message_Ptr = Message;

            /*
             * Give the current message to the calling process -
             * the parameter we got is a pointer, fill it.
             */
            i = device_read(file, (char *) ioctl_param, i, 0);

            /*
             * Put a zero at the end of the buffer, so it will be
             * properly terminated
             */
            put_user('\0', (char *) ioctl_param + i);
            break;

        case IOCTL_GET_NTH_BYTE:
            /*
             * This ioctl is both input (ioctl_param) and
             * output (the return value of this function)
             */
            return Message[ioctl_param];
            break;
    }
    return SUCCESS;
}

/* Module Declarations */

/* 
 * This structure will hold the functions to be called
 * when a process does something to the device we
 * created. Since a pointer to this structure is kept in
 * the devices table, it can't be local to
 * init_module. NULL is for unimplemented functions. 
 */
struct file_operations Fops = {
        .owner   = THIS_MODULE,
        .read    = device_read,
        .write   = device_write,
        .open    = device_open,
        .release = device_release,    /* a.k.a. close */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35))
        .ioctl = device_ioctl
#else
        .unlocked_ioctl = device_ioctl
#endif
};

/* 
 * Initialize the module - Register the character device 
 */
int init_module() {
    int ret_val;
    /*
     * Register the character device (atleast try)
     */
    ret_val = register_chrdev(MAJOR_NUM, DEVICE_NAME, &Fops);

    /*
     * Negative values signify an error
     */
    if (ret_val < 0) {
        printk(KERN_ALERT "%s failed with %d\n", "Sorry, registering the character device ", ret_val);
        return ret_val;
    }

    printk(KERN_INFO "\n%s The major device number is %d.\n", "Registration is a success", MAJOR_NUM);
    printk(KERN_INFO "If you want to talk to the device driver,\n");
    printk(KERN_INFO "you'll have to create a device file. \n");
    printk(KERN_INFO "We suggest you use:\n");
    printk(KERN_INFO "mknod %s c %d 0\n", DEVICE_FILE_NAME, MAJOR_NUM);
    printk(KERN_INFO "The device file name is important, because\n");
    printk(KERN_INFO "the ioctl program assumes that's the\n");
    printk(KERN_INFO "file you'll use.\n");

    return 0;
}

/* 
 * Cleanup - unregister the appropriate file from /proc 
 */
void cleanup_module() {
    //int ret;

    /*
     * Unregister the device
     */
    unregister_chrdev(MAJOR_NUM, DEVICE_NAME);

    /*
     * If there's an error, report it
     */
//    if (ret < 0) {
//        printk(KERN_ALERT "Error: unregister_chrdev: %d\n", ret);
//    }
}

MODULE_VERSION("0.0.1");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jose Ortiz");
MODULE_DESCRIPTION("Simple Linux device driver (IOCTL) reading the processes socket, memblock");