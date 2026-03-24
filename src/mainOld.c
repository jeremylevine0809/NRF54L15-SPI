#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/printk.h>

#define DAC_NODE DT_NODELABEL(max5532)

// Fix 1: Correct SPI operation flags
#define SPIOP (SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_MODE_CPHA)

// Fix 2: Use proper SPI configuration
struct spi_dt_spec spispec = SPI_DT_SPEC_GET(DAC_NODE, SPIOP, 0);
const struct device *port1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));

void send_dac_command(uint8_t command, uint16_t data)
{
    // Format: 4-bit command (MSBs) + 12-bit data (LSBs)
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

    // Fix 3: Use the spi_dt_spec structure properly
    int err = spi_write_dt(&spispec, &tx);
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
    int ret;
    //MUX1_A0
    ret = gpio_pin_configure(port1, 0, GPIO_OUTPUT);
    gpio_pin_set(port1, 0, 1);
    if (ret != 0) {
        printk("Failed to configure P1.%02d\n", 0);
    }
    //MUX1_A1
    ret = gpio_pin_configure(port1, 1, GPIO_OUTPUT);
    gpio_pin_set(port1, 1, 0);
    if (ret != 0) {
        printk("Failed to configure P1.%02d\n", 1);
    }
    //MUX1_A2
    ret = gpio_pin_configure(port1, 2, GPIO_OUTPUT);
    gpio_pin_set(port1, 2, 0);
    if (ret != 0) {
        printk("Failed to configure P1.%02d\n", 2);
    }
    //MUX2_A0
    ret = gpio_pin_configure(port1, 3, GPIO_OUTPUT);
    gpio_pin_set(port1, 3, 0);
    if (ret != 0) {
        printk("Failed to configure P1.%02d\n", 3);
    }
    //MUX2_A1
    ret = gpio_pin_configure(port1, 4, GPIO_OUTPUT);
    gpio_pin_set(port1, 4, 1);
    if (ret != 0) {
        printk("Failed to configure P1.%02d\n", 4);
    }
    //MUX2_A2
    ret = gpio_pin_configure(port1, 5, GPIO_OUTPUT);
    gpio_pin_set(port1, 5, 0);
    if (ret != 0) {
        printk("Failed to configure P1.%02d\n", 5);
    }
    //MUX_EN
    ret = gpio_pin_configure(port1, 6, GPIO_OUTPUT);
    gpio_pin_set(port1, 6, 1);
    if (ret != 0) {
        printk("Failed to configure P1.%02d\n", 6);
    }
    // BOOST_EN
    // ret = gpio_pin_configure(port1, 7, GPIO_OUTPUT);
    // gpio_pin_set(port1, 7, 1);
    // if (ret != 0) {
    //     printk("Failed to configure P1.%02d\n", 7);
    //}
    
    // Fix 4: Check if SPI device is ready
    if (!spi_is_ready_dt(&spispec)) {
        printk("DAC SPI device not ready!\n");
        return;
    }

    printk("SPI device ready, starting DAC communication...\n");

    // Fix 5: Use correct MAX5532 command - 0xF for Write & Update DAC A
    send_dac_command(0xD, 0x800);  // Configure DAC A to 2.425V internal reference
    send_dac_command(0xF, 0xBE1);  // Send 0xFFFF (maximum DAC Output of (4095/4096) * 2.425V )
    while (1) {
        //k_sleep(K_MSEC(1));
        //send_dac_command(0xF, 0x81D);  // Mid-scale to DAC A
        //send_dac_command(0xF, 0xFFF);  // Full scale to DAC A
    }
}