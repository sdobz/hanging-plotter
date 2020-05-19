#![no_std]
#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

pub use core::cell::UnsafeCell;
use core::ptr;
use core::marker::{Send, Sync};

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
    ($xTimeInMs:expr) => {
        ($xTimeInMs * configTICK_RATE_HZ) / 1000
    };
}

pub static pdTRUE: u32 = 1;
pub static pdPASS: i32 = 1;

include!("bindings.rs");

pub struct ble_gatt_svc_def_ptr(*const ble_gatt_svc_def);

impl ble_gatt_svc_def_ptr {
    pub const fn new(ptr: *const ble_gatt_svc_def) -> ble_gatt_svc_def_ptr {
        ble_gatt_svc_def_ptr(ptr)
    }
    pub const fn ptr(&self) -> *const ble_gatt_svc_def {
        self.0
    }
}

unsafe impl Send for ble_gatt_svc_def_ptr {}
unsafe impl Sync for ble_gatt_svc_def_ptr {}

unsafe impl Send for ble_gatt_chr_def {}
unsafe impl Sync for ble_gatt_chr_def {}

pub struct UnsafeCellWrapper<T>(pub UnsafeCell<T>);
unsafe impl<T> Send for UnsafeCellWrapper<T> {}
unsafe impl<T> Sync for UnsafeCellWrapper<T> {}

#[repr(C)]
pub struct ble_gatt_chr_def {
    #[doc = " Pointer to characteristic UUID; use BLE_UUIDxx_DECLARE macros to declare"]
    #[doc = " proper UUID; NULL if there are no more characteristics in the service."]
    pub uuid: *const ble_uuid_t,
    #[doc = " Callback that gets executed when this characteristic is read or"]
    #[doc = " written."]
    pub access_cb: ble_gatt_access_fn,
    #[doc = " Optional argument for callback."]
    pub arg: UnsafeCellWrapper<*mut ::core::ffi::c_void>,
    #[doc = " Array of this characteristic's descriptors.  NULL if no descriptors."]
    #[doc = " Do not include CCCD; it gets added automatically if this"]
    #[doc = " characteristic's notify or indicate flag is set."]
    pub descriptors: UnsafeCellWrapper<*mut ble_gatt_dsc_def>,
    #[doc = " Specifies the set of permitted operations for this characteristic."]
    pub flags: ble_gatt_chr_flags,
    #[doc = " Specifies minimum required key size to access this characteristic."]
    pub min_key_size: u8,
    #[doc = " At registration time, this is filled in with the characteristic's value"]
    #[doc = " attribute handle."]
    pub val_handle: UnsafeCellWrapper<*mut u16>,
}

#[macro_export]
macro_rules! mut_ptr {
    ($t:expr) => {
        $crate::UnsafeCellWrapper($crate::UnsafeCell::new($t))
    };
}
