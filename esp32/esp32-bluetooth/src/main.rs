#![no_std]
#![no_main]
#![feature(const_mut_refs)]
#![feature(const_fn)]

extern crate esp32_sys;

use core::panic::PanicInfo;
use core::ptr;
use core::ffi::c_void;
use core::mem::size_of;
use esp32_sys::*;

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}

const BLINK_GPIO: gpio_num_t = gpio_num_t_GPIO_NUM_5;
const UART_NUM: uart_port_t = uart_port_t_UART_NUM_1;
const ECHO_TEST_TXD: i32 = gpio_num_t_GPIO_NUM_17 as i32;
const ECHO_TEST_RXD: i32 = gpio_num_t_GPIO_NUM_16 as i32;
const ECHO_TEST_RTS: i32 = UART_PIN_NO_CHANGE;
const ECHO_TEST_CTS: i32 = UART_PIN_NO_CHANGE;

const BUF_SIZE: i32 = 1024;
// https://github.com/espressif/esp-idf/blob/0a03a55c1eb44a354c9ad5d91d91da371fe23f84/examples/bluetooth/nimble/bleprph/

// blehr_sens.h
//
/*

#define BLE_UUID16_DECLARE(uuid16) \
    ((ble_uuid_t *) (&(ble_uuid16_t) BLE_UUID16_INIT(uuid16)))
((ble_uuid_t *) (&(ble_uuid16_t) BLE_UUID16_INIT(uuid16)))


#define BLE_UUID16_INIT(uuid16)         \
    {                                   \
        .u.type = BLE_UUID_TYPE_16,     \
        .value = (uuid16),              \
    }
*/

const fn ble_uuid16_declare(value: u16) -> *const ble_uuid_t {
    &ble_uuid16_t {
        u: ble_uuid_t {
            type_: BLE_UUID_TYPE_16 as u8,
        },
        value,
    } as *const ble_uuid16_t as *const ble_uuid_t
}

const fn null_ble_gatt_chr_def() -> ble_gatt_chr_def {
    return ble_gatt_chr_def {
        uuid: ptr::null(),
        access_cb: None,
        arg: ptr::null_mut(),
        descriptors: ptr::null_mut(),
        flags: 0,
        min_key_size: 0,
        val_handle: ptr::null_mut(),
    }
}

const fn null_ble_gatt_svc_def() -> ble_gatt_svc_def {
    return ble_gatt_svc_def {
        type_: 0,
        uuid: ptr::null(),
        includes: ptr::null_mut(),
        characteristics: ptr::null(),
        
    }
}

/* Heart-rate configuration */
const GATT_HRS_UUID: u16 = 0x180D;
const GATT_HRS_MEASUREMENT_UUID: u16 = 0x2A37;
const GATT_HRS_BODY_SENSOR_LOC_UUID: u16 = 0x2A38;
const GATT_DEVICE_INFO_UUID: u16 = 0x180A;
const GATT_MANUFACTURER_NAME_UUID: u16 = 0x2A29;
const GATT_MODEL_NUMBER_UUID: u16 = 0x2A24;

//struct ble_hs_cfg;
//struct ble_gatt_register_ctxt;
//void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
//int gatt_svr_init(void);

// gatt_svr.c

const MANUF_NAME: &str = "Apache Mynewt ESP32 devkitC";
const MODEL_NUM: &str = "Mynewt HR Sensor demo";
static mut HRS_HRM_HANDLE: u16 = 0;

static mut GATT_SVR_SVCS: *const ble_gatt_svc_def = [
    ble_gatt_svc_def {
        type_: BLE_GATT_SVC_TYPE_PRIMARY as u8,
        uuid: ble_uuid16_declare(GATT_HRS_UUID),
        characteristics: [
            ble_gatt_chr_def {
                uuid: ble_uuid16_declare(GATT_HRS_MEASUREMENT_UUID),
                access_cb: Some(gatt_svr_chr_access_heart_rate),
                arg: ptr::null_mut(),
                descriptors: ptr::null_mut(),
                flags: BLE_GATT_CHR_F_NOTIFY as u16,
                min_key_size: 0,
                val_handle: unsafe { &mut HRS_HRM_HANDLE as *mut u16 },
            },
            ble_gatt_chr_def {
                uuid: ble_uuid16_declare(GATT_HRS_BODY_SENSOR_LOC_UUID),
                access_cb: Some(gatt_svr_chr_access_heart_rate),
                arg: ptr::null_mut(),
                descriptors: ptr::null_mut(),
                flags: BLE_GATT_CHR_F_READ as u16,
                min_key_size: 0,
                val_handle: ptr::null_mut(),
            },
            null_ble_gatt_chr_def()
        ].as_ptr(),
        includes: ptr::null_mut(),
    }, ble_gatt_svc_def {
        type_: BLE_GATT_SVC_TYPE_PRIMARY as u8,
        uuid: ble_uuid16_declare(GATT_DEVICE_INFO_UUID),
        characteristics: [
            ble_gatt_chr_def {
                uuid: ble_uuid16_declare(GATT_MANUFACTURER_NAME_UUID),
                access_cb: Some(gatt_svr_chr_access_device_info),
                arg: ptr::null_mut(),
                descriptors: ptr::null_mut(),
                flags: BLE_GATT_CHR_F_NOTIFY as u16,
                min_key_size: 0,
                val_handle: ptr::null_mut(),
            },
            ble_gatt_chr_def {
                uuid: ble_uuid16_declare(GATT_HRS_BODY_SENSOR_LOC_UUID),
                access_cb: Some(gatt_svr_chr_access_heart_rate),
                arg: ptr::null_mut(),
                descriptors: ptr::null_mut(),
                flags: BLE_GATT_CHR_F_READ as u16,
                min_key_size: 0,
                val_handle: ptr::null_mut(),
            },
            null_ble_gatt_chr_def()
        ].as_ptr(),
        includes: ptr::null_mut(),
    }, 
    null_ble_gatt_svc_def()
].as_ptr();

