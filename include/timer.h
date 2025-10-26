#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void timer_setup(void);
void timer_register_service(void (*handler)(void));
uint32_t timer_millis(void);
void timer_delay_ms(uint16_t delay_ms);
void timer_service(void);

#endif
