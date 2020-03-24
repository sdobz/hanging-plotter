void bluetooth_init(void);
void bluetooth_motor_position(long pos);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* Attributes State Machine */
enum
{
    IDX_SVC,
    IDX_CHAR_A,
    IDX_CHAR_VAL_A,
    IDX_CHAR_CFG_A,

    IDX_CHAR_B,
    IDX_CHAR_VAL_B,

    IDX_MOTOR_POS,
    IDX_MOTOR_POS_VALUE,
    IDX_MOTOR_POS_CFG,

    HRS_IDX_NB,
};