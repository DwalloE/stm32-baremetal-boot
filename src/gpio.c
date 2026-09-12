/*
 * gpio.c - one output pin, by register. RM0008 Rev 15 §9.2 pp.171-173.
 */
#include "gpio.h"
#include "regs.h"

#define LED_PIN 13u

void gpio_led_init(void)
{
    uint32_t crh;

    RCC_APB2ENR |= RCC_APB2ENR_IOPCEN;              /* clock port C first (§7.3.7) */

    /* PC13: general-purpose push-pull output, 2 MHz - an LED does not need slew (nibble 0010). */
    crh  = GPIO_CRH(GPIOC_BASE);
    crh &= ~(0xFu << GPIO_PIN_NIBBLE_SHIFT(LED_PIN));
    crh |=  (GPIO_CNF_MODE_OUT_PP_2MHZ << GPIO_PIN_NIBBLE_SHIFT(LED_PIN));
    GPIO_CRH(GPIOC_BASE) = crh;

    gpio_led_off();
}

void gpio_led_off(void)
{
    /* BSRR: writing BSy (bit y) sets the pin without touching the others (§9.2.5). LED is active low. */
    GPIO_BSRR(GPIOC_BASE) = (1u << LED_PIN);
}

void gpio_led_toggle(void)
{
    /* ODR read-modify-write is fine here: nothing else on this port, no interrupt touches it. */
    GPIO_ODR(GPIOC_BASE) ^= (1u << LED_PIN);
}
