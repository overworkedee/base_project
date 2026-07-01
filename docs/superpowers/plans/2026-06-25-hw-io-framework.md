# HW IO Framework Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the `hw/` static library (`libhw.a`) with I2C bus driver, device template, error handling, mutex wrapper, demo, and tests.

**Architecture:** Bus Handle + Device Context pattern. Bus layer wraps Linux kernel interfaces (open/read/write/ioctl) and exposes opaque handles with internal mutex. Device layer holds bus handle via dependency injection. Compiles as a standalone static library under `hw/`.

**Tech Stack:** C11, CMake 3.16+, pthread, Linux I2C dev interface (`<linux/i2c-dev.h>`), ARM64 cross-compilation (RK3588).

## Global Constraints

- All public APIs return `hw_err_t` (0 = HW_OK)
- Bus handles are opaque (forward-declared, struct definition in .c)
- All bus I/O is mutex-protected internally
- Compile as static library `libhw.a` under `hw/`
- Demo compiled only when `HW_BUILD_DEMO=ON`

## File Structure Map

```
hw/
├── CMakeLists.txt                  # Task 1 — static lib + subdirectories
├── include/hw/
│   ├── hw_error.h                  # Task 2 — error enum + hw_err_str()
│   ├── hw_types.h                  # Task 2 — common type forward-declarations
│   ├── hw_mutex.h                  # Task 2 — pthread_mutex_t wrapper
│   ├── bus/
│   │   └── bus_i2c.h               # Task 3 — I2C bus API
│   └── dev/
│       └── dev_template.h          # Task 6 — device template
├── src/
│   ├── hw_error.c                  # Task 2
│   ├── hw_mutex.c                  # Task 2
│   ├── bus/
│   │   └── bus_i2c.c               # Task 4 — I2C bus impl
│   └── dev/                        # (future IC drivers)
├── demo/
│   └── demo_main.c                 # Task 7 — quick validation
└── tests/
    └── test_bus_i2c.c              # Task 5 — I2C error-path tests
```

---

### Task 1: Scaffold directories and CMakeLists.txt

**Files:**
- Create: `hw/CMakeLists.txt`

**Interfaces:**
- Produces: `libhw` CMake target (static library)

- [ ] **Step 1: Create all directories**

```bash
mkdir -p hw/include/hw/bus hw/include/hw/dev hw/src/bus hw/src/dev hw/demo hw/tests
```

- [ ] **Step 2: Write hw/CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.16)

# Build option: demo
option(HW_BUILD_DEMO "Build hardware demo program" OFF)

# Collect bus sources
set(HW_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hw_error.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hw_mutex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bus/bus_i2c.c
)

# Collect headers (for IDE convenience, not required for build)
set(HW_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hw/hw_error.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hw/hw_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hw/hw_mutex.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hw/bus/bus_i2c.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/hw/dev/dev_template.h
)

# Static library
add_library(hw STATIC ${HW_SOURCES} ${HW_HEADERS})
target_include_directories(hw PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(hw PRIVATE pthread)

# Demo
if(HW_BUILD_DEMO)
    add_executable(hw_demo ${CMAKE_CURRENT_SOURCE_DIR}/demo/demo_main.c)
    target_link_libraries(hw_demo hw)
endif()

# Tests
enable_testing()
add_executable(test_bus_i2c ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_bus_i2c.c)
target_link_libraries(test_bus_i2c hw)
add_test(NAME test_bus_i2c COMMAND test_bus_i2c)
```

- [ ] **Step 3: Verify CMake parses**

```bash
cd /home/chenchizhao/project/build && cmake .. -DHW_BUILD_DEMO=OFF 2>&1 | tail -20
```

Expected: no errors, sees the `hw/` subdirectory (will fail until top-level adds `add_subdirectory` — that's Task 8).

- [ ] **Step 4: Commit**

```bash
git add hw/CMakeLists.txt hw/include/ hw/src/ hw/demo/ hw/tests/
git commit -m "feat(hw): scaffold directory tree and CMakeLists.txt"
```

---

### Task 2: Error codes, types, and mutex wrapper

**Files:**
- Create: `hw/include/hw/hw_error.h`
- Create: `hw/include/hw/hw_types.h`
- Create: `hw/include/hw/hw_mutex.h`
- Create: `hw/src/hw_error.c`
- Create: `hw/src/hw_mutex.c`

**Interfaces:**
- Produces: `hw_err_t` enum, `hw_err_str()`, `hw_mutex_t` type, `hw_mutex_init/lock/unlock/destroy()`

- [ ] **Step 1: Write hw_error.h**

```c
#ifndef HW_ERROR_H
#define HW_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HW_OK                = 0,
    HW_ERR_BUS_OPEN,          /* 总线打开失败 */
    HW_ERR_BUS_TRANSFER,      /* 传输失败 */
    HW_ERR_DEV_ADDR,          /* 设备地址无效 */
    HW_ERR_DEV_NOT_FOUND,     /* 设备无响应 */
    HW_ERR_MUTEX_INIT,        /* 锁初始化失败 */
    HW_ERR_PARAM,             /* 参数非法 */
} hw_err_t;

