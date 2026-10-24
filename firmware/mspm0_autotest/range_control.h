#ifndef RANGE_CONTROL_H
#define RANGE_CONTROL_H

#include <stdint.h>

typedef enum {
    RANGE_ALL_OFF = 0,
    RANGE_4R7     = 1,
    RANGE_47R     = 2,
    RANGE_470R    = 3,
    RANGE_4K7     = 4
} RangeSelection;

void RangeControl_init(void);
void RangeControl_select(RangeSelection range);
RangeSelection RangeControl_getActive(void);

#endif
