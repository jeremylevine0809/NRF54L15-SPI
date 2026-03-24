#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>

K_MUTEX_DEFINE(params_mutex);

// --- SPI / DAC ---
#define DAC_NODE DT_NODELABEL(max5532)
#define SPIOP (SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_CS_ACTIVE_HIGH)

struct spi_dt_spec spispec = SPI_DT_SPEC_GET(DAC_NODE, SPIOP, 0);
const struct device *port1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));
const struct device *port2 = DEVICE_DT_GET(DT_NODELABEL(gpio2));

#define CS1_PIN 10
#define CS2_PIN  5
#define CS3_PIN  8

// --- BLE UUIDs ---
#define BT_UUID_STIM_SERVICE_VAL \
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)
#define BT_UUID_PULSE_WIDTH_VAL \
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1)
#define BT_UUID_FREQUENCY_VAL \
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef2)
#define BT_UUID_AMPLITUDE_VAL \
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef3)
#define BT_UUID_STIM_ENABLE_VAL \
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef4)

static struct bt_uuid_128 stim_service_uuid = BT_UUID_INIT_128(BT_UUID_STIM_SERVICE_VAL);
static struct bt_uuid_128 pulse_width_uuid  = BT_UUID_INIT_128(BT_UUID_PULSE_WIDTH_VAL);
static struct bt_uuid_128 frequency_uuid    = BT_UUID_INIT_128(BT_UUID_FREQUENCY_VAL);
static struct bt_uuid_128 amplitude_uuid    = BT_UUID_INIT_128(BT_UUID_AMPLITUDE_VAL);
static struct bt_uuid_128 stim_enable_uuid  = BT_UUID_INIT_128(BT_UUID_STIM_ENABLE_VAL);

// --- Stimulation parameters ---
static volatile uint16_t pulse_width_us = 2500;
static volatile uint16_t frequency_hz   = 40;
static volatile uint16_t amplitude_val  = 0xCBA;
static volatile uint8_t  stim_enabled   = 0;

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

// --- MUX control ---
void mux_disable(void)
{
    gpio_pin_set(port1, 6, 0);
}

void mux_enable(void)
{
    gpio_pin_set(port1, 6, 1);
}

void mux_set_phase1(void)
{
    gpio_pin_set(port1, 0, 0);  // MUX1_A0
    gpio_pin_set(port1, 1, 0);  // MUX1_A1 -> Ch1
    gpio_pin_set(port1, 2, 0);  // MUX1_A2
    gpio_pin_set(port1, 3, 0);  // MUX2_A0
    gpio_pin_set(port1, 4, 1);  // MUX2_A1 -> Ch3
    gpio_pin_set(port1, 5, 0);  // MUX2_A2
}

void mux_set_phase2(void)
{
    gpio_pin_set(port1, 0, 0);  // MUX1_A0
    gpio_pin_set(port1, 1, 1);  // MUX1_A1 -> Ch3
    gpio_pin_set(port1, 2, 0);  // MUX1_A2
    gpio_pin_set(port1, 3, 0);  // MUX2_A0
    gpio_pin_set(port1, 4, 0);  // MUX2_A1 -> Ch1
    gpio_pin_set(port1, 5, 0);  // MUX2_A2
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
    int err = spi_write_dt(&spispec, &tx);
    cs_deselect_all();

    if (err != 0) {
        printk("SPI write failed: %d\n", err);
    }
}


// --- Stimulation cycle ---
void biphasic_stim_cycle(void)
{
    k_mutex_lock(&params_mutex, K_FOREVER);
    uint16_t pw         = pulse_width_us;
    // uint32_t period     = 1000000 / frequency_hz;
    uint16_t freq = *((volatile uint16_t*)&frequency_hz); // make volatile? 
    printk("Freq: %d\n", freq);
    uint32_t period     = 1000000u / (uint32_t)freq; // trying this too
    uint16_t interphase = 250; // CHANGED THIS FROM 250 to 25000! Still need to figure out why this fixes it
    k_mutex_unlock(&params_mutex);

    // Phase 1
    mux_set_phase1();
    mux_enable();
    k_usleep(pw);

    // Interphase
    mux_disable();
    k_usleep(interphase);

    // Phase 2
    mux_set_phase2();
    mux_enable();
    k_usleep(pw);

    // End of cycle
    mux_disable();

    // Remaining period
    uint32_t active_time = (2 * pw) + interphase;
    if (period > active_time) {
        k_usleep(period - active_time);
    }
}

