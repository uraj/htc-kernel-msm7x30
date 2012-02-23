#include <linux/elogk.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/jiffies.h>
#include <linux/proc_fs.h>

#include <mach/htc_battery.h>

#include <asm/uaccess.h>
#include <asm/param.h>

#define tick_to_millsec(n) ((n) * 1000 / HZ)

#define ELOG_BUF_LEN (1U << CONFIG_ELOG_BUF_SHIFT)
#define ELOG_BUF_MASK (ELOG_BUF_LEN - 1)
#define ELOG_BUF(idx) (elog_buf[(idx) & ELOG_BUF_MASK])

struct eevent_t elog_buf[ELOG_BUF_LEN];

DEFINE_MUTEX(elog_dev_mutex);

DEFINE_MUTEX(elog_rw_mutex);
static unsigned int elog_start = 0;
static unsigned int elog_end = 0;

void elogk(struct eevent_t *eevent)
{
    get_fresh_batt_info(&(eevent->ee_vol), &(eevent->ee_curr));
    eevent->time = tick_to_millsec(jiffies);
    
    mutex_lock(&elog_rw_mutex);
    
    ELOG_BUF(elog_end) = *eevent;
    elog_end++;
    if(elog_end - elog_start > ELOG_BUF_LEN)
        elog_start = elog_end - ELOG_BUF_LEN;

    mutex_unlock(&elog_rw_mutex);
    return;
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


////////////////// PROC FS ////////////////////
struct eevent_type_desc
{
    unsigned int id;
    const char const *desc;
};

static struct eevent_type_desc __ee_t_desc[] =
{
    [EE_LCD_BRIGHTNESS] = {.id = EE_LCD_BRIGHTNESS, .desc = "LCD Brightness"},
    [EE_CPU_FREQ] = {.id = EE_CPU_FREQ, .desc = "CPU Frequency"}
};

static int proc_read_elog_desc(char *page, char **start, off_t offset, int count, int *eof, void *data)  
{
    static char buf[2048];
    int i, len;
    
    if(offset  > 0){
        return 0;
    }
    
    for(len = i = 0; i < EE_COUNT; ++i)
    {        
        len += sprintf(buf + len, "%s\t%x\n",
                       __ee_t_desc[i].desc,
                       __ee_t_desc[i].id);
    }
    
    if(len >= 2048)
        return 0;
    
    buf[len] = 0;
    
    memcpy(page, buf, len);
    
    *start = page;
    
    return len;
}

static int __init init_elog_desc(void)
{
    struct proc_dir_entry *elog_desc_file;
    
    elog_desc_file = create_proc_read_entry("elogdesc", 0444, NULL, proc_read_elog_desc, NULL);
    
    if(elog_desc_file == NULL)
        return -ENOMEM;
    else
        return 0;
}

static void __exit exit_elog_desc(void)
{    
    remove_proc_entry("elogdesc", NULL);
}

module_init(init_elog_desc);
module_exit(exit_elog_desc);

MODULE_LICENSE("GPL");
