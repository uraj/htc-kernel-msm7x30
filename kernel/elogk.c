#include <linux/elogk.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/jiffies.h>
#include <linux/proc_fs.h>

#include <mach/htc_battery.h>

#include <asm/uaccess.h>
#include <asm/param.h>

#define HZ100 /* this is the defualt configuration for ARM architecture */

#ifdef HZ100
#define tick_to_millsec(n) ((n) * 10)
#else
#define tick_to_millsec(n) (((n) * 1000) / HZ
#endif

#define ELOG_BUF_LEN (1U << (CONFIG_ELOG_BUF_SHIFT - 1))
#define ELOG_BUF_MASK (ELOG_BUF_LEN - 1)

__u8 elog_buf1[ELOG_BUF_LEN];
__u8 elog_buf2[ELOG_BUF_LEN];
#define LOG1_MAGIC          0x0ade1081
#define LOG2_MAGIC          0x0ade1082

/* only one user process can open elog pool */
DEFINE_MUTEX(elog_dev_mutex);

struct elogk_suit
{
    struct mutex mutex;
    size_t w_off;
    size_t r_off;
    __u8* elogk_buf;
};

#define DEFINE_ELOGK_SUIT(NAME)                     \
    static __u8 __buf_ ## NAME[ELOG_BUF_LEN];       \
    static struct elogk_suit NAME =                 \
    {                                               \
        .mutex = __MUTEX_INITIALIZER(NAME .mutex),  \
        .w_off = 0,                                 \
        .r_off = 0,                                 \
        .elogk_buf = __buf_ ## NAME,                \
    };

DEFINE_ELOGK_SUIT(elogk1)
DEFINE_ELOGK_SUIT(elogk2)

/*
 * grabs the length of the payload of the next entry starting
 * from 'off'. caller needs to hold elog->mutex.
 */
static __u32 get_entry_len(struct elogk_suit *elog, size_t off)
{
	__u16 val;

	switch (ELOG_BUF_LEN - off)
    {
        case 1:
            memcpy(&val, elog->elogk_buf + off, 1);
            memcpy(((__u8 *) &val) + 1, elog->elogk_buf, 1);
            break;
        default:
            memcpy(&val, elog->elogk_buf + off, 2);
	}

	return sizeof(struct eevent_t) + val;
}

/*
 * return the offset of the first valid entry 'len' bytes after 'off'.
 * caller must hold elog->mutex.
 */
static size_t get_next_entry(struct elogk_suit *elog, size_t off, size_t len)
{
	size_t count = 0;

	do {
		size_t nr = get_entry_len(elog, off);
		off = (off + nr) & ELOG_BUF_MASK;
		count += nr;
	} while (count < len);

	return off;
}

/*
 * write the content of a log entry to the buff. caller must
 * hold elog->mutex
 */
static void elogk_write(struct eevent_t *eevent, struct elogk_suit *elog)
{
    ssize_t ret;
    size_t entry_len, buffer_tail, __w_off;

    entry_len = sizeof(struct eevent_t) + eevent->len;
    __w_off = (elog->w_off) & ELOG_BUF_MASK;
    buffer_tail = ELOG_BUF_LEN - __w_off;
    
    elog->w_off += entry_len;
    /*
     * if buffer overflows, pull the read pointer foward to the first
     * readable entry
     */
    if((elog->w_off - elog->r_off) > ELOG_BUF_LEN)
        elog->r_off = elog->w_off - ELOG_BUF_LEN
            + get_next_entry(elog, __w_off, entry_len);
    
    if(buffer_tail >= entry_len)
        memcpy(elog->elogk_buf + __w_off, eevent, entry_len);
    else /* ring buffer reaches the end, write operation split into 2 memcpy */
    {
        memcpy(elog->elogk_buf + __w_off, eevent, buffer_tail);
        memcpy(elog->elogk_buf,
               ((__u8 *)eevent) + buffer_tail,
               entry_len - buffer_tail);
    }
    
    return;
}

void elogk_pre_syscall(struct eevent_t *eevent)
{
    eevent->time = tick_to_millsec(jiffies);
    
    mutex_lock(&elog_rw_mutex);
    
    ELOG_BUF(elog_end) = *eevent;
    elog_end++;
    if(elog_end - elog_start > ELOG_BUF_LEN)
        elog_start = elog_end - ELOG_BUF_LEN;

    mutex_unlock(&elog_rw_mutex);
    return;
}

void elogk_post_syscall(struct eevent_t *eevent)
{}


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
