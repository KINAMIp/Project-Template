#ifndef INITIALISATION_H
#define INITIALISATION_H

#include <stdint.h>

void system_initialise(void);
void gpio_initialise(void);
void adc_initialise(void);
void timer_initialise(void);
void display_initialise(void);
void buzzer_initialise(void);
void uart_initialise(void);

#endif