const char* hw_err_str(hw_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* HW_ERROR_H */
```

- [ ] **Step 2: Write hw_error.c**

```c
#include "hw/hw_error.h"

const char* hw_err_str(hw_err_t err)
{
    switch (err) {
    case HW_OK:                return "OK";
    case HW_ERR_BUS_OPEN:      return "Bus open failed";
    case HW_ERR_BUS_TRANSFER:  return "Bus transfer failed";
    case HW_ERR_DEV_ADDR:      return "Device address invalid";
    case HW_ERR_DEV_NOT_FOUND: return "Device not found (no ACK)";
    case HW_ERR_MUTEX_INIT:    return "Mutex init failed";
    case HW_ERR_PARAM:         return "Invalid parameter";
    default:                   return "Unknown error";
    }
}
```

- [ ] **Step 3: Write hw_types.h**

```c
#ifndef HW_TYPES_H
#define HW_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Placeholder for future common types.
 * Bus-specific opaque types are forward-declared in their own headers. */

#ifdef __cplusplus
}
#endif

#endif /* HW_TYPES_H */
```

- [ ] **Step 4: Write hw_mutex.h**

```c
#ifndef HW_MUTEX_H
#define HW_MUTEX_H

#include <pthread.h>
#include "hw/hw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef pthread_mutex_t hw_mutex_t;

hw_err_t hw_mutex_init(hw_mutex_t* mutex);
hw_err_t hw_mutex_lock(hw_mutex_t* mutex);
hw_err_t hw_mutex_unlock(hw_mutex_t* mutex);
hw_err_t hw_mutex_destroy(hw_mutex_t* mutex);

#ifdef __cplusplus
}
#endif

#endif /* HW_MUTEX_H */
```

- [ ] **Step 5: Write hw_mutex.c**

```c
#include "hw/hw_mutex.h"

hw_err_t hw_mutex_init(hw_mutex_t* mutex)
{
    if (!mutex) return HW_ERR_PARAM;
    int ret = pthread_mutex_init(mutex, NULL);
    return (ret == 0) ? HW_OK : HW_ERR_MUTEX_INIT;
}

hw_err_t hw_mutex_lock(hw_mutex_t* mutex)
{
    if (!mutex) return HW_ERR_PARAM;
    int ret = pthread_mutex_lock(mutex);
    return (ret == 0) ? HW_OK : HW_ERR_MUTEX_INIT;
}

hw_err_t hw_mutex_unlock(hw_mutex_t* mutex)
{
    if (!mutex) return HW_ERR_PARAM;
    int ret = pthread_mutex_unlock(mutex);
    return (ret == 0) ? HW_OK : HW_ERR_MUTEX_INIT;
}

hw_err_t hw_mutex_destroy(hw_mutex_t* mutex)
{
    if (!mutex) return HW_ERR_PARAM;
    int ret = pthread_mutex_destroy(mutex);
    return (ret == 0) ? HW_OK : HW_ERR_MUTEX_INIT;
}
```

- [ ] **Step 6: Quick compile check (no link yet)**

```bash
source /home/chenchizhao/project/env/rk3588_product_orangerpi5plus.env
cd /home/chenchizhao/project/build
# Manually compile source files to check syntax
${CROSS_COMPILE}gcc -c ../hw/src/hw_error.c -o /tmp/hw_error.o -I ../hw/include
${CROSS_COMPILE}gcc -c ../hw/src/hw_mutex.c -o /tmp/hw_mutex.o -I ../hw/include
rm -f /tmp/hw_error.o /tmp/hw_mutex.o
```

Expected: no compile errors.

- [ ] **Step 7: Commit**

```bash
git add hw/include/hw/hw_error.h hw/src/hw_error.c \
        hw/include/hw/hw_types.h \
        hw/include/hw/hw_mutex.h hw/src/hw_mutex.c
