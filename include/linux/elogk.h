#ifndef _LINUX_ELOGK_H
#define _LINUX_ELOGK_H

#include <linux/types.h>
#include <linux/time.h>

#define ELOG_MMC            2
#define ELOG_VFS            3

#define ELOGK_LOCK_FREE     (1U << 0)
#define ELOGK_WITHOUT_TIME  (2U << 0)

/**
 * 'elogk.h' contans the function prototypes and data structures
 * used to log energy events in the kernel.
 */

struct elog_t {
    __u16 len;             /* length of the payload */
    __u16 type;            /* type of this entry */
    /* identify the subject generating this log. maybe pid or uid */
    __u32 belong;
    struct timespec etime;  /* timestamp for the entry */
    __u8 payload[0];        /* the entry's payload (syscall params) */
} __attribute__ ((packed));

/**
 * Write the energy event into the buffer. This function is not
 * thread-safe. It is the caller's responsibility to eliminate
 * data race. 
 */ 
extern void elogk(struct elog_t *eevent, int log, int flags);

#endif
