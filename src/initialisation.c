#include <avr/io.h>

#include "buzzer.h"
#include "display.h"
#include "initialisation.h"
#include "timer.h"
#include "uart.h"

void gpio_initialise(void)
{
    DDRA = 0xFF;
    PORTA = 0xFF;

    DDRB &= (uint8_t)~0x0F;
    PORTB |= 0x0F;

    DDRC |= 0x03;
    PORTC &= (uint8_t)~0x03;

    DDRD |= (1 << PD5);
    PORTD &= (uint8_t)~(1 << PD5);
}

void adc_initialise(void)
{
    ADMUX = (1 << REFS0);
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}

void timer_initialise(void)
{
    timer_setup();
}

void display_initialise(void)
{
    display_setup();
}

void buzzer_initialise(void)
{
    buzzer_setup();
}

void uart_initialise(void)
{
    uart_setup();
}

void system_initialise(void)
{
    gpio_initialise();
    adc_initialise();
    timer_initialise();
    display_initialise();
    buzzer_initialise();
    uart_initialise();
}
