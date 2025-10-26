#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>

void buzzer_setup(void);
void buzzer_start(uint16_t frequency_hz);
void buzzer_update(uint16_t frequency_hz);
void buzzer_stop(void);
uint16_t buzzer_current_frequency(void);

#endif
