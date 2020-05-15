// https://github.com/ctron/rust-esp32-hono/blob/master/esp32-sys/src/bindings.h
typedef long _off_t;
typedef short __dev_t;
typedef unsigned short __uid_t;
typedef unsigned short __gid_t;
typedef int _ssize_t;

typedef int _lock_t;
typedef _lock_t _LOCK_RECURSIVE_T;
typedef _lock_t _LOCK_T;
typedef struct _reent *_data;

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

// Bluetooth?
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "console/console.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "nimble/ble.h"
#include "modlog/modlog.h"

/* Manual defines because the macros are too complex to be processed */

/*
 * Original was
 * #define portTICK_PERIOD_MS ( ( TickType_t ) 1000 / configTICK_RATE_HZ )
 */
#undef portTICK_PERIOD_MS
#define portTICK_PERIOD_MS 1000 / configTICK_RATE_HZ
