#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

void display_setup(void);
void display_set_raw(uint8_t left, uint8_t right);
void display_show_number(uint16_t value);
void display_show_success(void);
void display_show_fail(void);
void display_blank(void);
void display_refresh_tick(void);

#endif