git commit -m "feat(hw): add error codes, types, and mutex wrapper"
```

---

### Task 3: I2C bus header

**Files:**
- Create: `hw/include/hw/bus/bus_i2c.h`

**Interfaces:**
- Consumes: `hw_err_t` from hw_error.h, `hw_types.h` (stdint)
- Produces: opaque `bus_i2c_t`, `bus_i2c_open/transfer/close()`

- [ ] **Step 1: Write bus_i2c.h**

```c
#ifndef BUS_I2C_H
#define BUS_I2C_H

#include <stddef.h>
#include <stdint.h>
#include "hw/hw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque I2C bus handle */
typedef struct bus_i2c_ctx bus_i2c_t;

/**
 * Open an I2C bus controller.
 *
 * @param device    Path to I2C device node, e.g. "/dev/i2c-1"
 * @return          Bus handle on success, NULL on failure.
 */
bus_i2c_t* bus_i2c_open(const char* device);

/**
 * Perform an I2C combined write-then-read transfer.
 *
 * Writes tx_len bytes from tx buffer, then reads rx_len bytes into rx buffer.
 * Uses Linux i2c_rdwr_ioctl_data for a repeated-start transaction.
 * Thread-safe: internal mutex serializes access to the bus.
 *
 * @param bus       Bus handle from bus_i2c_open()
 * @param addr      7-bit I2C slave address
 * @param tx        Write buffer
 * @param tx_len    Number of bytes to write
 * @param rx        Read buffer
 * @param rx_len    Number of bytes to read
 * @return          HW_OK on success, error code on failure.
 */
hw_err_t bus_i2c_transfer(bus_i2c_t* bus, uint8_t addr,
                          const uint8_t* tx, size_t tx_len,
                          uint8_t* rx, size_t rx_len);

/**
 * Perform an I2C write-only transfer (no read phase).
 */
hw_err_t bus_i2c_write(bus_i2c_t* bus, uint8_t addr,
                       const uint8_t* data, size_t len);

/**
 * Perform an I2C read-only transfer (no write phase).
 */
hw_err_t bus_i2c_read(bus_i2c_t* bus, uint8_t addr,
                      uint8_t* data, size_t len);

/**
 * Close the I2C bus and release all resources.
 *
 * @param bus   Bus handle to close. Safe to pass NULL.
 */
void bus_i2c_close(bus_i2c_t* bus);

#ifdef __cplusplus
}
#endif

#endif /* BUS_I2C_H */
```

**Design note:** `addr` moves from `bus_i2c_open()` parameter to each `transfer/write/read` call. This is the correct pattern for Linux I2C — the slave address belongs to the *transaction*, not the bus handle. A single bus handle serves multiple devices at different addresses, and the mutex serializes them.

- [ ] **Step 2: Verify header parses**

```bash
source /home/chenchizhao/project/env/rk3588_product_orangerpi5plus.env
echo '#include "hw/bus/bus_i2c.h"
int main(void) {
    bus_i2c_t* b = bus_i2c_open("/dev/i2c-1");
    if (b) bus_i2c_close(b);
    return 0;
}' | ${CROSS_COMPILE}gcc -fsyntax-only -xc -I /home/chenchizhao/project/hw/include -
```

Expected: parse success (may warn about unused result, that's fine).

- [ ] **Step 3: Commit**

```bash
git add hw/include/hw/bus/bus_i2c.h
git commit -m "feat(hw): add I2C bus header with opaque handle API"
```

---

### Task 4: I2C bus implementation

**Files:**
- Create: `hw/src/bus/bus_i2c.c`

**Interfaces:**
- Consumes: `bus_i2c_t` from bus_i2c.h, `hw_mutex_init/lock/unlock/destroy()`, `hw_err_str()`
- Produces: `bus_i2c_open/transfer/write/read/close()` implementations

- [ ] **Step 1: Write bus_i2c.c**

```c
#include "hw/bus/bus_i2c.h"
#include "hw/hw_mutex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <errno.h>

