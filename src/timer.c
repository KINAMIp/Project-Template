#include <avr/interrupt.h>
#include <avr/io.h>

#include "display.h"
#include "timer.h"

#ifndef F_CPU
#define F_CPU 8000000UL
#endif

#define TIMER0_PRESCALE_BITS ((1 << CS01) | (1 << CS00))
#define TIMER0_COMPARE 124

static volatile uint32_t system_tick_ms = 0;
static void (*service_handler)(void) = 0;

void timer_setup(void)
{
    TCCR0A = (1 << WGM01);
    TCCR0B = 0;
    OCR0A = TIMER0_COMPARE;
    TIMSK0 = (1 << OCIE0A);
    TCNT0 = 0;
    TCCR0B = TIMER0_PRESCALE_BITS;
}

void timer_register_service(void (*handler)(void))
{
    service_handler = handler;
}

ISR(TIMER0_COMPA_vect)
{
    ++system_tick_ms;
    display_refresh_tick();
}

uint32_t timer_millis(void)
{
    uint32_t ticks;
    uint8_t sreg = SREG;
    cli();
    ticks = system_tick_ms;
    SREG = sreg;
    return ticks;
}

static void timer_service_internal(void)
{
    if (service_handler)
    {
        service_handler();
    }
}

void timer_delay_ms(uint16_t delay_ms)
{
    uint32_t start = timer_millis();
    while ((uint32_t)(timer_millis() - start) < delay_ms)
    {
        timer_service_internal();
    }
}

void timer_service(void)
{
    timer_service_internal();
}
