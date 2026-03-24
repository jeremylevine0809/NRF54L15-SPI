#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

// --- SPI device nodes ---
#define DAC_NODE   DT_NODELABEL(max5532)
#define INTAN_NODE DT_NODELABEL(rhd2216)
#define SPST_NODE  DT_NODELABEL(adg1414)

// --- SPI operation flags ---
// DAC: Mode 3 (CPOL=1, CPHA=1), 8-bit
#define SPIOP_DAC (SPI_WORD_SET(8) | SPI_TRANSFER_MSB | \
                   SPI_MODE_CPOL | SPI_MODE_CPHA)

// Intan: Mode 0 (CPOL=0, CPHA=0), 16-bit
// (NRF54L15 only supports 8-bit)
#define SPIOP_INTAN (SPI_WORD_SET(8) | SPI_TRANSFER_MSB)

// ADG1414: Mode 0 (CPOL=0, CPHA=0), 8-bit
#define SPIOP_SPST (SPI_WORD_SET(8) | SPI_TRANSFER_MSB)

// --- SPI specs ---
static struct spi_dt_spec dac_spec   = SPI_DT_SPEC_GET(DAC_NODE,   SPIOP_DAC,   0);
static struct spi_dt_spec intan_spec = SPI_DT_SPEC_GET(INTAN_NODE, SPIOP_INTAN, 0);
static struct spi_dt_spec spst_spec  = SPI_DT_SPEC_GET(SPST_NODE,  SPIOP_SPST,  0);


// --- GPIO ---
const struct device *port2 = DEVICE_DT_GET(DT_NODELABEL(gpio2));

#define CS1_PIN 10  // DAC
#define CS2_PIN  5  // Intan
#define CS3_PIN  8  // SPST

// --- CS control ---
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

// --- DAC ---
void send_dac_command(uint8_t command, uint16_t data)
{
    uint16_t word = ((command & 0x0F) << 12) | (data & 0x0FFF);
    uint8_t tx_buf[2] = {
        (uint8_t)(word >> 8),
        (uint8_t)(word & 0xFF)
    };
    struct spi_buf spi_buf = { .buf = tx_buf, .len = sizeof(tx_buf) };
    struct spi_buf_set tx  = { .buffers = &spi_buf, .count = 1 };

    cs_select(1);
    spi_write_dt(&dac_spec, &tx);
    cs_deselect_all();
}

// --- ADG1414 SPST switch ---
void spst_set(uint8_t switch_mask)
{
    uint8_t tx_buf = switch_mask;
    struct spi_buf spi_buf = { .buf = &tx_buf, .len = 1 };
    struct spi_buf_set tx  = { .buffers = &spi_buf, .count = 1 };

    cs_select(3);
    spi_write_dt(&spst_spec, &tx);
    cs_deselect_all();

    printk("SPST switches set: 0x%02X\n", switch_mask);
}

uint16_t intan_send(uint16_t command)
{
    uint8_t tx_buf[2] = {
        (uint8_t)(command >> 8),    // MSB first
        (uint8_t)(command & 0xFF)   // LSB second
    };
    uint8_t rx_buf[2] = {0, 0};

    struct spi_buf tx_spi = { .buf = tx_buf, .len = 2 };
    struct spi_buf rx_spi = { .buf = rx_buf, .len = 2 };
    struct spi_buf_set tx  = { .buffers = &tx_spi, .count = 1 };
    struct spi_buf_set rx  = { .buffers = &rx_spi, .count = 1 };

    cs_select(2);
    spi_transceive_dt(&intan_spec, &tx, &rx);
    cs_deselect_all();

    // Reconstruct 16-bit result from two received bytes
    return ((uint16_t)rx_buf[0] << 8) | rx_buf[1];
}

