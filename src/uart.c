#include <avr/interrupt.h>
#include <avr/io.h>

#include "uart.h"

#ifndef F_CPU
#define F_CPU 8000000UL
#endif

#define UART_BAUD 9600UL
#define UART_UBRR ((F_CPU / (16UL * UART_BAUD)) - 1UL)

#define UART_BUFFER_SIZE 64

static volatile uint8_t rx_buffer[UART_BUFFER_SIZE];
static volatile uint8_t rx_head = 0;
static volatile uint8_t rx_tail = 0;

static inline uint8_t buffer_next(uint8_t index)
{
    return (uint8_t)((index + 1U) % UART_BUFFER_SIZE);
}

void uart_setup(void)
{
    UBRR0H = (uint8_t)(UART_UBRR >> 8);
    UBRR0L = (uint8_t)(UART_UBRR & 0xFF);
    UCSR0A = 0;
    UCSR0B = (1 << RXEN0) | (1 << TXEN0) | (1 << RXCIE0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
    rx_head = 0;
    rx_tail = 0;
}

bool uart_tx_ready(void)
{
    return (UCSR0A & (1 << UDRE0)) != 0;
}

void uart_send_char(char c)
{
    while (!uart_tx_ready())
        ;
    UDR0 = (uint8_t)c;
}

void uart_send_string(const char *string)
{
    while (*string)
    {
        uart_send_char(*string++);
    }
}

bool uart_rx_available(void)
{
    return rx_head != rx_tail;
}

uint8_t uart_read_byte(void)
{
    if (rx_head == rx_tail)
        return 0;
    uint8_t value = rx_buffer[rx_tail];
    rx_tail = buffer_next(rx_tail);
    return value;
}

ISR(USART_RX_vect)
{
    uint8_t data = UDR0;
    uint8_t next = buffer_next(rx_head);
    if (next != rx_tail)
    {
        rx_buffer[rx_head] = data;
        rx_head = next;
    }
}
