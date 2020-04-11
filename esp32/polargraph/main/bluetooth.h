#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef void (*bluetooth_stepper_vel_setter_t)(int8_t, int8_t);

void bluetooth_init(bluetooth_stepper_vel_setter_t);
void bluetooth_motor_position(long pos);


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