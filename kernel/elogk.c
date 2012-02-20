#include <linux/elogk.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>

#include <mach/htc_battery.h>

#include <asm/uaccess.h>

#define ELOG_BUF_LEN (1U << CONFIG_ELOG_BUF_SHIFT)
#define ELOG_BUF_MASK (ELOG_BUF_LEN - 1)
#define ELOG_BUF(idx) (elog_buf[(idx) & ELOG_BUF_MASK])

struct eevent_t elog_buf[ELOG_BUF_LEN];

DEFINE_MUTEX(elog_dev_mutex);

DEFINE_MUTEX(elog_rw_mutex);
static unsigned int elog_start = 0;
static unsigned int elog_end = 0;

asmlinkage int elogk(struct eevent_t *eevent)
{
    get_fresh_batt_info(&(eevent->ee_vol), &(eevent->ee_curr));
    
    mutex_lock(&elog_rw_mutex);
    
    ELOG_BUF(elog_end) = *eevent;
    elog_end++;
    if(elog_end - elog_start > ELOG_BUF_LEN)
        elog_start = elog_end - ELOG_BUF_LEN;

    mutex_unlock(&elog_rw_mutex);
    return 0;
}

static int elog_open(struct inode *inode, struct file *file)
{
    int ret;

    ret = nonseekable_open(inode, file);
    if(ret)
        return ret;

    if(file->f_mode != FMODE_READ)
        return -EROFS;

    if(!mutex_trylock(&elog_dev_mutex))
        return -EBUSY;

    return 0;
}

static ssize_t elog_read(struct file *file, char __user *buf,
                         size_t count, loff_t *pos)
{
    ssize_t ret;
    ssize_t __count;
    
    __count = count - (count % sizeof(struct eevent_t));
    
    mutex_lock(&elog_rw_mutex);
    ret = sizeof(struct eevent_t) * (elog_end - elog_start);
    __count = __count < ret ? __count : ret;
    ret = copy_to_user(buf,
                       elog_buf + (elog_start & ELOG_BUF_MASK),
                       __count);
    if(!ret)
        elog_start += __count / sizeof(struct eevent_t);
    
    mutex_unlock(&elog_rw_mutex);

    if(ret)
        return -EFAULT;
    else
        return __count;
}

static int elog_release(struct inode *ignored, struct file *file)
{
    mutex_unlock(&elog_dev_mutex);
    return 0;
}

static const struct file_operations elog_fops =
{
    .owner = THIS_MODULE,
    .read = elog_read,
    .open = elog_open,
    .release = elog_release,
};

static struct miscdevice elog_dev =
{
    .minor = MISC_DYNAMIC_MINOR,
    .name = "elog",
    .fops = &elog_fops,
    .parent = NULL,
};

static int __init elog_init(void)
{
    int ret;

    ret = misc_register(&elog_dev);
    if(unlikely(ret))
    {
        printk(KERN_ERR "elog: failed to regsiter misc device for elog!\n");
        return ret;
    }

    printk(KERN_INFO "elog: created %lu bytes log\n ",
           (unsigned long) sizeof(elog_buf));

    return 0;
}
device_initcall(elog_init);
