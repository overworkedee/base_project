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
