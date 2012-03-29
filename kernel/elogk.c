#include <linux/elogk.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/proc_fs.h>

#include <asm/uaccess.h>

#define ELOG_BUF_LEN (1U << (CONFIG_ELOG_BUF_SHIFT - 1))
#define ELOG_BUF_MASK (ELOG_BUF_LEN - 1)

struct elogk_suit {
    struct mutex rw_mutex;
    struct mutex open_mutex;
    size_t w_off;
    size_t r_off;
    __u8* elogk_buf;
};

#define DEFINE_ELOGK_SUIT(NAME)                                 \
    static __u8 __buf_ ## NAME[ELOG_BUF_LEN];                   \
    static struct elogk_suit NAME = {                           \
        .rw_mutex = __MUTEX_INITIALIZER(NAME .rw_mutex),        \
        .open_mutex = __MUTEX_INITIALIZER(NAME .open_mutex),    \
        .w_off = 0,                                             \
        .r_off = 0,                                             \
        .elogk_buf = __buf_ ## NAME,                            \
    };

DEFINE_ELOGK_SUIT(elogk_mmc)
DEFINE_ELOGK_SUIT(elogk_syscall)

/*
 * grabs the length of the payload of the next entry starting
 * from 'off'. caller needs to hold elog->rw_mutex.
 */
static __u32 get_entry_len(struct elogk_suit *elog, size_t off)
{
    __u16 val;

    switch (ELOG_BUF_LEN - off) {
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
 * caller must hold elog->rw_mutex.
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
 * hold elog->rw_mutex
 */
static void elogk_write(struct eevent_t *eevent, struct elogk_suit *elog)
{
    size_t entry_len, buffer_tail, __w_off, __r_off, __new_off, diff;
    
    __w_off = (elog->w_off) & ELOG_BUF_MASK;
    buffer_tail = ELOG_BUF_LEN - __w_off;
    entry_len = sizeof(struct eevent_t) + eevent->len;
    elog->w_off += entry_len;
    diff = elog->w_off - elog->r_off;
    
    /*
     * if buffer overflows, pull the read pointer foward to the first
     * readable entry
     */
    if (diff > ELOG_BUF_LEN) {
        __r_off = elog->r_off & ELOG_BUF_MASK;
        __new_off = get_next_entry(elog, __r_off, diff & ELOG_BUF_MASK);
        if(__new_off > __r_off)
            elog->r_off += __new_off - __r_off;
        else
            elog->r_off += ELOG_BUF_LEN - __r_off + __new_off;
    }
    
    if (buffer_tail >= entry_len)
        memcpy(elog->elogk_buf + __w_off, eevent, entry_len);
    else {/* ring buffer reaches the end, write operation split into 2 memcpy */
        memcpy(elog->elogk_buf + __w_off, eevent, buffer_tail);
        memcpy(elog->elogk_buf,
               ((__u8 *)eevent) + buffer_tail,
               entry_len - buffer_tail);
    }

    return;
}

void elogk(struct eevent_t *eevent, int log, int flags)
{
    struct elogk_suit *elog;
    
    ktime_get_ts(&eevent->etime);

    switch (log) {
        case ELOG_MMC:
            elog=&elogk_mmc;
            break;
        case ELOG_SYSCALL:
            elog=&elogk_syscall;
            break;
        default:
            return;
    }

    if(flags & ELOGK_LOCK_FREE) {
        mutex_lock(&elog->rw_mutex);
        elogk_write(eevent, elog);
        mutex_unlock(&elog->rw_mutex);
    } else {
        elogk_write(eevent, elog);
    }
    
    return;
}
EXPORT_SYMBOL(elogk);

static struct elogk_suit *get_elog_from_minor(int);

static int elog_open(struct inode *inode, struct file *file)
{
    struct elogk_suit *elog;
    int ret, minor;
    
    ret = nonseekable_open(inode, file);
    if (ret)
        return ret;

    if (file->f_mode != FMODE_READ)
        return -EROFS;

    minor = MINOR(inode->i_rdev);
    elog = get_elog_from_minor(minor);
    if(elog == NULL)
        return -ENODEV;

    if (!mutex_trylock(&elog->open_mutex))
        return -EBUSY;

    file->private_data = elog;
    
    return 0;
}

static ssize_t elog_read(struct file *file, char __user *buf,
                         size_t count, loff_t *pos)
{
    struct elogk_suit *elog = file->private_data;
    size_t len, __r_off, __count = 0;
    ssize_t ret;
    
    mutex_lock(&elog->rw_mutex);
    
    while (elog->r_off + __count < elog->w_off) {
        __r_off = (elog->r_off + __count) & ELOG_BUF_MASK;
        len = get_entry_len(elog, __r_off);
        __count += len;
        if (__count > count) {
            __count -= len;
            break;
        }
    }

    ret = __count;
    /*
     * Since we are using a ring buffer, one read may need to be
     * split into two disjoint operations due to reaching the end
     * of the buffer.
     */
    __r_off = elog->r_off & ELOG_BUF_MASK;
    len = min(__count, ELOG_BUF_LEN - __r_off);
    if (copy_to_user(buf, elog->elogk_buf + __r_off, len)) {
        ret = -EFAULT;
        goto out;
    }
    
    if(len != __count)
        if (copy_to_user(buf + len, elog->elogk_buf, __count - len)) {
            ret = -EFAULT;
            goto out;
        }

    elog->r_off += __count;

  out:
    mutex_unlock(&elog->rw_mutex);
    
    return ret;
}

static int elog_release(struct inode *ignored, struct file *file)
{
    struct elogk_suit *elog;

    elog = file->private_data;
    mutex_unlock(&elog->open_mutex);
    return 0;
}

static const struct file_operations elog_fops = {
    .owner = THIS_MODULE,
    .read = elog_read,
    .open = elog_open,
    .release = elog_release,
};

static struct miscdevice elog_mmc_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "elog_mmc",
    .fops = &elog_fops,
    .parent = NULL,
};

static struct miscdevice elog_syscall_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "elog_syscall",
    .fops = &elog_fops,
    .parent = NULL,
};

static struct elogk_suit *get_elog_from_minor(int minor)
{
    if (minor == elog_mmc_dev.minor)
        return &elogk_mmc;
    if (minor == elog_syscall_dev.minor)
        return &elogk_syscall;
    return NULL;
}

static int __init elog_init(void)
{
    int ret;

    ret = misc_register(&elog_mmc_dev);
    if (unlikely(ret))
        goto out;

    ret = misc_register(&elog_syscall_dev);
    if (unlikely(ret))
        goto out;
    
  out:
    return ret;
}
device_initcall(elog_init);