// --- BLE Write Callbacks ---
static ssize_t write_pulse_width(struct bt_conn *conn,
                                  const struct bt_gatt_attr *attr,
                                  const void *buf, uint16_t len,
                                  uint16_t offset, uint8_t flags)
{
    if (len != sizeof(uint16_t)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    uint16_t val;
    memcpy(&val, buf, len);
    k_mutex_lock(&params_mutex, K_FOREVER);
    pulse_width_us = val;
    k_mutex_unlock(&params_mutex);
    printk("Pulse width updated: %d us\n", pulse_width_us);
    return len;
}

static ssize_t write_frequency(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len,
                                uint16_t offset, uint8_t flags)
{
    if (len != sizeof(uint16_t)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    uint16_t val;
    memcpy(&val, buf, len);
    k_mutex_lock(&params_mutex, K_FOREVER);
    frequency_hz = val;
    k_mutex_unlock(&params_mutex);
    printk("Frequency updated: %d Hz\n", frequency_hz);
    return len;
}

static ssize_t write_amplitude(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len,
                                uint16_t offset, uint8_t flags)
{
    if (len != sizeof(uint16_t)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    uint16_t val;
    memcpy(&val, buf, len);
    k_mutex_lock(&params_mutex, K_FOREVER);
    amplitude_val = val;
    k_mutex_unlock(&params_mutex);
    send_dac_command(0xF, amplitude_val);
    printk("Amplitude updated: 0x%03X\n", amplitude_val);
    return len;
}

static ssize_t write_stim_enable(struct bt_conn *conn,
                                  const struct bt_gatt_attr *attr,
                                  const void *buf, uint16_t len,
                                  uint16_t offset, uint8_t flags)
{
    if (len != sizeof(uint8_t)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    memcpy(&stim_enabled, buf, len);
    if (!stim_enabled) {
        mux_disable();
        printk("Stimulation disabled\n");
    } else {
        printk("Stimulation enabled\n");
    }
    return len;
}

// --- GATT Service ---
BT_GATT_SERVICE_DEFINE(stim_svc,
    BT_GATT_PRIMARY_SERVICE(&stim_service_uuid),
    BT_GATT_CHARACTERISTIC(&pulse_width_uuid.uuid,
        BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
        BT_GATT_PERM_WRITE, NULL, write_pulse_width, &pulse_width_us),
    BT_GATT_CHARACTERISTIC(&frequency_uuid.uuid,
        BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
        BT_GATT_PERM_WRITE, NULL, write_frequency, &frequency_hz),
    BT_GATT_CHARACTERISTIC(&amplitude_uuid.uuid,
        BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
        BT_GATT_PERM_WRITE, NULL, write_amplitude, &amplitude_val),
    BT_GATT_CHARACTERISTIC(&stim_enable_uuid.uuid,
        BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
        BT_GATT_PERM_WRITE, NULL, write_stim_enable, &stim_enabled),
);

// --- BLE Advertising & Connection ---
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS,
                  BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
    BT_DATA(BT_DATA_NAME_COMPLETE,
            CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        printk("Connection failed: %d\n", err);
        return;
    }
    printk("Connected!\n");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    printk("Disconnected, reason: %d\n", reason);
    mux_disable();
    stim_enabled = 0;
    bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), NULL, 0);
}

static struct bt_conn_cb conn_callbacks = {
    .connected    = connected,
    .disconnected = disconnected,
};

static void bt_ready(int err)
{
    if (err) {
        printk("Bluetooth init failed: %d\n", err);
        return;
    }
    printk("Bluetooth initialized\n");
    bt_conn_cb_register(&conn_callbacks);
    err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        printk("Advertising failed to start: %d\n", err);
        return;
    }
    printk("Advertising as '%s'\n", CONFIG_BT_DEVICE_NAME);
}

// --- Main ---
void main(void)
{
    // GPIO init
    gpio_pin_configure(port1, 0, GPIO_OUTPUT); gpio_pin_set(port1, 0, 0);
    gpio_pin_configure(port1, 1, GPIO_OUTPUT); gpio_pin_set(port1, 1, 0);
    gpio_pin_configure(port1, 2, GPIO_OUTPUT); gpio_pin_set(port1, 2, 0);
    gpio_pin_configure(port1, 3, GPIO_OUTPUT); gpio_pin_set(port1, 3, 0);
    gpio_pin_configure(port1, 4, GPIO_OUTPUT); gpio_pin_set(port1, 4, 0);
    gpio_pin_configure(port1, 5, GPIO_OUTPUT); gpio_pin_set(port1, 5, 0);
    gpio_pin_configure(port1, 6, GPIO_OUTPUT); gpio_pin_set(port1, 6, 0);
    gpio_pin_configure(port1, 7, GPIO_OUTPUT); gpio_pin_set(port1, 7, 0);
    gpio_pin_configure(port2, CS1_PIN, GPIO_OUTPUT);
    gpio_pin_configure(port2, CS2_PIN, GPIO_OUTPUT);
    gpio_pin_configure(port2, CS3_PIN, GPIO_OUTPUT);
    cs_deselect_all();

    // DAC init
    k_msleep(10);
    send_dac_command(0xD, 0x800);
    send_dac_command(0xD, 0x800);
    send_dac_command(0xF, amplitude_val);

    // Enable boost
    gpio_pin_set(port1, 7, 1);
    k_msleep(10);

    // BLE init
    int err = bt_enable(bt_ready);
    if (err) {
        printk("bt_enable failed: %d\n", err);
        return;
    }

    printk("System ready. Waiting for BLE commands...\n");

    // Main loop
    while (1) {
        if (stim_enabled) {
            biphasic_stim_cycle();
        } else {
            mux_disable();
            k_msleep(10);
        }
    }
}