#include <linux/module.h>
#include <linux/kprobes.h>

/*
 * Our key idea: list every important syscall and instrument their
 * core rountines (i.e., do_vfs_read). By parsing the registers dumped
 * when kretprobes sucessfully intercept the routine at the entry,
 * according to our knowledge to the ABI of the architecture we are
 * working on.
 */

static void __init etrace_init(void)
{}

static void __exit etrace_exit(void)
{}

moduel_init(etrace_init)
moduel_exit(etrace_exit)
MODULE_LICENSE("GPL");

