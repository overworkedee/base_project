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
#include <stdint.h>
#include <stddef.h>
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
