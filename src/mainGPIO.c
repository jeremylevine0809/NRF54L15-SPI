#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

const struct device *port1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));

void main(void)
{
    if (!device_is_ready(port1)) {
        printk("Port 1 not ready\n");
        return;
    }

    for (int pin = 4; pin <= 8; pin++) {
        int ret;

        // Configure pin as output, initially HIGH
        ret = gpio_pin_configure(port1, pin, GPIO_OUTPUT);
        gpio_pin_set(port1, pin, 1);
        if (ret != 0) {
            printk("Failed to configure P1.%02d\n", pin);
        }
        int level = gpio_pin_get(port1, pin);
        printk("Pin %d = %d\n", pin, level); // Should print 1
    }


    while (1) {
        k_sleep(K_MSEC(1));
    }
}