/* ── Opaque handle ─────────────────────────────────────────────── */
struct bus_i2c_ctx {
    int             fd;
    hw_mutex_t      lock;
};

/* ── Public API ─────────────────────────────────────────────────── */

bus_i2c_t* bus_i2c_open(const char* device)
{
    if (!device) return NULL;

    bus_i2c_t* bus = (bus_i2c_t*)calloc(1, sizeof(bus_i2c_t));
    if (!bus) return NULL;

    hw_err_t ret = hw_mutex_init(&bus->lock);
    if (ret != HW_OK) {
        fprintf(stderr, "[hw:i2c] mutex init failed: %s\n", hw_err_str(ret));
        free(bus);
        return NULL;
    }

    bus->fd = open(device, O_RDWR);
    if (bus->fd < 0) {
        fprintf(stderr, "[hw:i2c] open(%s) failed: %s\n", device, strerror(errno));
        hw_mutex_destroy(&bus->lock);
        free(bus);
        return NULL;
    }

    return bus;
}

void bus_i2c_close(bus_i2c_t* bus)
{
    if (!bus) return;
    if (bus->fd >= 0) close(bus->fd);
    hw_mutex_destroy(&bus->lock);
    free(bus);
}

hw_err_t bus_i2c_transfer(bus_i2c_t* bus, uint8_t addr,
                          const uint8_t* tx, size_t tx_len,
                          uint8_t* rx, size_t rx_len)
{
    if (!bus) return HW_ERR_PARAM;
    if (tx_len > 0 && !tx) return HW_ERR_PARAM;
    if (rx_len > 0 && !rx) return HW_ERR_PARAM;

    hw_err_t ret = hw_mutex_lock(&bus->lock);
    if (ret != HW_OK) return ret;

    /* Build i2c_msg array */
    struct i2c_msg msgs[2];
    size_t nmsgs = 0;

    if (tx_len > 0) {
        msgs[nmsgs].addr  = addr;
        msgs[nmsgs].flags = 0;  /* write */
        msgs[nmsgs].len   = tx_len;
        msgs[nmsgs].buf   = (uint8_t*)tx;  /* const cast — kernel doesn't modify */
        nmsgs++;
    }

    if (rx_len > 0) {
        msgs[nmsgs].addr  = addr;
        msgs[nmsgs].flags = I2C_M_RD;  /* read */
        msgs[nmsgs].len   = rx_len;
        msgs[nmsgs].buf   = rx;
        nmsgs++;
    }

    struct i2c_rdwr_ioctl_data ioctl_data = {
        .msgs   = msgs,
        .nmsgs  = nmsgs,
    };

    if (ioctl(bus->fd, I2C_RDWR, &ioctl_data) < 0) {
        fprintf(stderr, "[hw:i2c] transfer failed (addr=0x%02x): %s\n",
                addr, strerror(errno));
        ret = HW_ERR_BUS_TRANSFER;
        goto out;
    }

    ret = HW_OK;

out:
    hw_mutex_unlock(&bus->lock);
    return ret;
}

hw_err_t bus_i2c_write(bus_i2c_t* bus, uint8_t addr,
                       const uint8_t* data, size_t len)
{
    return bus_i2c_transfer(bus, addr, data, len, NULL, 0);
}

hw_err_t bus_i2c_read(bus_i2c_t* bus, uint8_t addr,
                      uint8_t* data, size_t len)
{
    return bus_i2c_transfer(bus, addr, NULL, 0, data, len);
}
```

- [ ] **Step 2: Compile object file**

```bash
source /home/chenchizhao/project/env/rk3588_product_orangerpi5plus.env
cd /home/chenchizhao/project/build
${CROSS_COMPILE}gcc -c ../hw/src/bus/bus_i2c.c \
    -o /tmp/bus_i2c.o \
    -I ../hw/include \
    -Wall -Wextra -std=c11
rm -f /tmp/bus_i2c.o
```

Expected: compile success, no warnings.

- [ ] **Step 3: Commit**

```bash
git add hw/src/bus/bus_i2c.c
git commit -m "feat(hw): implement I2C bus driver with mutex-protected transfer"
```

---

### Task 5: I2C bus error-path test

**Files:**
- Create: `hw/tests/test_bus_i2c.c`

**Interfaces:**
- Consumes: `bus_i2c_open/close/transfer()`, `hw_err_t`

- [ ] **Step 1: Write test_bus_i2c.c**

```c
#include "hw/bus/bus_i2c.h"
#include "hw/hw_error.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { \
    printf("  TEST: %s ... ", name); \
} while(0)

