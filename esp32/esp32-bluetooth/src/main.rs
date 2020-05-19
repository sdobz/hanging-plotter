#![no_std]
#![no_main]
#![feature(const_mut_refs)]
#![feature(const_fn)]
#![feature(const_transmute)]

extern crate esp32_sys;

mod debug;
mod gatt_svr;

use core::ffi::c_void;
use core::mem::size_of;
use core::panic::PanicInfo;
use core::ptr;
use debug::print_addr;
use esp32_sys::*;
use gatt_svr::{gatt_svr_init, gatt_svr_register_cb, HRS_HRM_HANDLE};

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    unsafe { printf(cstr!("Panic!\n")) };

    loop {}
}

const BLINK_GPIO: gpio_num_t = gpio_num_t_GPIO_NUM_5;
const UART_NUM: uart_port_t = uart_port_t_UART_NUM_1;
const ECHO_TEST_TXD: i32 = gpio_num_t_GPIO_NUM_17 as i32;
const ECHO_TEST_RXD: i32 = gpio_num_t_GPIO_NUM_16 as i32;
const ECHO_TEST_RTS: i32 = UART_PIN_NO_CHANGE;
const ECHO_TEST_CTS: i32 = UART_PIN_NO_CHANGE;

const BUF_SIZE: i32 = 1024;
// https://github.com/espressif/esp-idf/tree/0a03a55c1eb44a354c9ad5d91d91da371fe23f84/examples/bluetooth/nimble/blehr

pub const BLE_HR_TAG: *const i8 = cstr!("NimBLE_BLE_HeartRate");

static mut BLEHR_TX_TIMER: TimerHandle_t = ptr::null_mut();

static mut NOTIFY_STATE: bool = false;

static mut CONN_HANDLE: u16 = 0;

const DEVICE_NAME: &str = "blehr_sensor_1.0\0";

static mut BLEHR_ADDRESS_TYPE: u8 = 0;

/* Variable to simulate heart beats */
static mut HEARTRATE: u8 = 90;

unsafe fn blehr_tx_hrate_stop() {
    xTimerStop(BLEHR_TX_TIMER, 1000 / portTICK_PERIOD_MS);
}

/* Reset heartrate measurment */
unsafe fn blehr_tx_hrate_reset() {
    let rc;

    if xTimerReset(BLEHR_TX_TIMER, 1000 / portTICK_PERIOD_MS) == pdPASS {
        rc = 0;
    } else {
        rc = 1;
    }
    assert!(rc == 0);
}

/* This function simulates heart beat and notifies it to the client */
unsafe extern "C" fn blehr_tx_hrate(_ev: TimerHandle_t) {
    let mut hrm: [u8; 2] = [0; 2];
    let rc;
    let om: *mut os_mbuf;

    if !NOTIFY_STATE {
        blehr_tx_hrate_stop();
        HEARTRATE = 90;
        return;
    }

    hrm[0] = 0x06; /* contact of a sensor */
    hrm[1] = HEARTRATE; /* storing dummy data */

    /* Simulation of heart beats */
    HEARTRATE += 1;
    if HEARTRATE == 160 {
        HEARTRATE = 90;
    }

    om = ble_hs_mbuf_from_flat(hrm.as_ptr() as *const _, size_of::<[u8; 2]>() as u16);
    rc = ble_gattc_notify_custom(CONN_HANDLE, HRS_HRM_HANDLE, om);

    assert!(rc == 0);

    blehr_tx_hrate_reset();
}

unsafe extern "C" fn blehr_gap_event(
    event: *mut ble_gap_event,
    _arg: *mut ::core::ffi::c_void,
) -> i32 {
    match (*event).type_ as u32 {
        BLE_GAP_EVENT_CONNECT => {
            /* A new connection was established or a connection attempt failed */
            esp_log!(
                BLE_HR_TAG,
                cstr!("connection %s; status=%d\n"),
                if (*event).__bindgen_anon_1.connect.status == 0 {
                    cstr!("established")
                } else {
                    cstr!("failed")
                },
                (*event).__bindgen_anon_1.connect.status
            );

            if (*event).__bindgen_anon_1.connect.status != 0 {
                /* Connection failed; resume advertising */
                blehr_advertise();
            }
            CONN_HANDLE = (*event).__bindgen_anon_1.connect.conn_handle;
        }

        BLE_GAP_EVENT_DISCONNECT => {
            esp_log!(
                BLE_HR_TAG,
                cstr!("disconnect; reason=%d\n"),
                (*event).__bindgen_anon_1.disconnect.reason
            );

            /* Connection terminated; resume advertising */
            blehr_advertise();
        }

        BLE_GAP_EVENT_ADV_COMPLETE => {
            esp_log!(BLE_HR_TAG, cstr!("adv complete\n"));
            blehr_advertise();
        }

        BLE_GAP_EVENT_SUBSCRIBE => {
            esp_log!(
                BLE_HR_TAG,
                cstr!("subscribe event; cur_notify=%d\n value handle; val_handle=%d\n"),
                (*event).__bindgen_anon_1.subscribe.cur_notify() as u32,
                HRS_HRM_HANDLE as u32
            );
            if (*event).__bindgen_anon_1.subscribe.attr_handle == HRS_HRM_HANDLE {
                NOTIFY_STATE = (*event).__bindgen_anon_1.subscribe.cur_notify() != 0;
                blehr_tx_hrate_reset();
            } else if (*event).__bindgen_anon_1.subscribe.attr_handle != HRS_HRM_HANDLE {
                NOTIFY_STATE = (*event).__bindgen_anon_1.subscribe.cur_notify() != 0;
                blehr_tx_hrate_stop();
            }
            esp_log!(
                cstr!("BLE_GAP_SUBSCRIBE_EVENT"),
                cstr!("conn_handle from subscribe=%d\n"),
                CONN_HANDLE as u32,
            );
        }

        BLE_GAP_EVENT_MTU => {
            esp_log!(
                BLE_HR_TAG,
                cstr!("mtu update event; conn_handle=%d mtu=%d\n"),
                (*event).__bindgen_anon_1.mtu.conn_handle as u32,
                (*event).__bindgen_anon_1.mtu.value as u32,
            );
        }
        _ => esp_log!(
            BLE_HR_TAG,
            cstr!("unknown operation: %d\n"),
            (*event).type_ as u32
        ),
    }

    return 0;
}

