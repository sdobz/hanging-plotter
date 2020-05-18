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
            esp_log!(BLE_HR_TAG, cstr!("line %d\n"), core::line!() as u32);
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
            esp_log!(
                BLE_HR_TAG,
                cstr!("%.*s:%d - %s"),
                file.len(),
                file.as_ptr(),
                core::line!(),
                $msg
            );
            panic!();
        }
    };
}