#define PASS() do { \
    printf("PASS\n"); tests_passed++; \
} while(0)

#define FAIL(msg) do { \
    printf("FAIL: %s\n", msg); tests_failed++; \
} while(0)

/* ── Test cases ─────────────────────────────────────────────────── */

static void test_open_null_device(void)
{
    TEST("bus_i2c_open(NULL) returns NULL");
    bus_i2c_t* bus = bus_i2c_open(NULL);
    if (bus == NULL) PASS();
    else { bus_i2c_close(bus); FAIL("expected NULL"); }
}

static void test_open_invalid_device(void)
{
    TEST("bus_i2c_open(invalid path) returns NULL");
    bus_i2c_t* bus = bus_i2c_open("/dev/i2c-nonexistent");
    if (bus == NULL) PASS();
    else { bus_i2c_close(bus); FAIL("expected NULL"); }
}

static void test_close_null(void)
{
    TEST("bus_i2c_close(NULL) does not crash");
    bus_i2c_close(NULL);
    PASS();
}

static void test_transfer_null_bus(void)
{
    TEST("bus_i2c_transfer(NULL) returns HW_ERR_PARAM");
    uint8_t buf[4] = {0};
    hw_err_t ret = bus_i2c_transfer(NULL, 0x50, buf, 1, NULL, 0);
    if (ret == HW_ERR_PARAM) PASS();
    else FAIL("expected HW_ERR_PARAM");
}

static void test_write_null_bus(void)
{
    TEST("bus_i2c_write(NULL) returns HW_ERR_PARAM");
    uint8_t buf = 0;
    hw_err_t ret = bus_i2c_write(NULL, 0x50, &buf, 1);
    if (ret == HW_ERR_PARAM) PASS();
    else FAIL("expected HW_ERR_PARAM");
}

static void test_read_null_bus(void)
{
    TEST("bus_i2c_read(NULL) returns HW_ERR_PARAM");
    uint8_t buf = 0;
    hw_err_t ret = bus_i2c_read(NULL, 0x50, &buf, 1);
    if (ret == HW_ERR_PARAM) PASS();
    else FAIL("expected HW_ERR_PARAM");
}

static void test_transfer_null_tx_buf(void)
{
    TEST("bus_i2c_transfer(tx_len>0, tx=NULL) returns HW_ERR_PARAM");
    bus_i2c_t* bus = bus_i2c_open("/dev/i2c-1");
    if (!bus) { printf("SKIP (no /dev/i2c-1)\n"); return; }
    hw_err_t ret = bus_i2c_transfer(bus, 0x50, NULL, 4, NULL, 0);
    if (ret == HW_ERR_PARAM) PASS();
    else FAIL("expected HW_ERR_PARAM");
    bus_i2c_close(bus);
}

static void test_transfer_null_rx_buf(void)
{
    TEST("bus_i2c_transfer(rx_len>0, rx=NULL) returns HW_ERR_PARAM");
    bus_i2c_t* bus = bus_i2c_open("/dev/i2c-1");
    if (!bus) { printf("SKIP (no /dev/i2c-1)\n"); return; }
    uint8_t tx = 0x00;
    hw_err_t ret = bus_i2c_transfer(bus, 0x50, &tx, 1, NULL, 4);
    if (ret == HW_ERR_PARAM) PASS();
    else FAIL("expected HW_ERR_PARAM");
    bus_i2c_close(bus);
}

static void test_err_str_known(void)
{
    TEST("hw_err_str(HW_OK) returns non-NULL string");
    const char* s = hw_err_str(HW_OK);
    if (s && strlen(s) > 0) PASS();
    else FAIL("expected non-empty string");
}

