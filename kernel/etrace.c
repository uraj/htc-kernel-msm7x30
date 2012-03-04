#include <linux/module.h>
#include <linux/kprobes.h>

/*
 * Our key idea: list every important syscall and instrument their
 * core rountines (i.e., do_vfs_read). By parsing the registers dumped
 * when kretprobes sucessfully intercept the routine at the entry,
 * according to our knowledge to the ABI of the architecture we are
 * working on.
 */

/*
 * We choose 3 syscalls to begin with:
 *   1. open
 *   2. read
 *   3. write
 * Corresponding core subroutines are:
 *   1. do_sys_open
 *   2. vfs_read
 *   3. vfs_write
 *
 * Note: do_sys_open is not a exported symbol, while vfs_read and
 * vfs_write are. What symbols are elected to be exported may be
 * an interesting question. Figure it out.
 */
static void __init etrace_init(void)
{}

static void __exit etrace_exit(void)
{}

moduel_init(etrace_init)
moduel_exit(etrace_exit)
MODULE_LICENSE("GPL");

