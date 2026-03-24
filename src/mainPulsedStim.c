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

// CS pin definitions on Port 2
#define CS1_PIN 10  // Pin 21, P2.10 - DAC
#define CS2_PIN  5  // Pin 16, P2.05 - Intan
#define CS3_PIN  8  // Pin 19, P2.08 - SPST

// Biphasic stimulation parameters
#define STIM_PULSE_WIDTH_US  250   // 250us pulse width
#define STIM_PERIOD_US       25000  // 20ms = 50Hz
#define STIM_INTERPHASE_US    25000   // interphase gap WTF!!!!! HOW DOES THIS WORK?

// CS control
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

// MUX control
void mux_disable(void)
{
    gpio_pin_set(port1, 6, 0);  // MUX_EN low
}

void mux_enable(void)
{
    gpio_pin_set(port1, 6, 1);  // MUX_EN high
}

void mux_set_phase1(void)
{
    // MUX1: A0=0, A1=0, A2=0 -> Ch1 (anode)
    gpio_pin_set(port1, 0, 0);  // MUX1_A0
    gpio_pin_set(port1, 1, 0);  // MUX1_A1
    gpio_pin_set(port1, 2, 0);  // MUX1_A2

    // k_usleep(100);

    // MUX2: A0=0, A1=1, A2=0 -> Ch3 (cathode)
    gpio_pin_set(port1, 3, 0);  // MUX2_A0
    gpio_pin_set(port1, 4, 1);  // MUX2_A1
    gpio_pin_set(port1, 5, 0);  // MUX2_A2
}

void mux_set_phase2(void)
{
    // MUX1: A0=0, A1=1, A2=0 -> Ch3 (anode)
    gpio_pin_set(port1, 0, 0);  // MUX1_A0
    gpio_pin_set(port1, 1, 1);  // MUX1_A1
    gpio_pin_set(port1, 2, 0);  // MUX1_A2

    // k_usleep(100);

    // MUX2: A0=0, A1=0, A2=0 -> Ch1 (cathode)
    gpio_pin_set(port1, 3, 0);  // MUX2_A0
    gpio_pin_set(port1, 4, 0);  // MUX2_A1
    gpio_pin_set(port1, 5, 0);  // MUX2_A2
}

// SPI / DAC
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
    int err = spi_write_dt(&spispec, &tx);
    cs_deselect_all();

    if (err == 0) {
        printk("DAC command sent: 0x%04X (cmd 0x%X, data 0x%03X)\n", word, command, data);
    } else {
        printk("SPI write failed: %d\n", err);
    }
}

// Biphasic stimulation cycle
void biphasic_stim_cycle(void)
{
    // Phase 1 - positive phase
    mux_set_phase1();
    mux_enable();
    k_usleep(STIM_PULSE_WIDTH_US);

    // Interphase - disable MUXes before switching
    mux_disable();    // SOMEHOW this should stay on??? Youd think it should be commented out to allow shorting!
    k_usleep(STIM_INTERPHASE_US);

    // Phase 2 - negative phase
    mux_set_phase2();
    mux_enable();
    k_usleep(STIM_PULSE_WIDTH_US);

    // End of cycle - disable MUXes
    mux_disable();

    // Remaining period time
    // Total active time = 2 * 250us + 50us = 550us
    // Remaining = 20000 - 550 = 19450us
    k_usleep(STIM_PERIOD_US - (2 * STIM_PULSE_WIDTH_US) - STIM_INTERPHASE_US);
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
    
    // MUX1_A0
    ret = gpio_pin_configure(port1, 0, GPIO_OUTPUT);
    gpio_pin_set(port1, 0, 0);
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

    // MUX2_A1
    ret = gpio_pin_configure(port1, 4, GPIO_OUTPUT);
    gpio_pin_set(port1, 4, 1);
    if (ret != 0) { printk("Failed to configure P1.%02d\n", 4); }

    // MUX2_A2
    ret = gpio_pin_configure(port1, 5, GPIO_OUTPUT);
    gpio_pin_set(port1, 5, 0);
    if (ret != 0) { printk("Failed to configure P1.%02d\n", 5); }

    

    // MUX_EN - start disabled
    ret = gpio_pin_configure(port1, 6, GPIO_OUTPUT);
    gpio_pin_set(port1, 6, 0);
    if (ret != 0) { printk("Failed to configure P1.%02d\n", 6); }

    // BOOST_EN - start disabled
    ret = gpio_pin_configure(port1, 7, GPIO_OUTPUT);
    gpio_pin_set(port1, 7, 0);
    if (ret != 0) { printk("Failed to configure P1.%02d\n", 7); }

    // CS1 (P2.10) - DAC
    ret = gpio_pin_configure(port2, CS1_PIN, GPIO_OUTPUT);
    if (ret != 0) { printk("Failed to configure CS1 (P2.%02d)\n", CS1_PIN); }

    // CS2 (P2.05) - Intan
    ret = gpio_pin_configure(port2, CS2_PIN, GPIO_OUTPUT);
    if (ret != 0) { printk("Failed to configure CS2 (P2.%02d)\n", CS2_PIN); }

    // CS3 (P2.08) - SPST
    ret = gpio_pin_configure(port2, CS3_PIN, GPIO_OUTPUT);
    if (ret != 0) { printk("Failed to configure CS3 (P2.%02d)\n", CS3_PIN); }

    // Deassert all CS lines
    cs_deselect_all();

    if (!spi_is_ready_dt(&spispec)) {
        printk("DAC SPI device not ready!\n");
        return;
    }

    printk("SPI device ready, starting DAC communication...\n");

    // DAC setup - configure reference and output voltage
    k_msleep(10);
    send_dac_command(0xD, 0x800);  // 2.425V internal reference
    send_dac_command(0xD, 0x800);  // repeat for timing
    send_dac_command(0xF, 0xCBA);  // 1.93V output = 7.72mA

    // Set initial MUX state
    mux_set_phase2();
    mux_enable();

    // Enable boost and let it stabilize
    gpio_pin_set(port1, 7, 1);
    k_msleep(10);

    printk("Starting biphasic stimulation: 50Hz, 250us pulse width\n");
    printk("MUX2_A0 (P1.03) set to: %d\\n", gpio_pin_get(port1, 3));
    /*
    gpio_pin_set(port1, 0, 0);
    gpio_pin_set(port1, 1, 0);
    gpio_pin_set(port1, 2, 0);
    gpio_pin_set(port1, 3, 1);
    gpio_pin_set(port1, 4, 1);
    gpio_pin_set(port1, 5, 1);
    */

    while (1) {
        biphasic_stim_cycle();
        // Phase 1
        // send_dac_command(0xF, 0xCBA);  // 1.93V
        // k_usleep(250);

        // Switch MUX channels while EN stays HIGH
        // mux_set_phase2();
        
        // Phase 2  
        // k_usleep(250);

        // Switch back and go to 0
        // mux_set_phase1();
        // send_dac_command(0xF, 0x000);  // 0V
        // k_usleep(19500);
    }
}
