#include <stdint.h>
#include "uart.h"

/* NS16550A UART on the QEMU virt board */
#define UART_BASE   0x10000000UL
#define UART_THR    0           /* transmit holding register (write) */
#define UART_RBR    0           /* receive buffer register (read)    */
#define UART_LSR    5           /* line status register              */
#define LSR_RX_READY   (1 << 0) /* a received byte is waiting        */
#define LSR_TX_EMPTY   (1 << 5) /* transmitter can take a byte       */

static volatile uint8_t *const uart = (volatile uint8_t *)UART_BASE;

void uart_putc(char c)
{
    while ((uart[UART_LSR] & LSR_TX_EMPTY) == 0)
        ;
    uart[UART_THR] = (uint8_t)c;
}

char uart_getc(void)
{
    while ((uart[UART_LSR] & LSR_RX_READY) == 0)
        ;
    return (char)uart[UART_RBR];
}

void uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            uart_putc('\r');
        uart_putc(*s++);
    }
}

void uart_puthex(uint64_t v)
{
    uart_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        uart_putc("0123456789abcdef"[(v >> shift) & 0xf]);
}

void uart_putdec(uint64_t v)
{
    char buf[21];
    int i = 0;

    if (v == 0) {
        uart_putc('0');
        return;
    }
    while (v > 0) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0)
        uart_putc(buf[--i]);
}