pub extern fn gatt_svr_chr_access_heart_rate(_conn_handle: u16, _attr_handle: u16,
                                ctxt: *mut ble_gatt_access_ctxt, _arg: *mut ::core::ffi::c_void) -> i32
{
    /* Sensor location, set to "Chest" */
    const BODY_SENS_LOC: u8 = 0x01;

    let uuid: u16 = unsafe { ble_uuid_u16((*(*ctxt).__bindgen_anon_1.chr).uuid) };

    if uuid == GATT_HRS_BODY_SENSOR_LOC_UUID {
        let rc: i32 = unsafe { os_mbuf_append((*ctxt).om, &BODY_SENS_LOC as *const u8 as *const c_void, size_of::<u8>() as u16) };

        return if rc == 0 { 0 } else { BLE_ATT_ERR_INSUFFICIENT_RES as i32 };
    }

    return BLE_ATT_ERR_UNLIKELY as i32;
}

pub extern fn gatt_svr_chr_access_device_info(_conn_handle: u16, _attr_handle: u16,
                                ctxt: *mut ble_gatt_access_ctxt, _arg: *mut ::core::ffi::c_void) -> i32
{
    let uuid: u16 = unsafe { ble_uuid_u16((*(*ctxt).__bindgen_anon_1.chr).uuid) };

    if uuid == GATT_MODEL_NUMBER_UUID {
        let rc: i32 = unsafe { os_mbuf_append((*ctxt).om, MODEL_NUM.as_ptr() as *const c_void, MODEL_NUM.len() as u16) };
        return if rc == 0 { 0 } else { BLE_ATT_ERR_INSUFFICIENT_RES as i32 };
    }

    if uuid == GATT_MANUFACTURER_NAME_UUID {
        let rc: i32 = unsafe { os_mbuf_append((*ctxt).om, MANUF_NAME.as_ptr() as *const c_void, MANUF_NAME.len() as u16) };
        return if rc == 0 { 0 } else { BLE_ATT_ERR_INSUFFICIENT_RES as i32 };
    }

    return BLE_ATT_ERR_UNLIKELY as i32;
}



unsafe fn gatt_svr_init() -> i32
{
    
    let mut rc;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(GATT_SVR_SVCS);
    if rc != 0 {
        return rc;
    }

    rc = ble_gatts_add_svcs(GATT_SVR_SVCS);
    if rc != 0 {
        return rc;
    }

    return 0;
}


#[no_mangle]
pub fn app_main() {
    unsafe {
        // TODO: check init
        gatt_svr_init();


        rust_blink_and_write();
    }
}

unsafe fn rust_blink_and_write() {
    gpio_pad_select_gpio(BLINK_GPIO as u8);

    /* Set the GPIO as a push/pull output */
    gpio_set_direction(BLINK_GPIO, gpio_mode_t_GPIO_MODE_OUTPUT);

    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    let uart_config = uart_config_t {
        baud_rate: 115200,
        data_bits: uart_word_length_t_UART_DATA_8_BITS,
        parity: uart_parity_t_UART_PARITY_DISABLE,
        stop_bits: uart_stop_bits_t_UART_STOP_BITS_1,
        flow_ctrl: uart_hw_flowcontrol_t_UART_HW_FLOWCTRL_DISABLE,
        rx_flow_ctrl_thresh: 0,
        use_ref_tick: false,
    };

    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(
        UART_NUM,
        ECHO_TEST_TXD,
        ECHO_TEST_RXD,
        ECHO_TEST_RTS,
        ECHO_TEST_CTS,
    );
    uart_driver_install(UART_NUM, BUF_SIZE * 2, 0, 0, ptr::null_mut(), 0);

    loop {
        /* Blink off (output low) */
        gpio_set_level(BLINK_GPIO, 0);

        vTaskDelay(1000 / portTICK_PERIOD_MS);

        // Write data to UART.
        //let test_str = "This is a test string.\n";
        // uart_write_bytes(UART_NUM, test_str.as_ptr() as *const _, test_str.len());
        let tag = "Rust\0";

        esp_log_write(
            esp_log_level_t_ESP_LOG_INFO,
            tag.as_ptr() as *const _,
            " (%d) %s: %s\n\0".as_ptr() as *const _,
            esp_log_timestamp(),
            tag.as_ptr() as *const _,
            "I live again!.\0".as_ptr() as *const _,
        );

        /* Blink on (output high) */
        gpio_set_level(BLINK_GPIO, 1);

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