static void test_err_str_unknown(void)
{
    TEST("hw_err_str(999) returns 'Unknown error'");
    const char* s = hw_err_str((hw_err_t)999);
    if (s && strcmp(s, "Unknown error") == 0) PASS();
    else FAIL("expected 'Unknown error'");
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== hw I2C Bus Tests ===\n\n");

    test_open_null_device();
    test_open_invalid_device();
    test_close_null();
    test_transfer_null_bus();
    test_write_null_bus();
    test_read_null_bus();
    test_transfer_null_tx_buf();
    test_transfer_null_rx_buf();
    test_err_str_known();
    test_err_str_unknown();

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
```

- [ ] **Step 2: Build and run test (host compiler — tests don't need cross-compilation)**

```bash
cd /home/chenchizhao/project/build
# Use host gcc for tests (no real I2C hardware needed for error-path tests)
gcc -std=c11 -Wall -Wextra \
    ../hw/tests/test_bus_i2c.c \
    ../hw/src/bus/bus_i2c.c \
    ../hw/src/hw_error.c \
    ../hw/src/hw_mutex.c \
    -I ../hw/include \
    -lpthread \
    -o /tmp/test_bus_i2c
/tmp/test_bus_i2c
```

Expected: all tests pass (some may SKIP if `/dev/i2c-1` doesn't exist on build host).

- [ ] **Step 3: Commit**

```bash
git add hw/tests/test_bus_i2c.c
git commit -m "test(hw): add I2C bus error-path tests"
```

---

### Task 6: Device template header

**Files:**
- Create: `hw/include/hw/dev/dev_template.h`

**Interfaces:**
- Consumes: `bus_i2c_t` from bus_i2c.h, `hw_err_t`
- Produces: `dev_template_t` struct pattern — documentation + reference for future IC drivers

- [ ] **Step 1: Write dev_template.h**

```c
#ifndef DEV_TEMPLATE_H
#define DEV_TEMPLATE_H

#include <stddef.h>
#include <stdint.h>
#include "hw/bus/bus_i2c.h"
#include "hw/hw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * dev_template.h — Device Driver Template
 *
 * This file defines the standard pattern for all I2C device drivers in this
 * framework. When adding a new chip (e.g. BMP280, MPU6050, ADS1115):
 *
 *   1. Copy this file to dev_<chip>.h under include/hw/dev/
 *   2. Rename dev_template_t → dev_<chip>_t
 *   3. Add chip-specific register addresses, config fields, and methods
 *   4. (Optional) Create src/dev/dev_<chip>.c for complex logic
 *
 * Key rules:
 *   - Device does NOT open/close the bus — it receives a bus handle at init
 *   - Device holds bus handle + chip address + private state
 *   - All bus I/O goes through bus_i2c_transfer/write/read which are
 *     already mutex-protected
 *   - Multiple devices on the same bus → share the same bus_i2c_t* handle
 */

/* ── Device context ──────────────────────────────────────────────── */

typedef struct {
    bus_i2c_t* bus;        /* I2C bus handle (dependency injection)       */
    uint8_t    addr;       /* 7-bit I2C slave address                     */

    /* Chip-specific fields — add below as needed:
     *   - register cache buffers
     *   - configuration values
     *   - calibration coefficients
     *   - device ID / revision
     */
    uint8_t    chip_id;    /* Example: device identification register     */

} dev_template_t;

/* ── Lifecycle ───────────────────────────────────────────────────── */

/**
 * Initialize the device context.
 *
 * Call once after bus_i2c_open(). Reads chip ID register to verify the
 * device is present on the bus.
 *
 * @param dev    Pointer to uninitialized device context
 * @param bus    Opened I2C bus handle (may be shared with other devices)
 * @param addr   7-bit I2C slave address of this chip
 * @return       HW_OK on success, HW_ERR_DEV_NOT_FOUND if chip doesn't
 *               ACK, HW_ERR_PARAM if dev is NULL
 */
hw_err_t dev_template_init(dev_template_t* dev, bus_i2c_t* bus, uint8_t addr)
{
    if (!dev || !bus) return HW_ERR_PARAM;

    dev->bus     = bus;
    dev->addr    = addr;
    dev->chip_id = 0;

    /* Read chip ID to verify device presence */
    uint8_t reg = 0x00; /* CHIP_ID register address — adjust per datasheet */
    hw_err_t ret = bus_i2c_transfer(bus, addr, &reg, 1, &dev->chip_id, 1);
    if (ret != HW_OK) return ret;

    /* Optional: validate expected chip_id value */
    return HW_OK;
}

/* ── Device operations ───────────────────────────────────────────── */

/**
 * Example: read from a device register.
 *
 * Pattern: bus_i2c_transfer(write_reg_addr, then read_data).
 */
static inline hw_err_t dev_template_read_reg(dev_template_t* dev,
                                             uint8_t reg, uint8_t* val)
{
    if (!dev || !val) return HW_ERR_PARAM;
    return bus_i2c_transfer(dev->bus, dev->addr, &reg, 1, val, 1);
}

/**
 * Example: write to a device register.
 *
 * Pattern: bus_i2c_write(reg_addr + data).
 */
static inline hw_err_t dev_template_write_reg(dev_template_t* dev,
                                              uint8_t reg, uint8_t val)
{
    if (!dev) return HW_ERR_PARAM;
    uint8_t buf[2] = { reg, val };
    return bus_i2c_write(dev->bus, dev->addr, buf, sizeof(buf));
}

/**
 * Example: read multiple bytes from consecutive registers.
 */
static inline hw_err_t dev_template_read_burst(dev_template_t* dev,
                                               uint8_t start_reg,
                                               uint8_t* data, size_t len)
{
    if (!dev || !data) return HW_ERR_PARAM;
    return bus_i2c_transfer(dev->bus, dev->addr, &start_reg, 1, data, len);
}

#ifdef __cplusplus
}
#endif

