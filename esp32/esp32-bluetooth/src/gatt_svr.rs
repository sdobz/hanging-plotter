use alloc::boxed::Box;
use core::ffi::c_void;
use core::mem::size_of;
use core::ptr;
use esp32_sys::*;

use crate::{cstr, debug, esp_assert};
use debug::print_svcs;

extern crate alloc;

const MANUF_NAME: &str = "Apache Mynewt ESP32 devkitC\0";
const MODEL_NUM: &str = "Mynewt HR Sensor demo\0";
pub static mut HRS_HRM_HANDLE: u16 = 0;

macro_rules! ble_uuid16_declare {
    ($value:expr) => {
        &(ble_uuid16_t {
            u: ble_uuid_t {
                type_: BLE_UUID_TYPE_16 as u8,
            },
            value: $value,
        }
        .u) as *const ble_uuid_t
    };
}

macro_rules! leaky_box {
    ($($items:expr),+) => (
        Box::into_raw(
            Box::new(
                [
                    $($items),*
                ]
            )
        ) as *const _
    )
}

const fn null_ble_gatt_chr_def() -> ble_gatt_chr_def {
    return ble_gatt_chr_def {
        uuid: ptr::null(),
        access_cb: None,
        arg: (ptr::null_mut()),
        descriptors: (ptr::null_mut()),
        flags: 0,
        min_key_size: 0,
        val_handle: (ptr::null_mut()),
    };
}

const fn null_ble_gatt_svc_def() -> ble_gatt_svc_def {
    return ble_gatt_svc_def {
        type_: BLE_GATT_SVC_TYPE_END as u8,
        uuid: ptr::null(),
        includes: ptr::null_mut(),
        characteristics: ptr::null(),
    };
}

/* Heart-rate configuration */
const GATT_HRS_UUID: u16 = 0x180D;
const GATT_HRS_MEASUREMENT_UUID: u16 = 0x2A37;
const GATT_HRS_BODY_SENSOR_LOC_UUID: u16 = 0x2A38;
const GATT_DEVICE_INFO_UUID: u16 = 0x180A;
const GATT_MANUFACTURER_NAME_UUID: u16 = 0x2A29;
const GATT_MODEL_NUMBER_UUID: u16 = 0x2A24;

fn alloc_svc_def() -> *const ble_gatt_svc_def {
    leaky_box!(
        ble_gatt_svc_def {
            type_: BLE_GATT_SVC_TYPE_PRIMARY as u8,
            uuid: ble_uuid16_declare!(GATT_HRS_UUID),
            includes: ptr::null_mut(),
            characteristics: leaky_box!(
                ble_gatt_chr_def {
                    uuid: ble_uuid16_declare!(GATT_HRS_MEASUREMENT_UUID),
                    access_cb: Some(gatt_svr_chr_access_heart_rate),
                    arg: (ptr::null_mut()),
                    descriptors: (ptr::null_mut()),
                    flags: BLE_GATT_CHR_F_NOTIFY as u16,
                    min_key_size: 0,
                    val_handle: (unsafe { &mut HRS_HRM_HANDLE as *mut u16 }),
                },
                ble_gatt_chr_def {
                    uuid: ble_uuid16_declare!(GATT_HRS_BODY_SENSOR_LOC_UUID),
                    access_cb: Some(gatt_svr_chr_access_heart_rate),
                    arg: (ptr::null_mut()),
                    descriptors: (ptr::null_mut()),
                    flags: BLE_GATT_CHR_F_READ as u16,
                    min_key_size: 0,
                    val_handle: ptr::null_mut(),
                },
                null_ble_gatt_chr_def()
            )
        },
        ble_gatt_svc_def {
            type_: BLE_GATT_SVC_TYPE_PRIMARY as u8,
            uuid: ble_uuid16_declare!(GATT_DEVICE_INFO_UUID),
            includes: ptr::null_mut(),
            characteristics: leaky_box!(
                ble_gatt_chr_def {
                    uuid: ble_uuid16_declare!(GATT_MANUFACTURER_NAME_UUID),
                    access_cb: Some(gatt_svr_chr_access_device_info),
                    arg: (ptr::null_mut()),
                    descriptors: (ptr::null_mut()),
                    flags: BLE_GATT_CHR_F_READ as u16,
                    min_key_size: 0,
                    val_handle: (ptr::null_mut()),
                },
                ble_gatt_chr_def {
                    uuid: ble_uuid16_declare!(GATT_MODEL_NUMBER_UUID),
                    access_cb: Some(gatt_svr_chr_access_device_info),
                    arg: (ptr::null_mut()),
                    descriptors: (ptr::null_mut()),
                    flags: BLE_GATT_CHR_F_READ as u16,
                    min_key_size: 0,
                    val_handle: (ptr::null_mut()),
                },
                null_ble_gatt_chr_def()
            )
        },
        null_ble_gatt_svc_def()
    )
}

