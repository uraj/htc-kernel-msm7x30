#ifndef _LINUX_ELOGK_H
#define _LINUX_ELOGK_H

#include <linux/types.h>

/**
 * 'elogk.h' contans the function prototypes and data structures
 * used to log energy events in the kernel.
 */

struct eevent_t
{
    __u16 len;              /* length of the payload */
    __u16 syscall_no;       /* system call number */
    __u32 time;             /* timestamp for the entry */
    __u16 current;          /* system loading current */
    char  params[0]         /* the entry's payload (syscall params) */
} __attribute__ ((packed));

/**
 * Write the energy event into the buffer. This function is not
 * thread-safe. It is the caller's responsibility to eliminate
 * data race. 
 */ 
extern void elogk_pre_syscall(struct eevent_t *eevent);
extern void elogk_post_syscall(struct eevent_t *eevent);

#endif
