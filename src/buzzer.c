#include <avr/io.h>

#include "buzzer.h"

#ifndef F_CPU
#define F_CPU 8000000UL
#endif

#define BUZZER_PORT PORTD
#define BUZZER_DDR DDRD
#define BUZZER_PIN PD5

static uint16_t current_frequency = 0;

static void buzzer_configure_timer(uint16_t frequency)
{
    static const struct
    {
        uint16_t prescaler;
        uint8_t cs_bits;
    } prescalers[] = {
        {1, (1 << CS10)},
        {8, (1 << CS11)},
        {64, (1 << CS11) | (1 << CS10)},
        {256, (1 << CS12)},
        {1024, (1 << CS12) | (1 << CS10)},
    };

    uint32_t best_error = 0xFFFFFFFFUL;
    uint16_t best_ocr = 0;
    uint8_t best_cs = 0;

    for (uint8_t i = 0; i < (sizeof prescalers / sizeof prescalers[0]); ++i)
    {
        uint32_t top = (F_CPU / (2UL * prescalers[i].prescaler * frequency));
        if (top == 0)
            continue;
        uint32_t ocr = top - 1;
        if (ocr > 0xFFFF)
            continue;
        uint32_t actual = F_CPU / (2UL * prescalers[i].prescaler * (ocr + 1));
        uint32_t error = (actual > frequency) ? (actual - frequency) : (frequency - actual);
        if (error < best_error)
        {
            best_error = error;
            best_ocr = (uint16_t)ocr;
            best_cs = prescalers[i].cs_bits;
        }
    }

    TCCR1A = (1 << COM1A0);
    TCCR1B = (1 << WGM12) | best_cs;
    OCR1A = best_ocr;
}

void buzzer_setup(void)
{
    BUZZER_DDR |= (1 << BUZZER_PIN);
    BUZZER_PORT &= ~(1 << BUZZER_PIN);
    TCCR1A = 0;
    TCCR1B = 0;
    current_frequency = 0;
}

void buzzer_start(uint16_t frequency_hz)
{
    if (frequency_hz == 0)
        return;
    current_frequency = frequency_hz;
    buzzer_configure_timer(frequency_hz);
}

void buzzer_update(uint16_t frequency_hz)
{
    if (frequency_hz == 0)
        return;
    current_frequency = frequency_hz;
    buzzer_configure_timer(frequency_hz);
}

void buzzer_stop(void)
{
    TCCR1A = 0;
    TCCR1B = 0;
    PORTD &= ~(1 << BUZZER_PIN);
    current_frequency = 0;
}

uint16_t buzzer_current_frequency(void)
{
    return current_frequency;
}
