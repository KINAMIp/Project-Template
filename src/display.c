#include <avr/io.h>

#include "display.h"
#include "display_macros.h"

#define DISPLAY_PORT PORTA
#define DISPLAY_DDR DDRA

#define DISPLAY_DIGIT_PORT PORTC
#define DISPLAY_DIGIT_DDR DDRC

#define DIGIT_RIGHT_ENABLE (1 << 0)
#define DIGIT_LEFT_ENABLE  (1 << 1)

static volatile uint8_t digit_buffer[2] = {DISP_OFF, DISP_OFF};
static volatile uint8_t active_digit = 0;

static uint8_t encode_digit(uint8_t digit)
{
    switch (digit)
    {
    case 0:
        return DISP_SEG_A & DISP_SEG_B & DISP_SEG_C & DISP_SEG_D & DISP_SEG_E & DISP_SEG_F;
    case 1:
        return DISP_SEG_B & DISP_SEG_C;
    case 2:
        return DISP_SEG_A & DISP_SEG_B & DISP_SEG_D & DISP_SEG_E & DISP_SEG_G;
    case 3:
        return DISP_SEG_A & DISP_SEG_B & DISP_SEG_C & DISP_SEG_D & DISP_SEG_G;
    case 4:
        return DISP_SEG_B & DISP_SEG_C & DISP_SEG_F & DISP_SEG_G;
    case 5:
        return DISP_SEG_A & DISP_SEG_C & DISP_SEG_D & DISP_SEG_F & DISP_SEG_G;
    case 6:
        return DISP_SEG_A & DISP_SEG_C & DISP_SEG_D & DISP_SEG_E & DISP_SEG_F & DISP_SEG_G;
    case 7:
        return DISP_SEG_A & DISP_SEG_B & DISP_SEG_C;
    case 8:
        return DISP_SEG_A & DISP_SEG_B & DISP_SEG_C & DISP_SEG_D & DISP_SEG_E & DISP_SEG_F & DISP_SEG_G;
    case 9:
        return DISP_SEG_A & DISP_SEG_B & DISP_SEG_C & DISP_SEG_D & DISP_SEG_F & DISP_SEG_G;
    default:
        return DISP_OFF;
    }
}

void display_setup(void)
{
    DISPLAY_DDR = 0xFF;
    DISPLAY_PORT = DISP_OFF;
    DISPLAY_DIGIT_DDR |= DIGIT_LEFT_ENABLE | DIGIT_RIGHT_ENABLE;
    DISPLAY_DIGIT_PORT &= ~(DIGIT_LEFT_ENABLE | DIGIT_RIGHT_ENABLE);
}

void display_set_raw(uint8_t left, uint8_t right)
{
    digit_buffer[0] = left;
    digit_buffer[1] = right;
}

void display_show_number(uint16_t value)
{
    uint16_t limited = value % 100U;
    uint8_t tens = (uint8_t)((limited / 10U) % 10U);
    uint8_t ones = (uint8_t)(limited % 10U);

    if (value >= 10U)
    {
        digit_buffer[0] = encode_digit(tens);
    }
    else if (value >= 100U)
    {
        digit_buffer[0] = encode_digit(tens);
    }
    else
    {
        digit_buffer[0] = DISP_OFF;
    }

    if (value >= 100U && limited < 10U)
    {
        digit_buffer[0] = encode_digit(0);
    }

    digit_buffer[1] = encode_digit(ones);
}

void display_show_success(void)
{
    digit_buffer[0] = DISP_ON;
    digit_buffer[1] = DISP_ON;
}

void display_show_fail(void)
{
    digit_buffer[0] = DISP_DASH;
    digit_buffer[1] = DISP_DASH;
}

void display_blank(void)
{
    digit_buffer[0] = DISP_OFF;
    digit_buffer[1] = DISP_OFF;
}

void display_refresh_tick(void)
{
    if (active_digit == 0)
    {
        DISPLAY_DIGIT_PORT &= ~(DIGIT_LEFT_ENABLE | DIGIT_RIGHT_ENABLE);
        DISPLAY_PORT = digit_buffer[0];
        DISPLAY_DIGIT_PORT |= DIGIT_LEFT_ENABLE;
        active_digit = 1;
    }
    else
    {
        DISPLAY_DIGIT_PORT &= ~(DIGIT_LEFT_ENABLE | DIGIT_RIGHT_ENABLE);
        DISPLAY_PORT = digit_buffer[1];
        DISPLAY_DIGIT_PORT |= DIGIT_RIGHT_ENABLE;
        active_digit = 0;
    }
}
