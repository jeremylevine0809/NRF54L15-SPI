// main.c
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/printk.h>

 #define DAC_NODE DT_NODELABEL(max5532)

//static const struct device *spi_dev = DEVICE_DT_GET(DAC_NODE);
#define SPIOP	SPI_WORD_SET(8) | SPI_TRANSFER_MSB 
struct spi_dt_spec spispec = SPI_DT_SPEC_GET(DT_NODELABEL(max5532), SPIOP, 0);



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

    int err = spi_write(spispec.bus, NULL, &tx);
    if (err == 0) {
        printk("DAC command sent: 0x%04X (cmd 0x%X, data 0x%03X)\n", word, command, data);
    } else {
        printk("SPI write failed: %d\n", err);
    }
}

void main(void)
{
    if (!device_is_ready(spispec.bus)) {
        printk("DAC SPI device not ready!\n");
        return;
    }

    // Example: Write full-scale (0xFFF) to DAC A using command 0x3 (Write & Update DAC A)
    send_dac_command(0x0F, 0x0FFF);

    while (1) {
        k_sleep(K_SECONDS(1));
        send_dac_command(0x0F, 0x0F21);
    }

}