extern "C" fn gatt_svr_chr_access_heart_rate(
    _conn_handle: u16,
    _attr_handle: u16,
    ctxt: *mut ble_gatt_access_ctxt,
    _arg: *mut ::core::ffi::c_void,
) -> i32 {
    /* Sensor location, set to "Chest" */
    const BODY_SENS_LOC: u8 = 0x01;

    let uuid: u16 = unsafe { ble_uuid_u16((*(*ctxt).__bindgen_anon_1.chr).uuid) };

    if uuid == GATT_HRS_BODY_SENSOR_LOC_UUID {
        let rc: i32 = unsafe {
            os_mbuf_append(
                (*ctxt).om,
                &BODY_SENS_LOC as *const u8 as *const c_void,
                size_of::<u8>() as u16,
            )
        };

        return if rc == 0 {
            0
        } else {
            BLE_ATT_ERR_INSUFFICIENT_RES as i32
        };
    }

    return BLE_ATT_ERR_UNLIKELY as i32;
}

extern "C" fn gatt_svr_chr_access_device_info(
    _conn_handle: u16,
    _attr_handle: u16,
    ctxt: *mut ble_gatt_access_ctxt,
    _arg: *mut ::core::ffi::c_void,
) -> i32 {
    let uuid: u16 = unsafe { ble_uuid_u16((*(*ctxt).__bindgen_anon_1.chr).uuid) };

    if uuid == GATT_MODEL_NUMBER_UUID {
        let rc: i32 = unsafe {
            os_mbuf_append(
                (*ctxt).om,
                MODEL_NUM.as_ptr() as *const c_void,
                MODEL_NUM.len() as u16,
            )
        };
        return if rc == 0 {
            0
        } else {
            BLE_ATT_ERR_INSUFFICIENT_RES as i32
        };
    }

    if uuid == GATT_MANUFACTURER_NAME_UUID {
        let rc: i32 = unsafe {
            os_mbuf_append(
                (*ctxt).om,
                MANUF_NAME.as_ptr() as *const c_void,
                MANUF_NAME.len() as u16,
            )
        };
        return if rc == 0 {
            0
        } else {
            BLE_ATT_ERR_INSUFFICIENT_RES as i32
        };
    }

    return BLE_ATT_ERR_UNLIKELY as i32;
}

pub unsafe extern "C" fn gatt_svr_register_cb(
    ctxt: *mut ble_gatt_register_ctxt,
    _arg: *mut ::core::ffi::c_void,
) {
    let mut buf_arr: [i8; BLE_UUID_STR_LEN as usize] = [0; BLE_UUID_STR_LEN as usize];
    let buf = buf_arr.as_mut_ptr();

    match (*ctxt).op as u32 {
        BLE_GATT_REGISTER_OP_SVC => {
            printf(
                cstr!("registered service %s with handle=%d\n"),
                ble_uuid_to_str((*(*ctxt).__bindgen_anon_1.svc.svc_def).uuid, buf),
                (*ctxt).__bindgen_anon_1.svc.handle as i32,
            );
        }

        BLE_GATT_REGISTER_OP_CHR => {
            printf(
                cstr!("registering characteristic %s with def_handle=%d val_handle=%d\n"),
                ble_uuid_to_str((*(*ctxt).__bindgen_anon_1.chr.chr_def).uuid, buf),
                (*ctxt).__bindgen_anon_1.chr.def_handle as i32,
                (*ctxt).__bindgen_anon_1.chr.val_handle as i32,
            );
        }

        BLE_GATT_REGISTER_OP_DSC => {
            printf(
                cstr!("registering descriptor %s with handle=%d\n"),
                ble_uuid_to_str((*(*ctxt).__bindgen_anon_1.dsc.dsc_def).uuid, buf),
                (*ctxt).__bindgen_anon_1.dsc.handle as i32,
            );
        }
        _ => {
            printf(cstr!("unknown operation: %d\n"), (*ctxt).op as u32);
        }
    }
}

pub unsafe fn gatt_svr_init() -> i32 {
    // Leaks the eff out of the svc_def
    let svcs_ptr = alloc_svc_def();
    print_svcs(svcs_ptr);

    ble_svc_gap_init();
    ble_svc_gatt_init();

    let mut rc;

    rc = ble_gatts_count_cfg(svcs_ptr);
    esp_assert!(rc == 0, cstr!("RC err after ble_gatts_count_cfg\n"));

    rc = ble_gatts_add_svcs(svcs_ptr);
    esp_assert!(rc == 0, cstr!("RC err after ble_gatts_add_svcs\n"));

    return 0;
}