/*
 * Enables advertising with parameters:
 *     o General discoverable mode
 *     o Undirected connectable mode
 */
unsafe fn blehr_advertise() {
    let mut fields: ble_hs_adv_fields = core::mem::MaybeUninit::zeroed().assume_init();
    /*
     * Advertise two flags:
     *      o Discoverability in forthcoming advertisement (general)
     *      o BLE-only (BR/EDR unsupported)
     */
    fields.flags = BLE_HS_ADV_F_DISC_GEN as u8 | BLE_HS_ADV_F_BREDR_UNSUP as u8;

    /*
     * Indicate that the TX power level field should be included; have the
     * stack fill this value automatically.  This is done by assigning the
     * special value BLE_HS_ADV_TX_PWR_LVL_AUTO.
     */
    fields.set_tx_pwr_lvl_is_present(1);
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO as i8;

    fields.name = DEVICE_NAME.as_ptr() as *mut u8;
    fields.name_len = DEVICE_NAME.len() as u8;
    fields.set_name_is_complete(1);

    let mut rc = ble_gap_adv_set_fields(&fields);
    if rc != 0 {
        esp_log!(
            BLE_HR_TAG,
            cstr!("error setting advertisement data; rc=%d\n"),
            rc
        );
        return;
    }

    /* Begin advertising */
    let mut adv_params: ble_gap_adv_params = std::mem::MaybeUninit::zeroed().assume_init();
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND as u8;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN as u8;
    rc = ble_gap_adv_start(
        BLEHR_ADDRESS_TYPE,
        ptr::null(),
        core::i32::MAX,
        &adv_params,
        Some(blehr_gap_event),
        ptr::null_mut(),
    );
    if rc != 0 {
        esp_log!(
            BLE_HR_TAG,
            cstr!("error enabling advertisement; rc=%d\n"),
            rc
        );
        return;
    }
}

unsafe extern "C" fn blehr_on_sync() {
    let mut rc;

    rc = ble_hs_id_infer_auto(0, &mut BLEHR_ADDRESS_TYPE as *mut _);
    assert!(rc == 0);

    let addr_val: *mut u8 = [0; 6].as_mut_ptr();
    rc = ble_hs_id_copy_addr(BLEHR_ADDRESS_TYPE, addr_val, ptr::null_mut());
    assert!(rc == 0);

    esp_log!(BLE_HR_TAG, cstr!("Device Address: "));
    print_addr(addr_val as *const c_void);
    esp_log!(BLE_HR_TAG, cstr!("\n"));

    /* Begin advertising */
    blehr_advertise();
}

unsafe extern "C" fn blehr_on_reset(reason: i32) {
    esp_log!(BLE_HR_TAG, cstr!("Resetting state; reason=%d\n"), reason);
}

unsafe extern "C" fn blehr_host_task(_param: *mut c_void) {
    esp_log!(BLE_HR_TAG, cstr!("BLE Host Task Started\n"));
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();

    nimble_port_freertos_deinit();
}

unsafe fn init_bt() {
    /* Initialize NVS — it is used to store PHY calibration data */
    let mut ret: esp_err_t = nvs_flash_init();
    if ret == ESP_ERR_NVS_NO_FREE_PAGES as i32 || ret == ESP_ERR_NVS_NEW_VERSION_FOUND as i32 {
        esp_error_check!(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    esp_error_check!(ret);

    esp_error_check!(esp_nimble_hci_and_controller_init());

    nimble_port_init();
    /* Initialize the NimBLE host configuration */
    ble_hs_cfg.sync_cb = Some(blehr_on_sync);
    ble_hs_cfg.reset_cb = Some(blehr_on_reset);
    ble_hs_cfg.gatts_register_cb = Some(gatt_svr_register_cb);

    /* name, period/time,  auto reload, timer ID, callback */
    BLEHR_TX_TIMER = xTimerCreate(
        cstr!("blehr_tx_timer"),
        pdMS_TO_TICKS!(1000),
        pdTRUE,
        ptr::null_mut(),
        Some(blehr_tx_hrate),
    );

    let mut rc: i32 = 0;
    rc = gatt_svr_init();
    esp_assert!(rc == 0, cstr!("gatt_svr_init failed\n"));

    /* Set the default device name */
    rc = ble_svc_gap_device_name_set(cstr!("Fake Device Name"));
    esp_assert!(rc == 0, cstr!("ble_svc_gap_device_name_set failed\n"));

    /* Start the task */
    nimble_port_freertos_init(Some(blehr_host_task));
}

#[no_mangle]
pub fn app_main() {
    unsafe {
        esp_log!(BLE_HR_TAG, cstr!("Setting up!\n"));
        init_bt();
        esp_log!(BLE_HR_TAG, cstr!("BT init!\n"));

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

        // esp_log!(BLE_TAG, "a string\n",);

        /* Blink on (output high) */
        gpio_set_level(BLINK_GPIO, 1);

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
