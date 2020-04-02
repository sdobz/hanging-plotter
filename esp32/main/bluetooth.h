void bluetooth_init(void);
void bluetooth_motor_position(long pos);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* Attributes State Machine */
enum
{
    IDX_SVC,

    IDX_MOTOR_VEL_SET,
    IDX_MOTOR_VEL_SET_VALUE,

    IDX_MOTOR_VEL_REAL,
    IDX_MOTOR_VEL_REAL_VALUE,
    IDX_MOTOR_VEL_REAL_CFG,

    IDX_MOTOR_POS,
    IDX_MOTOR_POS_VALUE,
    IDX_MOTOR_POS_CFG,

    HRS_IDX_NB,
};