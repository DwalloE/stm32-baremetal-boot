#ifndef GPIO_H
#define GPIO_H

/* The Blue Pill's user LED on PC13 (active low on the board). */
void gpio_led_init(void);
void gpio_led_toggle(void);
void gpio_led_off(void);

#endif
