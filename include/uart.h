#ifndef UART_H
#define UART_H

#include <stdbool.h>
#include <stdint.h>

void uart_setup(void);
bool uart_tx_ready(void);
void uart_send_char(char c);
void uart_send_string(const char *string);
bool uart_rx_available(void);
uint8_t uart_read_byte(void);

#endif
