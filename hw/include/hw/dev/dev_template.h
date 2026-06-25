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
static inline hw_err_t dev_template_init(dev_template_t* dev, bus_i2c_t* bus, uint8_t addr)
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