#endif /* DEV_TEMPLATE_H */
```

- [ ] **Step 2: Verify header parses**

```bash
echo '#include "hw/dev/dev_template.h"
int main(void) {
    dev_template_t d;
    bus_i2c_t* b = bus_i2c_open("/dev/i2c-1");
    if (b) {
        dev_template_init(&d, b, 0x77);
        bus_i2c_close(b);
    }
    return 0;
}' | gcc -fsyntax-only -xc -I /home/chenchizhao/project/hw/include -lpthread -
```

Expected: parse success.

- [ ] **Step 3: Commit**

```bash
git add hw/include/hw/dev/dev_template.h
git commit -m "feat(hw): add I2C device driver template with inline helpers"
```

---

### Task 7: Demo program

**Files:**
- Create: `hw/demo/demo_main.c`

**Interfaces:**
- Consumes: `bus_i2c_open/close/transfer()`, `dev_template.h`, `hw_err_str()`

- [ ] **Step 1: Write demo_main.c**

```c
/**
 * demo_main.c — HW framework quick validation
 *
 * Compile with: cmake -DHW_BUILD_DEMO=ON ..
 * Run on target: ./hw/demo/hw_demo
 *
 * This demo scans all I2C buses for devices, then exercises the device
 * template pattern.
 */
#include <stdio.h>
#include <stdlib.h>
#include "hw/bus/bus_i2c.h"
#include "hw/dev/dev_template.h"
#include "hw/hw_error.h"

/* I2C buses to scan on Orange Pi 5 Plus (RK3588 has up to 8 I2C controllers) */
static const char* i2c_buses[] = {
    "/dev/i2c-0", "/dev/i2c-1", "/dev/i2c-2", "/dev/i2c-3",
    "/dev/i2c-4", "/dev/i2c-5", "/dev/i2c-6", "/dev/i2c-7",
};

#define NUM_BUSES (sizeof(i2c_buses) / sizeof(i2c_buses[0]))

static void scan_i2c_bus(const char* device)
{
    bus_i2c_t* bus = bus_i2c_open(device);
    if (!bus) {
        printf("  %-16s — not available\n", device);
        return;
    }

    printf("  %-16s — opened, scanning addresses...\n", device);

    /* Scan 7-bit addresses 0x03–0x77 */
    int found = 0;
    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        /* Send a zero-length write to probe the address */
        hw_err_t ret = bus_i2c_transfer(bus, addr, NULL, 0, NULL, 0);
        if (ret == HW_OK) {
            printf("    Device found at 0x%02x\n", addr);
            found++;
        }
    }

    if (found == 0) {
        printf("    No devices detected\n");
    }

    bus_i2c_close(bus);
}

