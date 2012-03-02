#ifndef _LINUX_ELOGK_H
#define _LINUX_ELOGK_H

#include <linux/types.h>

/*
 * 'elogk.h' contans the function prototypes and data structures
 * used to log energy events in the kernel.
 */

struct eevent_t
{
    unsigned short ee_type;
    unsigned short ee_reserved;
    unsigned int ee_extra;
    unsigned short ee_vol;
    unsigned short ee_curr;
    unsigned int time;
} __attribute__ ((packed));

/**
 * Write the energy event into the buffer. This function is not
 * thread-safe. It is the caller's responsibility to eliminate
 * data race. 
 */ 
extern void elogk(struct eevent_t *eevent, int if_fresh_binfo);

#endif
