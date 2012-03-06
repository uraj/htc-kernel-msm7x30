#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/kernel.h>
#include <linux/elogk.h>
/*
 * Our key idea: list every important syscall and instrument their
 * core rountines (i.e., do_vfs_read). By parsing the registers dumped
 * when kretprobes (in some cases we will use jprobes only, because
 * there is no need to know how the syscall returns, e.g., fd_install)
 * sucessfully intercept the routine at the entry, according to our
 * knowledge to the ABI of the architecture we are working on (ARM).
 *
 * We choose 3 syscalls to begin with:
 *   1. open
 *   2. read
 *   3. write
 * Corresponding core subroutines are:
 *   1. do_sys_open
 *   2. vfs_read
 *   3. vfs_write
 *
 * Note: do_sys_open is not an exported symbol, while vfs_read and
 * vfs_write are. What symbols are elected to be exported may be
 * an interesting question. Figure it out.
 */

#define regs_arg(regs,no)      ((regs)->ARM_r ## no)

#define EEVENT_OPEN_NO       1
#define EEVENT_READ_NO       2
#define EEVENT_WRITE_NO      3

/*
 * hooked func:
 * void fd_install(unsgined int fd, struct file *file)
 */
static void open_handler(unsigned int fd, struct file *file)
{
    static __s16 id = 0;
    static char buf[128];
    struct eevent_t *eevent = (eevent_t *)buf;
    const unsigned char *fname = file->f_path.dentry->d_name.name;
    unsigned int fname_len = file->f_path.dentry->d_name.len;
    
    eevent->syscall_no = EEVENT_OPEN_NO;
    eevent->id = id++;
    eevent->len =
        sprintf(eevent->params, "%u,\"%.*s\"",
                fd, fname_len, fname);

    elogk(&eevent);

    jprobe_return();
}

static struct jprobe open_jprobe =
{
    .entry = open_handler,
    .kp =
    {
        .symbol = "fd_install",
    },
};

/*
 * hooked func:
 * ssize_t vfs_read(struct file *file, char *buf, size_t count, loff_t *pos)
 */
static int read_entry_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    static __s16 id = 0;
    static char buf[128];
    struct eevent_t *eevent = (eevent_t *)buf;

    struct file *arg0 = regs_arg(regs, 0);
    char *arg1        = regs_arg(regs, 1);
    size_t arg2       = regs_arg(regs, 2);
    loff_t *arg3      = regs_arg(regs, 3);
    
    const unsigned char *fname = arg0->f_path.dentry->d_name.name;
    unsigned int fname_len = arg0->f_path.dentry->d_name.len;
    
    *((__s16 *)ri->data) = id;
    eevent->syscall_no = EEVENT_READ_NO;
    eevent->id = id++;
    eevent->len =
        sprintf(eevent->params, "\"%.*s\",%x,%u,%x",
                fname_len, fname, arg1, arg2, arg3);
    
    elogk(&eevent);
    
    return 0;
}

static int read_ret_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    static char buf[64];
    struct eevent_t *eevent = (char *)buf;

    /* negative id for return entry */
    eevent->id = -*((__s16 *)ri->data);
    eevent->syscall_no = EEVENT_READ_NO;
    event->len = sprintf(event->params, "%d", regs_return_value(regs));
    
    elogk(&eevent);
    
    return 0;
}

static struct kretprobe read_kretprobe =
{
    .handler = read_ret_handler,
    .entry_handler = read_entry_handler,
    .data_size = sizeof(__s16),
    .kp =
    {
        .symbol_name = "vfs_read",
    },
    .maxactive = 10,
};

/*
 * hooked func:
 * ssize_t vfs_write(struct file *file, char *buf, size_t count, loff_t *pos)
 */
static int write_entry_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    static __s16 id = 0;
    static char buf[128];
    struct eevent_t *eevent = (eevent_t *)buf;

    struct file *arg0 = regs_arg(regs, 0);
    char *arg1        = regs_arg(regs, 1);
    size_t arg2       = regs_arg(regs, 2);
    loff_t *arg3      = regs_arg(regs, 3);
    
    const unsigned char *fname = arg0->f_path.dentry->d_name.name;
    unsigned int fname_len = arg0->f_path.dentry->d_name.len;
    
    *((__s16 *)ri->data) = id;
    eevent->syscall_no = EEVENT_WRITE_NO;
    eevent->id = id++;
    eevent->len =
        sprintf(eevent->params, "\"%.*s\",%x,%u,%x",
                fname_len, fname, arg1, arg2, arg3);
    
    elogk(&eevent);
    
    return 0;
}

static int write_ret_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    static char buf[64];
    struct eevent_t *eevent = (char *)buf;

    /* negative id for return entry */
    eevent->id = -*((__s16 *)ri->data);
    eevent->syscall_no = EEVENT_WRITE_NO;
    event->len = sprintf(event->params, "%d", regs_return_value(regs));
    
    elogk(&eevent);
    
    return 0;
}

static struct kretprobe write_kretprobe =
{
    .handler = write_ret_handler,
    .entry_handler = write_entry_handler,
    .data_size = sizeof(__s16),
    .kp =
    {
        .symbol_name = "vfs_write",
    },
    .maxactive = 10,
};

static int __init etrace_init(void)
{
    int ret;

    ret = register_jprobe(&open_jprobe);
    if (ret < 0)
        goto err;

    ret = register_kretprobe(&read_kretprobe);
    if (ret < 0)
        goto err;

    ret = register_kretprobe(&write_kretprobe);
    if (ret < 0)
        goto err;
    
    return 0;
    
  err:
    printk(KERN_INFO "etrace init failed\n");
    return -1;
}

static void __exit etrace_exit(void)
{
    unregister_jprobe(&open_jprobe);
    unregister_kretprobe(&read_kretprobe);
    unregister_kretprobe(&write_kretprobe);
    return;
}

moduel_init(etrace_init)
moduel_exit(etrace_exit)
MODULE_LICENSE("GPL");

