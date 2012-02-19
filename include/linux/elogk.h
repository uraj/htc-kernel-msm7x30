#ifndef _LINUX_ELOGK_H
#define _LINUX_ELOGK_H

#include <linux/types.h>
/*
 * 'elogk.h' contans the function prototypes and data structures
 * used to log energy events in the kernel.
 */

enum eevent_type_id
{
    LCD_BRIGHTNESS_CHANGE = 0,
};

struct eevent_t
{
    enum eevent_type_id ee_type;
    unsigned int ee_extra;
    unsigned int voltage;
    unsigned int current;
    unsigned long long time;
};

/**
 * Write the energy event into the buffer. This function is not
 * thread-safe. It is the caller's responsibility to eliminate
 * data race. 
 */ 
int elogk(struct eevent_t *eevent);

#endif