int main(void)
{
    printf("=== HW IO Framework Demo ===\n\n");
    printf("Error code demo: HW_ERR_BUS_OPEN = %s\n", hw_err_str(HW_ERR_BUS_OPEN));

    printf("\n--- I2C Bus Scan ---\n");
    for (size_t i = 0; i < NUM_BUSES; i++) {
        scan_i2c_bus(i2c_buses[i]);
    }

    printf("\n--- Device Template Demo ---\n");
    printf("(Open /dev/i2c-1 and init a device using dev_template_init)\n");

    bus_i2c_t* bus = bus_i2c_open("/dev/i2c-1");
    if (bus) {
        dev_template_t dev;
        hw_err_t ret = dev_template_init(&dev, bus, 0x77);
        if (ret == HW_OK) {
            printf("Device initialized at 0x%02x, chip_id=0x%02x\n",
                   dev.addr, dev.chip_id);

            uint8_t val;
            ret = dev_template_read_reg(&dev, 0x00, &val);
            if (ret == HW_OK) {
                printf("Read reg[0x00] = 0x%02x\n", val);
            }
        } else {
            printf("No device at 0x77: %s\n", hw_err_str(ret));
        }
        bus_i2c_close(bus);
    }

    printf("\n=== Demo Complete ===\n");
    return 0;
}
```

- [ ] **Step 2: Build demo (cross-compile — validates full toolchain integration)**

```bash
source /home/chenchizhao/project/env/rk3588_product_orangerpi5plus.env
cd /home/chenchizhao/project/build
# Manually test compilation (full build integration comes in Task 8)
${CROSS_COMPILE}gcc -std=c11 -Wall -Wextra \
    ../hw/demo/demo_main.c \
    ../hw/src/bus/bus_i2c.c \
    ../hw/src/hw_error.c \
    ../hw/src/hw_mutex.c \
    -I ../hw/include \
    -lpthread \
    -o /tmp/hw_demo
rm -f /tmp/hw_demo
```

Expected: compile success. (Cross-compiler may warn about unused result — fine.)

- [ ] **Step 3: Commit**

```bash
git add hw/demo/demo_main.c
git commit -m "feat(hw): add demo program with I2C bus scanner"
```

---

### Task 8: Integrate hw into top-level build

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Update top-level CMakeLists.txt**

Old:

```cmake
cmake_minimum_required(VERSION 3.16)
project(hello_world LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Cross-compilation toolchain
if(DEFINED ENV{CROSS_COMPILE})
    set(CMAKE_C_COMPILER $ENV{CROSS_COMPILE}gcc)
endif()

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/user)

# Library search paths
link_directories(${PROJECT_SOURCE_DIR}/part)

# Add executable
add_executable(hello_world
    ${PROJECT_SOURCE_DIR}/user/main.c
)

# Install
install(TARGETS hello_world DESTINATION ${INSTALL_DIR}/bin)
```

New — add after `link_directories()`:

```cmake
# HW IO library
add_subdirectory(${PROJECT_SOURCE_DIR}/hw)
```

And update `add_executable` → `target_link_libraries`:

```cmake
add_executable(hello_world
    ${PROJECT_SOURCE_DIR}/user/main.c
)
target_link_libraries(hello_world hw)
```

- [ ] **Step 2: Full build**

```bash
cd /home/chenchizhao/project && ./build.sh
```

Expected: `build/hello_world` built, `build/hw/libhw.a` built. Last line shows `file` output for `hello_world` (ELF 64-bit ARM aarch64).

- [ ] **Step 3: Verify libhw.a was produced**

```bash
file /home/chenchizhao/project/build/hw/libhw.a
aarch64-none-linux-gnu-ar t /home/chenchizhao/project/build/hw/libhw.a
```

Expected: `libhw.a` is `ar archive`, contains `hw_error.c.o`, `hw_mutex.c.o`, `bus_i2c.c.o`.

- [ ] **Step 4: Verify HW_BUILD_DEMO=ON works**

```bash
source /home/chenchizhao/project/env/rk3588_product_orangerpi5plus.env
cd /home/chenchizhao/project/build
rm -rf CMakeCache.txt CMakeFiles/
cmake .. -DHW_BUILD_DEMO=ON
make -j$(nproc)
file build/hw/hw_demo
```

Expected: `hw_demo` built as ARM64 ELF executable.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt
git commit -m "feat(build): integrate hw static library into top-level build"
```

---

### Task 9: (Optional) Build and run tests on target

After deploying to the Orange Pi 5 Plus board:

```bash
# On target
cd /path/to/project/build
ctest --test-dir hw -V
```

This runs `test_bus_i2c` — error-path tests pass immediately; I2C bus tests that need real `/dev/i2c-*` will PASS or SKIP depending on hardware availability.
