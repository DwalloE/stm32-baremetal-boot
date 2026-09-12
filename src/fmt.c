#include "fmt.h"
#include "uart.h"

void out_char(char c)
{
    uart_putc((uint8_t)c);
}

void out_str(const char *s)
{
    while (*s) {
        uart_putc((uint8_t)*s++);
    }
}

void out_nl(void)
{
    out_str("\r\n");
}

void out_hex32(uint32_t v)
{
    static const char digits[] = "0123456789abcdef";
    int i;

    out_str("0x");
    for (i = 28; i >= 0; i -= 4) {
        uart_putc((uint8_t)digits[(v >> i) & 0xFu]);
    }
}

void out_dec(uint32_t v)
{
    char buf[10];       /* 4294967295 is 10 digits */
    unsigned n = 0;

    do {
        buf[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u && n < sizeof buf);

    while (n > 0u) {
        uart_putc((uint8_t)buf[--n]);
    }
}
