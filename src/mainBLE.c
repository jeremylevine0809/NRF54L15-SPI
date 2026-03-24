#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/printk.h>

// Custom Service UUID
#define BT_UUID_STIM_SERVICE_VAL \
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)

// Characteristic UUIDs
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

// Current stimulation parameters
static uint16_t pulse_width_us = 1000;   // default 1ms
static uint16_t frequency_hz   = 50;     // default 50Hz
static uint16_t amplitude_val  = 0xCBA;  // default 7.72mA DAC value
static uint8_t  stim_enabled   = 0;      // default off

// Write callbacks
static ssize_t write_pulse_width(struct bt_conn *conn,
                                  const struct bt_gatt_attr *attr,
                                  const void *buf, uint16_t len,
                                  uint16_t offset, uint8_t flags)
{
    if (len != sizeof(uint16_t)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    memcpy(&pulse_width_us, buf, len);
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
    memcpy(&frequency_hz, buf, len);
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
    memcpy(&amplitude_val, buf, len);
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
    printk("Stim enable updated: %d\n", stim_enabled);
    return len;
}

// GATT Service Definition
BT_GATT_SERVICE_DEFINE(stim_svc,
    BT_GATT_PRIMARY_SERVICE(&stim_service_uuid),

    BT_GATT_CHARACTERISTIC(&pulse_width_uuid.uuid,
                            BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                            BT_GATT_PERM_WRITE,
                            NULL, write_pulse_width, &pulse_width_us),

    BT_GATT_CHARACTERISTIC(&frequency_uuid.uuid,
                            BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                            BT_GATT_PERM_WRITE,
                            NULL, write_frequency, &frequency_hz),

    BT_GATT_CHARACTERISTIC(&amplitude_uuid.uuid,
                            BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                            BT_GATT_PERM_WRITE,
                            NULL, write_amplitude, &amplitude_val),

    BT_GATT_CHARACTERISTIC(&stim_enable_uuid.uuid,
                            BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                            BT_GATT_PERM_WRITE,
                            NULL, write_stim_enable, &stim_enabled),
);

// Advertisement data
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS,
                  BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
    BT_DATA(BT_DATA_NAME_COMPLETE,
            CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

// Connection callbacks
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
    // Restart advertising after disconnect
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

void main(void)
{
    int err = bt_enable(bt_ready);
    if (err) {
        printk("bt_enable failed: %d\n", err);
        return;
    }

    while (1) {
        k_msleep(1000);
        printk("PW:%dus Freq:%dHz Amp:0x%03X En:%d\n",
               pulse_width_us, frequency_hz,
               amplitude_val, stim_enabled);
    }
}