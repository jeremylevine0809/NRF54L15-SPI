#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define DAC_NODE DT_NODELABEL(max5532)

#define SPIOP (SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_CS_ACTIVE_HIGH)

struct spi_dt_spec spispec = SPI_DT_SPEC_GET(DAC_NODE, SPIOP, 0);
const struct device *port1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));
const struct device *port2 = DEVICE_DT_GET(DT_NODELABEL(gpio2));

#define CS1_PIN 10
#define CS2_PIN  5
#define CS3_PIN  8

void cs_select(int cs)
{
    gpio_pin_set(port2, CS1_PIN, cs == 1 ? 0 : 1);
    gpio_pin_set(port2, CS2_PIN, cs == 2 ? 0 : 1);
    gpio_pin_set(port2, CS3_PIN, cs == 3 ? 0 : 1);
}

void cs_deselect_all(void)
{
    gpio_pin_set(port2, CS1_PIN, 1);
    gpio_pin_set(port2, CS2_PIN, 1);
    gpio_pin_set(port2, CS3_PIN, 1);
}

void send_dac_command(uint8_t command, uint16_t data)
{
    uint16_t word = ((command & 0x0F) << 12) | (data & 0x0FFF);

    uint8_t tx_buf[2] = {
        (uint8_t)(word >> 8),
        (uint8_t)(word & 0xFF)
    };

    struct spi_buf spi_buf = {
        .buf = tx_buf,
        .len = sizeof(tx_buf),
    };

    struct spi_buf_set tx = {
        .buffers = &spi_buf,
        .count = 1,
    };

    cs_select(1);
    k_msleep(1);
    int err = spi_write_dt(&spispec, &tx);
    cs_deselect_all();

    if (err == 0) {
        printk("DAC command sent: 0x%04X (cmd 0x%X, data 0x%03X)\n", word, command, data);
    } else {
        printk("SPI write failed: %d\n", err);
    }
}

void main(void)
{
    if (!device_is_ready(port1)) {
        printk("Port 1 not ready\n");
        return;
    }

    if (!device_is_ready(port2)) {
        printk("Port 2 not ready\n");
        return;
    }

    printk("Both ports ready\n");

    int ret;

    // MUX1_A0 - P1.00 - should be HIGH (1)
    ret = gpio_pin_configure(port1, 0, GPIO_OUTPUT);
    printk("P1.00 config ret: %d\n", ret);
    ret = gpio_pin_set(port1, 0, 1);
    printk("P1.00 set HIGH ret: %d\n", ret);

    // MUX1_A1 - P1.01 - should be LOW (0)
    ret = gpio_pin_configure(port1, 1, GPIO_OUTPUT);
    printk("P1.01 config ret: %d\n", ret);
    ret = gpio_pin_set(port1, 1, 0);
    printk("P1.01 set LOW ret: %d\n", ret);

    // MUX1_A2 - P1.02 - should be LOW (0)
    ret = gpio_pin_configure(port1, 2, GPIO_OUTPUT);
    printk("P1.02 config ret: %d\n", ret);
    ret = gpio_pin_set(port1, 2, 0);
    printk("P1.02 set LOW ret: %d\n", ret);

    // MUX2_A0 - P1.03 - should be LOW (0)
    ret = gpio_pin_configure(port1, 3, GPIO_OUTPUT);
    printk("P1.03 config ret: %d\n", ret);
    ret = gpio_pin_set(port1, 3, 0);
    printk("P1.03 set LOW ret: %d\n", ret);

    // MUX2_A1 - P1.04 - should be HIGH (1)
    ret = gpio_pin_configure(port1, 4, GPIO_OUTPUT);
    printk("P1.04 config ret: %d\n", ret);
    ret = gpio_pin_set(port1, 4, 1);
    printk("P1.04 set HIGH ret: %d\n", ret);

    // MUX2_A2 - P1.05 - should be LOW (0)
    ret = gpio_pin_configure(port1, 5, GPIO_OUTPUT);
    printk("P1.05 config ret: %d\n", ret);
    ret = gpio_pin_set(port1, 5, 0);
    printk("P1.05 set LOW ret: %d\n", ret);

    // MUX_EN - P1.06 - should be HIGH (1)
    ret = gpio_pin_configure(port1, 6, GPIO_OUTPUT);
    printk("P1.06 config ret: %d\n", ret);
    ret = gpio_pin_set(port1, 6, 1);
    printk("P1.06 set HIGH ret: %d\n", ret);

    // BOOST_EN - P1.07 - should be HIGH (1)
    ret = gpio_pin_configure(port1, 7, GPIO_OUTPUT);
    printk("P1.07 config ret: %d\n", ret);
    ret = gpio_pin_set(port1, 7, 1);
    printk("P1.07 set HIGH ret: %d\n", ret);

    // CS1 (P2.10) - DAC
    ret = gpio_pin_configure(port2, CS1_PIN, GPIO_OUTPUT);
    printk("CS1 P2.10 config ret: %d\n", ret);

    // CS2 (P2.05) - Intan
    ret = gpio_pin_configure(port2, CS2_PIN, GPIO_OUTPUT);
    printk("CS2 P2.05 config ret: %d\n", ret);

    // CS3 (P2.08) - SPST
    ret = gpio_pin_configure(port2, CS3_PIN, GPIO_OUTPUT);
    printk("CS3 P2.08 config ret: %d\n", ret);

    cs_deselect_all();
    k_msleep(50);

    if (!spi_is_ready_dt(&spispec)) {
        printk("DAC SPI device not ready!\n");
        return;
    }

    printk("SPI device ready, starting DAC communication...\n");
    k_msleep(100);

    send_dac_command(0xD, 0x800);
    send_dac_command(0xD, 0x800);
    send_dac_command(0xF, 0xCBA);

    printk("All done. Entering while loop.\n");

    while (1) {
    }
}