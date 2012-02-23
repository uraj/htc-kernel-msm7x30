#ifndef _LINUX_ELOGK_H
#define _LINUX_ELOGK_H

#include <linux/types.h>

/*
 * 'elogk.h' contans the function prototypes and data structures
 * used to log energy events in the kernel.
 */

#define EE_LCD_BRIGHTNESS 0
#define EE_CPU_FREQ       1
#define EE_COUNT          2

struct eevent_t
{
    unsigned short ee_type;
    unsigned short ee_extra;
    unsigned int ee_vol;
    unsigned int ee_curr;
    unsigned int time;
};

/**
 * Write the energy event into the buffer. This function is not
 * thread-safe. It is the caller's responsibility to eliminate
 * data race. 
 */ 
extern void elogk(struct eevent_t *eevent);

#endif
