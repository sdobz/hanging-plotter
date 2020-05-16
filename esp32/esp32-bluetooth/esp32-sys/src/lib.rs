#![no_std]
#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

pub mod std {
    pub use core::*;
    pub mod os {
        pub mod raw {
            pub enum c_void {}
            pub type c_uchar = u8;
            pub type c_schar = i8;
            pub type c_char = i8;
            pub type c_short = i16;
            pub type c_ushort = u16;
            pub type c_int = i32;
            pub type c_uint = u32;
            pub type c_long = i32;
            pub type c_ulong = u32;
            pub type c_longlong = i64;
            pub type c_ulonglong = u64;
        }
    }
}

#[macro_export]
macro_rules! xTimerStart {
    ($xTimer:expr, $xTicksToWait:expr) => {
        xTimerGenericCommand(
            ($xTimer),
            tmrCOMMAND_START,
            (xTaskGetTickCount()),
            ptr::null_ptr(),
            ($xTicksToWait),
        )
    };
}

use core::ptr;

enum TimerCommand {
    tmrCOMMAND_EXECUTE_CALLBACK_FROM_ISR = -2,
    tmrCOMMAND_EXECUTE_CALLBACK = -1,
    tmrCOMMAND_START_DONT_TRACE = 0,
    tmrCOMMAND_START = 1,
    tmrCOMMAND_RESET = 2,
    tmrCOMMAND_STOP = 3,
    tmrCOMMAND_CHANGE_PERIOD = 4,
    tmrCOMMAND_DELETE = 5,

    tmrCOMMAND_START_FROM_ISR = 6,
    tmrCOMMAND_RESET_FROM_ISR = 7,
    tmrCOMMAND_STOP_FROM_ISR = 8,
    tmrCOMMAND_CHANGE_PERIOD_FROM_ISR = 9,
}

impl TimerCommand {
    pub const tmrFIRST_FROM_ISR_COMMAND: TimerCommand = TimerCommand::tmrCOMMAND_START_FROM_ISR;
}

pub unsafe fn xTimerStart(handle: TimerHandle_t, ticksToWait: u32) -> i32 {
    xTimerGenericCommand(
        handle,
        TimerCommand::tmrCOMMAND_START as i32,
        xTaskGetTickCount(),
        ptr::null_mut::<i32>(),
        ticksToWait,
    )
}

pub unsafe fn xTimerStop(handle: TimerHandle_t, ticksToWait: u32) -> i32 {
    xTimerGenericCommand(
        handle,
        TimerCommand::tmrCOMMAND_STOP as i32,
        0,
        ptr::null_mut::<i32>(),
        ticksToWait,
    )
}

pub unsafe fn xTimerReset(handle: TimerHandle_t, ticksToWait: u32) -> i32 {
    xTimerGenericCommand(
        handle,
        TimerCommand::tmrCOMMAND_RESET as i32,
        xTaskGetTickCount(),
        ptr::null_mut::<i32>(),
        ticksToWait,
    )
}

#[macro_export]
macro_rules! esp_error_check {
    ($err:expr) => {
        if ($err != ESP_OK as esp_err_t) {
            //_esp_error_check_failed($err, file!(), line!(),
            //                        __ASSERT_FUNC, #x);
        }
    };
}

#[macro_export]
macro_rules! pdMS_TO_TICKS {
    ($xTimeInMs:expr) => (
        ( $xTimeInMs * configTICK_RATE_HZ ) / 1000
    );
}

pub static pdTRUE: u32 = 1;
pub static pdPASS: i32 = 1;

include!("bindings.rs");
