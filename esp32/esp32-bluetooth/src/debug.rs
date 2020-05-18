use core::{ffi::c_void, mem::size_of, ptr};
use esp32_sys::*;

#[macro_export]
macro_rules! cstr {
    ($src:expr) => {
        (concat!($src, "\0")).as_ptr() as *const _
    };
}

#[macro_export]
macro_rules! esp_log {
    ($tag:expr, $format:expr) => (esp_log_write(esp_log_level_t_ESP_LOG_INFO, $tag, $format));
    ($tag:expr, $format:expr, $($arg:tt)*) => (esp_log_write(esp_log_level_t_ESP_LOG_INFO, $tag, $format, $($arg)*));
}

#[macro_export]
macro_rules! emit_line {
    () => {
        unsafe {
            printf(cstr!("line %d\n"), core::line!() as u32);
        }
    };
}

#[macro_export]
macro_rules! esp_assert {
    ($check:expr) => {
        esp_assert!($check, cstr!("Assertion error"));
    };
    ($check:expr, $msg:expr) => {
        if !$check {
            let file = core::file!();
            printf(
                cstr!("%.*s:%d - %s"),
                file.len(),
                file.as_ptr(),
                core::line!(),
                $msg,
            );
            panic!();
        }
    };
}

pub unsafe fn print_bytes(bytes: *const u8, len: usize) {
    let u8p: &[u8];

    u8p = core::slice::from_raw_parts(bytes, len);

    for i in 0..len {
        if (i & 0b1111) == 0 && i > 0 {
            printf(cstr!("\n"));
        } else if (i & 0b1) == 0 {
            printf(cstr!(" "));
        }
        printf(cstr!("%02x"), u8p[i] as u32);
    }
}

pub unsafe fn print_ptr<T>(name: *const u8, p: *const T) {
    printf(cstr!("%p - %s:\n"), p, name);
    print_bytes(p as *const _, size_of::<T>());
    printf(cstr!("\n"));
}

pub unsafe fn print_addr(addr: *const c_void) {
    let u8p: &[u8];

    u8p = core::slice::from_raw_parts(addr as *const u8, 6);
    printf(
        cstr!("%02x:%02x:%02x:%02x:%02x:%02x"),
        u8p[5] as u32,
        u8p[4] as u32,
        u8p[3] as u32,
        u8p[2] as u32,
        u8p[1] as u32,
        u8p[0] as u32,
    );
}

pub unsafe fn print_svcs(svcs: *const ble_gatt_svc_def) {
    let mut svc_p = svcs;
    let mut svc = *svc_p;
    loop {
        print_ptr(cstr!("svc"), &svc);
        print_ptr(cstr!(" -type"), &svc.type_);
        print_ptr(cstr!(" -uuid"), &svc.uuid);
        print_ptr(cstr!(" -includes"), &svc.includes);
        print_ptr(cstr!(" -characteristics"), &svc.characteristics);
        print_chrs(svc.characteristics);
        svc = *svc_p;
        svc_p = svc_p.add(1);

        if svc.type_ == 0 {
            printf(cstr!("end\n"));
            return;
        }
        if svc.type_ != BLE_GATT_SVC_TYPE_PRIMARY as u8
            && svc.type_ != BLE_GATT_SVC_TYPE_SECONDARY as u8
        {
            printf(cstr!("insanity detected\n"));
            break;
        }
    }
}

pub unsafe fn print_chrs(svcs: *const ble_gatt_chr_def) {
    let mut chr_p = svcs;
    let mut chr = *chr_p;
    while chr.uuid != ptr::null() {
        print_ptr(cstr!("  chr"), &chr);
        print_ptr(cstr!("   -uuid"), &chr.uuid);
        print_ptr(cstr!("   -access_cb"), &chr.access_cb);
        print_ptr(cstr!("   -val_handle"), &chr.val_handle);
        print_ptr(cstr!("   -flags"), &chr.flags);

        chr = *chr_p;
        chr_p = chr_p.add(1);

        if chr.uuid == ptr::null() {
            printf(cstr!("end"));
            return;
        }
        if (*chr.uuid).type_ != BLE_UUID_TYPE_16 as u8 {
            printf(cstr!("insanity detected"));
            return;
        }
    }
}
