#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define DAC_NODE DT_NODELABEL(max5532)

#define SPIOP (SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_MODE_CPHA)

struct spi_dt_spec spispec = SPI_DT_SPEC_GET(DAC_NODE, SPIOP, 0);
const struct device *port1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));
const struct device *port2 = DEVICE_DT_GET(DT_NODELABEL(gpio2));

// CS pin definitions on Port 2
// Pin 21 -> P2.10 = CS1 (active by default)
// Pin 16 -> P2.05 = CS2
// Pin 19 -> P2.08 = CS3
#define CS1_PIN 10
#define CS2_PIN  5
#define CS3_PIN  8

// CS is active-low: 0 = selected, 1 = deselected
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

    if (!device_is_ready(port2)) {
        printk("Port 2 not ready\n");
        return;
    }

    int ret;

    printk("Test...\n");
    printf("Test...\n");


    // MUX1_A0
    ret = gpio_pin_configure(port1, 0, GPIO_OUTPUT);
    gpio_pin_set(port1, 0, 1);
    if (ret != 0) { printk("Failed to configure P1.%02d\n", 0); }

    // MUX1_A1
    ret = gpio_pin_configure(port1, 1, GPIO_OUTPUT);
    gpio_pin_set(port1, 1, 0);
    if (ret != 0) { printk("Failed to configure P1.%02d\n", 1); }

    // MUX1_A2
    ret = gpio_pin_configure(port1, 2, GPIO_OUTPUT);
    gpio_pin_set(port1, 2, 0);
    if (ret != 0) { printk("Failed to configure P1.%02d\n", 2); }

    // MUX2_A0
    ret = gpio_pin_configure(port1, 3, GPIO_OUTPUT);
    gpio_pin_set(port1, 3, 0);
    if (ret != 0) { printk("Failed to configure P1.%02d\n", 3); }

    printk("Test2...\n");

    // MUX2_A1
    ret = gpio_pin_configure(port1, 4, GPIO_OUTPUT);
    gpio_pin_set(port1, 4, 1);
    if (ret != 0) { printk("Failed to configure P1.%02d\n", 4); }

    // MUX2_A2
    ret = gpio_pin_configure(port1, 5, GPIO_OUTPUT);
    gpio_pin_set(port1, 5, 0);
    if (ret != 0) { printk("Failed to configure P1.%02d\n", 5); }

    // MUX_EN
    ret = gpio_pin_configure(port1, 6, GPIO_OUTPUT);
    gpio_pin_set(port1, 6, 1);
    if (ret != 0) { printk("Failed to configure P1.%02d\n", 6); }

    // CS1 (Pin 21, P2.10) - active by default (low)
    ret = gpio_pin_configure(port2, CS1_PIN, GPIO_OUTPUT);
    if (ret != 0) { printk("Failed to configure CS1 (P2.%02d)\n", CS1_PIN); }

    // CS2 (Pin 16, P2.05) - inactive by default (high)
    ret = gpio_pin_configure(port2, CS2_PIN, GPIO_OUTPUT);
    if (ret != 0) { printk("Failed to configure CS2 (P2.%02d)\n", CS2_PIN); }

    // CS3 (Pin 19, P2.08) - inactive by default (high)
    ret = gpio_pin_configure(port2, CS3_PIN, GPIO_OUTPUT);
    if (ret != 0) { printk("Failed to configure CS3 (P2.%02d)\n", CS3_PIN); }

    // Default state: CS1 active (low), CS2 and CS3 inactive (high)
    cs_select(1);

    if (!spi_is_ready_dt(&spispec)) {
        printk("DAC SPI device not ready!\n");
        return;
    }

    printk("SPI device ready, starting DAC communication...\n");

    // send_dac_command(0xD, 0x800);
    send_dac_command(0xD, 0x800);
    send_dac_command(0xF, 0xFFF);
    // send_dac_command(0xF, 0xBE1);  // VOUT = 2.425 × 3041/4096 ≈ 1.800V

    while (1) {
        // To switch active CS, call cs_select(2) or cs_select(3) before sending
    }
}