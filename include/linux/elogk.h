#ifndef _LINUX_ELOGK_H
#define _LINUX_ELOGK_H

#include <linux/types.h>
#include <linux/time.h>

#define ELOG_MMC            1
#define ELOG_NET            2
#define ELOG_SYSCALL        3

#define ELOGK_LOCK_FREE     (1U << 0)

/**
 * 'elogk.h' contans the function prototypes and data structures
 * used to log energy events in the kernel.
 */

struct eevent_t {
    __u16 len;             /* length of the payload */
    __u16 type;            /* system call number */
    __s16 id;              /* ID for the entry, pos:invoke, neg:ret */
    /* identify the subject generating this log. maybe pid or uid */
    __u16 belong;
    struct timespec etime;  /* timestamp for the entry */
    char payload[0];        /* the entry's payload (syscall params) */
} __attribute__ ((packed));

/**
 * Write the energy event into the buffer. This function is not
 * thread-safe. It is the caller's responsibility to eliminate
 * data race. 
 */ 
extern void elogk(struct eevent_t *eevent, int log, int flags);

#endif