// --- Intan command macros ---
// CONVERT: bits[15:14]=00, bits[13:8]=channel
#define INTAN_CONVERT(ch)     ((uint16_t)((ch) & 0x3F) << 8)
// CALIBRATE: 0101 0101 0000 0000
#define INTAN_CALIBRATE       (0x5500)
// DUMMY: 0101 1010 0000 0000
#define INTAN_DUMMY           (0x5A00)
// WRITE register: bits[15:14]=11, bits[13:8]=reg, bits[7:0]=data
#define INTAN_WRITE(reg, val) ((uint16_t)(0xC000 | (((reg) & 0x3F) << 8) | ((val) & 0xFF)))
// READ register: bits[15:14]=11, bits[13:8]=reg, bits[7:0]=0
#define INTAN_READ(reg)       ((uint16_t)(0xC000 | (((reg) & 0x3F) << 8)))

// --- Convert raw ADC to nanovolts (avoid float) ---
// 0.195 uV/LSB, offset binary (0x8000 = 0V)
int32_t intan_to_nv(uint16_t raw)
{
    int16_t signed_val = (int16_t)((int32_t)raw - 0x8000);
    return (int32_t)signed_val * 195;  // result in nV (0.195uV = 195nV per LSB)
}

void intan_init(void)
{
    // Flush pipeline with dummy commands
    intan_send(INTAN_DUMMY);
    intan_send(INTAN_DUMMY);

    // Send calibrate command
    intan_send(INTAN_CALIBRATE);

    // 9 dummy cycles required after calibrate
    for (int i = 0; i < 9; i++) {
        intan_send(INTAN_DUMMY);
    }

    // Power down all amplifiers except channel 8
    // Register 14: amplifiers 0-7 (all off)
    intan_send(INTAN_WRITE(14, 0x00));
    // Register 15: amplifiers 8-15 (only amp 8 on = bit 0)
    intan_send(INTAN_WRITE(15, 0x01));

    // Set lower bandwidth to ~0.1 Hz
    intan_send(INTAN_WRITE(8, 28));   // RL_DAC1
    intan_send(INTAN_WRITE(9, 2));    // RL_DAC2

    // Set upper bandwidth to ~10 kHz
    intan_send(INTAN_WRITE(6, 21));   // RH1_DAC1
    intan_send(INTAN_WRITE(7, 8));    // RH1_DAC2

    // Flush pipeline after register writes
    intan_send(INTAN_DUMMY);
    intan_send(INTAN_DUMMY);

    printk("Intan initialized - only Ch8 powered\n");
}

// --- Main ---
void main(void)
{
    // GPIO init
    gpio_pin_configure(port2, CS1_PIN, GPIO_OUTPUT);
    gpio_pin_configure(port2, CS2_PIN, GPIO_OUTPUT);
    gpio_pin_configure(port2, CS3_PIN, GPIO_OUTPUT);
    cs_deselect_all();

    k_msleep(100);  // wait for power to stabilize

    // Verify SPI devices are ready
    if (!spi_is_ready_dt(&dac_spec)) {
        printk("DAC SPI not ready\n");
        return;
    }
    if (!spi_is_ready_dt(&intan_spec)) {
        printk("Intan SPI not ready\n");
        return;
    }
    if (!spi_is_ready_dt(&spst_spec)) {
        printk("SPST SPI not ready\n");
        return;
    }

    printk("All SPI devices ready\n");

    // Initialize Intan
    intan_init();

    // Close SPST switch S8 to connect Ch8 to Intan IN8+
    spst_set(0x80);  // bit7 = S8 closed

    printk("Starting recording on Ch8...\n");

    // Pipeline fill - two dummy converts before real data
    intan_send(INTAN_CONVERT(8));
    intan_send(INTAN_CONVERT(8));

    // Sampling loop at ~1 kHz
    while (1) {
        uint16_t raw = intan_send(INTAN_CONVERT(8));
        int32_t nv   = intan_to_nv(raw);

        // Print every 10 samples to avoid RTT flooding
        static int count = 0;
        if (++count >= 10) {
            count = 0;
            printk("Raw: 0x%04X  Voltage: %d nV\n", raw, nv);
        }

        k_msleep(1);  // 1ms = 1 kHz sample rate
    }
}