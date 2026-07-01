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
